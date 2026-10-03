/* Play screen: full-screen overlay in the settings-overlay style - fixed
 * left sidebar (Back + Stop/Pause/Next/Prev transport) + content area.
 * The menu is the shared tile grid: Song and Playlist open a text box
 * whose query the music engine resolves with LocalAI; Random just starts.
 * Both query pages also show a tappable result list (mirrors the web UI,
 * main/www/index.html): Song searches the server as you type (debounced,
 * top 10), Playlist lists everything once and filters locally; a row plays
 * exactly that song/playlist by id, Find keeps the AI-matched query path.
 * A Now Playing bar at the bottom of the content area shows title/artist
 * and the engine status; a 500 ms timer polls music_status() /
 * music_now_playing() to refresh it and grey unusable transport buttons. */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"

#include "bsp_board_extra.h"

#include "app_common.h"
#include "navidrome_client.h"
#include "settings.h"
#include "music_player.h"
#include "song_cache.h"
#include "ui_common.h"
#include "ui_play.h"
#include "voice_actions.h"
#include "wifi_mgr.h"
#include "utf8_util.h"
#include "fonts/app_fonts.h"

#define COL_BG      0x101418
#define COL_BTN     0x2C5F8A
#define COL_TEXT    0xE8EDF2
#define COL_MUTED   0x9AA2AC
#define COL_STOP    0x9B3535
#define COL_DIS_BG  0x1A1E24
#define COL_DIS_TXT 0x4A4F55

static lv_obj_t *s_root;      /* NULL when closed */
static lv_obj_t *s_content;
static lv_obj_t *s_page;
static lv_obj_t *s_kb;
static lv_obj_t *s_btn_stop;
static lv_obj_t *s_btn_pause;
static lv_obj_t *s_btn_next;
static lv_obj_t *s_btn_prev;
static lv_obj_t *s_np_title;  /* Now Playing bar in the content area */
static lv_obj_t *s_np_artist;
static lv_obj_t *s_np_status;
static lv_obj_t *s_cache_label; /* "Cache free ..." top-right of every page */
static lv_obj_t *s_query_ta;  /* textarea on the Song/Playlist page */
static lv_obj_t *s_list;      /* result list on the Song/Playlist page */
static lv_timer_t *s_timer;
static lv_timer_t *s_deb_timer; /* paused one-shot song-search debounce */
static bool s_on_menu;
static bool s_query_is_playlist;
static uint32_t s_gen;        /* bumped on page rebuild/close: makes an
                               * in-flight async job drop its result */
static uint32_t s_search_seq; /* discards superseded song searches */

static void build_menu(void);
static void ui_play_close(void);
static void free_song_job(void);
static void free_pl_job(void);

/* Only set label text when it changed: lv_label_set_text restarts the
 * circular-scroll marquee, so a blind 500 ms update would freeze it. */
static void label_set_if_changed(lv_obj_t *l, const char *text)
{
    if (l != NULL && strcmp(lv_label_get_text(l), text) != 0) {
        lv_label_set_text(l, text);
    }
}

/* Last enabled state applied to each transport button (-1 = unknown, so
 * the first poll after open always applies). ui_sidebar_btn_enable()
 * rewrites three style props every call, which invalidated the whole
 * sidebar twice a second on top of the Now Playing marquee. */
static int8_t s_en_stop = -1, s_en_pause = -1, s_en_next = -1, s_en_prev = -1;

static void btn_enable_if_changed(lv_obj_t *btn, int8_t *last, bool en,
                                  uint32_t color)
{
    if (*last != (int8_t)en) {
        ui_sidebar_btn_enable(btn, en, color);
        *last = (int8_t)en;
    }
}

/* Song-cache headroom, top-right of the page. Refreshed every 4th poll
 * (2 s): heap_caps_get_free_size walks the PSRAM heap, no need at 500 ms. */
