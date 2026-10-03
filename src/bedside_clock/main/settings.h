#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One alarm. Exactly 4 bytes, no padding: the alarm task reads a slot as one
 * 32-bit copy, and LVGL callbacks write single bytes without the settings
 * lock. Assign whole structs only under settings_lock(). */
#define ALARM_COUNT      3
#define ALARM_DAYS_MASK  0x7F   /* bit0 = Sunday ... bit6 = Saturday (tm_wday) */
#define ALARM_DAYS_ONCE  0x80   /* ring at the next matching instant, then
                                 * switch the slot off (and save) */
#define ALARM_DAYS_ALL   0x7F
#define ALARM_DAYS_WEEK  0x3E   /* Mon..Fri */
#define ALARM_DAYS_WEND  0x41   /* Sat, Sun */
typedef struct {
    uint8_t enabled;            /* 0/1 */
    uint8_t hh;                 /* 0-23 */
    uint8_t mm;                 /* 0-59 */
    uint8_t days;               /* ALARM_DAYS_MASK bits | optional ALARM_DAYS_ONCE;
                                 * the weekday part is never 0 (see settings_norm_days) */
} alarm_slot_t;

/* All user configuration, persisted as one NVS blob. Mutate the struct
 * returned by settings_get() then call settings_save(). */
typedef struct {
    uint32_t magic;            /* SETTINGS_MAGIC */
    uint16_t version;          /* SETTINGS_VERSION */

    char wifi_ssid[33];
    char wifi_pass[65];

    char nav_url[96];          /* https://host[:port], no trailing slash */
    char nav_user[33];
    char nav_pass[49];

    /* legacy pre-v9 single alarm: a MIRROR of alarms[0] (settings_save
     * copies it there so a firmware downgrade keeps Alarm 1); never read */
    bool    alarm_enabled;
    uint8_t alarm_hh;          /* 0-23 */
    uint8_t alarm_mm;          /* 0-59 */
    uint8_t alarm_mode;        /* ALARM_MODE_* (shared by all alarms) */
    char    alarm_playlist_id[40];
    char    alarm_playlist_name[64];

    uint8_t clock_face;        /* 0 = large digital (only one for now) */
    uint8_t orientation;       /* ORIENT_* */
    uint8_t bright_min;        /* 1-100, night floor */
    uint8_t bright_max;        /* 1-100, day ceiling */
    uint8_t volume;            /* 0-100, general playback */
    int16_t tz_offset_min;     /* minutes east of UTC, e.g. +600 for UTC+10 */

    /* v2+ fields: append only, so older stored blobs load as a prefix */
    uint8_t alarm_volume;      /* 0-100, used while the alarm rings */

    /* v3+ fields */
    uint8_t luma_floor;        /* darkest luma ever measured; 255 = uncalibrated */
    uint8_t luma_ceil;         /* brightest luma ever measured; 0 = uncalibrated */

    /* v4+ fields */
    uint8_t voice_enabled;     /* 0/1: "Hi ESP" voice control */
    uint8_t voice_mic_gain;    /* ES8311 ADC gain in dB (0..42, steps of 6) */

    /* v5+ fields: AI server for natural-language music matching */
    uint8_t ai_provider;       /* AI_PROVIDER_* (LocalAI only for now) */
    char    ai_url[96];        /* http(s)://host[:port], no trailing slash */
    char    ai_key[49];        /* API key (Bearer) */
    char    ai_model[33];      /* chat model name, e.g. "qwen" */

    /* v6+ fields: local HTTP control API (api_server.c) */
    uint8_t api_enabled;       /* 0/1; only honored while api_key is set */
    char    api_key[49];       /* shared secret; empty = API cannot run */

    /* v7+ fields */
    uint16_t svc_poll_s;       /* seconds between music/AI server checks:
                                * 0 = off, else SVC_POLL_MIN_S..SVC_POLL_MAX_S.
                                * A green indicator means "answered within
                                * ~2 of these periods". */

    /* v8+ fields */
    uint16_t ambient_period_s; /* seconds between camera light samples,
                                * AMBIENT_PERIOD_MIN_S..AMBIENT_PERIOD_MAX_S.
                                * The camera sleeps in between (ambient.c). */

    /* v9+ fields: up to ALARM_COUNT alarms, each with its own weekday mask;
     * sound mode / playlist / volume above are shared by all of them. */
    alarm_slot_t alarms[ALARM_COUNT];
    uint16_t     max_alarm_seconds; /* a ringing alarm stops by itself after
                                     * this long; 0 = ring until dismissed,
                                     * else ALARM_MAX_RING_MIN_S..MAX_S */

    /* v10+ fields: wake-word tuning (voice.c) */
    uint8_t voice_wake_thr;     /* WakeNet detection threshold in percent,
                                 * VOICE_WAKE_THR_MIN..MAX; lower = wakes more
                                 * easily (and more falsely) */
    uint8_t voice_wake_verify;  /* 0/1: send each wake's audio to the AI
                                 * server's whisper and drop the wake when the
                                 * transcript does not contain the phrase */
    uint8_t voice_wake_collect; /* 0/1: keep the last wake / recording clips
                                 * in RAM for GET /api/v1/voice/clips */

    /* v11+ fields */
    char voice_wake_word[24];   /* wake-word model id, one of BEDSIDE_WAKE_MODELS
                                 * in main/CMakeLists.txt ("hi_esp", ...);
                                 * empty/unknown = the first model (wake_mww.h) */
} bedside_settings_t;

