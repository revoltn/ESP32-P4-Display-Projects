/* Ambient light sensing via the OV02C10 camera: every ambient_period_s
 * seconds wake the camera for a short burst, take one frame's mean luma, put
 * the camera back to sleep, and glide the backlight between the configured
 * min (night) and max (day) brightness.
 *
 * Duty cycle: the sensor, ISP pipeline, CSI DMA and the stream task only run
 * for ~250 ms per capture (a few frames for the ISP auto-exposure to settle,
 * then the measured one). VIDIOC_STREAMOFF drives the sensor's sleep register
 * (ov02c10_set_stream -> OV02C10_REG_SLEEP_MODE), so between captures the
 * camera is idle instead of streaming 30 fps that nobody reads. The task
 * itself still ticks every 2 s: that feeds the task watchdog and runs the
 * brightness glide, so dimming stays smooth however long the interval is.
 *
 * Caveat: the ISP auto-exposure compresses the usable range (it tries to
 * normalize frames), but dark-room vs lit-room still separates clearly at
 * the AE limits, which is exactly the bedside use case. To make the most of
 * that compressed range, the darkest and brightest luma ever measured are
 * remembered (persisted in settings) and used as floor/ceiling to normalize
 * each sample before it is mapped to brightness.
 *
 * Resilience: every capture is a full re-arm (REQBUFS/QBUF, STREAMON, task,
 * STREAMOFF), so a driver hiccup only costs one sample. After a few captures
 * in a row without a frame the panel is parked at day brightness so the clock
 * stays readable, and captures simply keep being attempted each period. The
 * one unrecoverable case is a stream task that never exits (wedged in DQBUF):
 * then the driver is left alone until reboot. */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

#include "app_common.h"
#include "settings.h"
#include "alarm.h"
#include "ui_clock.h"
#include "ui_settings.h"
#include "ambient.h"
#include "app_video.h"

static const char *TAG = "ambient";

#define TICK_MS          2000    /* watchdog feed + brightness glide */
#define SLEW_MIN         4       /* smallest step once close to target */

#define CAL_MIN_SPAN        16                    /* below this, treat as uncalibrated */
#define CAL_SAVE_PERIOD_US  (5LL * 60 * 1000000)  /* NVS write throttle */

/* One capture: STREAMON, let the ISP AE settle for CAPTURE_SKIP_FRAMES
 * (~200 ms at 30 fps), measure the next frame, STREAMOFF. */
#define CAPTURE_SKIP_FRAMES 6
#define CAPTURE_TIMEOUT_MS  600                   /* skip + 1 frame is ~230 ms */
#define STOP_TIMEOUT_MS     1000                  /* stream task exit after STREAMOFF */

#define STALL_CAPTURES      3                     /* consecutive empty captures */

static volatile bool s_sample_req;    /* frame_cb: a measurement is wanted */
static volatile int  s_skip;          /* frame_cb: AE-settle frames left to drop */
static volatile uint8_t s_luma = 128;
static int s_target = -1;             /* brightness the last capture asked for */
static int s_applied = -1;            /* brightness last written to the panel */
static int64_t s_last_cal_save;       /* 0 = never */
static bool s_cal_dirty;              /* bounds changed since last persist */

static int s_fd = -1;                 /* camera device */
static bool s_first_capture = true;   /* buffers still queued from init */
static int64_t s_last_capture_us;     /* 0 = never */
static int s_no_frame;                /* consecutive captures without a frame */
static bool s_stalled;
static bool s_camera_dead;            /* stream task wedged: hands off */

static void frame_cb(uint8_t *camera_buf, uint8_t camera_buf_index,
                     uint32_t hes, uint32_t ves, size_t len, void *user_data)
{
    if (!s_sample_req || camera_buf == NULL || len < 2) {
        return;
    }
    if (s_skip > 0) {
        s_skip--; /* first frames after wake: exposure still converging */
        return;
    }
    /* subsample every 64th RGB565 pixel */
    const uint16_t *px = (const uint16_t *)camera_buf;
    size_t n = len / 2;
    uint64_t acc = 0; /* 64-bit so a future resolution change can't overflow */
    uint32_t cnt = 0;
    for (size_t i = 0; i < n; i += 64) {
        uint16_t p = px[i];
        uint32_t r = (p >> 11) & 0x1F;
        uint32_t g = (p >> 5) & 0x3F;
        uint32_t b = p & 0x1F;
        /* to 8-bit-ish luma */
        acc += (r << 3) * 77 + (g << 2) * 150 + (b << 3) * 29;
        cnt++;
    }
    if (cnt > 0) {
        s_luma = (uint8_t)((acc / cnt) >> 8);
    }
    s_sample_req = false;
}