static void update_cache_label(bool force)
{
    static uint8_t tick;
    if (s_cache_label == NULL) {
        return;
    }
    if (!force && (++tick % 4) != 0) {
        return;
    }
    size_t held, budget;
    song_cache_stats(&held, &budget);
    size_t free_b = budget > held ? budget - held : 0;
    int pct = budget > 0 ? (int)((free_b * 100 + budget / 2) / budget) : 0;
    char line[48];
    snprintf(line, sizeof line, "Cache free %u KB (%d%%)",
             (unsigned)(free_b / 1024), pct);
    label_set_if_changed(s_cache_label, line);
}

static void poll_cb(lv_timer_t *t)
{
    if (s_np_status == NULL) {
        return;
    }
    update_cache_label(t == NULL);
    music_state_t st = music_state();
    bool transport = (st == MUSIC_STATE_PLAYING || st == MUSIC_STATE_PAUSED);

    char line[128];
    if (music_status(line, sizeof line)) { /* skip if contended (0-tick) */
        label_set_if_changed(s_np_status, line);
    }

    char title[96], artist[96];
    if (music_now_playing(title, sizeof title, artist, sizeof artist)) {
        label_set_if_changed(s_np_title, title);
        label_set_if_changed(s_np_artist, artist);
    } else {
        label_set_if_changed(s_np_title, "Nothing playing");
        label_set_if_changed(s_np_artist, "");
    }

    btn_enable_if_changed(s_btn_stop, &s_en_stop, st != MUSIC_STATE_IDLE,
                          COL_STOP);
    btn_enable_if_changed(s_btn_pause, &s_en_pause, transport, COL_BTN);
    ui_sidebar_btn_set_text(s_btn_pause, st == MUSIC_STATE_PAUSED
            ? LV_SYMBOL_PLAY "\nResume" : LV_SYMBOL_PAUSE "\nPause");
    btn_enable_if_changed(s_btn_next, &s_en_next, transport, COL_BTN);
    btn_enable_if_changed(s_btn_prev, &s_en_prev, transport, COL_BTN);
}

static void back_cb(lv_event_t *e)
{
    if (s_on_menu) {
        ui_play_close(); /* music keeps playing in the background */
    } else {
        build_menu();
    }
}

static void stop_cb(lv_event_t *e)
{
    music_stop();
}

static void pause_cb(lv_event_t *e)
{
    music_toggle_pause();
    poll_cb(NULL); /* flip the label right away */
}

static void next_cb(lv_event_t *e)
{
    music_next();
}

static void prev_cb(lv_event_t *e)
{
    music_prev();
}

/* Media volume only - the alarm rings at settings alarm_volume regardless.
 * Applies live and updates the in-RAM setting (persisted by Settings/Save,
 * like the settings screen's slider); play_list re-reads it per session. */
static void vol_cb(lv_event_t *e)
{
    int v = (int)lv_slider_get_value(lv_event_get_target(e));
    settings_get()->volume = (uint8_t)v;
    /* LVGL task (or the voice task under the LVGL lock): never wait on the
     * codec mutex here - the decoder holds it for most of every cycle while
     * a song plays, and a per-pixel drag used to park the whole UI. */
    app_audio_set_volume_async(v);
}

/* ---------- pages ---------- */