#define VOICE_WAKE_THR_MIN     40
#define VOICE_WAKE_THR_MAX     99
#define VOICE_WAKE_THR_DEFAULT 50   /* the 2026-09 hand-tuned value */
#define VOICE_WAKE_WORD_DEFAULT "hi_esp"

_Static_assert(sizeof(alarm_slot_t) == 4, "alarm_slot_t must stay 4 bytes");

#define ALARM_MAX_RING_MIN_S     30
#define ALARM_MAX_RING_MAX_S     7200
#define ALARM_MAX_RING_DEFAULT_S 600

/* Normalise a max ring time: 0 stays "never", 1..MIN-1 becomes MIN, above
 * MAX clamps to MAX. Shared by the NVS sanitizer, PATCH and the dropdown. */
static inline uint16_t settings_norm_max_ring(int v)
{
    if (v <= 0) {
        return 0;
    }
    if (v < ALARM_MAX_RING_MIN_S) {
        return ALARM_MAX_RING_MIN_S;
    }
    if (v > ALARM_MAX_RING_MAX_S) {
        return ALARM_MAX_RING_MAX_S;
    }
    return (uint16_t)v;
}

/* Normalise a days byte: unknown bits dropped, an empty weekday mask becomes
 * every day (a slot with no ringable day is never stored). */
static inline uint8_t settings_norm_days(int v)
{
    uint8_t d = (uint8_t)v & (ALARM_DAYS_MASK | ALARM_DAYS_ONCE);
    if ((d & ALARM_DAYS_MASK) == 0) {
        d |= ALARM_DAYS_ALL;
    }
    return d;
}

/* Does this slot ring on weekday `wday` (tm_wday: 0 = Sunday)? */
static inline bool alarm_days_match(uint8_t days, int wday)
{
    return (days & ALARM_DAYS_MASK & (1u << wday)) != 0;
}

#define AMBIENT_PERIOD_MIN_S     2
#define AMBIENT_PERIOD_MAX_S     300
#define AMBIENT_PERIOD_DEFAULT_S 20

#define SVC_POLL_MIN_S     30
#define SVC_POLL_MAX_S     3600
#define SVC_POLL_DEFAULT_S 300

/* Normalise a requested check interval: 0 stays off, 1..MIN-1 becomes MIN,
 * above MAX clamps to MAX. One rule shared by the NVS sanitizer, the HTTP
 * PATCH validator and the device dropdown. */
static inline uint16_t settings_norm_poll(int v)
{
    if (v <= 0) {
        return 0;
    }
    if (v < SVC_POLL_MIN_S) {
        return SVC_POLL_MIN_S;
    }
    if (v > SVC_POLL_MAX_S) {
        return SVC_POLL_MAX_S;
    }
    return (uint16_t)v;
}

#define ALARM_MODE_BEEP     0
#define ALARM_MODE_PLAYLIST 1

#define AI_PROVIDER_LOCALAI 0

#define CLOCK_FACE_LARGE 0
#define CLOCK_FACE_GIANT 1

#define ORIENT_LANDSCAPE      0   /* panel rotated 90 deg */
#define ORIENT_LANDSCAPE_FLIP 1   /* panel rotated 270 deg */
#define ORIENT_PORTRAIT       2   /* native panel orientation */

void settings_init(void);              /* load from NVS or defaults */
bedside_settings_t *settings_get(void);
/* Persist to NVS. False on an NVS/flash error (already logged); callers
 * that show a "Saved" note should say so instead. */
bool settings_save(void);
/* Number of fields settings_init had to clamp/repair (0 = blob was clean).
 * Lets app_main put one line on the device log once the UI exists. */
int settings_load_repairs(void);

/* Hold this while writing strings/multi-field groups from a non-LVGL task;
 * settings_save() snapshots under the same lock. Single-field integer
 * writes don't need it (atomic on this core). */
void settings_lock(void);
void settings_unlock(void);

#ifdef __cplusplus
}
#endif
