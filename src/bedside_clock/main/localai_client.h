#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "app_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* OpenAI-compatible chat client for the configured AI server (LocalAI).
 * Blocking; call from the worker/music task, never the LVGL task. */

/* True when an AI server URL is stored in settings. */
bool ai_is_configured(void);

/* Configured-ness folded together with the outcome of the most recent
 * request, for the clock screen's AI indicator. SVC_UP only while the last
 * 200 is younger than boot_svc_fresh_window_us(); older reads SVC_WAIT. */
svc_state_t ai_service_state(void);

/* esp_timer_get_time() of the last request the server answered 200 (probe,
 * pick, transcription); 0 = never. Lets boot_seq skip a redundant probe. */
int64_t ai_last_ok_us(void);

/* GET {ai_url}/v1/models to prove the server is up and the key accepted;
 * updates ai_service_state(). 5 s timeout, no retry. Blocking (TLS): worker
 * task only. Requires WiFi; check wifi_mgr_is_connected() first. */
bool ai_ping(void);

/* Ask the model which of `options[0..count-1]` best matches `query`.
 * Writes the winning index into *best_idx. ESP_ERR_NOT_FOUND when the model
 * says nothing matches; other errors are logged through ui_log(). */
esp_err_t ai_pick_best(const char *query, const char *const *options,
                       int count, int *best_idx);

/* Speech-to-text: POST a complete WAV (16 kHz mono s16) to the AI server's
 * /v1/audio/transcriptions (whisper) and copy the transcript into out.
 * Blocking (seconds); call from the worker/music task. */
esp_err_t ai_transcribe_wav(const uint8_t *wav, size_t wav_len,
                            char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif
