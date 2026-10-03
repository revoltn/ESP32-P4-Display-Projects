/* Record 5 s from the mic into PSRAM, count down 5 s, play it back.
 * Strictly sequential (record fully, then play) — avoids unproven full-duplex. */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "bsp_board_extra.h"

#include "app_common.h"

static const char *TAG = "echo";

#define RECORD_SECONDS  5
#define WAIT_SECONDS    5
#define RECORD_BYTES    (RECORD_SECONDS * AUDIO_SAMPLE_RATE * (AUDIO_BITS / 8))
#define CHUNK_BYTES     4096

void feature_echo_run(void)
{
    if (!app_audio_ensure_init()) {
        return;
    }
    esp_err_t err = bsp_extra_codec_set_fs(AUDIO_SAMPLE_RATE, AUDIO_BITS, AUDIO_SLOT_MODE);
    if (err != ESP_OK) {
        ui_log("codec set_fs failed: %s", esp_err_to_name(err));
        return;
    }
    bsp_extra_codec_volume_set(70, NULL);
    bsp_extra_codec_in_gain_set(24); /* vendor demo value */

    uint8_t *rec = heap_caps_malloc(RECORD_BYTES, MALLOC_CAP_SPIRAM);
    if (!rec) {
        ui_log("Out of PSRAM for %d byte recording buffer", RECORD_BYTES);
        return;
    }

    ui_log("Recording %d seconds - speak now!", RECORD_SECONDS);

    size_t got = 0;
    int last_sec = -1;
    while (got < RECORD_BYTES) {
        size_t want = RECORD_BYTES - got;
        if (want > CHUNK_BYTES) {
            want = CHUNK_BYTES;
        }
        size_t n = 0;
        err = bsp_extra_i2s_read(rec + got, want, &n, 1000);
        if (err != ESP_OK) {
            ui_log("i2s read failed: %s", esp_err_to_name(err));
            heap_caps_free(rec);
            return;
        }
        got += n;
        int sec = got / (AUDIO_SAMPLE_RATE * (AUDIO_BITS / 8));
        if (sec != last_sec) {
            last_sec = sec;
            ui_set_status("Recording... %d/%d s", sec, RECORD_SECONDS);
        }
    }
    ui_log("Recording done (%u bytes)", (unsigned)got);

    for (int s = WAIT_SECONDS; s > 0; s--) {
        ui_set_status("Playback in %d...", s);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ui_set_status("Playing back...");

    /* Prefer the USB speaker (e.g. Jabra) when one is attached; fall back
     * to the wired ES8311/I2S output otherwise. */
    bool usb_done = false;
    if (usb_audio_ready(2000)) {
        usb_done = usb_audio_play_mono16((const int16_t *)rec,
                                         got / sizeof(int16_t),
                                         AUDIO_SAMPLE_RATE);
        if (!usb_done) {
            ui_log("USB playback failed - falling back to wired output");
        }
    }
    if (!usb_done) {
        ui_log("Playing back your recording (wired output)");
        size_t played = 0;
        while (played < got) {
            size_t want = got - played;
            if (want > CHUNK_BYTES) {
                want = CHUNK_BYTES;
            }
            size_t n = 0;
            err = bsp_extra_i2s_write(rec + played, want, &n, 1000);
            if (err != ESP_OK) {
                ui_log("i2s write failed: %s", esp_err_to_name(err));
                break;
            }
            played += n;
        }
    }
    heap_caps_free(rec);

    ESP_LOGI(TAG, "echo cycle complete");
    ui_log("Playback done");
    ui_set_status("Ready");
}