static void apply_brightness(int level)
{
    bsp_display_brightness_set(level);
    ui_clock_set_dim_level(level);
}

/* Wake the camera, measure one frame into s_luma, put it back to sleep.
 * Runs on the ambient task; the stream task is created and deleted inside.
 * False when no frame arrived (the stream is stopped regardless). */
static bool capture_frame(void)
{
    s_skip = CAPTURE_SKIP_FRAMES;
    s_sample_req = true;
    /* init left the buffers queued, so the very first run is a plain START;
     * every later one re-queues them (STREAMOFF dequeues everything) */
    esp_err_t err = s_first_capture ? app_video_stream_task_start(s_fd, 1, NULL)
                                    : app_video_stream_task_restart(s_fd);
    if (err != ESP_OK) {
        s_sample_req = false;
        ESP_LOGW(TAG, "camera start failed: %s", esp_err_to_name(err));
        return false;
    }
    s_first_capture = false;

    for (int i = 0; i < CAPTURE_TIMEOUT_MS / 50 && s_sample_req; i++) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    bool got = !s_sample_req;
    s_sample_req = false;

    /* the stream task STREAMOFFs (sensor sleep) on its way out */
    app_video_stream_task_stop(s_fd);
    if (!app_video_stream_wait_stop(STOP_TIMEOUT_MS)) {
        s_camera_dead = true;
        ui_log("Ambient: camera stream task did not stop - auto-dim off until reboot");
        return false;
    }
    return got;
}

/* A capture produced no frame. After STALL_CAPTURES in a row make the failure
 * visible and keep the panel readable; the next period tries again anyway. */
static void handle_no_frame(const bedside_settings_t *cfg)
{
    if (++s_no_frame < STALL_CAPTURES && !s_camera_dead) {
        return;
    }
    if (!s_stalled) {
        s_stalled = true;
        ui_log("Ambient: no camera frames - auto-dim suspended, panel at day "
               "brightness");
        s_target = cfg->bright_max;
        s_applied = cfg->bright_max;
        apply_brightness(s_applied);
    }
}

/* Turn the measured luma into a target brightness (expanding the stored
 * floor/ceiling on the way). Writes are limited to the two single-byte
 * calibration fields; the LVGL task never touches them while this task runs
 * (it skips while settings are open), so no locking is needed. */
static void update_target(bedside_settings_t *cfg)
{
    uint8_t luma = s_luma;

    if (luma < cfg->luma_floor) {
        cfg->luma_floor = luma;
        s_cal_dirty = true;
    }
    if (luma > cfg->luma_ceil) {
        cfg->luma_ceil = luma;
        s_cal_dirty = true;
    }

    /* Normalize against the measured range so the compressed AE output still
     * spans the full brightness range once calibrated. Until the span is
     * meaningful (early life, or right after a reset), use the raw luma. */
    int span = (int)cfg->luma_ceil - (int)cfg->luma_floor;
    int norm;
    if (span >= CAL_MIN_SPAN) {
        norm = (((int)luma - (int)cfg->luma_floor) * 255) / span;
        if (norm < 0) {
            norm = 0;
        }
        if (norm > 255) {
            norm = 255;
        }
    } else {
        norm = luma;
    }

    /* Square the luma so the mapping is biased dark: the camera's
     * auto-exposure lifts dim scenes toward mid-gray, so a linear map
     * barely dims. Squaring pushes anything below ~50% luma firmly
     * toward the night floor while bright rooms still read bright. */
    int luma_sq = (norm * norm) / 255;
    int target = cfg->bright_min +
                 ((cfg->bright_max - cfg->bright_min) * luma_sq) / 255;
    if (target < 1) {
        target = 1;
    }
    if (target != s_target) {
        ESP_LOGI(TAG, "luma %u range [%u..%u] norm %d (sq %d) -> target %d",
                 luma, cfg->luma_floor, cfg->luma_ceil, norm, luma_sq, target);
    }
    s_target = target;
}

/* Cover half the remaining distance to s_target each tick (at least
 * SLEW_MIN, never overshooting). */
static void glide(void)
{
    if (s_target < 0) {
        return; /* nothing measured yet */
    }
    int prev = s_applied;
    if (s_applied < 0) {
        s_applied = s_target;
    } else {
        int diff = s_target - s_applied;
        int step = diff / 2;
        if (step == 0) {
            step = diff;
        } else if (step > 0 && step < SLEW_MIN) {
            step = (diff < SLEW_MIN) ? diff : SLEW_MIN;
        } else if (step < 0 && step > -SLEW_MIN) {
            step = (diff > -SLEW_MIN) ? diff : -SLEW_MIN;
        }
        s_applied += step;
    }
    if (s_applied != prev) {
        apply_brightness(s_applied);
        ESP_LOGD(TAG, "glide %d -> %d (target %d)", prev, s_applied, s_target);
    }
}

