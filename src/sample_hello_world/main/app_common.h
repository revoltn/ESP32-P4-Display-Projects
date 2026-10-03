#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2s_std.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FEAT_WIFI,
    FEAT_BT,
    FEAT_MUSIC,
    FEAT_ECHO,
    FEAT_SPECS,
    FEAT_USB_AUDIO,
    FEAT_CAMERA,
    FEAT_NAVIDROME,
} feature_id_t;

/* One sample rate for the whole app (proven on this board by the xiaozhi port).
 * If mono playback misbehaves on the ES8311, switch AUDIO_SLOT_MODE to
 * I2S_SLOT_MODE_STEREO and duplicate each sample in the feature code. */
#define AUDIO_SAMPLE_RATE   16000
#define AUDIO_BITS          16
#define AUDIO_SLOT_MODE     I2S_SLOT_MODE_MONO

/* Post a feature to the worker queue (safe from LVGL callbacks). */
void app_request_feature(feature_id_t id);

/* Lazy codec init; returns false (and logs) on failure. */
bool app_audio_ensure_init(void);

/* UI helpers (take the LVGL lock internally; call from any task except the LVGL task). */
void ui_log(const char *fmt, ...);
void ui_set_status(const char *fmt, ...);

/* Feature entry points, run on the worker task. */
void feature_wifi_run(void);
void feature_bt_run(void);
void feature_music_run(void);
void feature_echo_run(void);
void feature_specs_run(void);
void feature_usb_audio_run(void);
void feature_navidrome_run(void);

/* Init WiFi if needed and connect if not connected; true when online
 * (feature_wifi.c). Callable from any feature on the worker task. */
bool wifi_ensure_connected(void);

/* USB speaker helpers (feature_usb_audio.c). usb_audio_ready() brings up the
 * USB host on first use and reports whether a USB audio output is attached
 * (waiting up to wait_ms for one to enumerate). usb_audio_play_mono16()
 * plays a mono 16-bit PCM buffer through it, resampling to the negotiated
 * device format; returns false when unavailable or the stream failed. */
bool usb_audio_ready(int wait_ms);
bool usb_audio_play_mono16(const int16_t *pcm, size_t frames, uint32_t src_rate);
void feature_camera_run(void);

#ifdef __cplusplus
}
#endif
