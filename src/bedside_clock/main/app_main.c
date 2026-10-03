/* bedside_clock - JC4880P443C bedside alarm clock with Navidrome wake-up.
 *
 * Boot: NVS + settings -> display (landscape via LVGL sw rotation) -> clock
 * UI -> worker task -> async: WiFi autoconnect, SNTP, alarm scheduler,
 * camera ambient-light dimming. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_core_dump.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"

#include "app_common.h"
#include "settings.h"
#include "clock_sync.h"
#include "ui_clock.h"
#include "boot_seq.h"
#include "ui_watch.h"
#include "evlog.h"
#include "fonts/app_fonts.h"

static const char *TAG = "app_main";

static bool s_display_ok;

bool app_display_ready(void)
{
    return s_display_ok;
}

/* ---------- worker: run blocking jobs off the LVGL task ---------- */

typedef struct {
    void (*fn)(void *);
    void *arg;
} async_job_t;

static QueueHandle_t s_job_queue;
static bool s_audio_init_done;
/* Recursive: the lazy-init chain nests (music task -> player -> codec). */
static SemaphoreHandle_t s_init_mtx;

bool app_run_async(void (*fn)(void *), void *arg)
{
    async_job_t job = { .fn = fn, .arg = arg };
    if (s_job_queue == NULL || xQueueSend(s_job_queue, &job, 0) != pdTRUE) {
        ESP_LOGE(TAG, "async job queue full, dropping job");
        return false;
    }
    return true;
}

void app_init_lock(void)
{
    if (s_init_mtx != NULL) {
        xSemaphoreTakeRecursive(s_init_mtx, portMAX_DELAY);
    }
}

void app_init_unlock(void)
{
    if (s_init_mtx != NULL) {
        xSemaphoreGiveRecursive(s_init_mtx);
    }
}

static void worker_task(void *arg)
{
    async_job_t job;
    for (;;) {
        if (xQueueReceive(s_job_queue, &job, portMAX_DELAY) == pdTRUE &&
            job.fn != NULL) {
            job.fn(job.arg);
        }
    }
}

bool app_audio_ensure_init(void)
{
    if (s_audio_init_done) {
        return true;
    }
    app_init_lock();
    if (s_audio_init_done) { /* another task won the race */
        app_init_unlock();
        return true;
    }
    esp_err_t err = bsp_extra_codec_init();
    if (err != ESP_OK) {
        app_init_unlock();
        ui_log("Audio codec init FAILED: %s", esp_err_to_name(err));
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(200)); /* settle time, per vendor demo */
    bsp_extra_codec_volume_set(settings_get()->volume, NULL);
    s_audio_init_done = true;
    app_init_unlock();
    return true;
}

bool app_audio_is_ready(void)
{
    return s_audio_init_done;
}

/* ---------- async media volume ----------
 * bsp_extra_codec_volume_set() takes the codec's _play_mtx with
 * portMAX_DELAY. While a song plays the decoder task (prio 5) holds that
 * mutex for the whole blocking I2S write - ~26 ms of every ~28 ms cycle -
 * and re-takes it within a few ms of releasing it, so a lower-priority
 * waiter can lose the hand-off for many cycles in a row. Called from an
 * LVGL callback (a slider drag fires once per pixel) that parked the LVGL
 * task, and with it touch polling and redraws, for as long as the user
 * dragged: the "UI dead while a song plays" bug of 2026-09-06. Only the
 * worker waits on the codec now; requests coalesce so a drag posts one job. */

static portMUX_TYPE s_vol_mux = portMUX_INITIALIZER_UNLOCKED;
static int s_vol_req = -1;       /* latest requested volume */
static bool s_vol_job_pending;   /* a worker job will pick s_vol_req up */

static void volume_job(void *arg)
{
    for (;;) {
        taskENTER_CRITICAL(&s_vol_mux);
        int v = s_vol_req;
        s_vol_job_pending = false;
        taskEXIT_CRITICAL(&s_vol_mux);
        if (v >= 0 && app_audio_is_ready()) {
            bsp_extra_codec_volume_set(v, NULL);
        }
        /* a newer value arrived while we were applying: go again rather
         * than leave the slider's final position unapplied */
        taskENTER_CRITICAL(&s_vol_mux);
        bool again = (s_vol_req != v) && !s_vol_job_pending;
        if (again) {
            s_vol_job_pending = true;
        }
        taskEXIT_CRITICAL(&s_vol_mux);
        if (!again) {
            return;
        }
    }
}

