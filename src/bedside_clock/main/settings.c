/* NVS-persisted user settings (single versioned blob). */
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "settings.h"
#include "evlog.h"

static const char *TAG = "settings";

#define SETTINGS_NS      "bedside"
#define SETTINGS_KEY     "cfg"
#define SETTINGS_MAGIC   0xBED51DE0
#define SETTINGS_VERSION 11

/* AI backend is configured at runtime via Settings > Voice or the HTTP API
 * (PATCH ai_url / ai_key / ai_model); there is no default endpoint. An
 * empty URL means "no AI": localai_client.c checks ai_is_configured() at
 * every entry point and the service indicator shows SVC_OFF. */
#define AI_DEFAULT_URL   ""
#define AI_DEFAULT_KEY   ""
#define AI_DEFAULT_MODEL "qwen_qwen3-4b-instruct-2507"

/* Fresh-install alarm slots: three distinct times so enabling Alarm 2 or 3
 * without editing it doesn't just duplicate Alarm 1. */
static const alarm_slot_t k_alarm_defaults[ALARM_COUNT] = {
    { .enabled = 0, .hh = 7, .mm = 0,  .days = ALARM_DAYS_ALL },
    { .enabled = 0, .hh = 7, .mm = 30, .days = ALARM_DAYS_ALL },
    { .enabled = 0, .hh = 8, .mm = 0,  .days = ALARM_DAYS_ALL },
};

static bedside_settings_t s_cfg;
/* Guards multi-field/string updates and the save-time snapshot; s_cfg is
 * written from the LVGL, worker and ambient tasks. */
static SemaphoreHandle_t s_mtx;
static int s_repairs;        /* fields clamped/fixed by settings_sanitize */

void settings_lock(void)
{
    if (s_mtx != NULL) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
    }
}

void settings_unlock(void)
{
    if (s_mtx != NULL) {
        xSemaphoreGive(s_mtx);
    }
}

static void settings_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.magic = SETTINGS_MAGIC;
    s_cfg.version = SETTINGS_VERSION;
    s_cfg.alarm_enabled = false;
    s_cfg.alarm_hh = 7;
    s_cfg.alarm_mm = 0;
    s_cfg.alarm_mode = ALARM_MODE_BEEP;
    s_cfg.clock_face = 0;
    s_cfg.orientation = ORIENT_LANDSCAPE;
    s_cfg.bright_min = 10;
    s_cfg.bright_max = 80;
    s_cfg.volume = 70;
    s_cfg.alarm_volume = 80;
    s_cfg.tz_offset_min = 10 * 60;
    s_cfg.luma_floor = 255;
    s_cfg.luma_ceil = 0;
    s_cfg.voice_enabled = 1;
    s_cfg.voice_mic_gain = 24;
    s_cfg.ai_provider = AI_PROVIDER_LOCALAI;
    strlcpy(s_cfg.ai_url, AI_DEFAULT_URL, sizeof(s_cfg.ai_url));
    strlcpy(s_cfg.ai_key, AI_DEFAULT_KEY, sizeof(s_cfg.ai_key));
    strlcpy(s_cfg.ai_model, AI_DEFAULT_MODEL, sizeof(s_cfg.ai_model));
    /* api_enabled / api_key stay zeroed: control API off until configured */
    s_cfg.svc_poll_s = SVC_POLL_DEFAULT_S;
    s_cfg.ambient_period_s = AMBIENT_PERIOD_DEFAULT_S;
    memcpy(s_cfg.alarms, k_alarm_defaults, sizeof(s_cfg.alarms));
    s_cfg.max_alarm_seconds = ALARM_MAX_RING_DEFAULT_S;
    s_cfg.voice_wake_thr = VOICE_WAKE_THR_DEFAULT;
    s_cfg.voice_wake_verify = 1;  /* skipped automatically without an AI server */
    s_cfg.voice_wake_collect = 1; /* RAM only, nothing is written to flash */
    strlcpy(s_cfg.voice_wake_word, VOICE_WAKE_WORD_DEFAULT, sizeof(s_cfg.voice_wake_word));
}

/* ---------- validation ----------
 * A blob whose magic and version survived corruption is otherwise accepted
 * wholesale, and every consumer trusts the ranges (brightness -> BSP PWM,
 * alarm_hh -> mktime, volume -> codec, strings -> %s/strlen/strstr). Clamp
 * everything to what the UI and the HTTP PATCH validator would accept and
 * guarantee NUL termination on every string. */

#define CLAMP_U8(field, lo, hi) \
    do { \
        if ((c)->field < (lo) || (c)->field > (hi)) { \
            (c)->field = ((c)->field < (lo)) ? (lo) : (hi); \
            n++; \
        } \
    } while (0)

#define TERMINATE(field) \
    do { \
        if ((c)->field[sizeof((c)->field) - 1] != '\0') { \
            (c)->field[sizeof((c)->field) - 1] = '\0'; \
            n++; \
        } \
    } while (0)

