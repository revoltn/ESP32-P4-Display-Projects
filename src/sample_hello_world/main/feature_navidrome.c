/* Navidrome playback: pick a playlist, stream its songs to the ES8311.
 *
 * Each song is downloaded whole into PSRAM (server transcodes to 128 kbps
 * MP3) and handed to the audio_player as a seekable fmemopen() FILE*; the
 * next song downloads while the current one plays, so track changes are
 * near-gapless. The worker task owns all sequencing — LVGL callbacks only
 * set event-group bits. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"

#include "app_common.h"
#include "navidrome_client.h"

static const char *TAG = "navidrome_ui";

#define BIT_PICKED    BIT0
#define BIT_CLOSE     BIT1
#define BIT_STOP      BIT2
#define BIT_SONG_IDLE BIT3
#define BIT_NEXT      BIT4
#define BIT_PREV      BIT5
#define BITS_ALL      (BIT_PICKED | BIT_CLOSE | BIT_STOP | BIT_SONG_IDLE | \
                       BIT_NEXT | BIT_PREV)

static EventGroupHandle_t s_ev;
static volatile int  s_picked_index;
static volatile bool s_abort_dl;

/* Overlay widgets (valid between nav_ui_create and nav_ui_destroy). */
static lv_obj_t *s_root;
static lv_obj_t *s_view_list;
static lv_obj_t *s_view_np;
static lv_obj_t *s_np_playlist;
static lv_obj_t *s_np_title;
static lv_obj_t *s_np_artist;
static lv_obj_t *s_np_count;
static lv_obj_t *s_np_state;
static lv_obj_t *s_np_error;

/* ---------- LVGL callbacks: set bits only, never block ---------- */

static void playlist_btn_cb(lv_event_t *e)
{
    s_picked_index = (int)(uintptr_t)lv_event_get_user_data(e);
    xEventGroupSetBits(s_ev, BIT_PICKED);
}

static void close_btn_cb(lv_event_t *e)
{
    xEventGroupSetBits(s_ev, BIT_CLOSE);
}

static void stop_btn_cb(lv_event_t *e)
{
    s_abort_dl = true;     /* breaks an in-flight download */
    xEventGroupSetBits(s_ev, BIT_STOP);
}

static void next_btn_cb(lv_event_t *e)
{
    s_abort_dl = true;
    xEventGroupSetBits(s_ev, BIT_NEXT);
}

static void prev_btn_cb(lv_event_t *e)
{
    s_abort_dl = true;
    xEventGroupSetBits(s_ev, BIT_PREV);
}

/* Volume slider. Setting the codec volume is one small I2C register write;
 * the i2c_master driver serializes bus access, so doing it directly from the
 * LVGL task is safe (same pattern as the vendor demo's volume UI). */
static lv_obj_t *s_vol_label;

static void volume_slider_cb(lv_event_t *e)
{
    int v = (int)lv_slider_get_value(lv_event_get_target(e));
    bsp_extra_codec_volume_set(v, NULL);
    if (s_vol_label != NULL) {
        lv_label_set_text_fmt(s_vol_label, "%d%%", v);
    }
}

/* Runs on the audio_player task — no LVGL, no blocking. */
static void nav_player_cb(audio_player_cb_ctx_t *ctx)
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

/* ---------- overlay UI (all helpers take the display lock) ---------- */

