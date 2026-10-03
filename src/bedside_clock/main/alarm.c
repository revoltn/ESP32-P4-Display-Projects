/* Alarm scheduler + ringer.
 *
 * A 1 s task watches the wall clock against up to ALARM_COUNT alarm slots
 * (settings alarms[]: enabled, hh:mm, weekday mask, once flag). When a slot's
 * instant arrives on one of its days it shows a full-screen overlay (Snooze /
 * Dismiss) at max brightness and rings: in playlist mode it picks random
 * songs from the configured Navidrome playlist (download-whole -> fmemopen
 * -> audio_player); on any failure - or in beep mode - it falls back to a
 * generated two-tone chime. Snooze re-arms that slot 9 minutes out. A ring
 * stops by itself after settings max_alarm_seconds (0 = never) - the
 * "nobody home" case - and the clock face then shows it as missed. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "nvs.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"

#include "app_common.h"
#include "settings.h"
#include "clock_sync.h"
#include "ui_clock.h"
#include "wifi_mgr.h"
#include "navidrome_client.h"
#include "music_player.h"
#include "alarm.h"
#include "evlog.h"
#include "fonts/app_fonts.h"

static const char *TAG = "alarm";

#define SNOOZE_MINUTES 9

#define BIT_DISMISS   BIT0
#define BIT_SNOOZE    BIT1
#define BIT_SONG_IDLE BIT2

static EventGroupHandle_t s_ev;
static volatile bool s_active;
static volatile bool s_sound_abort;   /* also aborts an in-flight download */
static volatile int  s_ring_idx = -1; /* slot ringing now, -1 = idle */

/* Per-slot scheduler state (index = settings alarms[] slot). */
static time_t s_snooze_at[ALARM_COUNT];     /* 0 = no snooze pending */
static time_t s_last_fired_at[ALARM_COUNT]; /* scheduled instant last fired */
static time_t s_armed_since[ALARM_COUNT];   /* slot config last (re)armed */
static time_t s_skip_logged[ALARM_COUNT];   /* candidate whose skip was journaled */
static alarm_slot_t s_last_cfg[ALARM_COUNT];/* re-arm change detection */
static bool   s_boot_armed;                 /* first valid tick has run */

/* Ring timeout (settings max_alarm_seconds). One esp_timer, created once,
 * started per ring; its callback is a third "Dismiss" source. The deadline
 * is monotonic time, so an SNTP step mid-ring can't move it. s_ring_gen
 * lets a callback that fires after the ring already ended recognise it is
 * stale (esp_timer_stop does not wait for a callback in flight). */
static esp_timer_handle_t s_ring_timer;
static volatile uint32_t  s_ring_gen;
static volatile uint32_t  s_timer_gen;      /* generation the timer was armed for */
static volatile bool      s_timed_out;      /* this ring ended by timeout */
static int64_t            s_ring_deadline_us; /* 0 = unlimited */

/* Snapshot published by the scheduler once a second for lock-free readers
 * (ui_clock tick, HTTP status): the next ring, and the last missed one. */
static portMUX_TYPE s_snap_mux = portMUX_INITIALIZER_UNLOCKED;
static alarm_next_t s_next;
static bool         s_next_valid;
static alarm_next_t s_missed;
static bool         s_missed_valid;

/* s_last_fired_at[] is persisted so a reboot inside the catch-up window can
 * tell "already rang, was dismissed" from "crashed before ringing" (the
 * first is silent, the second rings). The write runs on the worker: the
 * alarm task never stalls on flash. When the worker queue is full the
 * request stays pending and the scheduler retries it every tick - same for
 * the settings save that once-mode needs. */
#define FIRED_NVS_NS      "bedside"
#define FIRED_NVS_KEY     "alarm_fired3"   /* int64_t[ALARM_COUNT] blob */
#define FIRED_NVS_KEY_V1  "alarm_fired"    /* pre-v9 single i64 (seeds slot 0) */
static int64_t s_fired_to_save[ALARM_COUNT];
static bool    s_fired_save_pending;
static bool    s_cfg_save_pending;