static int settings_sanitize(bedside_settings_t *c)
{
    int n = 0;
    TERMINATE(wifi_ssid);
    TERMINATE(wifi_pass);
    TERMINATE(nav_url);
    TERMINATE(nav_user);
    TERMINATE(nav_pass);
    TERMINATE(alarm_playlist_id);
    TERMINATE(alarm_playlist_name);
    TERMINATE(ai_url);
    TERMINATE(ai_key);
    TERMINATE(ai_model);
    TERMINATE(api_key);

    CLAMP_U8(alarm_hh, 0, 23);
    CLAMP_U8(alarm_mm, 0, 59);
    CLAMP_U8(alarm_mode, ALARM_MODE_BEEP, ALARM_MODE_PLAYLIST);
    CLAMP_U8(clock_face, CLOCK_FACE_LARGE, CLOCK_FACE_GIANT);
    CLAMP_U8(orientation, ORIENT_LANDSCAPE, ORIENT_PORTRAIT);
    CLAMP_U8(bright_min, 1, 100);
    CLAMP_U8(bright_max, 1, 100);
    if (c->bright_max < c->bright_min) {
        c->bright_max = c->bright_min;
        n++;
    }
    CLAMP_U8(volume, 0, 100);
    CLAMP_U8(alarm_volume, 0, 100);
    CLAMP_U8(voice_enabled, 0, 1);
    CLAMP_U8(voice_mic_gain, 0, 42);
    CLAMP_U8(ai_provider, AI_PROVIDER_LOCALAI, AI_PROVIDER_LOCALAI);
    CLAMP_U8(api_enabled, 0, 1);
    if (c->tz_offset_min < -720 || c->tz_offset_min > 840) {
        c->tz_offset_min = 0;
        n++;
    }
    {
        uint16_t norm = settings_norm_poll(c->svc_poll_s);
        if (norm != c->svc_poll_s) {
            c->svc_poll_s = norm;
            n++;
        }
    }
    if (c->ambient_period_s < AMBIENT_PERIOD_MIN_S ||
        c->ambient_period_s > AMBIENT_PERIOD_MAX_S) {
        c->ambient_period_s = AMBIENT_PERIOD_DEFAULT_S;
        n++;
    }
    for (int i = 0; i < ALARM_COUNT; i++) {
        CLAMP_U8(alarms[i].enabled, 0, 1);
        CLAMP_U8(alarms[i].hh, 0, 23);
        CLAMP_U8(alarms[i].mm, 0, 59);
        uint8_t days = settings_norm_days(c->alarms[i].days);
        if (days != c->alarms[i].days) {
            c->alarms[i].days = days;
            n++;
        }
    }
    {
        uint16_t norm = settings_norm_max_ring(c->max_alarm_seconds);
        if (norm != c->max_alarm_seconds) {
            c->max_alarm_seconds = norm;
            n++;
        }
    }
    CLAMP_U8(voice_wake_thr, VOICE_WAKE_THR_MIN, VOICE_WAKE_THR_MAX);
    CLAMP_U8(voice_wake_verify, 0, 1);
    CLAMP_U8(voice_wake_collect, 0, 1);
    TERMINATE(voice_wake_word); /* unknown ids fall back to the first model */
    /* luma_floor/ceil: every byte pair is meaningful (floor > ceil is the
     * "uncalibrated" marker and ambient.c treats a span < 16 as such) */
    /* bool from a byte: anything non-zero is true, but normalize */
    c->alarm_enabled = c->alarm_enabled ? true : false;
    return n;
}

