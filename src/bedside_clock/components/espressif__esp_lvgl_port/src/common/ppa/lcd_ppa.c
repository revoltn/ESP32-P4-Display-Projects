/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#include <string.h>
#include "esp_err.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "soc/soc_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lcd_ppa.h"

/* LOCAL PATCH (bedside_v01, 2026-09-06): the upstream code issues the rotate in
 * PPA_TRANS_MODE_BLOCKING, which is xSemaphoreTake(portMAX_DELAY) inside
 * esp_driver_ppa waiting for the transaction-done ISR. On the ESP32-P4 rev
 * v1.3 that ISR was seen never arriving (JTAG backtrace: taskLVGL parked in
 * ppa_do_operation for 7+ minutes while a song streamed), which freezes every
 * redraw and - since touch is polled from the LVGL task - all input too.
 * The rotate is now issued NON_BLOCKING and waited for with a bounded
 * timeout; the caller treats ESP_ERR_TIMEOUT as "PPA dead" and falls back to
 * CPU rotation. Keep this patch if the component is refreshed. */
#define PPA_LCD_ENABLE_CB   1
#define PPA_LCD_TIMEOUT_MS  500 /* a full 480x800 RGB565 rotate takes a few ms */

#if SOC_PPA_SUPPORTED
#define ALIGN_UP(num, align)    (((num) + ((align) - 1)) & ~((align) - 1))

struct lvgl_port_ppa_t {
    uint8_t              *buffer;
    uint32_t             buffer_size;
    ppa_client_handle_t  srm_handle;
    ppa_srm_color_mode_t color_mode;
    SemaphoreHandle_t    done_sem;   /* given by the PPA done ISR */
};

static const char *TAG = "PPA";
/*******************************************************************************
* Function definitions
*******************************************************************************/
#if PPA_LCD_ENABLE_CB
static bool _lvgl_port_ppa_callback(ppa_client_handle_t ppa_client, ppa_event_data_t *event_data, void *user_data);
#endif
/*******************************************************************************
* Public API functions
*******************************************************************************/

lvgl_port_ppa_handle_t lvgl_port_ppa_create(const lvgl_port_ppa_cfg_t *cfg)
{
    esp_err_t ret = ESP_OK;
    assert(cfg != NULL);

    lvgl_port_ppa_t *ppa_ctx = malloc(sizeof(lvgl_port_ppa_t));
    ESP_GOTO_ON_FALSE(ppa_ctx, ESP_ERR_NO_MEM, err, TAG, "Not enough memory for PPA context allocation!");
    memset(ppa_ctx, 0, sizeof(lvgl_port_ppa_t));

    uint32_t buffer_caps = 0;
    if (cfg->flags.buff_dma) {
        buffer_caps |= MALLOC_CAP_DMA;
    }
    if (cfg->flags.buff_spiram) {
        buffer_caps |= MALLOC_CAP_SPIRAM;
    }
    if (buffer_caps == 0) {
        buffer_caps |= MALLOC_CAP_DEFAULT;
    }

    ppa_ctx->buffer_size = ALIGN_UP(cfg->buffer_size, CONFIG_CACHE_L2_CACHE_LINE_SIZE);
    ppa_ctx->buffer = heap_caps_aligned_calloc(CONFIG_CACHE_L2_CACHE_LINE_SIZE, ppa_ctx->buffer_size, sizeof(uint8_t),
                      buffer_caps);
    assert(ppa_ctx->buffer != NULL);

    ppa_client_config_t ppa_client_config = {
        .oper_type = PPA_OPERATION_SRM,
    };
    ESP_GOTO_ON_ERROR(ppa_register_client(&ppa_client_config, &ppa_ctx->srm_handle), err, TAG,
                      "Error when registering PPA client!");

#if PPA_LCD_ENABLE_CB
    ppa_event_callbacks_t ppa_cbs = {
        .on_trans_done = _lvgl_port_ppa_callback,
    };
    ESP_GOTO_ON_ERROR(ppa_client_register_event_callbacks(ppa_ctx->srm_handle, &ppa_cbs), err, TAG,
                      "Error when registering PPA callbacks!");
#endif

    ppa_ctx->color_mode = cfg->color_mode;

#if PPA_LCD_ENABLE_CB
    ppa_ctx->done_sem = xSemaphoreCreateBinary();
    ESP_GOTO_ON_FALSE(ppa_ctx->done_sem, ESP_ERR_NO_MEM, err, TAG, "Not enough memory for PPA semaphore!");
#endif

err:
    if (ret != ESP_OK) {
        if (ppa_ctx->srm_handle) {
            ppa_unregister_client(ppa_ctx->srm_handle);
        }
        if (ppa_ctx->buffer) {
            free(ppa_ctx->buffer);
        }
        if (ppa_ctx) {
            free(ppa_ctx);
        }
        ppa_ctx = NULL;
    }

    return ppa_ctx;
}

void lvgl_port_ppa_delete(lvgl_port_ppa_handle_t handle)
{
    lvgl_port_ppa_t *ppa_ctx = (lvgl_port_ppa_t *)handle;
    assert(ppa_ctx != NULL);

    if (ppa_ctx->buffer) {
        free(ppa_ctx->buffer);
    }

    ppa_unregister_client(ppa_ctx->srm_handle);

    if (ppa_ctx->done_sem) {
        vSemaphoreDelete(ppa_ctx->done_sem);
    }

    free(ppa_ctx);
}

uint8_t *lvgl_port_ppa_get_output_buffer(lvgl_port_ppa_handle_t handle)
{
    lvgl_port_ppa_t *ppa_ctx = (lvgl_port_ppa_t *)handle;
    assert(ppa_ctx != NULL);
    return ppa_ctx->buffer;
}

