#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "app_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* On-device voice control (esp-sr: WakeNet9 "Hi ESP" + MultiNet7 EN).
 * voice_init() blocks while models load - call it from the worker task
 * (boot_job), never from an LVGL callback.  Honors settings voice_enabled
 * (skips the whole pipeline when off at boot). */
void voice_init(void);

/* Suspend/resume listening at runtime (first enable does the full init
 * asynchronously).  Caller updates settings_get()->voice_enabled itself. */
void voice_set_enabled(bool en);

/* Re-read settings_get()->voice_mic_gain and apply it to the ADC. */
void voice_apply_mic_gain(void);

/* Re-read settings_get()->voice_wake_thr and apply it to WakeNet. Cheap;
 * no-op while the pipeline is down (voice_start applies it itself). */
void voice_apply_wake_threshold(void);

/* Re-read settings_get()->voice_wake_word and switch the microWakeWord
 * model (wake_mww.h registry). A single store; the detect task does the
 * swap. No-op on the WakeNet engine or while the pipeline is down
 * (voice_start reads the setting itself). */
void voice_apply_wake_word(void);

/* Listening state for the clock screen's voice indicator: OFF when disabled
 * in settings, UP while the pipeline is up and the mic is being read, PAUSED
 * while the pipeline is up but the mic is not being read (a song owns the
 * codec), FAIL when the bring-up was tried and gave up, WAIT in between.
 * Lock-free: safe from the LVGL tick. */
svc_state_t voice_service_state(void);

/* ---------- wake clips (training-data flywheel + verifier) ----------
 * Every wake word detection snapshots the last ~2 s of AFE output as a 16 kHz
 * mono WAV; POST /api/v1/voice/record captures N seconds on demand through
 * the same path. Clips live in PSRAM only (oldest evicted) and are served by
 * api_server.c so a PC can pull them as positives / hard negatives. */

typedef enum {
    VOICE_CLIP_WAKE = 0,   /* WakeNet fired */
    VOICE_CLIP_REC  = 1,   /* requested recording */
} voice_clip_kind_t;

typedef enum {
    VOICE_VERDICT_NONE = 0,   /* not verified (verifier off / no AI server) */
    VOICE_VERDICT_PENDING,    /* whisper round-trip in flight */
    VOICE_VERDICT_OK,         /* transcript contained the wake phrase */
    VOICE_VERDICT_REJECT,     /* it did not: the wake was ignored */
} voice_verdict_t;

typedef struct {
    uint32_t id;              /* monotonic, 1-based */
    voice_clip_kind_t kind;
    voice_verdict_t verdict;
    time_t   epoch;           /* 0 when the clock was not valid */
    uint32_t uptime_s;
    uint16_t ms;              /* audio length */
    size_t   len;             /* WAV bytes incl. 44-byte header */
    char     text[48];        /* verifier transcript, if any */
} voice_clip_info_t;

/* Newest last. Returns the number written (<= cap). Any task. */
int voice_clips_list(voice_clip_info_t *out, int cap);

/* Copy clip `id` (whole WAV) into dst; returns bytes copied, 0 if unknown.
 * dst == NULL returns the length only. Any task. */
size_t voice_clip_copy(uint32_t id, uint8_t *dst, size_t cap);

void voice_clips_clear(void);

/* Record `ms` (<= VOICE_REC_MAX_MS) of AFE output into a new clip. Blocks
 * until done (an open command window is allowed to finish first). Returns
 * the clip id, 0 on failure / busy / mic paused. Not from the LVGL task. */
#define VOICE_REC_MAX_MS 8000
uint32_t voice_record_clip(int ms);

#ifdef __cplusplus
}
#endif
