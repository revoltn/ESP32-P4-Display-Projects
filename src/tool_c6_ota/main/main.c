/*
 * tool_c6_ota: reflash the ESP32-C6 esp_hosted slave from the ESP32-P4 over the existing
 * SDIO link, with the slave image embedded in this app. One shot: flash this tool to the
 * P4, watch the log, then flash the real firmware back.
 *
 * Flow (after esp_hosted's host_performs_slave_ota example, Apache-2.0):
 *   1. esp_hosted_init + connect, print the running slave version
 *   2. if it already equals the embedded image's version: stop
 *   3. ota_begin, stream the image in 1500 B chunks, ota_end
 *   4. ota_activate when the slave is >= 2.6 (older slaves switch on ota_end), restart host
 *   5. after the restart the version line shows the result
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_hosted_ota.h"
#include "esp_hosted_api_types.h"

static const char *TAG = "c6_ota";

extern const uint8_t slave_image_start[] asm("_binary_slave_image_bin_start");
extern const uint8_t slave_image_end[]   asm("_binary_slave_image_bin_end");

#define CHUNK 1500

/* Version string of the embedded image: esp_app_desc_t follows the image header and the
 * first segment header. */
static const char *embedded_version(void)
{
    const esp_app_desc_t *d = (const esp_app_desc_t *)(slave_image_start +
            sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
    if (d->magic_word == ESP_APP_DESC_MAGIC_WORD) {
        return d->version;
    }
    return "unknown";
}

static bool read_slave_version(esp_hosted_coprocessor_fwver_t *v)
{
    memset(v, 0, sizeof(*v));
    esp_err_t r = esp_hosted_get_coprocessor_fwversion(v);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "slave version query failed: %s", esp_err_to_name(r));
        return false;
    }
    ESP_LOGI(TAG, "slave reports esp_hosted %" PRIu32 ".%" PRIu32 ".%" PRIu32,
             v->major1, v->minor1, v->patch1);
    return true;
}

void app_main(void)
{
    const size_t img_len = slave_image_end - slave_image_start;
    const esp_image_header_t *hdr = (const esp_image_header_t *)slave_image_start;

    ESP_LOGI(TAG, "embedded slave image: %u bytes, magic 0x%02x, chip id %d, app version '%s'",
             (unsigned)img_len, hdr->magic, hdr->chip_id, embedded_version());
    if (hdr->magic != ESP_IMAGE_HEADER_MAGIC) {
        ESP_LOGE(TAG, "embedded image is not an ESP image, refusing");
        return;
    }

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_hosted_init());
    ESP_ERROR_CHECK(esp_hosted_connect_to_slave());
    ESP_LOGI(TAG, "esp_hosted transport up");

    esp_hosted_coprocessor_fwver_t before;
    bool have_before = read_slave_version(&before);
    if (have_before) {
        char cur[32];
        snprintf(cur, sizeof cur, "%" PRIu32 ".%" PRIu32 ".%" PRIu32,
                 before.major1, before.minor1, before.patch1);
        if (strcmp(cur, embedded_version()) == 0) {
            ESP_LOGW(TAG, "slave already runs %s: nothing to do. Flash the real firmware back.", cur);
            return;
        }
    }

    ESP_LOGW(TAG, "starting slave OTA -> %s in 3 s", embedded_version());
    vTaskDelay(pdMS_TO_TICKS(3000));

    esp_err_t r = esp_hosted_slave_ota_begin();
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "ota_begin failed: %s (slave keeps its current firmware)", esp_err_to_name(r));
        return;
    }
    size_t off = 0;
    uint32_t n = 0;
    int64_t t0 = esp_timer_get_time();
    while (off < img_len) {
        size_t len = (img_len - off > CHUNK) ? CHUNK : (img_len - off);
        r = esp_hosted_slave_ota_write((uint8_t *)slave_image_start + off, len);
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "ota_write failed at %u/%u: %s", (unsigned)off, (unsigned)img_len, esp_err_to_name(r));
            esp_hosted_slave_ota_end();
            return;
        }
        off += len;
        if (++n % 100 == 0) {
            ESP_LOGI(TAG, "  %u/%u bytes (%u%%)", (unsigned)off, (unsigned)img_len, (unsigned)(off * 100 / img_len));
        }
    }
    r = esp_hosted_slave_ota_end();
    ESP_LOGI(TAG, "ota_end -> %s after %.1f s", esp_err_to_name(r), (esp_timer_get_time() - t0) / 1e6);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "OTA rejected by the slave; it keeps its current firmware");
        return;
    }

    bool activate_api = have_before &&
        ((before.major1 > 2) || (before.major1 == 2 && before.minor1 > 5));
    if (activate_api) {
        r = esp_hosted_slave_ota_activate();
        ESP_LOGI(TAG, "ota_activate -> %s", esp_err_to_name(r));
    } else {
        ESP_LOGI(TAG, "slave < 2.6: no activate API, the slave switches on ota_end");
    }

    ESP_LOGW(TAG, "restarting host in 5 s to resync; the next boot prints the new slave version");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
}
