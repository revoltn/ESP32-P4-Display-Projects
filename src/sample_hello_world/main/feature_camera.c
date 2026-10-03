/* Live camera preview: OV02C10 (1920x1080 RGB565 via ISP) -> PPA rotate/scale
 * -> full-screen LVGL canvas. Tap the preview to exit.
 *
 * The DSI framebuffers are owned by lvgl_port (AVOID_TEAR + DIRECT_MODE), so
 * frames must go through LVGL: PPA writes into our own PSRAM canvas buffer
 * under the display lock, then the canvas is invalidated. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"
#include "driver/ppa.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"

#include "app_common.h"
#include "app_video.h"

static const char *TAG = "camera";

/* Screen is 480x800 portrait; sensor is 1920x1080. No rotation (per user):
 * centered 640x1066 crop scaled by 0.75 (12/16 PPA fragments, exact) fills
 * the screen: 640*0.75 = 480 wide, 1066*0.75 = 799 tall (the last canvas
 * row stays black - the buffer is zeroed at alloc). */
#define CAM_ROTATION        PPA_SRM_ROTATION_ANGLE_0
#define CAM_CROP_W          640
#define CAM_CROP_H          1066
#define CAM_SCALE           0.75f
#define PREVIEW_W           480
#define PREVIEW_H           800

static ppa_client_handle_t s_ppa_client;
static size_t s_cache_line;
static bool s_video_inited;
static int s_cam_fd = -1;
static bool s_bufs_ok;
static bool s_stream_started_once;
static uint8_t *s_cam_bufs[APP_VIDEO_CAM_BUF_NUM];
static uint8_t *s_canvas_buf;

static SemaphoreHandle_t s_exit_sem;
static lv_obj_t *s_canvas;

/* LVGL task context. */
static void canvas_click_cb(lv_event_t *e)
{
    xSemaphoreGive(s_exit_sem);
}

