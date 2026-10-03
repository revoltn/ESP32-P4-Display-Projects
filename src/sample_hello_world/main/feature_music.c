/* Note-table organ synthesizer streaming to the ES8311 via bsp_extra. */
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bsp_board_extra.h"

#include "app_common.h"
#include "music_score.h"

static const char *TAG = "music";

#define CHUNK_SAMPLES   512
#define ATTACK_MS       8
#define RELEASE_MS      25
#define TWO_PI          6.28318530718f

/* Additive organ voice: fundamental + 3 harmonics. */
static inline float organ(float phase)
{
    return sinf(phase) + 0.5f * sinf(2.0f * phase)
         + 0.25f * sinf(3.0f * phase) + 0.15f * sinf(4.0f * phase);
}

void feature_music_run(void)
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

    ui_log("Playing Toccata and Fugue in D minor (BWV 565)...");
    ui_set_status("Playing Toccata...");

    static int16_t buf[CHUNK_SAMPLES];
    const int attack_n = AUDIO_SAMPLE_RATE * ATTACK_MS / 1000;
    const int release_n = AUDIO_SAMPLE_RATE * RELEASE_MS / 1000;

    for (size_t ni = 0; ni < TOCCATA_SCORE_LEN; ni++) {
        const note_t *note = &TOCCATA_SCORE[ni];
        const int total = AUDIO_SAMPLE_RATE * note->ms / 1000;

        int n_voices = 0;
        float w[4] = { 0 };
        for (int v = 0; v < 4; v++) {
            if (note->f[v] > 0.0f) {
                w[v] = TWO_PI * note->f[v] / AUDIO_SAMPLE_RATE;
                n_voices++;
            }
        }
        const float gain = n_voices ? (6000.0f / n_voices) : 0.0f;

        for (int done = 0; done < total; ) {
            int n = total - done;
            if (n > CHUNK_SAMPLES) {
                n = CHUNK_SAMPLES;
            }
            for (int i = 0; i < n; i++) {
                const int t = done + i;
                float s = 0.0f;
                for (int v = 0; v < n_voices; v++) {
                    s += organ(w[v] * t);
                }
                /* click-killing envelope */
                float env = 1.0f;
                if (t < attack_n) {
                    env = (float)t / attack_n;
                } else if (t >= total - release_n) {
                    env = (float)(total - t) / release_n;
                }
                buf[i] = (int16_t)(s * gain * env);
            }
            size_t written = 0;
            err = bsp_extra_i2s_write(buf, n * sizeof(int16_t), &written, 1000);
            if (err != ESP_OK) {
                ui_log("i2s write failed: %s", esp_err_to_name(err));
                ui_set_status("Playback error");
                return;
            }
            done += n;
        }
    }

    ESP_LOGI(TAG, "score finished");
    ui_log("Toccata finished");
    ui_set_status("Ready");
}