static void nav_ui_create(const nav_playlist_list_t *pls)
{
    if (!bsp_display_lock(1000)) {
        return;
    }

    s_root = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 12, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    /* --- playlist picker view --- */
    s_view_list = lv_obj_create(s_root);
    lv_obj_set_size(s_view_list, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_view_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_view_list, 0, 0);
    lv_obj_set_style_pad_all(s_view_list, 0, 0);
    lv_obj_set_flex_flow(s_view_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_view_list, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_view_list, 10, 0);
    lv_obj_clear_flag(s_view_list, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_view_list);
    lv_label_set_text(title, "Navidrome");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8EDF2), 0);

    lv_obj_t *list = lv_list_create(s_view_list);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x181C22), 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_row(list, 6, 0);

    for (int i = 0; i < pls->count; i++) {
        char txt[96];
        snprintf(txt, sizeof txt, "%s  (%d songs)",
                 pls->items[i].name, pls->items[i].song_count);
        lv_obj_t *btn = lv_list_add_button(list, LV_SYMBOL_AUDIO, txt);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2C5F8A), 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(0xE8EDF2), 0);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_20, 0);
        lv_obj_set_style_pad_ver(btn, 18, 0);
        lv_obj_add_event_cb(btn, playlist_btn_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
    }

    lv_obj_t *close = lv_button_create(s_view_list);
    lv_obj_set_size(close, lv_pct(100), 64);
    lv_obj_set_style_bg_color(close, lv_color_hex(0x4A4F55), 0);
    lv_obj_add_event_cb(close, close_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(close);
    lv_label_set_text(cl, "Close");
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_20, 0);
    lv_obj_center(cl);

    /* --- now-playing view (hidden until a playlist starts) --- */
    s_view_np = lv_obj_create(s_root);
    lv_obj_set_size(s_view_np, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_view_np, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_view_np, 0, 0);
    lv_obj_set_style_pad_all(s_view_np, 0, 0);
    lv_obj_set_flex_flow(s_view_np, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_view_np, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_view_np, 18, 0);
    lv_obj_clear_flag(s_view_np, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_view_np, LV_OBJ_FLAG_HIDDEN);

    s_np_playlist = lv_label_create(s_view_np);
    lv_obj_set_style_text_font(s_np_playlist, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_np_playlist, lv_color_hex(0x7FD08A), 0);

    s_np_title = lv_label_create(s_view_np);
    lv_label_set_long_mode(s_np_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_np_title, lv_pct(100));
    lv_obj_set_style_text_align(s_np_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_np_title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_np_title, lv_color_hex(0xFFD24D), 0);

    s_np_artist = lv_label_create(s_view_np);
    lv_obj_set_style_text_font(s_np_artist, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_np_artist, lv_color_hex(0xB8C2CC), 0);

    s_np_count = lv_label_create(s_view_np);
    lv_obj_set_style_text_font(s_np_count, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_np_count, lv_color_hex(0xB8C2CC), 0);

    s_np_state = lv_label_create(s_view_np);
    lv_obj_set_style_text_font(s_np_state, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_np_state, lv_color_hex(0x7FD08A), 0);

    /* persistent error line: stays visible until the next successful play */
    s_np_error = lv_label_create(s_view_np);
    lv_label_set_long_mode(s_np_error, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_np_error, lv_pct(96));
    lv_obj_set_style_text_align(s_np_error, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_np_error, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_np_error, lv_color_hex(0xFF6B6B), 0);
    lv_label_set_text(s_np_error, "");

    /* Volume row: icon, slider, percent label */
    lv_obj_t *vrow = lv_obj_create(s_view_np);
    lv_obj_set_size(vrow, lv_pct(90), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(vrow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vrow, 0, 0);
    lv_obj_set_style_pad_all(vrow, 6, 0);
    lv_obj_set_style_pad_column(vrow, 14, 0);
    lv_obj_set_flex_flow(vrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vrow, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(vrow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *vicon = lv_label_create(vrow);
    lv_label_set_text(vicon, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_font(vicon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(vicon, lv_color_hex(0xB8C2CC), 0);

    int cur_vol = bsp_extra_codec_volume_get();
    lv_obj_t *slider = lv_slider_create(vrow);
    lv_obj_set_flex_grow(slider, 1);
    lv_obj_set_height(slider, 16);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, cur_vol, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2C5F8A), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2C5F8A), LV_PART_KNOB);
    /* generous touch area for fingers */
    lv_obj_set_ext_click_area(slider, 24);
    lv_obj_add_event_cb(slider, volume_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_vol_label = lv_label_create(vrow);
    lv_label_set_text_fmt(s_vol_label, "%d%%", cur_vol);
    lv_obj_set_style_text_font(s_vol_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_vol_label, lv_color_hex(0xB8C2CC), 0);
    lv_obj_set_width(s_vol_label, 52);

    /* Prev / Next row */
    lv_obj_t *row = lv_obj_create(s_view_np);
    lv_obj_set_size(row, lv_pct(90), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    static const struct {
        const char *label;
        lv_event_cb_t cb;
    } NAV_BTNS[] = {
        { LV_SYMBOL_PREV "  Prev", prev_btn_cb },
        { "Next  " LV_SYMBOL_NEXT, next_btn_cb },
    };
    for (size_t b = 0; b < 2; b++) {
        lv_obj_t *btn = lv_button_create(row);
        lv_obj_set_size(btn, lv_pct(48), 80);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2C5F8A), 0);
        lv_obj_add_event_cb(btn, NAV_BTNS[b].cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *bl = lv_label_create(btn);
        lv_label_set_text(bl, NAV_BTNS[b].label);
        lv_obj_set_style_text_font(bl, &lv_font_montserrat_20, 0);
        lv_obj_center(bl);
    }

    lv_obj_t *stop = lv_button_create(s_view_np);
    lv_obj_set_size(stop, lv_pct(90), 96);
    lv_obj_set_style_bg_color(stop, lv_color_hex(0x9B3535), 0);
    lv_obj_add_event_cb(stop, stop_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(stop);
    lv_label_set_text(sl, "Stop");
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_28, 0);
    lv_obj_center(sl);

    bsp_display_unlock();
}

static void nav_ui_destroy(void)
{
    if (s_root != NULL && bsp_display_lock(1000)) {
        lv_obj_delete(s_root);
        s_root = NULL;
        s_vol_label = NULL;
        bsp_display_unlock();
    }
}

static void nav_ui_show_nowplaying(bool nowplaying)
{
    if (!bsp_display_lock(1000)) {
        return;
    }
    if (nowplaying) {
        lv_obj_add_flag(s_view_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_view_np, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_view_np, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_view_list, LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
}

static void nav_ui_update_song(const char *playlist_name, int idx, int total,
                               const nav_song_t *song)
{
    if (!bsp_display_lock(1000)) {
        return;
    }
    lv_label_set_text(s_np_playlist, playlist_name);
    lv_label_set_text(s_np_title, song->title);
    lv_label_set_text(s_np_artist, song->artist[0] != '\0' ? song->artist : "-");
    lv_label_set_text_fmt(s_np_count, "Song %d of %d", idx + 1, total);
    bsp_display_unlock();
}

static void nav_ui_set_state(const char *state)
{
    if (bsp_display_lock(1000)) {
        lv_label_set_text(s_np_state, state);
        bsp_display_unlock();
    }
}

/* Show a playback error on screen (also mirrored to the log). Cleared by
 * nav_ui_clear_error() when the next song starts playing successfully. */
static void nav_ui_set_error(const char *fmt, ...)
{
    char line[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    ui_log("%s", line);
    if (bsp_display_lock(1000)) {
        lv_label_set_text(s_np_error, line);
        bsp_display_unlock();
    }
}

static void nav_ui_clear_error(void)
{
    if (bsp_display_lock(1000)) {
        lv_label_set_text(s_np_error, "");
        bsp_display_unlock();
    }
}

/* ---------- player bring-up ---------- */

static bool nav_player_ensure_init(void)
{
    static bool s_inited;
    if (s_inited) {
        return true;
    }
    if (!app_audio_ensure_init()) {
        return false;
    }
    esp_err_t err = bsp_extra_player_init();
    if (err != ESP_OK) {
        ui_log("audio player init failed: %s", esp_err_to_name(err));
        return false;
    }
    /* starting volume; afterwards the slider owns it */
    bsp_extra_codec_volume_set(70, NULL);
    s_inited = true;
    return true;
}

/* ---------- playback: strictly ordered, one song prefetched ahead ---------- */

typedef struct {
    uint8_t *buf;
    size_t   len;
} song_buf_t;

/* Halt the player and wait for it to reach IDLE (where it fcloses the FILE*). */
static void nav_player_halt(void)
{
    audio_player_stop();
    xEventGroupWaitBits(s_ev, BIT_SONG_IDLE, pdTRUE, pdFALSE,
                        pdMS_TO_TICKS(3000));
}

static void play_playlist(const nav_playlist_t *pl, const nav_song_list_t *songs)
{
    song_buf_t cur = { 0 }, next = { 0 };
    bool have_prefetch = false;
    int prefetch_idx = -1;
    int i = 0;
    bool stopped = false;

    s_abort_dl = false;
    xEventGroupClearBits(s_ev, BIT_STOP | BIT_SONG_IDLE | BIT_NEXT | BIT_PREV);
    nav_ui_show_nowplaying(true);

    while (i < songs->count && !stopped) {
        const nav_song_t *song = &songs->items[i];
        nav_ui_update_song(pl->name, i, songs->count, song);

        if (have_prefetch && prefetch_idx == i) {
            cur = next;
            next = (song_buf_t) { 0 };
            have_prefetch = false;
        } else {
            if (have_prefetch) { /* stale prefetch (Prev pressed) */
                heap_caps_free(next.buf);
                next = (song_buf_t) { 0 };
                have_prefetch = false;
            }
            nav_ui_set_state("Downloading...");
            esp_err_t err = nav_download_song(song->id, &cur.buf, &cur.len,
                                              &s_abort_dl);
            if (err == ESP_ERR_NOT_FINISHED) {
                /* aborted by Stop/Next/Prev - see which */
                EventBits_t b = xEventGroupWaitBits(s_ev,
                        BIT_STOP | BIT_NEXT | BIT_PREV,
                        pdTRUE, pdFALSE, pdMS_TO_TICKS(100));
                s_abort_dl = false;
                if (b & BIT_STOP) {
                    stopped = true;
                } else if (b & BIT_PREV) {
                    i = (i > 0) ? i - 1 : 0;
                } else {
                    i++;
                }
                continue;
            }
            if (err != ESP_OK) {
                nav_ui_set_error("%s: download failed - skipping", song->title);
                i++;
                continue;
            }
        }

        FILE *fp = fmemopen(cur.buf, cur.len, "rb");
        if (fp == NULL) {
            nav_ui_set_error("%s: out of memory - skipping", song->title);
            heap_caps_free(cur.buf);
            cur = (song_buf_t) { 0 };
            i++;
            continue;
        }

        xEventGroupClearBits(s_ev, BIT_SONG_IDLE);
        /* audio_player fcloses fp when the song ends or errors — the PSRAM
         * buffer stays ours to free. */
        if (audio_player_play(fp) != ESP_OK) {
            nav_ui_set_error("%s: player rejected file (head %02x %02x %02x %02x)",
                             song->title, cur.buf[0], cur.buf[1],
                             cur.buf[2], cur.buf[3]);
            fclose(fp);
            heap_caps_free(cur.buf);
            cur = (song_buf_t) { 0 };
            i++;
            continue;
        }
        ui_log("Playing %d/%d: %s", i + 1, songs->count, song->title);
        nav_ui_set_state("Playing");
        TickType_t play_start = xTaskGetTickCount();
        ESP_LOGI(TAG, "free heap: internal %u, psram %u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

        /* Prefetch the next song while this one plays. A failure just means
         * an inline download attempt on the next iteration. */
        if (i + 1 < songs->count && !have_prefetch) {
            esp_err_t err = nav_download_song(songs->items[i + 1].id,
                                              &next.buf, &next.len, &s_abort_dl);
            have_prefetch = (err == ESP_OK);
            prefetch_idx = i + 1;
        }

        EventBits_t bits = xEventGroupWaitBits(s_ev,
                BIT_SONG_IDLE | BIT_STOP | BIT_NEXT | BIT_PREV,
                pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & BIT_STOP) {
            nav_player_halt();
            heap_caps_free(cur.buf);
            cur = (song_buf_t) { 0 };
            stopped = true;
            continue;
        }
        if (bits & (BIT_NEXT | BIT_PREV)) {
            nav_player_halt();
            s_abort_dl = false; /* button cb set it; downloads may resume */
            heap_caps_free(cur.buf);
            cur = (song_buf_t) { 0 };
            if (bits & BIT_PREV) {
                i = (i > 0) ? i - 1 : 0;
            } else {
                i++;
            }
            continue;
        }

        /* Natural IDLE. If it arrived suspiciously fast for a long song, the
         * decoder gave up on the data - surface that instead of hiding it. */
        if ((xTaskGetTickCount() - play_start) < pdMS_TO_TICKS(5000) &&
            song->duration > 15) {
            nav_ui_set_error("%s: decode failed - not valid MP3 data "
                             "(head %02x %02x %02x %02x)",
                             song->title, cur.buf[0], cur.buf[1],
                             cur.buf[2], cur.buf[3]);
        } else {
            nav_ui_clear_error();
        }
        heap_caps_free(cur.buf);
        cur = (song_buf_t) { 0 };
        i++;
    }

    heap_caps_free(cur.buf);
    heap_caps_free(next.buf);
    s_abort_dl = false;
    ui_log("Playback finished");
}

/* ---------- feature entry point (worker task) ---------- */

void feature_navidrome_run(void)
{
    if (s_ev == NULL) {
        s_ev = xEventGroupCreate();
    }

    if (!nav_configured()) {
        ui_set_status("Navidrome: not configured");
        return;
    }

    ui_set_status("Navidrome: connecting WiFi...");
    if (!wifi_ensure_connected()) {
        ui_log("Navidrome needs WiFi - connection failed");
        ui_set_status("Navidrome: WiFi failed");
        return;
    }
    if (!nav_player_ensure_init()) {
        return;
    }
    bsp_extra_player_register_callback(nav_player_cb, NULL);

    ui_log("Fetching playlists from %s...", nav_server_url());
    ui_set_status("Navidrome: fetching playlists...");
    nav_playlist_list_t pls;
    if (nav_get_playlists(&pls) != ESP_OK) {
        ui_set_status("Navidrome: server error");
        bsp_extra_player_register_callback(NULL, NULL);
        return;
    }
    if (pls.count == 0) {
        ui_log("Navidrome: no playlists on the server");
        ui_set_status("Navidrome: no playlists");
        nav_free_playlists(&pls);
        bsp_extra_player_register_callback(NULL, NULL);
        return;
    }
    ui_log("Navidrome: %d playlist(s)", pls.count);
    ui_set_status("Navidrome: pick a playlist");

    nav_ui_create(&pls);
    xEventGroupClearBits(s_ev, BITS_ALL);

    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(s_ev, BIT_PICKED | BIT_CLOSE,
                                               pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & BIT_CLOSE) {
            break;
        }
        int pick = s_picked_index;
        if (pick < 0 || pick >= pls.count) {
            continue;
        }

        ui_set_status("Navidrome: loading \"%s\"...", pls.items[pick].name);
        nav_song_list_t songs;
        if (nav_get_playlist_songs(pls.items[pick].id, &songs) != ESP_OK) {
            ui_set_status("Navidrome: playlist load failed");
            continue;
        }
        if (songs.count == 0) {
            ui_log("Playlist \"%s\" is empty", pls.items[pick].name);
            continue;
        }

        play_playlist(&pls.items[pick], &songs);
        nav_free_songs(&songs);
        nav_ui_show_nowplaying(false);
        ui_set_status("Navidrome: pick a playlist");
        xEventGroupClearBits(s_ev, BITS_ALL);
    }

    bsp_extra_player_register_callback(NULL, NULL);
    nav_ui_destroy();
    nav_free_playlists(&pls);
    ui_set_status("Ready");
}