void app_audio_set_volume_async(int v)
{
    if (v < 0) {
        v = 0;
    } else if (v > 100) {
        v = 100;
    }
    bool post;
    taskENTER_CRITICAL(&s_vol_mux);
    s_vol_req = v;
    post = !s_vol_job_pending;
    if (post) {
        s_vol_job_pending = true;
    }
    taskEXIT_CRITICAL(&s_vol_mux);
    if (post && !app_run_async(volume_job, NULL)) {
        /* queue full: drop the live update; play_list() re-applies the
         * stored setting at the next session anyway */
        taskENTER_CRITICAL(&s_vol_mux);
        s_vol_job_pending = false;
        taskEXIT_CRITICAL(&s_vol_mux);
        ESP_LOGD(TAG, "volume job dropped (worker queue full)");
    }
}

/* ---------- allocation-failure trace ----------
 * Internal RAM is the scarce resource on this board and most callers degrade
 * quietly on a NULL. One rate-limited line per failure (size + caps + the
 * function that asked) makes an OOM-driven glitch attributable from the
 * serial log. Runs in the allocating task's context: no malloc, no ESP_LOG
 * (which may itself allocate) - esp_rom_printf only. */
static void failed_alloc_cb(size_t size, uint32_t caps, const char *fn)
{
    static int64_t s_last_us;
    int64_t now = esp_timer_get_time();
    if (s_last_us != 0 && now - s_last_us < 5 * 1000000) {
        return;
    }
    s_last_us = now;
    esp_rom_printf("E app_main: alloc FAILED %u B caps=0x%x in %s (internal free %u, largest %u)\n",
                   (unsigned)size, (unsigned)caps, fn != NULL ? fn : "?",
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

/* ---------- crash-loop breaker (carried over from hello_world) ---------- */

#define CRASH_NVS_NS  "bedside"
#define CRASH_NVS_KEY "crash_count"
/* The count is passed to boot_seq_start, which decides what safe mode means. */

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_SW:        return "software reset";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "other watchdog";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_USB:       return "USB reset";
    case ESP_RST_JTAG:      return "JTAG reset";
    default:                return "other";
    }
}

