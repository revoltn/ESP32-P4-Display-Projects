#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* microWakeWord streaming wake-word engine (TensorFlow Lite Micro + ESP-NN
 * PIE kernels, TF microfrontend features). Compiled in only with
 * CONFIG_BEDSIDE_WAKE_MWW; otherwise every call is a cheap no-op/false and
 * the registry below reports the WakeNet build's single "Hi ESP".
 *
 * Model registry: one entry per id in BEDSIDE_WAKE_MODELS
 * (main/CMakeLists.txt), each embedded from main/models/<id>.tflite +
 * main/models/<id>.json (ESPHome v2 manifest: wake_word, micro.probability_cutoff,
 * feature_step_size, sliding_window_size, tensor_arena_size). The id is what
 * settings (voice_wake_word) and the API store; the name is the manifest's
 * wake_word. Adding a model = the two files + one line in the CMake list.
 *
 * Single consumer: feed/reset/peak are meant for the voice_detect task.
 * wake_mww_set_cutoff and wake_mww_select are single-word stores, any task;
 * the registry getters are read-only after init. */

/* Load the model whose id matches (empty/unknown = the first entry) plus the
 * picker names of every entry: frontend state, interpreter, PSRAM arena.
 * Idempotent. false = not compiled in or the model would not load (caller
 * falls back to WakeNet). */
bool wake_mww_init(const char *model_id);

/* True after a successful init. */
bool wake_mww_ready(void);

/* Registry: count, id/name by index ("" when out of range), index by id
 * (-1 when unknown or empty), index of the live model (-1 before init). */
int wake_mww_model_count(void);
const char *wake_mww_model_id(int i);
const char *wake_mww_model_name(int i);
int wake_mww_model_index(const char *id);
int wake_mww_current(void);

/* Ask for a different embedded model. Takes effect at the next
 * wake_mww_feed() on the detect task (a full new interpreter is built
 * before the old one is freed; on failure the old model stays). The cutoff
 * set with wake_mww_set_cutoff is kept across the switch. */
void wake_mww_select(int i);

/* Detection cutoff in percent (1..99) of the sliding-window mean probability;
 * the manifest's value is used until this is called. */
void wake_mww_set_cutoff(uint8_t pct);

/* Forget recent probabilities and arm the cool-off (after a command window
 * or a pause, so stale state cannot re-trigger). */
void wake_mww_reset(void);

/* Feed 16 kHz mono PCM (any sample count; the frontend buffers internally).
 * Returns true exactly once per detection. ~0.6 ms of CPU per 30 ms of
 * audio on the P4. */
bool wake_mww_feed(const int16_t *pcm, size_t samples);

/* Highest single-invoke probability (0..255) since the previous call;
 * for the heartbeat log. */
uint8_t wake_mww_peak(void);

/* Live model's manifest name / arena size for logs. */
const char *wake_mww_name(void);
size_t wake_mww_arena_bytes(void);

#ifdef __cplusplus
}
#endif