void settings_init(void)
{
    if (s_mtx == NULL) {
        s_mtx = xSemaphoreCreateMutex();
    }
    settings_defaults();

    nvs_handle_t nvs;
    if (nvs_open(SETTINGS_NS, NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGI(TAG, "no stored settings, using defaults");
        return;
    }
    /* Older versions store a shorter blob (fields are append-only), so read
     * whatever length is there into a zeroed struct and migrate. A LONGER
     * blob (a newer firmware wrote it, then this one was flashed back) is
     * read as a prefix for the same reason - never wipe WiFi credentials
     * because of a downgrade. */
    bedside_settings_t stored;
    memset(&stored, 0, sizeof(stored));
    size_t len = 0;
    esp_err_t err = nvs_get_blob(nvs, SETTINGS_KEY, NULL, &len);
    if (err == ESP_OK && len > 0 && len <= sizeof(stored)) {
        err = nvs_get_blob(nvs, SETTINGS_KEY, &stored, &len);
    } else if (err == ESP_OK && len > sizeof(stored)) {
        uint8_t *tmp = malloc(len);
        if (tmp == NULL) {
            err = ESP_ERR_NO_MEM;
        } else {
            err = nvs_get_blob(nvs, SETTINGS_KEY, tmp, &len);
            if (err == ESP_OK) {
                memcpy(&stored, tmp, sizeof(stored));
                ESP_LOGW(TAG, "stored blob is %u bytes, struct is %u: "
                         "loading the common prefix (firmware downgrade?)",
                         (unsigned)len, (unsigned)sizeof(stored));
            }
            free(tmp);
        }
    } else if (err == ESP_OK) {
        err = ESP_ERR_INVALID_SIZE;
    }
    nvs_close(nvs);

    if (err == ESP_OK && stored.magic == SETTINGS_MAGIC &&
        stored.version >= 1) {
        if (stored.version < 2) {
            stored.alarm_volume = 80;
        }
        if (stored.version < 3) {
            /* the zeroed struct would otherwise read as "calibrated from
             * pitch black" (floor 0); seed the real uncalibrated markers */
            stored.luma_floor = 255;
            stored.luma_ceil = 0;
        }
        if (stored.version < 4) {
            stored.voice_enabled = 1;
            stored.voice_mic_gain = 24;
        }
        if (stored.version < 5) {
            stored.ai_provider = AI_PROVIDER_LOCALAI;
            strlcpy(stored.ai_url, AI_DEFAULT_URL, sizeof(stored.ai_url));
            strlcpy(stored.ai_key, AI_DEFAULT_KEY, sizeof(stored.ai_key));
            strlcpy(stored.ai_model, AI_DEFAULT_MODEL, sizeof(stored.ai_model));
        }
        if (stored.version < 6) {
            /* zeroed already by the memset: API off, no key */
            stored.api_enabled = 0;
            stored.api_key[0] = '\0';
        }
        if (stored.version < 7) {
            stored.svc_poll_s = SVC_POLL_DEFAULT_S; /* the old fixed cadence */
        }
        if (stored.version < 8) {
            stored.ambient_period_s = AMBIENT_PERIOD_DEFAULT_S;
        }
        if (stored.version < 9) {
            /* the single alarm becomes Alarm 1, every day; the other two
             * slots start disabled; ringing now stops by itself after the
             * default max ring time (0 = the old "until dismissed") */
            memcpy(stored.alarms, k_alarm_defaults, sizeof(stored.alarms));
            stored.alarms[0].enabled = stored.alarm_enabled ? 1 : 0;
            stored.alarms[0].hh = stored.alarm_hh;
            stored.alarms[0].mm = stored.alarm_mm;
            stored.max_alarm_seconds = ALARM_MAX_RING_DEFAULT_S;
            ESP_LOGW(TAG, "settings v%u -> v9: alarm %s %02u:%02u -> Alarm 1 "
                     "(every day); alarms stop after %d s",
                     stored.version, stored.alarm_enabled ? "on" : "off",
                     stored.alarm_hh, stored.alarm_mm, ALARM_MAX_RING_DEFAULT_S);
            evlog(EV_BOOT, "settings v%u->v9: alarm->A1 daily, stop %dm",
                  stored.version, ALARM_MAX_RING_DEFAULT_S / 60);
        }
        if (stored.version < 10) {
            /* the wake threshold that used to be compiled in, verifier and
             * clip collection on (both are no-ops without an AI server /
             * API client) */
            stored.voice_wake_thr = VOICE_WAKE_THR_DEFAULT;
            stored.voice_wake_verify = 1;
            stored.voice_wake_collect = 1;
        }
        if (stored.version < 11) {
            /* wake-word picker: the phrase every earlier build used */
            strlcpy(stored.voice_wake_word, VOICE_WAKE_WORD_DEFAULT,
                    sizeof(stored.voice_wake_word));
        }
        uint16_t loaded_version = stored.version;
        s_repairs = settings_sanitize(&stored);
        /* early v5 builds shipped a model name the server doesn't have */
        if (strcmp(stored.ai_model, "qwen") == 0) {
            strlcpy(stored.ai_model, AI_DEFAULT_MODEL, sizeof(stored.ai_model));
        }
        stored.version = SETTINGS_VERSION;
        s_cfg = stored;
        if (s_repairs > 0) {
            ESP_LOGW(TAG, "settings loaded (v%u) with %d out-of-range field(s) repaired",
                     loaded_version, s_repairs);
        } else {
            ESP_LOGI(TAG, "settings loaded (v%u)", loaded_version);
        }
    } else {
        ESP_LOGW(TAG, "stored settings invalid (err %s, len %u), using defaults",
                 esp_err_to_name(err), (unsigned)len);
    }
}

int settings_load_repairs(void)
{
    return s_repairs;
}

bedside_settings_t *settings_get(void)
{
    return &s_cfg;
}

bool settings_save(void)
{
    /* Snapshot under the lock so a concurrent string write (e.g. WiFi
     * credentials on the worker) can't be persisted half-updated. */
    bedside_settings_t snap;
    settings_lock();
    /* keep the pre-v9 fields equal to Alarm 1 so a firmware downgrade
     * (which reads this blob as a prefix) still has the right alarm */
    s_cfg.alarm_enabled = s_cfg.alarms[0].enabled != 0;
    s_cfg.alarm_hh = s_cfg.alarms[0].hh;
    s_cfg.alarm_mm = s_cfg.alarms[0].mm;
    snap = s_cfg;
    settings_unlock();

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(SETTINGS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_blob(nvs, SETTINGS_KEY, &snap, sizeof(snap));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "settings save failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "settings saved");
    return true;
}