esp_err_t lvgl_port_ppa_rotate(lvgl_port_ppa_handle_t handle, lvgl_port_ppa_disp_rotate_t *rotate_cfg)
{
    lvgl_port_ppa_t *ppa_ctx = (lvgl_port_ppa_t *)handle;
    assert(ppa_ctx != NULL);
    assert(rotate_cfg != NULL);
    const int w = rotate_cfg->area.x2 - rotate_cfg->area.x1 + 1;
    const int h = rotate_cfg->area.y2 - rotate_cfg->area.y1 + 1;

    /* Set dimension by screen size and rotation */
    int out_w = w;
    int out_h = h;

    int x1 = rotate_cfg->area.x1;
    int x2 = rotate_cfg->area.x2;
    int y1 = rotate_cfg->area.y1;
    int y2 = rotate_cfg->area.y2;

    /* Rotate coordinates */
    switch (rotate_cfg->rotation) {
    case PPA_SRM_ROTATION_ANGLE_0:
        break;
    case PPA_SRM_ROTATION_ANGLE_90:
        out_w = h;
        out_h = w;
        x1 = rotate_cfg->area.y1;
        x2 = rotate_cfg->area.y2;
        y1 = rotate_cfg->disp_size.hres - rotate_cfg->area.x2 - 1;
        y2 = rotate_cfg->disp_size.hres - rotate_cfg->area.x1 - 1;
        break;
    case PPA_SRM_ROTATION_ANGLE_180:
        x1 = rotate_cfg->disp_size.hres - rotate_cfg->area.x2 - 1;
        x2 = rotate_cfg->disp_size.hres - rotate_cfg->area.x1 - 1;
        y1 = rotate_cfg->disp_size.vres - rotate_cfg->area.y2 - 1;
        y2 = rotate_cfg->disp_size.vres - rotate_cfg->area.y1 - 1;
        break;
    case PPA_SRM_ROTATION_ANGLE_270:
        out_w = h;
        out_h = w;
        x1 = rotate_cfg->disp_size.vres - rotate_cfg->area.y2 - 1;
        x2 = rotate_cfg->disp_size.vres - rotate_cfg->area.y1 - 1;
        y1 = rotate_cfg->area.x1;
        y2 = rotate_cfg->area.x2;
        break;
    }
    /* Return new coordinates */
    rotate_cfg->area.x1 = x1;
    rotate_cfg->area.x2 = x2;
    rotate_cfg->area.y1 = y1;
    rotate_cfg->area.y2 = y2;

    /* Prepare Operation     */
    ppa_srm_oper_config_t srm_oper_config = {
        .in.buffer = rotate_cfg->in_buff,
        .in.pic_w = w,
        .in.pic_h = h,
        .in.block_w = w,
        .in.block_h = h,
        .in.block_offset_x = 0,
        .in.block_offset_y = 0,
        .in.srm_cm = ppa_ctx->color_mode,

        .out.buffer = ppa_ctx->buffer,
        .out.buffer_size = ppa_ctx->buffer_size,
        .out.pic_w = out_w,
        .out.pic_h = out_h,
        .out.block_offset_x = 0,
        .out.block_offset_y = 0,
        .out.srm_cm = ppa_ctx->color_mode,

        .rotation_angle = rotate_cfg->rotation,
        .scale_x = 1.0,
        .scale_y = 1.0,

        .byte_swap = rotate_cfg->swap_bytes,

        .mode = rotate_cfg->ppa_mode,
        .user_data = rotate_cfg->user_data,
    };

#if PPA_LCD_ENABLE_CB
    if (rotate_cfg->ppa_mode == PPA_TRANS_MODE_BLOCKING && ppa_ctx->done_sem != NULL) {
        /* Bounded wait instead of the driver's portMAX_DELAY (see header comment).
         * The done callback receives the TRANSACTION's user_data, so it must be
         * our context (the caller's user_data was unused upstream anyway). */
        srm_oper_config.mode = PPA_TRANS_MODE_NON_BLOCKING;
        srm_oper_config.user_data = ppa_ctx;
        xSemaphoreTake(ppa_ctx->done_sem, 0); /* drop a stale completion */
        esp_err_t err = ppa_do_scale_rotate_mirror(ppa_ctx->srm_handle, &srm_oper_config);
        if (err != ESP_OK) {
            return err;
        }
        if (xSemaphoreTake(ppa_ctx->done_sem, pdMS_TO_TICKS(PPA_LCD_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGE(TAG, "SRM rotate did not complete within %d ms", PPA_LCD_TIMEOUT_MS);
            return ESP_ERR_TIMEOUT;
        }
        return ESP_OK;
    }
#endif

    return ppa_do_scale_rotate_mirror(ppa_ctx->srm_handle, &srm_oper_config);
}

#if PPA_LCD_ENABLE_CB
/* ISR context (DMA2D transaction-done). */
static bool _lvgl_port_ppa_callback(ppa_client_handle_t ppa_client, ppa_event_data_t *event_data, void *user_data)
{
    (void)ppa_client;
    (void)event_data;
    lvgl_port_ppa_t *ppa_ctx = (lvgl_port_ppa_t *)user_data;
    BaseType_t woken = pdFALSE;
    if (ppa_ctx != NULL && ppa_ctx->done_sem != NULL) {
        xSemaphoreGiveFromISR(ppa_ctx->done_sem, &woken);
    }
    return woken == pdTRUE;
}
#endif

#endif