/* Runs on the video stream task for every dequeued frame. */
static void camera_frame_cb(uint8_t *camera_buf, uint8_t camera_buf_index,
                            uint32_t camera_buf_hes, uint32_t camera_buf_ves,
                            size_t camera_buf_len, void *user_data)
{
    if (!bsp_display_lock(100)) {
        return; /* renderer busy - drop this frame */
    }

    ppa_srm_oper_config_t srm_config = {
        .in = {
            .buffer         = camera_buf,
            .pic_w          = camera_buf_hes,
            .pic_h          = camera_buf_ves,
            .block_w        = CAM_CROP_W,
            .block_h        = CAM_CROP_H,
            .block_offset_x = (camera_buf_hes - CAM_CROP_W) / 2,
            .block_offset_y = (camera_buf_ves - CAM_CROP_H) / 2,
            .srm_cm         = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer         = s_canvas_buf,
            .buffer_size    = PREVIEW_W * PREVIEW_H * 2,
            .pic_w          = PREVIEW_W,
            .pic_h          = PREVIEW_H,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm         = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = CAM_ROTATION,
        .scale_x        = CAM_SCALE,
        .scale_y        = CAM_SCALE,
        .mode           = PPA_TRANS_MODE_BLOCKING,
    };
    esp_err_t err = ppa_do_scale_rotate_mirror(s_ppa_client, &srm_config);
    if (err == ESP_OK) {
        lv_obj_invalidate(s_canvas);
    } else {
        ESP_LOGE(TAG, "PPA scale-rotate failed: %s", esp_err_to_name(err));
    }

    bsp_display_unlock();
}

/* Create the full-screen preview canvas + hint label on the top layer once. */
static bool camera_ui_ensure_created(void)
{
    if (s_canvas) {
        return true;
    }

    s_exit_sem = xSemaphoreCreateBinary();
    if (!s_exit_sem) {
        return false;
    }

    s_canvas_buf = heap_caps_aligned_calloc(s_cache_line, 1, PREVIEW_W * PREVIEW_H * 2,
                                            MALLOC_CAP_SPIRAM);
    if (!s_canvas_buf) {
        ui_log("Camera: canvas buffer alloc failed");
        return false;
    }

    bsp_display_lock(0);
    s_canvas = lv_canvas_create(lv_layer_top());
    lv_canvas_set_buffer(s_canvas, s_canvas_buf, PREVIEW_W, PREVIEW_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas, 0, 0);
    lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_canvas, canvas_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hint = lv_label_create(s_canvas);
    lv_label_set_text(hint, "Tap to exit");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(hint, lv_color_white(), 0);
    lv_obj_set_style_bg_color(hint, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(hint, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(hint, 6, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_remove_flag(hint, LV_OBJ_FLAG_CLICKABLE);
    bsp_display_unlock();

    return true;
}

/* One-time camera bring-up; failures do not latch, so a re-tap re-probes. */
static bool camera_ensure_init(void)
{
    if (s_cam_fd >= 0 && s_bufs_ok) {
        return true;
    }

    esp_err_t err;

    if (s_cache_line == 0) {
        ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &s_cache_line));
    }

    if (!s_ppa_client) {
        ppa_client_config_t ppa_config = { .oper_type = PPA_OPERATION_SRM };
        err = ppa_register_client(&ppa_config, &s_ppa_client);
        if (err != ESP_OK) {
            ui_log("Camera: PPA client register failed: %s", esp_err_to_name(err));
            return false;
        }
    }

    if (!camera_ui_ensure_created()) {
        return false;
    }

    if (!s_video_inited) {
        ui_log("Probing OV02C10 on MIPI-CSI (SCCB 0x36, shared I2C bus)...");
        err = app_video_main(bsp_i2c_get_handle());
        if (err != ESP_OK) {
            ui_log("Camera init failed (%s) - is the camera module plugged in?",
                   esp_err_to_name(err));
            ui_set_status("Camera not detected");
            return false;
        }
        s_video_inited = true;
    }

    if (s_cam_fd < 0) {
        s_cam_fd = app_video_open(APP_VIDEO_CAM_DEV_PATH, APP_VIDEO_FMT);
        if (s_cam_fd < 0) {
            ui_log("Camera device open failed - is the camera module plugged in?");
            ui_set_status("Camera not detected");
            return false;
        }
    }

    uint32_t buf_size = app_video_get_buf_size();
    for (int i = 0; i < APP_VIDEO_CAM_BUF_NUM; i++) {
        if (!s_cam_bufs[i]) {
            s_cam_bufs[i] = heap_caps_aligned_calloc(s_cache_line, 1,
                                                     (buf_size + s_cache_line - 1) & ~(s_cache_line - 1),
                                                     MALLOC_CAP_SPIRAM);
        }
        if (!s_cam_bufs[i]) {
            ui_log("Camera: frame buffer alloc failed (%lu bytes)", (unsigned long)buf_size);
            return false;
        }
    }

    if (app_video_set_bufs(s_cam_fd, APP_VIDEO_CAM_BUF_NUM, (const void **)s_cam_bufs) != ESP_OK) {
        ui_log("Camera: buffer setup failed");
        return false; /* fd stays open; next tap retries */
    }

    app_video_register_frame_operation_cb(camera_frame_cb);
    s_bufs_ok = true;
    return true;
}

void feature_camera_run(void)
{
    ui_log("Starting camera preview...");

    if (!camera_ensure_init()) {
        return;
    }

    while (xSemaphoreTake(s_exit_sem, 0) == pdTRUE) {
        /* drain stale taps */
    }

    esp_err_t err;
    if (!s_stream_started_once) {
        err = app_video_stream_task_start(s_cam_fd, 1, NULL);
    } else {
        err = app_video_stream_task_restart(s_cam_fd);
    }
    if (err != ESP_OK) {
        /* fd stays open; the next tap retries the restart path */
        ui_log("Camera stream start failed");
        ui_set_status("Camera error");
        return;
    }
    s_stream_started_once = true;

    bsp_display_lock(0);
    lv_obj_remove_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
    ui_set_status("Camera preview - tap to exit");
    ESP_LOGI(TAG, "preview running");

    xSemaphoreTake(s_exit_sem, portMAX_DELAY);

    app_video_stream_task_stop(s_cam_fd);
    app_video_stream_wait_stop();

    bsp_display_lock(0);
    lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
    ui_log("Camera preview stopped");
    ui_set_status("Ready");
}