static void ambient_task(void *arg)
{
    bool held = false; /* brightness was changed behind our back */

    if (esp_task_wdt_add(NULL) != ESP_OK) {
        ESP_LOGW(TAG, "task watchdog subscribe failed");
    }

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
        esp_task_wdt_reset();

        /* hands off while the alarm rings or the user is in settings */
        if (alarm_is_active() || ui_settings_is_open()) {
            held = true;
            continue;
        }
        if (held) {
            /* The alarm (bright_max) or a settings slider drove the panel
             * while we were hands-off, so s_applied no longer reflects the
             * hardware. Without this reset, a target equal to the stale
             * s_applied would skip the write and the panel could stay at
             * alarm brightness all night. */
            held = false;
            s_applied = -1;
        }

        bedside_settings_t *cfg = settings_get();

        /* capture on the user's interval; settings_sanitize keeps it in
         * range, and a change takes effect from the next tick */
        int64_t now = esp_timer_get_time();
        int64_t period_us = (int64_t)cfg->ambient_period_s * 1000000LL;
        if (!s_camera_dead &&
            (s_last_capture_us == 0 || now - s_last_capture_us >= period_us)) {
            s_last_capture_us = now;
            if (capture_frame()) {
                if (s_stalled) {
                    s_stalled = false;
                    s_applied = -1; /* re-apply from the measured target */
                    ui_log("Ambient: camera frames resumed - auto-dim active");
                }
                s_no_frame = 0;
                update_target(cfg);
            } else {
                handle_no_frame(cfg);
            }
        }

        glide();

        /* persist expanded bounds, throttled to spare the flash; any
         * user-triggered settings_save() also carries them (same blob) */
        if (s_cal_dirty) {
            if (s_last_cal_save == 0 ||
                now - s_last_cal_save >= CAL_SAVE_PERIOD_US) {
                settings_save();
                s_last_cal_save = now;
                s_cal_dirty = false;
            }
        }
    }
}

void ambient_get_calibration(uint8_t *min_out, uint8_t *max_out)
{
    const bedside_settings_t *cfg = settings_get();
    if (min_out != NULL) {
        *min_out = cfg->luma_floor;
    }
    if (max_out != NULL) {
        *max_out = cfg->luma_ceil;
    }
}

uint8_t ambient_get_last_luma(void)
{
    return s_luma;
}

void ambient_reset_calibration(void)
{
    bedside_settings_t *cfg = settings_get();
    cfg->luma_floor = 255;
    cfg->luma_ceil = 0;
    settings_save();
}

void ambient_init(void)
{
    esp_err_t err = app_video_main(bsp_i2c_get_handle());
    if (err != ESP_OK) {
        ui_log("Ambient light: camera init failed (%s) - auto-dim disabled",
               esp_err_to_name(err));
        return;
    }

    int fd = app_video_open(APP_VIDEO_CAM_DEV_PATH, APP_VIDEO_FMT);
    if (fd < 0) {
        ui_log("Ambient light: camera open failed - auto-dim disabled");
        return;
    }

    uint32_t buf_sz = app_video_get_buf_size();
    static const void *bufs[APP_VIDEO_CAM_BUF_NUM];
    bool failed = false;
    for (int i = 0; i < APP_VIDEO_CAM_BUF_NUM; i++) {
        bufs[i] = heap_caps_aligned_alloc(128, buf_sz,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (bufs[i] == NULL) {
            ui_log("Ambient light: no PSRAM for camera buffers");
            failed = true;
            break;
        }
    }
    if (!failed && app_video_set_bufs(fd, APP_VIDEO_CAM_BUF_NUM, bufs) != ESP_OK) {
        ui_log("Ambient light: camera buffer setup failed");
        failed = true;
    }
    if (failed) {
        /* free whatever was allocated; auto-dim simply stays off */
        for (int i = 0; i < APP_VIDEO_CAM_BUF_NUM; i++) {
            heap_caps_free((void *)bufs[i]);
            bufs[i] = NULL;
        }
        app_video_close();
        return;
    }
    app_video_register_frame_operation_cb(frame_cb);
    s_fd = fd;

    /* The camera is NOT started here: the task's first tick does the first
     * capture, and the sensor sleeps between captures from then on. */

    /* 6 KB: settings_save() (NVS+flash path) runs on this task, so the
     * stack must stay in internal RAM */
    if (xTaskCreate(ambient_task, "ambient", 6144, NULL, 3, NULL) != pdPASS) {
        ui_log("Ambient light: task create failed (internal RAM?) - auto-dim disabled");
        return; /* camera stays asleep; buffers are simply never used */
    }
    ESP_LOGI(TAG, "ambient light sensing running");
}
