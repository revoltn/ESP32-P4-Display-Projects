/*
 * V4L2 camera wrapper for the OV02C10 on MIPI-CSI, ported from the vendor
 * video_lcd_display demo (Espressif CC0 example code) with the Brookesia
 * camera app's stop handshake grafted on.
 */
#pragma once

#include "esp_err.h"
#include "linux/videodev2.h"
#include "esp_video_device.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_VIDEO_FMT_RAW8   = V4L2_PIX_FMT_SBGGR8,
    APP_VIDEO_FMT_RAW10  = V4L2_PIX_FMT_SBGGR10,
    APP_VIDEO_FMT_GREY   = V4L2_PIX_FMT_GREY,
    APP_VIDEO_FMT_RGB565 = V4L2_PIX_FMT_RGB565,
    APP_VIDEO_FMT_RGB888 = V4L2_PIX_FMT_RGB24,
    APP_VIDEO_FMT_YUV422 = V4L2_PIX_FMT_YUV422P,
    APP_VIDEO_FMT_YUV420 = V4L2_PIX_FMT_YUV420,
} video_fmt_t;

#define APP_VIDEO_CAM_DEV_PATH  (ESP_VIDEO_MIPI_CSI_DEVICE_NAME)
#define APP_VIDEO_CAM_BUF_NUM   (2)
#define APP_VIDEO_FMT           (APP_VIDEO_FMT_RGB565)

typedef void (*app_video_frame_operation_cb_t)(uint8_t *camera_buf, uint8_t camera_buf_index,
                                               uint32_t camera_buf_hes, uint32_t camera_buf_ves,
                                               size_t camera_buf_len, void *user_data);

/* Init esp_video with SCCB riding the shared board I2C bus (GT911 + ES8311
 * live on the same bus — never let esp_video create its own driver). */
esp_err_t app_video_main(i2c_master_bus_handle_t i2c_bus_handle);

/* Open the CSI capture device and select init_fmt. Returns fd or -1. */
int app_video_open(char *dev, video_fmt_t init_fmt);

/* REQBUFS/QBUF the given user-pointer buffers (NULL fb = MMAP mode).
 * The fd stays open on failure, so the caller may retry. */
esp_err_t app_video_set_bufs(int video_fd, uint32_t fb_num, const void **fb);

/* Frame size in bytes for the negotiated resolution and APP_VIDEO_FMT. */
uint32_t app_video_get_buf_size(void);

/* STREAMON + spawn the DQBUF->callback->QBUF stream task on core_id. */
esp_err_t app_video_stream_task_start(int video_fd, int core_id, void *user_data);

/* Re-queue the buffers from the previous run and start streaming again. */
esp_err_t app_video_stream_task_restart(int video_fd);

/* Ask the stream task to STREAMOFF and delete itself... */
esp_err_t app_video_stream_task_stop(int video_fd);

/* ...then block here until it has actually done so. False if the task did
 * not exit within timeout_ms (wedged in DQBUF with no frames coming): the
 * handle stays set, so a later restart is refused rather than doubled. */
bool app_video_stream_wait_stop(uint32_t timeout_ms);

esp_err_t app_video_register_frame_operation_cb(app_video_frame_operation_cb_t operation_cb);

/* Close the capture device opened by app_video_open (bring-up failed and
 * the caller is giving up). Safe to call when nothing is open. */
void app_video_close(void);

#ifdef __cplusplus
}
#endif