static uint32_t crash_guard_check(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    bool abnormal = (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
                     reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT ||
                     reason == ESP_RST_BROWNOUT);

    nvs_handle_t nvs;
    if (nvs_open(CRASH_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) {
        return 0;
    }
    uint32_t count = 0;
    nvs_get_u32(nvs, CRASH_NVS_KEY, &count);
    count = abnormal ? count + 1 : 0;
    nvs_set_u32(nvs, CRASH_NVS_KEY, count);
    nvs_commit(nvs);
    nvs_close(nvs);
    if (abnormal) {
        ESP_LOGW(TAG, "Abnormal reset (%s), consecutive crash count %lu",
                 reset_reason_str(reason), (unsigned long)count);
    } else {
        ESP_LOGI(TAG, "Reset reason: %s", reset_reason_str(reason));
    }
    return count;
}

/* A stored core dump (coredump partition, ELF) survives until the next
 * panic overwrites it: say so on the device log so it gets pulled with
 * `idf.py coredump-info` before it is lost. */
static bool s_coredump_present;

static void coredump_check(void)
{
    s_coredump_present = (esp_core_dump_image_check() == ESP_OK);
    if (s_coredump_present) {
        ESP_LOGW(TAG, "core dump from an earlier crash is stored in flash");
    }
}

static esp_timer_handle_t s_stable_timer;

/* The timer is periodic so a dropped job (worker queue full during boot) or
 * a failed NVS write is simply retried next period; it is deleted only once
 * the counter is provably cleared. A one-shot timer used to leave the
 * counter standing for the whole uptime whenever the mark was dropped. */
static void crash_guard_mark_stable_job(void *arg)
{
    nvs_handle_t nvs;
    bool ok = false;
    if (nvs_open(CRASH_NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        ok = nvs_set_u32(nvs, CRASH_NVS_KEY, 0) == ESP_OK &&
             nvs_commit(nvs) == ESP_OK;
        nvs_close(nvs);
    }
    if (!ok) {
        ESP_LOGW(TAG, "crash counter clear failed - retrying in 30 s");
        return;
    }
    if (s_stable_timer != NULL) {
        esp_timer_stop(s_stable_timer);
        esp_timer_delete(s_stable_timer);
        s_stable_timer = NULL;
    }
    ESP_LOGI(TAG, "stable for 30 s: crash counter cleared");
}

/* esp_timer callbacks must stay short - the flash write runs on the worker. */
static void crash_guard_timer_cb(void *arg)
{
    if (!app_run_async(crash_guard_mark_stable_job, NULL)) {
        /* queue full: the periodic timer tries again next period */
        ESP_LOGW(TAG, "crash-stable mark deferred: job queue full");
    }
}

/* ---------- orientation ---------- */

void app_apply_orientation(void)
{
    lv_display_rotation_t rot;
    switch (settings_get()->orientation) {
    case ORIENT_LANDSCAPE_FLIP: rot = LV_DISPLAY_ROTATION_270; break;
    case ORIENT_PORTRAIT:       rot = LV_DISPLAY_ROTATION_0;   break;
    case ORIENT_LANDSCAPE:
    default:                    rot = LV_DISPLAY_ROTATION_90;  break;
    }
    lv_display_set_rotation(lv_display_get_default(), rot);
}

/* ---------- boot ---------- */

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    uint32_t crash_count = crash_guard_check();
    coredump_check();
    /* journal first, so every later stage (even a failing one) is recorded */
    evlog_init(reset_reason_str(esp_reset_reason()), crash_count,
               s_coredump_present);
    heap_caps_register_failed_alloc_callback(failed_alloc_cb);
    settings_init();

    /* One-time-init serialization and the TZ lock must exist before any
     * task that might race a lazy init or read local time. */
    s_init_mtx = xSemaphoreCreateRecursiveMutex();
    assert(s_init_mtx);
    clock_sync_early_init();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * 160,
        .double_buffer = false,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = true,   /* landscape on the portrait panel */
        }
    };
    /* LVGL 9 renders recursively (one frame per object nesting level plus
     * the draw pipeline); the port's 7168 B default left ~150 B of headroom
     * and the Alarms page (2026-09-19) tripped the stack guard inside
     * lv_draw_label. Internal RAM: the LVGL task never writes flash, but
     * the port pins its stack internal anyway. */
    cfg.lvgl_port_cfg.task_stack = 12288;
    /* A panel that fails to come up must not take the alarm with it: every
     * LVGL helper checks app_display_ready() before touching the port. */
    s_display_ok = (bsp_display_start_with_config(&cfg) != NULL);
    if (s_display_ok) {
        bsp_display_brightness_set(settings_get()->bright_max);
        app_fonts_init(); /* Cyrillic/accent fallbacks, before any label */
        bsp_display_lock(0);
        app_apply_orientation();
        ui_clock_create();
        ui_watch_start(); /* LVGL stall detector + touch trace */
        bsp_display_unlock();
    } else {
        ESP_LOGE(TAG, "display start FAILED - running headless (alarm chime, "
                 "voice and API still work)");
    }

    /* 16 deep (128 bytes): boot_job holds the worker for ~25 s while UI
     * jobs, the crash-stable mark and API reconciles queue up behind it */
    s_job_queue = xQueueCreate(16, sizeof(async_job_t));
    assert(s_job_queue);
    /* 16 KB: TLS handshakes and cJSON run on this task */
    bool worker_ok =
        xTaskCreate(worker_task, "worker", 16384, NULL, 5, NULL) == pdPASS;

    const esp_timer_create_args_t stable_args = {
        .callback = crash_guard_timer_cb,
        .name = "crash_stable",
    };
    if (esp_timer_create(&stable_args, &s_stable_timer) == ESP_OK &&
        esp_timer_start_periodic(s_stable_timer, 30 * 1000000ULL) != ESP_OK) {
        esp_timer_delete(s_stable_timer);
        s_stable_timer = NULL;
    }

    if (crash_count > 0) {
        ui_set_status("Recovered from crash (%lu)%s", (unsigned long)crash_count,
                      clock_sync_time_valid() ? " - time kept by RTC" : "");
    } else if (clock_sync_time_valid() && !clock_sync_is_synced()) {
        ui_set_status("Time from RTC - waiting for sync");
    }
    if (settings_load_repairs() > 0) {
        ui_log("Settings: %d stored value(s) were out of range and repaired",
               settings_load_repairs());
    }
    if (s_coredump_present) {
        ui_log("Crash dump stored in flash - read it with: idf.py coredump-info");
    }
    /* what happened before this boot (alarm decisions, stalls, resets) */
    evlog_echo_ui(8);

    /* Stage 0 is done: settings, timezone, display and the clock UI are up and
     * the worker exists. boot_seq owns every service from here. */
    boot_seq_start(crash_count, !worker_ok);
    ESP_LOGI(TAG, "setup done");
}