static lv_obj_t *new_page(const char *title)
{
    if (s_page != NULL) {
        lv_obj_delete(s_page);
    }
    s_gen++; /* an in-flight fetch/search job now discards its result */
    s_query_ta = NULL;
    s_list = NULL;
    s_cache_label = NULL; /* died with the old page */
    if (s_deb_timer != NULL) {
        lv_timer_delete(s_deb_timer);
        s_deb_timer = NULL;
    }
    free_song_job();
    free_pl_job();
    s_page = lv_obj_create(s_content);
    lv_obj_set_width(s_page, lv_pct(100));
    lv_obj_set_flex_grow(s_page, 1);
    lv_obj_move_to_index(s_page, 0); /* keep the page above the NP bar */
    lv_obj_set_style_bg_opa(s_page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_page, 0, 0);
    lv_obj_set_style_pad_all(s_page, 12, 0);
    lv_obj_set_style_pad_row(s_page, 12, 0);
    lv_obj_set_flex_flow(s_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_page, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_page, LV_DIR_VER);

    /* header row: page title on the left, cache headroom on the right */
    lv_obj_t *hdr = lv_obj_create(s_page);
    lv_obj_set_width(hdr, lv_pct(100));
    lv_obj_set_height(hdr, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_style_pad_column(hdr, 8, 0);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tl = lv_label_create(hdr);
    lv_label_set_text(tl, title);
    lv_label_set_long_mode(tl, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(tl, 1);
    lv_obj_set_style_text_font(tl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(tl, lv_color_hex(COL_TEXT), 0);

    s_cache_label = lv_label_create(hdr);
    lv_label_set_text(s_cache_label, "");
    lv_obj_set_style_text_font(s_cache_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_cache_label, lv_color_hex(COL_MUTED), 0);
    update_cache_label(true);
    return s_page;
}

static void kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ta_focus_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(s_kb, ta);
        lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_kb);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void query_go_cb(lv_event_t *e)
{
    if (s_query_ta == NULL) {
        return;
    }
    const char *text = lv_textarea_get_text(s_query_ta);
    if (text[0] == '\0') {
        return;
    }
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    if (s_query_is_playlist) {
        music_play_playlist_query(text);
    } else {
        music_play_song_query(text);
    }
    build_menu(); /* back to the tiles; sidebar status tracks progress */
}

/* ---------- result lists ----------
 * Jobs run nav_* (blocking HTTPS) on the worker task, then take the LVGL
 * lock and fill s_list - only if the page generation (and, for songs, the
 * search sequence) still matches. The successful job stays alive as the
 * rows' backing data (row callbacks index into it) until the next job,
 * page change or close frees it. Same pattern as ui_settings.c. */

#define SONG_SHOW_MAX 10  /* web UI SHOW_MAX */

typedef struct {
    uint32_t gen;         /* s_gen at submit time */
    uint32_t seq;         /* s_search_seq at submit time */
    char q[96];
    nav_song_list_t songs;
    esp_err_t err;
} song_job_t;

typedef struct {
    uint32_t gen;
    nav_playlist_list_t pls;
    esp_err_t err;
} pl_job_t;

static song_job_t *s_song_job; /* backing data for the song rows */
static pl_job_t *s_pl_job;     /* backing data for the playlist rows */

static void free_song_job(void)
{
    if (s_song_job != NULL) {
        if (s_song_job->err == ESP_OK) {
            nav_free_songs(&s_song_job->songs);
        }
        heap_caps_free(s_song_job);
        s_song_job = NULL;
    }
}

static void free_pl_job(void)
{
    if (s_pl_job != NULL) {
        if (s_pl_job->err == ESP_OK) {
            nav_free_playlists(&s_pl_job->pls);
        }
        heap_caps_free(s_pl_job);
        s_pl_job = NULL;
    }
}

/* Case-insensitive substring match (don't rely on newlib strcasestr). */
static bool ci_contains(const char *hay, const char *needle)
{
    size_t nlen = strlen(needle);
    if (nlen == 0) {
        return true;
    }
    for (; *hay != '\0'; hay++) {
        if (strncasecmp(hay, needle, nlen) == 0) {
            return true;
        }
    }
    return false;
}

static void song_pick_cb(lv_event_t *e)
{
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    if (s_song_job == NULL || idx >= s_song_job->songs.count) {
        return;
    }
    nav_song_t *s = &s_song_job->songs.items[idx];
    /* the command copies the strings, so freeing the job right after
     * (build_menu -> new_page) is safe */
    music_play_song_id(s->id, s->title, s->artist);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    build_menu();
}

static void pl_pick_cb(lv_event_t *e)
{
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    if (s_pl_job == NULL || idx >= s_pl_job->pls.count) {
        return;
    }
    nav_playlist_t *p = &s_pl_job->pls.items[idx];
    music_play_playlist_id(p->id, p->name);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    build_menu();
}

/* Rebuild the playlist rows from the cached fetch, filtered by the textarea
 * text - a pure local filter on every keystroke, like the web UI. Caller
 * holds the LVGL lock (LVGL task, or the fetch job under bsp_display_lock). */
static void render_playlists(void)
{
    if (s_list == NULL || s_pl_job == NULL) {
        return;
    }
    const char *q = (s_query_ta != NULL) ? lv_textarea_get_text(s_query_ta)
                                         : "";
    lv_obj_clean(s_list);
    int shown = 0;
    for (int i = 0; i < s_pl_job->pls.count; i++) {
        nav_playlist_t *p = &s_pl_job->pls.items[i];
        if (!ci_contains(p->name, q)) {
            continue;
        }
        char row[128];
        snprintf(row, sizeof(row), "%s  (%d songs)", p->name, p->song_count);
        utf8_trim_tail(row);
        lv_obj_t *btn = lv_list_add_button(s_list, LV_SYMBOL_LIST, row);
        lv_obj_add_event_cb(btn, pl_pick_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        shown++;
    }
    if (shown == 0) {
        lv_list_add_text(s_list, "no matching playlist");
    }
}

static void pl_fetch_job(void *arg) /* worker task */
{
    pl_job_t *job = (pl_job_t *)arg;
    if (!wifi_mgr_is_connected() || !nav_is_configured()) {
        job->err = ESP_ERR_INVALID_STATE;
    } else {
        job->err = nav_get_playlists(&job->pls);
    }

    if (app_display_ready() && bsp_display_lock(1000)) {
        if (job->gen == s_gen && s_list != NULL) {
            if (job->err != ESP_OK) {
                lv_obj_clean(s_list);
                lv_list_add_text(s_list, job->err == ESP_ERR_INVALID_STATE
                        ? "WiFi or Navidrome not configured" : "server error");
            } else if (job->pls.count == 0) {
                lv_obj_clean(s_list);
                lv_list_add_text(s_list, "no playlists on server");
            } else {
                free_pl_job();
                s_pl_job = job; /* rows' backing data from here on */
                render_playlists();
                bsp_display_unlock();
                return; /* ownership transferred */
            }
        }
        bsp_display_unlock();
    }
    if (job->err == ESP_OK) {
        nav_free_playlists(&job->pls);
    }
    heap_caps_free(job);
}

static void start_pl_fetch(void)
{
    pl_job_t *job = heap_caps_calloc(1, sizeof(*job), MALLOC_CAP_SPIRAM);
    if (job == NULL) {
        return;
    }
    job->gen = s_gen;
    lv_obj_clean(s_list);
    lv_list_add_text(s_list, "fetching playlists...");
    if (!app_run_async(pl_fetch_job, job)) {
        heap_caps_free(job);
        lv_obj_clean(s_list);
        lv_list_add_text(s_list, "busy - try again");
    }
}

static void song_search_job(void *arg) /* worker task */
{
    song_job_t *job = (song_job_t *)arg;
    if (!wifi_mgr_is_connected() || !nav_is_configured()) {
        job->err = ESP_ERR_INVALID_STATE;
    } else {
        job->err = nav_search_songs(job->q, SONG_SHOW_MAX, &job->songs);
    }

    if (app_display_ready() && bsp_display_lock(1000)) {
        /* drop the result if the page changed or a newer search started */
        if (job->gen == s_gen && job->seq == s_search_seq && s_list != NULL) {
            lv_obj_clean(s_list);
            if (job->err != ESP_OK) {
                lv_list_add_text(s_list, job->err == ESP_ERR_INVALID_STATE
                        ? "WiFi or Navidrome not configured" : "server error");
            } else if (job->songs.count == 0) {
                lv_list_add_text(s_list, "no matching song");
            } else {
                free_song_job();
                s_song_job = job; /* rows' backing data from here on */
                for (int i = 0; i < job->songs.count; i++) {
                    nav_song_t *s = &job->songs.items[i];
                    char row[160];
                    if (s->artist[0] != '\0') {
                        snprintf(row, sizeof(row), "%s - %s",
                                 s->title, s->artist);
                    } else {
                        snprintf(row, sizeof(row), "%s", s->title);
                    }
                    utf8_trim_tail(row);
                    lv_obj_t *btn = lv_list_add_button(s_list,
                                                       LV_SYMBOL_AUDIO, row);
                    lv_obj_add_event_cb(btn, song_pick_cb, LV_EVENT_CLICKED,
                                        (void *)(uintptr_t)i);
                }
                bsp_display_unlock();
                return; /* ownership transferred */
            }
        }
        bsp_display_unlock();
    }
    if (job->err == ESP_OK) {
        nav_free_songs(&job->songs);
    }
    heap_caps_free(job);
}

static void start_song_search(void)
{
    if (s_list == NULL || s_query_ta == NULL) {
        return;
    }
    s_search_seq++; /* supersede any in-flight search either way */
    const char *q = lv_textarea_get_text(s_query_ta);
    if (strlen(q) < 2) { /* web UI minimum; empty search3 = whole library */
        lv_obj_clean(s_list);
        lv_list_add_text(s_list, "type at least 2 characters to search");
        return;
    }
    song_job_t *job = heap_caps_calloc(1, sizeof(*job), MALLOC_CAP_SPIRAM);
    if (job == NULL) {
        return;
    }
    job->gen = s_gen;
    job->seq = s_search_seq;
    strlcpy(job->q, q, sizeof(job->q));
    lv_obj_clean(s_list);
    lv_list_add_text(s_list, "searching...");
    if (!app_run_async(song_search_job, job)) {
        heap_caps_free(job);
        lv_obj_clean(s_list);
        lv_list_add_text(s_list, "busy - try again");
    }
}

static void search_deb_cb(lv_timer_t *t)
{
    lv_timer_pause(t); /* one-shot until the next keystroke resumes it */
    start_song_search();
}

/* Textarea edits: the playlist page filters its cached list immediately;
 * the song page restarts the 350 ms debounce before hitting the server. */
static void ta_changed_cb(lv_event_t *e)
{
    if (s_query_is_playlist) {
        render_playlists(); /* no-op until the fetch has landed */
    } else if (s_deb_timer != NULL) {
        lv_timer_reset(s_deb_timer);
        lv_timer_resume(s_deb_timer);
    }
}

/* Shared Song/Playlist page: prompt, textarea, go button. */
static void build_query_page(bool playlist)
{
    s_query_is_playlist = playlist;
    lv_obj_t *page = new_page(playlist ? "Playlist" : "Song");

    lv_obj_t *hint = lv_label_create(page);
    lv_label_set_text(hint, playlist
            ? "Tap a playlist, or type to filter. Find = AI closest match."
            : "Type to search - tap a result, or Find = AI closest match.");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_MUTED), 0);

    s_query_ta = lv_textarea_create(page);
    lv_obj_set_width(s_query_ta, lv_pct(100));
    lv_textarea_set_one_line(s_query_ta, true);
    lv_textarea_set_placeholder_text(s_query_ta,
            playlist ? "filter playlists" : "search songs");
    lv_obj_add_event_cb(s_query_ta, ta_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_query_ta, ta_focus_cb, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(s_query_ta, ta_changed_cb, LV_EVENT_VALUE_CHANGED,
                        NULL);

    lv_obj_t *go = lv_button_create(page);
    lv_obj_set_size(go, lv_pct(100), 64);
    lv_obj_set_style_bg_color(go, lv_color_hex(COL_BTN), 0);
    lv_obj_add_event_cb(go, query_go_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gl = lv_label_create(go);
    lv_label_set_text(gl, LV_SYMBOL_PLAY "  Find and play");
    lv_obj_set_style_text_font(gl, &lv_font_montserrat_20, 0);
    lv_obj_center(gl);

    /* result list below the Find button (fills the rest of the page) */
    s_list = lv_list_create(page);
    lv_obj_set_width(s_list, lv_pct(100));
    /* rows show server titles: inherited by the row labels */
    lv_obj_set_style_text_font(s_list, &app_font_14, 0);
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_style_min_height(s_list, 160, 0);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(0x161B21), 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    voice_actions_set_option_list(s_list);

    if (playlist) {
        start_pl_fetch(); /* all playlists up front, filtered as you type */
    } else {
        lv_list_add_text(s_list, "type at least 2 characters to search");
        s_deb_timer = lv_timer_create(search_deb_cb, 350, NULL);
        lv_timer_pause(s_deb_timer);
    }
}

static void song_tile_cb(lv_event_t *e)
{
    s_on_menu = false;
    build_query_page(false);
}

static void playlist_tile_cb(lv_event_t *e)
{
    s_on_menu = false;
    build_query_page(true);
}

static void random_tile_cb(lv_event_t *e)
{
    music_play_random();
}

static void build_menu(void)
{
    s_on_menu = true;
    lv_obj_t *page = new_page("Play");
    ui_grid_config(page);

    ui_grid_tile(page, LV_SYMBOL_AUDIO, "Song", song_tile_cb, NULL);
    ui_grid_tile(page, LV_SYMBOL_LIST, "Playlist", playlist_tile_cb, NULL);
    ui_grid_tile(page, LV_SYMBOL_SHUFFLE, "Random", random_tile_cb, NULL);
}

/* ---------- open / close ---------- */

static void ui_play_close(void)
{
    s_gen++; /* an in-flight fetch/search job now discards its result */
    free_song_job();
    free_pl_job();
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_deb_timer != NULL) {
        lv_timer_delete(s_deb_timer);
        s_deb_timer = NULL;
    }
    if (s_root != NULL) {
        lv_obj_delete(s_root);
        s_root = NULL;
        s_content = NULL;
        s_page = NULL;
        s_kb = NULL;
        s_btn_stop = NULL;
        s_btn_pause = NULL;
        s_btn_next = NULL;
        s_btn_prev = NULL;
        s_np_title = NULL;
        s_np_artist = NULL;
        s_np_status = NULL;
        s_cache_label = NULL;
        s_query_ta = NULL;
        s_list = NULL;
    }
}

bool ui_play_is_open(void)
{
    return s_root != NULL;
}

void ui_play_open(void)
{
    if (s_root != NULL) {
        return;
    }
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_root, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* sidebar: Back + transport, mirroring the settings overlay */
    lv_obj_t *side = lv_obj_create(s_root);
    lv_obj_set_size(side, 120, lv_pct(100));
    lv_obj_set_style_bg_color(side, lv_color_hex(0x0C0F13), 0);
    lv_obj_set_style_border_width(side, 0, 0);
    lv_obj_set_style_radius(side, 0, 0);
    lv_obj_set_style_pad_all(side, 10, 0);
    lv_obj_set_style_pad_row(side, 14, 0);
    lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(side, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(side, LV_OBJ_FLAG_SCROLLABLE);

    ui_sidebar_btn(side, LV_SYMBOL_LEFT "\nBack", 96, 0x4A4F55, back_cb);
    s_btn_stop = ui_sidebar_btn(side, LV_SYMBOL_STOP "\nStop", 80,
                                COL_STOP, stop_cb);
    s_btn_pause = ui_sidebar_btn(side, LV_SYMBOL_PAUSE "\nPause", 80,
                                 COL_BTN, pause_cb);
    s_btn_next = ui_sidebar_btn(side, LV_SYMBOL_NEXT "\nNext", 80,
                                COL_BTN, next_cb);
    s_btn_prev = ui_sidebar_btn(side, LV_SYMBOL_PREV "\nPrev", 80,
                                COL_BTN, prev_cb);
    s_en_stop = s_en_pause = s_en_next = s_en_prev = -1; /* fresh widgets */

    s_content = lv_obj_create(s_root);
    lv_obj_set_flex_grow(s_content, 1);
    lv_obj_set_height(s_content, lv_pct(100));
    lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, 0, 0);
    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);

    build_menu();

    /* Now Playing bar: title + artist + engine status, pinned under the
     * page (new_page keeps recreated pages above it) */
    lv_obj_t *np = lv_obj_create(s_content);
    lv_obj_set_size(np, lv_pct(100), 168);
    lv_obj_set_style_bg_color(np, lv_color_hex(0x161B21), 0);
    lv_obj_set_style_bg_opa(np, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(np, 0, 0);
    lv_obj_set_style_radius(np, 12, 0);
    lv_obj_set_style_pad_all(np, 14, 0);
    lv_obj_set_style_pad_row(np, 4, 0);
    lv_obj_set_flex_flow(np, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(np, LV_OBJ_FLAG_SCROLLABLE);

    s_np_title = lv_label_create(np);
    lv_label_set_text(s_np_title, "Nothing playing");
    lv_label_set_long_mode(s_np_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_np_title, lv_pct(100));
    lv_obj_set_style_text_font(s_np_title, &app_font_28, 0);
    lv_obj_set_style_text_color(s_np_title, lv_color_hex(COL_TEXT), 0);

    s_np_artist = lv_label_create(np);
    lv_label_set_text(s_np_artist, "");
    lv_label_set_long_mode(s_np_artist, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_np_artist, lv_pct(100));
    lv_obj_set_style_text_font(s_np_artist, &app_font_20, 0);
    lv_obj_set_style_text_color(s_np_artist, lv_color_hex(COL_MUTED), 0);

    s_np_status = lv_label_create(np);
    lv_label_set_text(s_np_status, "");
    lv_label_set_long_mode(s_np_status, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_np_status, lv_pct(100));
    lv_obj_set_style_text_font(s_np_status, &app_font_16, 0);
    lv_obj_set_style_text_color(s_np_status, lv_color_hex(COL_MUTED), 0);

    /* media volume row: icon + slider */
    lv_obj_t *vrow = lv_obj_create(np);
    lv_obj_set_size(vrow, lv_pct(100), 30);
    lv_obj_set_style_bg_opa(vrow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vrow, 0, 0);
    lv_obj_set_style_pad_all(vrow, 0, 0);
    lv_obj_set_style_pad_column(vrow, 12, 0);
    lv_obj_set_flex_flow(vrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vrow, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(vrow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *vicon = lv_label_create(vrow);
    lv_label_set_text(vicon, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_font(vicon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(vicon, lv_color_hex(COL_MUTED), 0);

    lv_obj_t *vsl = lv_slider_create(vrow);
    lv_obj_set_flex_grow(vsl, 1);
    lv_obj_set_height(vsl, 16);
    lv_slider_set_range(vsl, 0, 100);
    lv_slider_set_value(vsl, settings_get()->volume, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(vsl, lv_color_hex(COL_BTN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(vsl, lv_color_hex(COL_BTN), LV_PART_KNOB);
    lv_obj_set_ext_click_area(vsl, 20);
    lv_obj_add_event_cb(vsl, vol_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(vsl, vol_cb, LV_EVENT_RELEASED, NULL);
    voice_actions_bind_slider(VA_VOL_UP, VA_VOL_DOWN, vsl, 10);

    s_kb = lv_keyboard_create(s_root);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);

    s_timer = lv_timer_create(poll_cb, 500, NULL);
    poll_cb(NULL);
}