static void fired_save_job(void *arg)
{
    int64_t v[ALARM_COUNT];
    taskENTER_CRITICAL(&s_snap_mux);
    memcpy(v, s_fired_to_save, sizeof(v));
    taskEXIT_CRITICAL(&s_snap_mux);

    nvs_handle_t nvs;
    if (nvs_open(FIRED_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    if (nvs_set_blob(nvs, FIRED_NVS_KEY, v, sizeof(v)) == ESP_OK) {
        nvs_commit(nvs);
    }
    nvs_close(nvs);
}

static void fired_load(void)
{
    nvs_handle_t nvs;
    if (nvs_open(FIRED_NVS_NS, NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }
    int64_t v[ALARM_COUNT] = { 0 };
    size_t len = sizeof(v);
    if (nvs_get_blob(nvs, FIRED_NVS_KEY, v, &len) != ESP_OK || len != sizeof(v)) {
        memset(v, 0, sizeof(v));
        int64_t old = 0;
        if (nvs_get_i64(nvs, FIRED_NVS_KEY_V1, &old) == ESP_OK) {
            v[0] = old; /* the single alarm became slot 0 */
        }
    }
    nvs_close(nvs);
    for (int i = 0; i < ALARM_COUNT; i++) {
        s_last_fired_at[i] = (time_t)v[i];
        s_fired_to_save[i] = v[i];
    }
}

/* Post the fired-at write; on a full worker queue leave it pending. */
static void fired_save_request(void)
{
    taskENTER_CRITICAL(&s_snap_mux);
    for (int i = 0; i < ALARM_COUNT; i++) {
        s_fired_to_save[i] = (int64_t)s_last_fired_at[i];
    }
    taskEXIT_CRITICAL(&s_snap_mux);
    s_fired_save_pending = !app_run_async(fired_save_job, NULL);
    if (s_fired_save_pending) {
        ESP_LOGW(TAG, "fired-at save deferred (worker busy)");
    }
}

/* Settings save for once-mode auto-off (worker task: internal stack, flash
 * writes are fine there). */
static void cfg_save_job(void *arg)
{
    if (!settings_save()) {
        ui_log("Alarm: settings save FAILED after once-alarm switched off");
    }
}

static void cfg_save_request(void)
{
    s_cfg_save_pending = !app_run_async(cfg_save_job, NULL);
    if (s_cfg_save_pending) {
        ESP_LOGW(TAG, "settings save deferred (worker busy)");
    }
}

/* How far past the scheduled minute the alarm still fires. SNTP syncs are
 * hard steps (IMMED mode): an hourly resync can jump the clock straight
 * over the alarm minute, so exact minute equality would silently skip the
 * alarm. */
#define ALARM_CATCHUP_S (5 * 60)

static lv_obj_t *s_overlay;
static lv_obj_t *s_overlay_song;

/* ---------- task watchdog ----------
 * The alarm task is the one process whose silent wedge is unacceptable, so
 * it subscribes to the Task WDT (60 s, PANIC=y -> reboot; RTC time survives
 * and the scheduler re-arms). Every wait below goes through alarm_wait(),
 * which feeds in <= 5 s slices. The playlist path is UNsubscribed for its
 * duration: Navidrome calls are bounded but can legitimately exceed 60 s. */
#define WDT_SLICE_MS 5000
static bool s_wdt_subscribed;

static void alarm_wdt_feed(void)
{
    if (s_wdt_subscribed) {
        esp_task_wdt_reset();
    }
}

static void alarm_wdt_subscribe(bool on)
{
    if (on && !s_wdt_subscribed) {
        s_wdt_subscribed = (esp_task_wdt_add(NULL) == ESP_OK);
    } else if (!on && s_wdt_subscribed) {
        esp_task_wdt_delete(NULL);
        s_wdt_subscribed = false;
    }
}

/* xEventGroupWaitBits(s_ev, bits, clear, ANY) in watchdog-fed slices. */
static EventBits_t alarm_wait(EventBits_t bits, bool clear, int timeout_ms)
{
    int waited = 0;
    for (;;) {
        int slice = timeout_ms - waited;
        if (slice > WDT_SLICE_MS) {
            slice = WDT_SLICE_MS;
        }
        EventBits_t b = xEventGroupWaitBits(s_ev, bits, clear ? pdTRUE : pdFALSE,
                                            pdFALSE, pdMS_TO_TICKS(slice));
        alarm_wdt_feed();
        waited += slice;
        if ((b & bits) || waited >= timeout_ms) {
            return b;
        }
    }
}

/* ---------- LVGL ---------- */

static void dismiss_cb(lv_event_t *e)
{
    s_sound_abort = true;
    if (s_ev != NULL) {
        xEventGroupSetBits(s_ev, BIT_DISMISS);
    }
}

static void snooze_cb(lv_event_t *e)
{
    s_sound_abort = true;
    if (s_ev != NULL) {
        xEventGroupSetBits(s_ev, BIT_SNOOZE);
    }
}

/* voice equivalents of the two overlay buttons (see alarm.h) */
void alarm_voice_snooze(void)
{
    if (s_active && s_ev != NULL) {
        s_sound_abort = true;
        xEventGroupSetBits(s_ev, BIT_SNOOZE);
    }
}

void alarm_voice_dismiss(void)
{
    if (s_active && s_ev != NULL) {
        s_sound_abort = true;
        xEventGroupSetBits(s_ev, BIT_DISMISS);
    }
}

/* Ring-timeout timer callback (esp_timer task). Identical in effect to the
 * Dismiss button, so every wait in the ring loops ends the same way; the
 * generation check drops a callback that raced the end of the ring. */
static void ring_timeout_cb(void *arg)
{
    if (!s_active || s_timer_gen != s_ring_gen || s_ev == NULL) {
        return;
    }
    s_timed_out = true;
    s_sound_abort = true;
    xEventGroupSetBits(s_ev, BIT_DISMISS);
}

static void alarm_ui_show(void)
{
    if (!app_display_ready() || !bsp_display_lock(1000)) {
        return; /* headless: voice/API dismiss still work */
    }
    if (s_overlay != NULL) {
        /* a previous hide lost the lock race - clean it up now */
        lv_obj_delete(s_overlay);
        s_overlay = NULL;
        s_overlay_song = NULL;
    }
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x2A1010), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_flex_flow(s_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_overlay, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    time_t now = time(NULL);
    struct tm tm;
    clock_sync_localtime(&now, &tm);

    lv_obj_t *t = lv_label_create(s_overlay);
    lv_label_set_text_fmt(t, "%02d:%02d", tm.tm_hour, tm.tm_min);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0xFFD24D), 0);
    lv_obj_set_style_transform_scale(t, 256 * 2, 0);
    lv_obj_set_style_transform_pivot_x(t, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(t, lv_pct(50), 0);

    lv_obj_t *a = lv_label_create(s_overlay);
    lv_label_set_text_fmt(a, LV_SYMBOL_BELL "  ALARM %d", s_ring_idx + 1);
    lv_obj_set_style_text_font(a, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(a, lv_color_hex(0xE8EDF2), 0);

    s_overlay_song = lv_label_create(s_overlay);
    lv_label_set_text(s_overlay_song, "");
    lv_label_set_long_mode(s_overlay_song, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_overlay_song, lv_pct(90));
    lv_obj_set_style_text_align(s_overlay_song, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_overlay_song, &app_font_20, 0);
    lv_obj_set_style_text_color(s_overlay_song, lv_color_hex(0x9AA2AC), 0);

    lv_obj_t *snooze = lv_button_create(s_overlay);
    lv_obj_set_size(snooze, lv_pct(90), 110);
    lv_obj_set_style_bg_color(snooze, lv_color_hex(0x2C5F8A), 0);
    lv_obj_add_event_cb(snooze, snooze_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(snooze);
    lv_label_set_text_fmt(sl, "Snooze  (%d min)", SNOOZE_MINUTES);
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_28, 0);
    lv_obj_center(sl);

    lv_obj_t *dismiss = lv_button_create(s_overlay);
    lv_obj_set_size(dismiss, lv_pct(90), 110);
    lv_obj_set_style_bg_color(dismiss, lv_color_hex(0x9B3535), 0);
    lv_obj_add_event_cb(dismiss, dismiss_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dl = lv_label_create(dismiss);
    lv_label_set_text(dl, "Dismiss");
    lv_obj_set_style_text_font(dl, &lv_font_montserrat_28, 0);
    lv_obj_center(dl);

    bsp_display_unlock();
}

static void alarm_ui_hide(void)
{
    if (s_overlay == NULL) {
        return;
    }
    /* The overlay covers the whole screen: a single missed lock must not
     * leave it up forever. Retry hard; alarm_ui_show also self-heals. */
    for (int attempt = 0; attempt < 5; attempt++) {
        if (bsp_display_lock(1000)) {
            lv_obj_delete(s_overlay);
            s_overlay = NULL;
            s_overlay_song = NULL;
            bsp_display_unlock();
            return;
        }
    }
    ESP_LOGE(TAG, "could not take LVGL lock to remove alarm overlay");
}

static void alarm_ui_song(const char *fmt, ...)
{
    char line[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (s_overlay_song != NULL && app_display_ready() && bsp_display_lock(1000)) {
        if (s_overlay_song != NULL) {
            lv_label_set_text(s_overlay_song, line);
        }
        bsp_display_unlock();
    }
}

/* ---------- sound ---------- */

static void player_cb(audio_player_cb_ctx_t *ctx)
{
    switch (ctx->audio_event) {
    case AUDIO_PLAYER_CALLBACK_EVENT_IDLE:
    case AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN_FILE_TYPE:
        xEventGroupSetBits(s_ev, BIT_SONG_IDLE);
        break;
    default:
        break;
    }
}

static bool player_ensure_init(void)
{
    static bool s_inited;
    if (s_inited) {
        return true;
    }
    app_init_lock(); /* may race music_player's identical lazy init */
    if (!s_inited) {
        if (!app_audio_ensure_init() || bsp_extra_player_init() != ESP_OK) {
            app_init_unlock();
            ui_log("audio player init failed");
            return false;
        }
        s_inited = true;
    }
    app_init_unlock();
    return true;
}

/* Pleasant rising two-tone chime, looped until dismissed/snoozed.
 * Runs on the alarm task; checks s_sound_abort every chunk. */
/* The overlay is the only way to stop the alarm by touch. alarm_ui_show can
 * miss its 1 s lock; keep trying from inside the ring loops (cheap: a NULL
 * check per tone / per song) so a ringing alarm never runs button-less. */
static void alarm_ui_ensure_shown(void)
{
    if (s_overlay == NULL) {
        alarm_ui_show();
    }
}

static void chime_loop(void)
{
    if (!app_audio_ensure_init()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        return;
    }
    if (bsp_extra_codec_set_fs(AUDIO_SAMPLE_RATE, AUDIO_BITS,
                               AUDIO_SLOT_MODE) != ESP_OK) {
        /* both codec devices may be closed after a failed reconfigure
         * (I2S DMA descriptor OOM): no point burning 10 failed writes -
         * back off and let alarm_fire's retry loop try again */
        ui_log("Alarm chime: codec reconfigure failed - retrying");
        vTaskDelay(pdMS_TO_TICKS(1000));
        return;
    }
    bsp_extra_codec_volume_set(settings_get()->alarm_volume, NULL);

    static int16_t buf[512];
    const float tones[2] = { 523.25f, 659.25f };  /* C5, E5 */
    int tone = 0;
    int io_errs = 0; /* consecutive codec write failures */

    while (!s_sound_abort) {
        alarm_ui_ensure_shown();
        alarm_wdt_feed(); /* one tone = 0.6 s */
        /* 400 ms tone, 200 ms gap */
        const int total = AUDIO_SAMPLE_RATE * 2 / 5;
        const float w = 2.0f * (float)M_PI * tones[tone] / AUDIO_SAMPLE_RATE;
        for (int done = 0; done < total && !s_sound_abort; ) {
            int n = total - done;
            if (n > 512) {
                n = 512;
            }
            for (int i = 0; i < n; i++) {
                int t = done + i;
                float env = 1.0f;
                if (t < 400) {
                    env = t / 400.0f;
                } else if (t > total - 800) {
                    env = (total - t) / 800.0f;
                }
                buf[i] = (int16_t)(sinf(w * t) * 8000.0f * env);
            }
            size_t written;
            if (bsp_extra_i2s_write(buf, n * sizeof(int16_t), &written,
                                    1000) != ESP_OK) {
                /* a failing write returns instantly: without a backoff this
                 * loop would spin at 100% CPU and starve the idle task */
                if (++io_errs >= 10) {
                    ui_log("Alarm chime: codec write keeps failing - muting");
                evlog(EV_ERR, "chime: codec write failing - muted");
                    return; /* overlay stays up; Dismiss/Snooze still work */
                }
                vTaskDelay(pdMS_TO_TICKS(100));
                continue; /* retry the same chunk */
            }
            io_errs = 0;
            done += n;
        }
        memset(buf, 0, sizeof(buf));
        for (int done = 0; done < AUDIO_SAMPLE_RATE / 5 && !s_sound_abort; ) {
            size_t written;
            if (bsp_extra_i2s_write(buf, sizeof(buf), &written,
                                    1000) != ESP_OK) {
                if (++io_errs >= 10) {
                    ui_log("Alarm chime: codec write keeps failing - muting");
                evlog(EV_ERR, "chime: codec write failing - muted");
                    return;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            io_errs = 0;
            done += 512;
        }
        tone ^= 1;
    }
}

/* How long the playlist path may keep trying (other songs) before the ring
 * falls back to the chime, and how many consecutive failures it tolerates.
 * The budget restarts after every song that did play. One silent-server
 * download costs NAV_HTTP_TIMEOUT_MS (15 s), so ~2 attempts fit; the ring
 * cap (settings max_alarm_seconds, default 10 min) easily covers it. */
#define ALARM_PLAYLIST_BUDGET_MS 30000
#define ALARM_DL_MAX_FAILS       3

/* Fetch the wake playlist's songs. A playlist that was deleted and recreated
 * server-side (the category_* lists are script-generated) keeps its name but
 * gets a new id, so on failure the stored id is re-resolved by name once and,
 * when it changed, persisted. Returns false with the reason journaled by the
 * caller. */
static bool fetch_playlist_songs(int idx, nav_song_list_t *songs)
{
    char id[sizeof(((bedside_settings_t *)0)->alarm_playlist_id)];
    char name[sizeof(((bedside_settings_t *)0)->alarm_playlist_name)];
    settings_lock();
    strlcpy(id, settings_get()->alarm_playlist_id, sizeof id);
    strlcpy(name, settings_get()->alarm_playlist_name, sizeof name);
    settings_unlock();

    if (nav_get_playlist_songs(id, songs) == ESP_OK) {
        return true;
    }
    /* Only a server that answered "not found" (Subsonic 70) is worth a
     * second round trip; an unreachable one would just double the silence
     * before the chime. */
    if (s_sound_abort || name[0] == '\0' || nav_last_api_error() != 70) {
        return false;
    }

    nav_playlist_list_t pls;
    if (nav_get_playlists(&pls) != ESP_OK) {
        return false;
    }
    bool found = false;
    for (int i = 0; i < pls.count; i++) {
        if (strcmp(pls.items[i].name, name) == 0) {
            found = true;
            if (strcmp(pls.items[i].id, id) != 0) {
                strlcpy(id, pls.items[i].id, sizeof id);
                settings_lock();
                strlcpy(settings_get()->alarm_playlist_id, id,
                        sizeof(settings_get()->alarm_playlist_id));
                settings_unlock();
                ui_log("Alarm: playlist '%s' has a new id - stored", name);
                evlog(EV_ALARM, "A%d playlist id refreshed", idx + 1);
                cfg_save_request();
            }
            break;
        }
    }
    nav_free_playlists(&pls);
    if (!found) {
        ui_log("Alarm: playlist '%s' no longer exists on the server", name);
        evlog(EV_ERR, "A%d playlist gone: %.22s", idx + 1, name);
        return false;
    }
    return nav_get_playlist_songs(id, songs) == ESP_OK;
}

/* Play random songs from the wake playlist until dismissed/snoozed.
 * Returns false if it could not play anything (caller falls back to chime).
 * Every way out that is not a button press is journaled: "it chimed instead
 * of playing" must be answerable from the device the next morning. */
static bool playlist_loop(int idx)
{
    const bedside_settings_t *cfg = settings_get();
    const char *pre = NULL;
    if (cfg->alarm_playlist_id[0] == '\0') {
        pre = "no playlist chosen";
    } else if (!nav_is_configured()) {
        pre = "music server not set";
    } else if (!wifi_mgr_is_connected()) {
        pre = "wifi down";
    } else if (!player_ensure_init()) {
        pre = "player init failed";
    }
    if (pre != NULL) {
        ui_log("Alarm: playlist unavailable - %s", pre);
        evlog(EV_ERR, "A%d playlist: %s", idx + 1, pre);
        return false;
    }

    nav_song_list_t songs;
    if (!fetch_playlist_songs(idx, &songs)) {
        if (!s_sound_abort) {
            evlog(EV_ERR, "A%d playlist fetch failed", idx + 1);
        }
        return false;
    }
    if (songs.count == 0) {
        nav_free_songs(&songs);
        ui_log("Alarm: playlist is empty");
        evlog(EV_ERR, "A%d playlist is empty", idx + 1);
        return false;
    }

    bsp_extra_codec_volume_set(cfg->alarm_volume, NULL);
    bsp_extra_player_register_callback(player_cb, NULL);
    audio_song_buf_reap();

    bool played_any = false;
    int fails = 0; /* consecutive */
    int64_t budget_end = esp_timer_get_time() + ALARM_PLAYLIST_BUDGET_MS * 1000LL;
    while (!s_sound_abort) {
        alarm_ui_ensure_shown();
        const nav_song_t *song = &songs.items[esp_random() % songs.count];
        alarm_ui_song("%s", song->title);

        uint8_t *buf = NULL;
        size_t len = 0;
        char why[28];
        if (nav_download_song(song->id, &buf, &len, &s_sound_abort,
                              why, sizeof why) != ESP_OK) {
            if (s_sound_abort) {
                break;
            }
            fails++;
            ui_log("Alarm: song download failed (%s), attempt %d/%d",
                   why, fails, ALARM_DL_MAX_FAILS);
            evlog(EV_ERR, "A%d dl fail %d: %.24s", idx + 1, fails, why);
            if (fails >= ALARM_DL_MAX_FAILS ||
                esp_timer_get_time() >= budget_end) {
                break; /* server unwell: caller falls back to the chime */
            }
            vTaskDelay(pdMS_TO_TICKS(2000)); /* no delay-free retry hammer */
            continue;
        }
        FILE *fp = fmemopen(buf, len, "rb");
        if (fp == NULL) {
            heap_caps_free(buf);
            fails++;
            evlog(EV_ERR, "A%d fmemopen failed", idx + 1);
            if (fails >= ALARM_DL_MAX_FAILS ||
                esp_timer_get_time() >= budget_end) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(500)); /* no back-to-back re-downloads */
            continue;
        }
        xEventGroupClearBits(s_ev, BIT_SONG_IDLE);
        if (audio_player_play(fp) != ESP_OK) {
            fclose(fp);
            heap_caps_free(buf);
            fails++;
            ui_log("Alarm: audio player refused the song");
            evlog(EV_ERR, "A%d player start failed", idx + 1);
            if (fails >= ALARM_DL_MAX_FAILS ||
                esp_timer_get_time() >= budget_end) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        played_any = true;
        fails = 0;
        ui_log("Alarm: playing %s", song->title);
        evlog(EV_ALARM, "A%d playing %.30s", idx + 1, song->title);

        /* Wake on song end OR Dismiss/Snooze - the buttons must be able to
         * interrupt mid-song. Don't clear the button bits here; alarm_fire
         * reads them after the sound stops. Bounded: a lost IDLE event
         * (stolen callback, wedged decoder) must not park the alarm forever
         * with a silent overlay - fall back to the chime instead. */
        EventBits_t bits = alarm_wait(BIT_SONG_IDLE | BIT_DISMISS | BIT_SNOOZE,
                                      false, 15 * 60 * 1000);
        if (bits & (BIT_DISMISS | BIT_SNOOZE)) {
            audio_player_stop();
            /* player fcloses the FILE* when it reaches IDLE; if that wait
             * times out the decoder may still read buf - defer the free */
            EventBits_t idle = xEventGroupWaitBits(s_ev, BIT_SONG_IDLE,
                                                   pdTRUE, pdFALSE,
                                                   pdMS_TO_TICKS(3000));
            audio_song_buf_release(buf, (idle & BIT_SONG_IDLE) != 0);
            break;
        }
        if ((bits & BIT_SONG_IDLE) == 0) {
            ui_log("Alarm: song wait timed out - falling back to chime");
            evlog(EV_ERR, "alarm song wait timed out - chime");
            audio_player_stop();
            EventBits_t idle = xEventGroupWaitBits(s_ev, BIT_SONG_IDLE,
                                                   pdTRUE, pdFALSE,
                                                   pdMS_TO_TICKS(3000));
            audio_song_buf_release(buf, (idle & BIT_SONG_IDLE) != 0);
            break;
        }
        xEventGroupClearBits(s_ev, BIT_SONG_IDLE);
        audio_song_buf_release(buf, true); /* song finished: decoder idle */
        budget_end = esp_timer_get_time() + ALARM_PLAYLIST_BUDGET_MS * 1000LL;
    }

    bsp_extra_player_register_callback(NULL, NULL);
    nav_free_songs(&songs);
    return played_any;
}

/* ---------- firing ---------- */

/* Ring slot `idx` (scheduled instant `at`; `mask` = every slot that shares
 * this instant, so their once-flags are honoured too). Runs on the alarm
 * task; returns when the ring ended by Snooze, Dismiss or timeout. */
static void alarm_fire(int idx, uint8_t mask, time_t at, bool from_snooze)
{
    const bedside_settings_t *cfg = settings_get();
    const alarm_slot_t slot = cfg->alarms[idx];
    s_ring_idx = idx;
    s_timed_out = false;
    s_active = true;
    s_sound_abort = false;
    xEventGroupClearBits(s_ev, BIT_DISMISS | BIT_SNOOZE | BIT_SONG_IDLE);
    /* a new ring supersedes any unacknowledged "missed" marker */
    taskENTER_CRITICAL(&s_snap_mux);
    s_missed_valid = false;
    taskEXIT_CRITICAL(&s_snap_mux);

    ui_log("ALARM %d firing (%02d:%02d, mode %d)", idx + 1, slot.hh, slot.mm,
           cfg->alarm_mode);
    evlog(EV_ALARM, "A%d ringing %02d:%02d (%s)%s", idx + 1, slot.hh, slot.mm,
          cfg->alarm_mode == ALARM_MODE_PLAYLIST ? "playlist" : "chime",
          from_snooze ? " after snooze" : "");
    /* the alarm owns the audio player while ringing; s_active is already
     * set, so a still-busy music task bails at its next should_stop() */
    bool music_stopped = music_stop_sync(3000);
    if (!music_stopped) {
        /* The music task can be blocked in a long HTTP/AI call. If it were
         * still driving the player, registering our callback would steal
         * its IDLE events and its eventual halt would kill our song - so
         * ring with the chime, which bypasses the audio player entirely. */
        ui_log("Alarm: music engine still busy - using chime");
        evlog(EV_ERR, "music engine busy at alarm - chime");
    }
    bsp_display_brightness_set(cfg->bright_max);
    ui_clock_set_dim_level(cfg->bright_max);
    alarm_ui_show();

    /* Arm the ring timeout now, so the budget covers audible time rather
     * than the music-stop wait above. 0 = ring until dismissed. */
    int max_s = cfg->max_alarm_seconds;
    s_ring_deadline_us = 0;
    if (max_s > 0 && s_ring_timer != NULL) {
        s_timer_gen = s_ring_gen;
        s_ring_deadline_us = esp_timer_get_time() + (int64_t)max_s * 1000000;
        if (esp_timer_start_once(s_ring_timer, (uint64_t)max_s * 1000000) != ESP_OK) {
            ESP_LOGW(TAG, "ring timeout timer failed to start");
            s_ring_deadline_us = 0;
        }
    }

    /* Ring until Dismiss/Snooze (or the max ring time, when one is set) and
     * NEVER give up before that: a bedside alarm that silently self-dismisses
     * on an audio failure is the one bug this device must not have. If even
     * the chime cannot sound (codec dead), keep the overlay up and retry -
     * touch and voice still work. */
    bool try_playlist = (cfg->alarm_mode == ALARM_MODE_PLAYLIST) &&
                        music_stopped;
    while (!s_sound_abort) {
        alarm_ui_ensure_shown();
        alarm_wdt_feed();
        if (try_playlist) {
            try_playlist = false; /* one pass; failures fall through */
            /* HTTPS fetch + whole-song downloads: bounded, but not by 60 s */
            alarm_wdt_subscribe(false);
            /* Whole-file TLS downloads with software AES are CPU-bound; at
             * the alarm task's priority 5 they starve taskLVGL (4) for the
             * whole transfer (bench: 6 s UI stall = Snooze/Dismiss dead).
             * Run this phase at the bulk-transfer level like song_dl/music. */
            vTaskPrioritySet(NULL, 3);
            playlist_loop(idx);
            vTaskPrioritySet(NULL, 5);
            alarm_wdt_subscribe(true);
            if (s_sound_abort) {
                break;
            }
            evlog(EV_ERR, "A%d playlist gave up - chime", idx + 1);
            alarm_ui_song("(playlist unavailable - chime)");
        }
        chime_loop();
        if (s_sound_abort) {
            break;
        }
        /* chime could not sound: wait a bit for a button, then retry */
        EventBits_t b = alarm_wait(BIT_DISMISS | BIT_SNOOZE, false, 10000);
        if (b & (BIT_DISMISS | BIT_SNOOZE)) {
            break;
        }
    }

    /* sound has stopped; find out which button did it */
    if (s_ring_timer != NULL) {
        esp_timer_stop(s_ring_timer); /* INVALID_STATE when it already fired */
    }
    EventBits_t bits = alarm_wait(BIT_DISMISS | BIT_SNOOZE, true, 1000);
    bool timed_out = s_timed_out;
    alarm_ui_hide();

    if ((bits & BIT_SNOOZE) && !timed_out) {
        s_snooze_at[idx] = time(NULL) + SNOOZE_MINUTES * 60;
        struct tm tm;
        clock_sync_localtime(&s_snooze_at[idx], &tm);
        ui_set_status("Alarm %d snoozing until %02d:%02d", idx + 1,
                      tm.tm_hour, tm.tm_min);
        evlog(EV_ALARM, "A%d snoozed until %02d:%02d", idx + 1,
              tm.tm_hour, tm.tm_min);
    } else {
        /* dismissed or timed out: this instant is finished for every slot
         * that shared it - no pending snooze survives */
        for (int j = 0; j < ALARM_COUNT; j++) {
            if (mask & (1u << j)) {
                s_snooze_at[j] = 0;
            }
        }
        if (timed_out) {
            taskENTER_CRITICAL(&s_snap_mux);
            s_missed.index = idx;
            s_missed.at = at;
            s_missed.snooze = from_snooze;
            s_missed_valid = true;
            taskEXIT_CRITICAL(&s_snap_mux);
            ui_set_status("Alarm %d stopped after %d min - nobody home?",
                          idx + 1, (max_s + 30) / 60);
            evlog(EV_ALARM, "A%d %stimed out after %d min", idx + 1,
                  from_snooze ? "snooze ring " : "", (max_s + 30) / 60);
        } else {
            taskENTER_CRITICAL(&s_snap_mux);
            s_missed_valid = false; /* a real dismiss acknowledges it */
            taskEXIT_CRITICAL(&s_snap_mux);
            ui_set_status("Alarm %d dismissed", idx + 1);
            evlog(EV_ALARM, "A%d dismissed%s", idx + 1,
                  (bits & BIT_DISMISS) ? "" : " (no button: sound ended)");
        }
        /* once-mode: the slot has done its job - switch it off and persist.
         * Never on snooze, or the re-ring would cancel itself. */
        bool changed = false;
        bedside_settings_t *wcfg = settings_get();
        settings_lock();
        for (int j = 0; j < ALARM_COUNT; j++) {
            if ((mask & (1u << j)) && (wcfg->alarms[j].days & ALARM_DAYS_ONCE) &&
                wcfg->alarms[j].enabled) {
                wcfg->alarms[j].enabled = 0;
                changed = true;
                evlog(EV_ALARM, "A%d once: switched off", j + 1);
            }
        }
        settings_unlock();
        if (changed) {
            cfg_save_request();
        }
    }

    s_ring_idx = -1;
    s_active = false;
    s_ring_gen++; /* any timer callback still in flight is now stale */
    /* hand the codec back to the general volume */
    bsp_extra_codec_volume_set(settings_get()->volume, NULL);
}

/* Publish the next-ring snapshot: the earliest of every pending snooze and
 * every slot's next scheduled instant strictly after `now`. `sched[i]` is
 * today's instant of slot i; the TZ is a fixed UTC offset (no DST), so
 * stepping whole days by 86400 s is exact and the weekday is arithmetic. */
static void publish_next(time_t now, int today_wday,
                         const alarm_slot_t *slots, const time_t *sched)
{
    alarm_next_t best = { .index = -1, .at = 0, .snooze = false };
    for (int i = 0; i < ALARM_COUNT; i++) {
        if (s_snooze_at[i] != 0 && s_snooze_at[i] > now &&
            (best.index < 0 || s_snooze_at[i] < best.at)) {
            best.index = i;
            best.at = s_snooze_at[i];
            best.snooze = true;
        }
    }
    for (int i = 0; i < ALARM_COUNT; i++) {
        if (!slots[i].enabled) {
            continue;
        }
        for (int d = 0; d <= 7; d++) {
            time_t cand = sched[i] + (time_t)d * 86400;
            if (cand <= now || !alarm_days_match(slots[i].days, (today_wday + d) % 7)) {
                continue;
            }
            if (best.index < 0 || cand < best.at) {
                best.index = i;
                best.at = cand;
                best.snooze = false;
            }
            break;
        }
    }
    taskENTER_CRITICAL(&s_snap_mux);
    s_next = best;
    s_next_valid = best.index >= 0;
    taskEXIT_CRITICAL(&s_snap_mux);
}

static void alarm_task(void *arg)
{
    int last_tz = 0;

    alarm_wdt_subscribe(true);
    if (!s_wdt_subscribed) {
        ESP_LOGW(TAG, "task watchdog subscribe failed - running unwatched");
        evlog(EV_ERR, "alarm task WDT subscribe failed");
    }

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        alarm_wdt_feed();

        /* deferred flash writes: retry until the worker takes them */
        if (s_fired_save_pending) {
            s_fired_save_pending = !app_run_async(fired_save_job, NULL);
        }
        if (s_cfg_save_pending) {
            s_cfg_save_pending = !app_run_async(cfg_save_job, NULL);
        }

        if (!clock_sync_time_valid() || s_active) {
            continue;
        }

        time_t now = time(NULL);
        bedside_settings_t *cfg = settings_get();

        /* One consistent copy of the slots: PATCH writes a slot's fields
         * under the settings lock, and a torn {new hh, old mm} read that
         * happened to equal the current minute would ring. */
        alarm_slot_t slots[ALARM_COUNT];
        settings_lock();
        memcpy(slots, cfg->alarms, sizeof(slots));
        int tz = cfg->tz_offset_min;
        settings_unlock();

        /* Re-arm a slot whenever its config changes: the catch-up window
         * below must never fire for an instant that predates the current
         * arming, or "set alarm to 07:00" at 07:02 would ring immediately.
         * A timezone edit moves every local instant, so it re-arms all.
         *
         * The FIRST valid tick after boot is different: arm from the start of
         * the catch-up window, not from now. A crash/WDT/ui_watch reboot that
         * lands 30 s after the alarm minute used to arm at boot time and drop
         * that day's alarm on the floor. The persisted s_last_fired_at[] keeps
         * an alarm that already rang before the reboot from ringing twice. */
        bool first_tick = !s_boot_armed;
        for (int i = 0; i < ALARM_COUNT; i++) {
            if (first_tick || tz != last_tz ||
                memcmp(&slots[i], &s_last_cfg[i], sizeof(alarm_slot_t)) != 0) {
                s_armed_since[i] = first_tick ? now - ALARM_CATCHUP_S : now;
                if (!first_tick) {
                    evlog(EV_ALARM, "A%d re-armed: %s %02d:%02d d=%02X", i + 1,
                          slots[i].enabled ? "on" : "off", slots[i].hh,
                          slots[i].mm, slots[i].days);
                }
                s_last_cfg[i] = slots[i];
            }
            if (now < s_armed_since[i]) {
                s_armed_since[i] = now; /* clock stepped backwards: re-arm */
            }
            if (!slots[i].enabled) {
                s_snooze_at[i] = 0; /* disabling a slot cancels its snooze */
            }
        }
        if (first_tick) {
            /* one line for the whole set - the ring only holds 40 entries */
            char line[EVLOG_TEXT_LEN];
            int n = snprintf(line, sizeof(line), "armed at boot:");
            bool any = false;
            for (int i = 0; i < ALARM_COUNT && n < (int)sizeof(line); i++) {
                if (slots[i].enabled) {
                    n += snprintf(line + n, sizeof(line) - n, " A%d %02d:%02d",
                                  i + 1, slots[i].hh, slots[i].mm);
                    any = true;
                }
            }
            evlog(EV_ALARM, "%s%s", line, any ? "" : " no alarm on");
            s_boot_armed = true;
        }
        last_tz = tz;

        /* Today's instant of every slot. Fire on "scheduled instant has
         * passed and was not yet fired" with a catch-up window, NOT on
         * minute equality: an SNTP hard step (or a busy second) may jump
         * straight over the minute. */
        struct tm tm;
        clock_sync_localtime(&now, &tm);
        time_t sched[ALARM_COUNT];
        for (int i = 0; i < ALARM_COUNT; i++) {
            struct tm atm = tm;
            atm.tm_hour = slots[i].hh;
            atm.tm_min = slots[i].mm;
            atm.tm_sec = 0;
            atm.tm_isdst = -1;
            sched[i] = clock_sync_mktime(&atm);
        }

        /* Earliest due ring wins (lowest slot on a tie). A due snooze is a
         * candidate like any other. */
        int    fire_idx = -1;
        time_t fire_at = 0;
        bool   fire_snooze = false;
        time_t elig[ALARM_COUNT];   /* eligible scheduled candidate per slot, 0 = none */
        for (int i = 0; i < ALARM_COUNT; i++) {
            elig[i] = 0;
            if (s_snooze_at[i] != 0 && now >= s_snooze_at[i] &&
                (fire_idx < 0 || s_snooze_at[i] < fire_at)) {
                fire_idx = i;
                fire_at = s_snooze_at[i];
                fire_snooze = true;
            }
            if (!slots[i].enabled) {
                continue;
            }
            /* today's instant, or yesterday's when a step just crossed
             * midnight; the weekday test is on the candidate's own day */
            for (int d = 0; d < 2 && elig[i] == 0; d++) {
                time_t cand = sched[i] - (time_t)d * 86400;
                if (now < cand || now - cand >= ALARM_CATCHUP_S) {
                    continue; /* not inside this candidate's window */
                }
                if (!alarm_days_match(slots[i].days, (tm.tm_wday + 7 - d) % 7)) {
                    continue; /* not one of this slot's days */
                }
                if (cand != s_last_fired_at[i] && cand >= s_armed_since[i]) {
                    elig[i] = cand;
                } else if (s_skip_logged[i] != cand) {
                    /* One journal line per skipped instant: after a reboot
                     * this is the difference between "already rang before
                     * the reset" and "config changed after the minute". */
                    s_skip_logged[i] = cand;
                    evlog(EV_ALARM, "A%d %02d:%02d not rung: %s", i + 1,
                          slots[i].hh, slots[i].mm,
                          cand == s_last_fired_at[i] ? "already rang"
                                                     : "armed after it");
                }
            }
            if (elig[i] != 0 && (fire_idx < 0 || elig[i] < fire_at ||
                                 (elig[i] == fire_at && fire_snooze))) {
                fire_idx = i;
                fire_at = elig[i];
                fire_snooze = false;
            }
        }

        publish_next(now, tm.tm_wday, slots, sched);

        if (fire_idx < 0) {
            continue;
        }
        uint8_t mask = 0;
        if (fire_snooze) {
            s_snooze_at[fire_idx] = 0; /* served; alarm_fire re-arms it on Snooze */
            mask = (uint8_t)(1u << fire_idx);
            evlog(EV_ALARM, "A%d snooze due", fire_idx + 1);
        } else {
            /* every slot due at this same instant rings once, together */
            for (int i = 0; i < ALARM_COUNT; i++) {
                if (elig[i] == fire_at) {
                    s_last_fired_at[i] = fire_at;
                    s_skip_logged[i] = fire_at; /* the post-ring window is not a skip */
                    mask |= (uint8_t)(1u << i);
                }
            }
            fired_save_request();
        }
        alarm_fire(fire_idx, mask, fire_at, fire_snooze);
    }
}

void alarm_init(void)
{
    /* worker task (boot_seq): NVS read is fine here */
    fired_load();

    s_ev = xEventGroupCreate();
    if (s_ev == NULL) {
        ui_log("Alarm: event group alloc failed - alarm DISABLED");
        evlog(EV_ERR, "alarm DISABLED: event group alloc failed");
        return;
    }
    const esp_timer_create_args_t targs = {
        .callback = ring_timeout_cb,
        .name = "alarm_max",
    };
    if (esp_timer_create(&targs, &s_ring_timer) != ESP_OK) {
        s_ring_timer = NULL; /* alarms still ring - just never time out */
        ui_log("Alarm: ring-timeout timer alloc failed - no auto-stop");
        evlog(EV_ERR, "alarm: no ring timeout timer");
    }
    if (xTaskCreate(alarm_task, "alarm", 16384, NULL, 5, NULL) != pdPASS) {
        /* never run the overlay callbacks against a half-built alarm */
        vEventGroupDelete(s_ev);
        s_ev = NULL;
        ui_log("Alarm: task create failed (internal RAM?) - alarm DISABLED");
        evlog(EV_ERR, "alarm DISABLED: task create failed");
    }
}

bool alarm_is_active(void)
{
    return s_active;
}

int alarm_ringing_index(void)
{
    return s_active ? s_ring_idx : -1;
}

int alarm_ring_remaining_s(void)
{
    if (!s_active || s_ring_deadline_us == 0) {
        return 0;
    }
    int64_t rem = (s_ring_deadline_us - esp_timer_get_time() + 999999) / 1000000;
    return rem > 0 ? (int)rem : 0;
}

bool alarm_next(alarm_next_t *out)
{
    taskENTER_CRITICAL(&s_snap_mux);
    bool valid = s_next_valid;
    if (valid && out != NULL) {
        *out = s_next;
    }
    taskEXIT_CRITICAL(&s_snap_mux);
    return valid;
}

bool alarm_last_missed(alarm_next_t *out)
{
    taskENTER_CRITICAL(&s_snap_mux);
    bool valid = s_missed_valid;
    if (valid && out != NULL) {
        *out = s_missed;
    }
    taskEXIT_CRITICAL(&s_snap_mux);
    return valid;
}

void alarm_ack_missed(void)
{
    taskENTER_CRITICAL(&s_snap_mux);
    s_missed_valid = false;
    taskEXIT_CRITICAL(&s_snap_mux);
}
