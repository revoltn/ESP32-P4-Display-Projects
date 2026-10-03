#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "driver/i2s_std.h"

#ifdef __cplusplus
extern "C" {
#endif

/* State of one optional service, as shown by its indicator on the clock face.
 * Every service module exposes its own <mod>_service_state(); ui_clock maps
 * these to colours. Reads must be lock-free (the 1 s LVGL tick calls them). */
typedef enum {
    SVC_OFF = 0,   /* not configured, or switched off by the user */
    SVC_WAIT,      /* configured: waiting on a dependency, or not probed yet */
    SVC_UP,        /* verified working */
    SVC_FAIL,      /* configured, but the probe / bring-up failed */
    SVC_PAUSED,    /* up, but temporarily suspended (e.g. the mic while the
                    * player owns the codec) - shown greyed out */
} svc_state_t;

/* Audio constants kept from the donor app (feature_specs references them). */
#define AUDIO_SAMPLE_RATE   16000
#define AUDIO_BITS          16
#define AUDIO_SLOT_MODE     I2S_SLOT_MODE_MONO

/* Run fn(arg) on the worker task (LVGL callbacks must never block).
 * Returns false if the job queue is full and the job was dropped - the
 * caller still owns arg and must free it. */
bool app_run_async(void (*fn)(void *), void *arg);

/* Serialize lazy one-time inits (codec, player, music task). Recursive,
 * so nested ensure_*() chains are fine. No-ops before app_main creates it. */
void app_init_lock(void);
void app_init_unlock(void);

/* Lazy ES8311 + audio_player bring-up; false (and logs) on failure. Blocks
 * (init mutex, 200 ms codec settle): worker/music/alarm/http tasks only. */
bool app_audio_ensure_init(void);

/* Non-blocking: has app_audio_ensure_init() succeeded? LVGL callbacks and
 * the HTTP server use this to apply a live volume only when the codec is
 * up; a stored volume is applied by the init itself when it runs later. */
bool app_audio_is_ready(void);

/* Apply the media volume (0-100) on the worker task. Never blocks: the
 * codec mutex is held by the decoder for ~95% of each cycle during
 * playback, so waiting for it on the LVGL/voice/httpd task stalled the
 * whole UI. Rapid calls (slider drag) coalesce into one job; the last
 * value wins. Safe from any task, including under bsp_display_lock. */
void app_audio_set_volume_async(int v);

/* Apply the orientation setting to the display (caller holds the LVGL lock). */
void app_apply_orientation(void);

/* False when the DSI panel / LVGL port failed to start. Every helper that
 * takes bsp_display_lock() must check this first: the port's lock asserts
 * on a NULL mutex, and a clock with a dead panel must still ring its alarm
 * (chime + voice/API dismiss) instead of panicking on the first ui_log. */
bool app_display_ready(void);

/* UI helpers (take the LVGL lock internally; call from any task except the
 * LVGL task). ui_log goes to the serial log and, when open, the Device
 * Specifications page; ui_set_status updates the clock screen status line. */
void ui_log(const char *fmt, ...);
void ui_set_status(const char *fmt, ...);

/* Append a pre-formatted \n-separated block to the specifications log in
 * one locked pass (one label re-layout instead of one per line). */
void ui_log_bulk(const char *text);

/* Copy the in-RAM log (the Specs page text, newest lines last) into dst,
 * NUL-terminated; the tail when it does not fit. Returns the length copied
 * (0 when the display lock is busy or the display never came up). Serves
 * GET /api/v1/log so a same-day failure can be read without a serial cable. */
size_t ui_log_copy(char *dst, size_t cap);

/* Device spec sheet (ported from hello_world), prints via ui_log. */
void feature_specs_run(void);

#ifdef __cplusplus
}
#endif
