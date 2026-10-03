/* Settings overlay: a 3-wide tile-grid menu plus sub-pages, ordered by how
 * often they are opened: Alarms, Alarm sound, Voice / Volume, Brightness,
 * Clock Face / Specifications, WiFi, Timezone / Music API (Navidrome), AI,
 * API.
 *
 * Threading rules (same as the rest of the app): everything here that builds
 * or edits widgets runs on the LVGL task (event callbacks); blocking work
 * (WiFi scan/connect, Navidrome API) is posted to the worker task with
 * app_run_async(), and those jobs re-take bsp_display_lock and check the page
 * generation counter before touching any widget, because the user may have
 * navigated away or closed the overlay while the job ran. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"

#include "app_common.h"
#include "settings.h"
#include "wifi_mgr.h"
#include "clock_sync.h"
#include "navidrome_client.h"
#include "alarm.h"
#include "ui_clock.h"
#include "ambient.h"
#include "ui_common.h"
#include "ui_settings.h"
#include "voice.h"
#include "voice_actions.h"
#include "wake_mww.h"
#include "api_server.h"
#include "boot_seq.h"
#include "fonts/app_icons.h"
#include "fonts/app_fonts.h"
#include "utf8_util.h"

static const char *TAG = "ui_settings";

#define COL_BG      0x101418
#define COL_CARD    0x181C22
#define COL_BTN     0x2C5F8A
#define COL_TEXT    0xE8EDF2
#define COL_MUTED   0x9AA2AC
#define COL_GOOD    0x7FD08A
#define COL_BAD     0xFF6B6B
#define COL_PGBTN   0x24435F   /* page up/down active */
#define COL_DIS_BG  0x1A1E24   /* sidebar button greyed out */
#define COL_DIS_TXT 0x4A4F55

static lv_obj_t *s_root;       /* NULL when closed */
static lv_obj_t *s_content;    /* column between sidebar and scroll rail */
static lv_obj_t *s_page;       /* current page container (child of s_content) */
static lv_obj_t *s_kb;         /* shared keyboard */
static lv_obj_t *s_side_note;  /* feedback label in the sidebar */
static lv_obj_t *s_btn_save;   /* sidebar Save: greyed until s_dirty */
static lv_obj_t *s_btn_pgup;   /* sidebar Page buttons: greyed when the */
static lv_obj_t *s_btn_pgdn;   /*   page cannot scroll that way */
static bool s_on_menu;         /* Back = close when true, else back to menu */
static bool s_dirty;           /* unsaved settings changes exist */
static void (*s_commit)(void); /* page hook Save runs before persisting */
static uint32_t s_gen;         /* bumped on every page change / close */

/* widgets async jobs may update (validity guarded by s_gen) */
static lv_obj_t *s_wifi_list;
static lv_obj_t *s_wifi_status;
static lv_obj_t *s_wifi_pass_ta;
static lv_obj_t *s_nav_status;
static lv_obj_t *s_pl_list;
static lv_obj_t *s_pl_status;
static lv_obj_t *s_specs_label;
static lv_obj_t *s_cal_label;

static char s_pending_ssid[33];

/* log buffer shown on the Device Specifications page. Display-only data:
 * lives in PSRAM (.ext_ram.bss) - 16 KB of internal RAM is a fifth of what
 * is free during playback, and internal DMA-capable RAM is what the I2S
 * reconfigure at song start must find (docs/10 §1a). */
#define LOG_BUF_CAP 16384
EXT_RAM_BSS_ATTR static char s_log_buf[LOG_BUF_CAP];

static void build_menu_page(void);
static void ui_settings_close(void);
static void alarm_page_teardown(void);   /* Alarm page widgets (defined below) */
static lv_obj_t *s_alarm_pl_label;       /* Alarm sound page */

/* Grey out / re-activate a sidebar button. Disabled buttons ignore clicks. */
static void sidebar_btn_enable(lv_obj_t *btn, bool en, uint32_t active_color)
{
    if (btn == NULL) {
        return;
    }
    if (en) {
        lv_obj_remove_state(btn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(btn, lv_color_hex(active_color), 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(COL_TEXT), 0);
    } else {
        lv_obj_add_state(btn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(btn, lv_color_hex(COL_DIS_BG), 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(COL_DIS_TXT), 0);
    }
}

/* Called by every settings control on change: lights up Save. */
static void mark_dirty(void)
{
    s_dirty = true;
    sidebar_btn_enable(s_btn_save, true, COL_BTN);
    if (s_side_note != NULL) {
        lv_label_set_text(s_side_note, "");
    }
}

/* Grey Page Up/Down according to the current scroll room. */
static void update_page_btns(void)
{
    if (s_page == NULL) {
        return;
    }
    lv_obj_update_layout(s_page);
    sidebar_btn_enable(s_btn_pgup, lv_obj_get_scroll_y(s_page) > 0, COL_PGBTN);
    sidebar_btn_enable(s_btn_pgdn, lv_obj_get_scroll_bottom(s_page) > 0,
                       COL_PGBTN);
}

static void page_scroll_cb(lv_event_t *e)
{
    update_page_btns();
}

/* Deferred so it runs after the page builder finished adding content. */
static void page_btns_async_cb(void *arg)
{
    update_page_btns();
}

static void back_cb(lv_event_t *e)
{
    if (s_on_menu) {
        ui_settings_close();
    } else {
        build_menu_page();
    }
}

/* settings_save() is an NVS write = flash erase/program, so it runs on the
 * worker (MUST-IMPLEMENT §2: no feature work on the LVGL task). arg carries
 * the page generation so a stale job never touches a rebuilt sidebar. */
static void settings_save_job(void *arg)
{
    uint32_t gen = (uint32_t)(uintptr_t)arg;
    bool ok = settings_save();
    if (!ok) {
        ui_log("Settings: save to NVS FAILED - values are live but not stored");
    }
    if (bsp_display_lock(1000)) {
        if (gen == s_gen && s_side_note != NULL) {
            lv_label_set_text(s_side_note, ok ? "Saved" : "Save FAILED (NVS)");
        }
        bsp_display_unlock();
    }
    /* The save may have changed a server URL or credentials. Re-probe so the
     * clock face's indicators reflect the new settings now rather than at the
     * next 5-minute sweep. Posted, not inline: we are on the worker and the
     * probes would otherwise delay the "Saved" feedback above. */
    boot_net_probe_kick();
}

static void save_cb(lv_event_t *e)
{
    if (!s_dirty) {
        return; /* nothing to save (voice path; touch is blocked by state) */
    }
    if (s_commit != NULL) {
        s_commit(); /* page pulls widget values into cfg first */
    }
    s_dirty = false;
    sidebar_btn_enable(s_btn_save, false, COL_BTN);
    if (s_side_note != NULL) {
        lv_label_set_text(s_side_note, "Saving...");
    }
    if (!app_run_async(settings_save_job, (void *)(uintptr_t)s_gen)) {
        /* NEVER save inline here: this runs on the LVGL task (7 KB stack)
         * or, via a voice command, on the PSRAM-stack voice task where a
         * flash write asserts. Keep the change pending instead. */
        s_dirty = true;
        sidebar_btn_enable(s_btn_save, true, COL_BTN);
        if (s_side_note != NULL) {
            lv_label_set_text(s_side_note, "Busy - tap Save again");
        }
    }
}

/* Page through the settings: jump 80% of the visible height per press. */
static void page_down_cb(lv_event_t *e)
{
    if (s_page == NULL) {
        return;
    }
    int32_t step = (lv_obj_get_height(s_page) * 4) / 5;
    int32_t room = lv_obj_get_scroll_bottom(s_page);
    if (step > room) {
        step = room;
    }
    if (step > 0) {
        lv_obj_scroll_to_y(s_page, lv_obj_get_scroll_y(s_page) + step, LV_ANIM_ON);
    }
}

static void page_up_cb(lv_event_t *e)
{
    if (s_page == NULL) {
        return;
    }
    int32_t y = lv_obj_get_scroll_y(s_page);
    int32_t step = (lv_obj_get_height(s_page) * 4) / 5;
    if (step > y) {
        step = y;
    }
    if (step > 0) {
        lv_obj_scroll_to_y(s_page, y - step, LV_ANIM_ON);
    }
}

/* ---------- shared helpers ---------- */

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

static lv_obj_t *make_textarea(lv_obj_t *parent, const char *placeholder,
                               const char *value, bool password)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_width(ta, lv_pct(100));
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_password_mode(ta, password);
    if (value != NULL && value[0] != '\0') {
        lv_textarea_set_text(ta, value);
    }
    lv_obj_add_event_cb(ta, ta_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta, ta_focus_cb, LV_EVENT_DEFOCUSED, NULL);
    return ta;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text,
                             lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, lv_pct(100), 64);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_BTN), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_center(l);
    return btn;
}

/* Grid tile for the menu page (shared look - see ui_common.c). */
static lv_obj_t *make_tile(lv_obj_t *parent, const char *symbol,
                           const char *text, lv_event_cb_t cb)
{
    return ui_grid_tile(parent, symbol, text, cb, NULL);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}

/* Fresh scrollable page with a title; Back/Save live in the fixed sidebar.
 * Returns the container new content should be added to. */
static lv_obj_t *new_page(const char *title, bool is_menu)
{
    s_gen++;
    s_on_menu = is_menu;
    s_commit = NULL;
    s_wifi_list = s_wifi_status = s_wifi_pass_ta = NULL;
    s_nav_status = s_pl_list = s_pl_status = s_specs_label = NULL;
    s_cal_label = NULL;
    s_alarm_pl_label = NULL;
    alarm_page_teardown();

    if (s_page != NULL) {
        lv_obj_delete(s_page);
    }
    s_page = lv_obj_create(s_content);
    lv_obj_set_size(s_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_page, 0, 0);
    lv_obj_set_style_pad_all(s_page, 12, 0);
    lv_obj_set_style_pad_row(s_page, 12, 0);
    lv_obj_set_flex_flow(s_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_page, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_page, LV_DIR_VER);
    lv_obj_add_event_cb(s_page, page_scroll_cb, LV_EVENT_SCROLL, NULL);
    /* size the Page buttons once the builder has filled the page */
    lv_async_call(page_btns_async_cb, NULL);

    lv_obj_t *t = lv_label_create(s_page);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(COL_TEXT), 0);

    if (s_side_note != NULL) {
        lv_label_set_text(s_side_note, "");
    }
    return s_page;
}

/* ---------- WiFi page ---------- */

typedef struct {
    uint32_t gen;
    int count;
    wifi_scan_result_t results[WIFI_SCAN_MAX];
} wifi_scan_job_t;

typedef struct {
    uint32_t gen;
    char ssid[33];
    char pass[65];
} wifi_conn_req_t;

static void wifi_connect_job(void *arg)
{
    wifi_conn_req_t *req = (wifi_conn_req_t *)arg;

    bool ok = wifi_mgr_connect(req->ssid, req->pass);
    if (ok) {
        bedside_settings_t *cfg = settings_get();
        settings_lock(); /* string writes race settings_save on other tasks */
        strlcpy(cfg->wifi_ssid, req->ssid, sizeof(cfg->wifi_ssid));
        strlcpy(cfg->wifi_pass, req->pass, sizeof(cfg->wifi_pass));
        settings_unlock();
        settings_save();
        clock_sync_start();
    }
    if (bsp_display_lock(1000)) {
        /* the user may have left the page or closed settings meanwhile */
        if (req->gen == s_gen && s_wifi_status != NULL) {
            lv_label_set_text_fmt(s_wifi_status, ok ? "Connected: %s" : "Failed: %s",
                                  req->ssid);
            lv_obj_set_style_text_color(s_wifi_status,
                                        lv_color_hex(ok ? COL_GOOD : COL_BAD), 0);
        }
        bsp_display_unlock();
    }
    heap_caps_free(req);
}

static void wifi_connect_cb(lv_event_t *e)
{
    if (s_wifi_pass_ta == NULL) {
        return;
    }
    wifi_conn_req_t *req = heap_caps_calloc(1, sizeof(*req), MALLOC_CAP_DEFAULT);
    if (req == NULL) {
        return;
    }
    req->gen = s_gen;
    strlcpy(req->ssid, s_pending_ssid, sizeof(req->ssid));
    strlcpy(req->pass, lv_textarea_get_text(s_wifi_pass_ta), sizeof(req->pass));
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    if (s_wifi_status != NULL) {
        lv_label_set_text_fmt(s_wifi_status, "Connecting to %s...", s_pending_ssid);
        lv_obj_set_style_text_color(s_wifi_status, lv_color_hex(COL_MUTED), 0);
    }
    if (!app_run_async(wifi_connect_job, req)) {
        heap_caps_free(req);
        if (s_wifi_status != NULL) {
            lv_label_set_text(s_wifi_status, "Busy - try again");
        }
    }
}

static void wifi_ssid_pick_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *label = lv_obj_get_child(btn, -1);
    const char *txt = lv_label_get_text(label);
    /* list rows are "SSID  (-NNdBm)" - take up to the double space */
    const char *cut = strstr(txt, "  (");
    size_t n = cut != NULL ? (size_t)(cut - txt) : strlen(txt);
    if (n >= sizeof(s_pending_ssid)) {
        n = sizeof(s_pending_ssid) - 1;
    }
    memcpy(s_pending_ssid, txt, n);
    s_pending_ssid[n] = '\0';

    if (s_wifi_status != NULL) {
        lv_label_set_text_fmt(s_wifi_status, "Enter password for %s", s_pending_ssid);
        lv_obj_set_style_text_color(s_wifi_status, lv_color_hex(COL_MUTED), 0);
    }
}

static void wifi_scan_done(void *arg)
{
    wifi_scan_job_t *job = (wifi_scan_job_t *)arg;
    if (bsp_display_lock(1000)) {
        if (job->gen == s_gen && s_wifi_list != NULL) {
            lv_obj_clean(s_wifi_list);
            if (job->count <= 0) {
                lv_list_add_text(s_wifi_list, job->count == 0 ?
                                 "no networks found" : "scan failed");
            }
            for (int i = 0; i < job->count; i++) {
                char row[64];
                snprintf(row, sizeof(row), "%s  (%ddBm)",
                         job->results[i].ssid, job->results[i].rssi);
                lv_obj_t *btn = lv_list_add_button(s_wifi_list,
                        job->results[i].secure ? LV_SYMBOL_WIFI : LV_SYMBOL_WIFI,
                        row);
                lv_obj_add_event_cb(btn, wifi_ssid_pick_cb, LV_EVENT_CLICKED, NULL);
            }
        }
        bsp_display_unlock();
    }
    heap_caps_free(job);
}

static void wifi_scan_job(void *arg)
{
    wifi_scan_job_t *job = (wifi_scan_job_t *)arg;
    job->count = wifi_mgr_scan(job->results, WIFI_SCAN_MAX);
    wifi_scan_done(job);
}

static void wifi_scan_cb(lv_event_t *e)
{
    wifi_scan_job_t *job = heap_caps_calloc(1, sizeof(*job), MALLOC_CAP_SPIRAM);
    if (job == NULL) {
        return;
    }
    job->gen = s_gen;
    if (s_wifi_list != NULL) {
        lv_obj_clean(s_wifi_list);
        lv_list_add_text(s_wifi_list, "scanning...");
    }
    if (!app_run_async(wifi_scan_job, job)) {
        heap_caps_free(job);
    }
}

static void build_wifi_page(lv_event_t *e)
{
    lv_obj_t *page = new_page("WiFi", false);
    bedside_settings_t *cfg = settings_get();

    wifi_conn_info_t info;
    if (wifi_mgr_get_info(&info)) {
        lv_obj_t *card = lv_obj_create(page);
        lv_obj_set_width(card, lv_pct(100));
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD), 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_set_style_pad_row(card, 6, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *hdr = make_label(card, "", COL_GOOD);
        lv_label_set_text_fmt(hdr, LV_SYMBOL_WIFI "  Connected to %s",
                              info.ssid);

        const char *quality = info.rssi >= -50 ? "excellent"
                            : info.rssi >= -60 ? "good"
                            : info.rssi >= -70 ? "fair" : "weak";
        lv_obj_t *det = make_label(card, "", COL_MUTED);
        lv_label_set_text_fmt(det,
                              "IP %s    Signal %d dBm (%s)    Ch %u    %s",
                              info.ip, info.rssi, quality, info.channel,
                              info.security);
    } else if (cfg->wifi_ssid[0] != '\0') {
        lv_obj_t *l = make_label(page, "", COL_BAD);
        lv_label_set_text_fmt(l, "Not connected (saved network: %s)",
                              cfg->wifi_ssid);
    } else {
        make_label(page, "Not connected", COL_MUTED);
    }

    voice_actions_bind(VA_SCAN,
        make_button(page, LV_SYMBOL_REFRESH "  Scan networks", wifi_scan_cb, NULL));

    s_wifi_list = lv_list_create(page);
    lv_obj_set_width(s_wifi_list, lv_pct(100));
    lv_obj_set_height(s_wifi_list, 220);
    lv_obj_set_style_bg_color(s_wifi_list, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_border_width(s_wifi_list, 0, 0);
    voice_actions_set_option_list(s_wifi_list);

    s_wifi_status = make_label(page, "Scan, pick a network, enter password.",
                               COL_MUTED);

    s_wifi_pass_ta = make_textarea(page, "password", NULL, true);
    voice_actions_bind(VA_CONNECT,
        make_button(page, "Connect", wifi_connect_cb, NULL));

    if (cfg->wifi_ssid[0] != '\0') {
        strlcpy(s_pending_ssid, cfg->wifi_ssid, sizeof(s_pending_ssid));
        lv_textarea_set_text(s_wifi_pass_ta, cfg->wifi_pass);
    }
}

/* ---------- Navidrome page ---------- */

static lv_obj_t *s_nav_url_ta;
static lv_obj_t *s_nav_user_ta;
static lv_obj_t *s_nav_pass_ta;

static void nav_test_job(void *arg)
{
    uint32_t gen = (uint32_t)(uintptr_t)arg;
    nav_playlist_list_t pls;
    esp_err_t err = wifi_mgr_is_connected() ? nav_get_playlists(&pls)
                                            : ESP_ERR_INVALID_STATE;
    int count = (err == ESP_OK) ? pls.count : -1;
    if (err == ESP_OK) {
        nav_free_playlists(&pls);
    }
    if (bsp_display_lock(1000)) {
        if (gen == s_gen && s_nav_status != NULL) {
            if (count >= 0) {
                lv_label_set_text_fmt(s_nav_status,
                        "Server OK - %d playlists.", count);
                lv_obj_set_style_text_color(s_nav_status, lv_color_hex(COL_GOOD), 0);
            } else {
                lv_label_set_text(s_nav_status,
                        err == ESP_ERR_INVALID_STATE ?
                        "WiFi is not connected." :
                        "Server test failed (see log).");
                lv_obj_set_style_text_color(s_nav_status, lv_color_hex(COL_BAD), 0);
            }
        }
        bsp_display_unlock();
    }
}

/* Pull the textareas into cfg; run by the sidebar Save and before a test. */
static void nav_commit(void)
{
    if (s_nav_url_ta == NULL) {
        return;
    }
    bedside_settings_t *cfg = settings_get();
    settings_lock(); /* the music/alarm tasks read these mid-request */
    strlcpy(cfg->nav_url, lv_textarea_get_text(s_nav_url_ta), sizeof(cfg->nav_url));
    strlcpy(cfg->nav_user, lv_textarea_get_text(s_nav_user_ta), sizeof(cfg->nav_user));
    strlcpy(cfg->nav_pass, lv_textarea_get_text(s_nav_pass_ta), sizeof(cfg->nav_pass));
    /* trim a trailing slash so url + "/rest/..." stays well-formed */
    size_t n = strlen(cfg->nav_url);
    if (n > 0 && cfg->nav_url[n - 1] == '/') {
        cfg->nav_url[n - 1] = '\0';
    }
    settings_unlock();
}

static void nav_dirty_cb(lv_event_t *e)
{
    mark_dirty();
}

static void nav_test_cb(lv_event_t *e)
{
    nav_commit(); /* test what is typed; persisting stays with Save */
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    if (s_nav_status != NULL) {
        lv_label_set_text(s_nav_status, "Testing server...");
        lv_obj_set_style_text_color(s_nav_status, lv_color_hex(COL_MUTED), 0);
    }
    if (!app_run_async(nav_test_job, (void *)(uintptr_t)s_gen) &&
        s_nav_status != NULL) {
        lv_label_set_text(s_nav_status, "Busy - try again");
    }
}

/* Server check interval: how often boot_seq pings the music and AI servers
 * to refresh the clock-face indicators. Dropdown index <-> seconds. */
static const uint16_t s_poll_choices[] = { 30, 60, 300, 900, 0 };
#define POLL_CHOICES (sizeof(s_poll_choices) / sizeof(s_poll_choices[0]))

static uint32_t poll_choice_index(uint16_t secs)
{
    /* exact match first, then the nearest non-off choice (a value set via
     * the API, e.g. 120 s) so the dropdown never shows a misleading "Off" */
    for (uint32_t i = 0; i < POLL_CHOICES; i++) {
        if (s_poll_choices[i] == secs) {
            return i;
        }
    }
    uint32_t best = 0;
    for (uint32_t i = 1; i < POLL_CHOICES - 1; i++) {
        if (abs((int)s_poll_choices[i] - (int)secs) <
            abs((int)s_poll_choices[best] - (int)secs)) {
            best = i;
        }
    }
    return best;
}

static void svc_poll_cb(lv_event_t *e)
{
    uint32_t i = lv_dropdown_get_selected(lv_event_get_target(e));
    if (i < POLL_CHOICES) {
        settings_get()->svc_poll_s = s_poll_choices[i]; /* u16: no lock */
        mark_dirty(); /* Save persists and re-kicks the probe */
    }
}

static void build_nav_page(lv_event_t *e)
{
    lv_obj_t *page = new_page("Music API", false);
    bedside_settings_t *cfg = settings_get();
    s_commit = nav_commit;

    make_label(page, "Server URL (https://host)", COL_MUTED);
    s_nav_url_ta = make_textarea(page, "https://music.example.com", cfg->nav_url, false);
    make_label(page, "Username", COL_MUTED);
    s_nav_user_ta = make_textarea(page, "user", cfg->nav_user, false);
    make_label(page, "Password", COL_MUTED);
    s_nav_pass_ta = make_textarea(page, "password", cfg->nav_pass, true);
    lv_obj_add_event_cb(s_nav_url_ta, nav_dirty_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_nav_user_ta, nav_dirty_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_nav_pass_ta, nav_dirty_cb, LV_EVENT_VALUE_CHANGED, NULL);

    voice_actions_bind(VA_SAVE_TEST,
        make_button(page, "Test server", nav_test_cb, NULL));
    s_nav_status = make_label(page, "", COL_MUTED);

    make_label(page, "Server check interval (music & AI indicators)", COL_MUTED);
    lv_obj_t *dd = lv_dropdown_create(page);
    lv_obj_set_width(dd, lv_pct(100));
    lv_dropdown_set_options(dd, "Every 30 s\nEvery minute\nEvery 5 minutes\n"
                                "Every 15 minutes\nOff");
    lv_dropdown_set_selected(dd, poll_choice_index(cfg->svc_poll_s));
    lv_obj_add_event_cb(dd, svc_poll_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

/* ---------- AI page ---------- */

static lv_obj_t *s_ai_url_ta;
static lv_obj_t *s_ai_key_ta;
static lv_obj_t *s_ai_model_ta;

/* Pull the textareas into cfg; run by the sidebar Save. */
static void ai_commit(void)
{
    if (s_ai_url_ta == NULL) {
        return;
    }
    bedside_settings_t *cfg = settings_get();
    settings_lock(); /* localai_client snapshots these under the same lock */
    strlcpy(cfg->ai_url, lv_textarea_get_text(s_ai_url_ta), sizeof(cfg->ai_url));
    strlcpy(cfg->ai_key, lv_textarea_get_text(s_ai_key_ta), sizeof(cfg->ai_key));
    strlcpy(cfg->ai_model, lv_textarea_get_text(s_ai_model_ta),
            sizeof(cfg->ai_model));
    size_t n = strlen(cfg->ai_url);
    if (n > 0 && cfg->ai_url[n - 1] == '/') {
        cfg->ai_url[n - 1] = '\0';
    }
    settings_unlock();
}

static void ai_dirty_cb(lv_event_t *e)
{
    mark_dirty();
}

static void build_ai_page(lv_event_t *e)
{
    lv_obj_t *page = new_page("AI", false);
    bedside_settings_t *cfg = settings_get();
    s_commit = ai_commit;

    make_label(page, "Matches your music requests (Play screen) to real "
                     "songs and playlists.", COL_MUTED);

    make_label(page, "Server type", COL_MUTED);
    lv_obj_t *dd = lv_dropdown_create(page);
    lv_obj_set_width(dd, lv_pct(100));
    lv_dropdown_set_options(dd, "LocalAI");
    lv_dropdown_set_selected(dd, cfg->ai_provider);

    make_label(page, "Server address (http://host:port)", COL_MUTED);
    s_ai_url_ta = make_textarea(page, "http://host:8080",
                                cfg->ai_url, false);
    make_label(page, "API key", COL_MUTED);
    s_ai_key_ta = make_textarea(page, "key", cfg->ai_key, true);
    make_label(page, "Model", COL_MUTED);
    s_ai_model_ta = make_textarea(page, "qwen_qwen3-4b-instruct-2507",
                                  cfg->ai_model, false);

    lv_obj_add_event_cb(s_ai_url_ta, ai_dirty_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_ai_key_ta, ai_dirty_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_ai_model_ta, ai_dirty_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

/* ---------- API page (local HTTP control API) ---------- */

static lv_obj_t *s_api_key_ta;
static lv_obj_t *s_api_enable_sw;

/* Pull the widgets into cfg and reconcile the server; run by the sidebar
 * Save and by the enable switch (so a just-typed key is live before Save
 * persists it - auth re-reads the key per request). */
static void api_commit(void)
{
    if (s_api_key_ta == NULL || s_api_enable_sw == NULL) {
        return;
    }
    bedside_settings_t *cfg = settings_get();
    settings_lock(); /* string write races auth/save on other tasks */
    strlcpy(cfg->api_key, lv_textarea_get_text(s_api_key_ta),
            sizeof(cfg->api_key));
    settings_unlock();
    cfg->api_enabled =
        lv_obj_has_state(s_api_enable_sw, LV_STATE_CHECKED) ? 1 : 0;
    api_server_set_enabled(cfg->api_enabled != 0); /* async, idempotent */
}

/* No key, no API: the switch is only operable while a key is present, and
 * emptying the key while enabled force-disables. */
static void api_sync_switch_state(void)
{
    if (s_api_key_ta == NULL || s_api_enable_sw == NULL) {
        return;
    }
    if (lv_textarea_get_text(s_api_key_ta)[0] != '\0') {
        lv_obj_remove_state(s_api_enable_sw, LV_STATE_DISABLED);
    } else {
        if (lv_obj_has_state(s_api_enable_sw, LV_STATE_CHECKED)) {
            lv_obj_remove_state(s_api_enable_sw, LV_STATE_CHECKED);
            api_commit(); /* clears cfg->api_enabled, stops the server */
        }
        lv_obj_add_state(s_api_enable_sw, LV_STATE_DISABLED);
    }
}

static void api_key_changed_cb(lv_event_t *e)
{
    mark_dirty();
    api_sync_switch_state();
}

static void api_enable_cb(lv_event_t *e)
{
    api_commit(); /* switch state is already updated when this fires */
    mark_dirty();
}

static void build_api_page(lv_event_t *e)
{
    lv_obj_t *page = new_page("API", false);
    bedside_settings_t *cfg = settings_get();
    s_commit = api_commit;

    make_label(page, "Remote control over HTTP: settings, playback and "
                     "panel controls. Send the key in an \"X-Api-Key\" "
                     "header with every request.", COL_MUTED);

    lv_obj_t *addr = make_label(page, "", COL_MUTED);
    if (wifi_mgr_is_connected()) {
        char ip[16];
        wifi_mgr_ip(ip, sizeof(ip));
        lv_label_set_text_fmt(addr, "Address: http://%s/api/v1  (%s)", ip,
                              api_server_is_running() ? "running" : "stopped");
    } else {
        lv_label_set_text(addr, "WiFi not connected");
    }

    make_label(page, "API key (required to enable)", COL_MUTED);
    s_api_key_ta = make_textarea(page, "key", cfg->api_key, true);
    lv_obj_add_event_cb(s_api_key_ta, api_key_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *row = lv_obj_create(page);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, "Enable API");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
    s_api_enable_sw = lv_switch_create(row);
    if (cfg->api_enabled) {
        lv_obj_add_state(s_api_enable_sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(s_api_enable_sw, api_enable_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    api_sync_switch_state();
}

/* ---------- Alarm page ----------
 * Three cards (one per settings slot), accordion style: a header row shows
 * switch + name + time + days, and tapping it expands that card by moving
 * the single shared editor (hour/minute rollers, Mo..Su day matrix, presets)
 * into it. Controls write cfg->alarms[sel] straight away (single bytes, no
 * lock needed) and light Save; a 1 s timer repaints the page when the alarm
 * task (once-mode auto-off) or a web PATCH changes the slots underneath. */

static lv_obj_t *s_alarm_cards[ALARM_COUNT];
static lv_obj_t *s_alarm_sw[ALARM_COUNT];
static lv_obj_t *s_alarm_time_lbl[ALARM_COUNT];
static lv_obj_t *s_alarm_days_lbl[ALARM_COUNT];
static lv_obj_t *s_alarm_chev[ALARM_COUNT];
static lv_obj_t *s_alarm_editor;               /* re-parented into the open card */
static lv_obj_t *s_alarm_presets;              /* preset row, moves with the editor */
static lv_obj_t *s_alarm_hh_roller;
static lv_obj_t *s_alarm_mm_roller;
static lv_obj_t *s_alarm_days_bm;              /* Mo..Su button matrix */
static lv_obj_t *s_alarm_preset[4];
static lv_obj_t *s_alarm_next_lbl;
static lv_timer_t *s_alarm_refresh;
static alarm_slot_t s_alarm_shadow[ALARM_COUNT];
static int s_alarm_sel = -1;                   /* expanded card */

/* Display is Monday-first; storage is tm_wday bits (bit0 = Sunday). */
static const uint8_t k_col_wday[7] = { 1, 2, 3, 4, 5, 6, 0 };
static const char *k_day_map[] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su", "" };
static const char *k_wday_short[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
/* order matches VA_DAYS_ALL..VA_DAYS_ONCE */
static const struct { const char *text; uint8_t mask; } k_presets[4] = {
    { "Every day", ALARM_DAYS_ALL  },
    { "Weekdays",  ALARM_DAYS_WEEK },
    { "Weekends",  ALARM_DAYS_WEND },
    { "Once",      ALARM_DAYS_ONCE },
};
#define COL_DAY_OFF 0x232A33

/* "Every day" / "Weekdays" / "Weekends" / "Mon Wed Fri" (+ "Once" prefix) */
static void days_text(uint8_t days, char *buf, size_t cap)
{
    uint8_t m = days & ALARM_DAYS_MASK;
    bool once = (days & ALARM_DAYS_ONCE) != 0;
    char list[48] = "";
    const char *base;
    if (m == ALARM_DAYS_ALL) {
        base = once ? "" : "Every day";
    } else if (m == ALARM_DAYS_WEEK) {
        base = "Weekdays";
    } else if (m == ALARM_DAYS_WEND) {
        base = "Weekends";
    } else {
        size_t n = 0;
        for (int k = 0; k < 7; k++) {
            if (m & (1u << k_col_wday[k])) {
                n += snprintf(list + n, sizeof(list) - n, "%s%s", n ? " " : "",
                              k_wday_short[k_col_wday[k]]);
            }
        }
        base = list;
    }
    if (!once) {
        strlcpy(buf, base, cap);
    } else if (base[0] != '\0') {
        snprintf(buf, cap, "Once (%s)", base);
    } else {
        strlcpy(buf, "Once", cap);
    }
}

/* Alarm page widgets are gone (page change / close): drop the timer and
 * every pointer an event or timer callback could still reach. */
static void alarm_page_teardown(void)
{
    if (s_alarm_refresh != NULL) {
        lv_timer_delete(s_alarm_refresh);
        s_alarm_refresh = NULL;
    }
    for (int i = 0; i < ALARM_COUNT; i++) {
        s_alarm_cards[i] = s_alarm_sw[i] = s_alarm_time_lbl[i] = NULL;
        s_alarm_days_lbl[i] = s_alarm_chev[i] = NULL;
    }
    s_alarm_editor = s_alarm_presets = s_alarm_hh_roller = s_alarm_mm_roller = NULL;
    s_alarm_days_bm = s_alarm_next_lbl = NULL;
    memset(s_alarm_preset, 0, sizeof(s_alarm_preset));
    s_alarm_sel = -1;
}

static void alarm_header_refresh(int i)
{
    const alarm_slot_t *a = &settings_get()->alarms[i];
    if (s_alarm_time_lbl[i] != NULL) {
        lv_label_set_text_fmt(s_alarm_time_lbl[i], "%02d:%02d", a->hh, a->mm);
    }
    if (s_alarm_days_lbl[i] != NULL) {
        char d[48];
        days_text(a->days, d, sizeof(d));
        lv_label_set_text(s_alarm_days_lbl[i], d);
    }
    if (s_alarm_sw[i] != NULL) {
        /* state only - no VALUE_CHANGED, so the switch callback stays quiet */
        if (a->enabled) {
            lv_obj_add_state(s_alarm_sw[i], LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s_alarm_sw[i], LV_STATE_CHECKED);
        }
    }
}

/* "Next: Alarm 2 - Tue 07:00" from the scheduler's snapshot (it re-arms
 * within a second of any edit, so this follows the rollers with a small lag). */
static void alarm_next_refresh(void)
{
    if (s_alarm_next_lbl == NULL) {
        return;
    }
    alarm_next_t nx;
    if (!alarm_next(&nx)) {
        lv_label_set_text(s_alarm_next_lbl, "No alarm scheduled");
        return;
    }
    time_t now = time(NULL);
    struct tm tm;
    clock_sync_localtime(&nx.at, &tm);
    char wd[8] = "";
    if (nx.at - now >= 86400) {
        strftime(wd, sizeof(wd), "%a ", &tm);
    }
    lv_label_set_text_fmt(s_alarm_next_lbl, "%s: Alarm %d  -  %s%02d:%02d",
                          nx.snooze ? "Snoozing" : "Next", nx.index + 1, wd,
                          tm.tm_hour, tm.tm_min);
}

/* Push the selected slot into the editor widgets (no events fire). */
static void alarm_editor_sync(void)
{
    if (s_alarm_sel < 0 || s_alarm_editor == NULL) {
        return;
    }
    const alarm_slot_t *a = &settings_get()->alarms[s_alarm_sel];
    lv_roller_set_selected(s_alarm_hh_roller, a->hh, LV_ANIM_OFF);
    lv_roller_set_selected(s_alarm_mm_roller, a->mm, LV_ANIM_OFF);
    for (int k = 0; k < 7; k++) {
        if (a->days & (1u << k_col_wday[k])) {
            lv_buttonmatrix_set_button_ctrl(s_alarm_days_bm, k, LV_BUTTONMATRIX_CTRL_CHECKED);
        } else {
            lv_buttonmatrix_clear_button_ctrl(s_alarm_days_bm, k, LV_BUTTONMATRIX_CTRL_CHECKED);
        }
    }
    uint8_t m = a->days & ALARM_DAYS_MASK;
    bool once = (a->days & ALARM_DAYS_ONCE) != 0;
    for (int p = 0; p < 4; p++) {
        bool on = (k_presets[p].mask == ALARM_DAYS_ONCE) ? once
                                                         : (!once && m == k_presets[p].mask);
        if (s_alarm_preset[p] != NULL) {
            lv_obj_set_style_bg_color(s_alarm_preset[p],
                                      lv_color_hex(on ? COL_BTN : COL_DAY_OFF), 0);
        }
    }
}

/* Expand card idx: highlight it, move the editor in, retarget the voice
 * on/off commands (rollers and presets are shared, already bound). */
static void alarm_select(int idx)
{
    if (idx < 0 || idx >= ALARM_COUNT || s_alarm_cards[idx] == NULL) {
        return;
    }
    s_alarm_sel = idx;
    for (int i = 0; i < ALARM_COUNT; i++) {
        lv_obj_set_style_border_width(s_alarm_cards[i], i == idx ? 2 : 0, 0);
        lv_label_set_text(s_alarm_chev[i], i == idx ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
    }
    lv_obj_set_parent(s_alarm_editor, s_alarm_cards[idx]);
    lv_obj_set_parent(s_alarm_presets, s_alarm_cards[idx]); /* stays below the editor */
    lv_obj_remove_flag(s_alarm_editor, LV_OBJ_FLAG_HIDDEN);
    voice_actions_bind_switch(VA_ALARM_ON, VA_ALARM_OFF, s_alarm_sw[idx]);
    alarm_editor_sync();
}

static void alarm_hdr_cb(lv_event_t *e)
{
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    if (idx != s_alarm_sel) {
        alarm_select(idx);
    }
}

static void alarm_sw_cb(lv_event_t *e)
{
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    lv_obj_t *sw = lv_event_get_target(e);
    settings_get()->alarms[idx].enabled = lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0;
    s_alarm_shadow[idx].enabled = settings_get()->alarms[idx].enabled;
    mark_dirty();
}

/* Rollers apply straight to cfg; the sidebar Save persists. */
static void alarm_roller_cb(lv_event_t *e)
{
    if (s_alarm_sel < 0) {
        return;
    }
    alarm_slot_t *a = &settings_get()->alarms[s_alarm_sel];
    a->hh = (uint8_t)lv_roller_get_selected(s_alarm_hh_roller);
    a->mm = (uint8_t)lv_roller_get_selected(s_alarm_mm_roller);
    s_alarm_shadow[s_alarm_sel] = *a;
    mark_dirty();
    alarm_header_refresh(s_alarm_sel);
}

static void alarm_days_cb(lv_event_t *e)
{
    if (s_alarm_sel < 0) {
        return;
    }
    uint32_t id = lv_buttonmatrix_get_selected_button(s_alarm_days_bm);
    uint8_t mask = 0;
    for (int k = 0; k < 7; k++) {
        if (lv_buttonmatrix_has_button_ctrl(s_alarm_days_bm, k, LV_BUTTONMATRIX_CTRL_CHECKED)) {
            mask |= (uint8_t)(1u << k_col_wday[k]);
        }
    }
    if (mask == 0) {
        /* an alarm with no day is not a thing: the last day stays lit */
        if (id < 7) {
            lv_buttonmatrix_set_button_ctrl(s_alarm_days_bm, id, LV_BUTTONMATRIX_CTRL_CHECKED);
        }
        return;
    }
    alarm_slot_t *a = &settings_get()->alarms[s_alarm_sel];
    a->days = (uint8_t)((a->days & ALARM_DAYS_ONCE) | mask);
    s_alarm_shadow[s_alarm_sel] = *a;
    mark_dirty();
    alarm_header_refresh(s_alarm_sel);
    alarm_editor_sync();
}

static void alarm_preset_cb(lv_event_t *e)
{
    if (s_alarm_sel < 0) {
        return;
    }
    uint8_t mask = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    alarm_slot_t *a = &settings_get()->alarms[s_alarm_sel];
    if (mask == ALARM_DAYS_ONCE) {
        a->days ^= ALARM_DAYS_ONCE; /* Once toggles; the days stay */
    } else {
        a->days = mask;             /* a day preset also clears Once */
    }
    s_alarm_shadow[s_alarm_sel] = *a;
    mark_dirty();
    alarm_header_refresh(s_alarm_sel);
    alarm_editor_sync();
}

/* 1 s: repaint when something other than this page changed the slots
 * (once-mode auto-off on the alarm task, a web PATCH), and keep the
 * "Next:" line current. */
static void alarm_refresh_cb(lv_timer_t *t)
{
    const bedside_settings_t *cfg = settings_get();
    if (memcmp(cfg->alarms, s_alarm_shadow, sizeof(s_alarm_shadow)) != 0) {
        memcpy(s_alarm_shadow, cfg->alarms, sizeof(s_alarm_shadow));
        for (int i = 0; i < ALARM_COUNT; i++) {
            alarm_header_refresh(i);
        }
        alarm_editor_sync();
    }
    alarm_next_refresh();
}

static lv_obj_t *make_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static void alarm_mode_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    settings_get()->alarm_mode = (uint8_t)lv_dropdown_get_selected(dd);
    mark_dirty();
}

typedef struct {
    uint32_t gen;
    nav_playlist_list_t pls;
    esp_err_t err;
} pl_job_t;

static pl_job_t *s_pl_job;   /* playlists shown in the picker (freed on reuse) */

static void alarm_pl_pick_cb(lv_event_t *e)
{
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    if (s_pl_job == NULL || idx >= s_pl_job->pls.count) {
        return;
    }
    bedside_settings_t *cfg = settings_get();
    settings_lock(); /* the alarm task reads the id across a network fetch */
    strlcpy(cfg->alarm_playlist_id, s_pl_job->pls.items[idx].id,
            sizeof(cfg->alarm_playlist_id));
    strlcpy(cfg->alarm_playlist_name, s_pl_job->pls.items[idx].name,
            sizeof(cfg->alarm_playlist_name));
    cfg->alarm_mode = ALARM_MODE_PLAYLIST;
    settings_unlock();
    mark_dirty();
    if (s_alarm_pl_label != NULL) {
        lv_label_set_text_fmt(s_alarm_pl_label, "Wake playlist: %s",
                              cfg->alarm_playlist_name);
    }
    if (s_pl_status != NULL) {
        lv_label_set_text_fmt(s_pl_status, "Selected \"%s\"",
                              cfg->alarm_playlist_name);
        lv_obj_set_style_text_color(s_pl_status, lv_color_hex(COL_GOOD), 0);
    }
}

static void alarm_pl_fetch_job(void *arg)
{
    pl_job_t *job = (pl_job_t *)arg;
    if (!wifi_mgr_is_connected() || !nav_is_configured()) {
        job->err = ESP_ERR_INVALID_STATE;
    } else {
        job->err = nav_get_playlists(&job->pls);
    }

    if (bsp_display_lock(1000)) {
        if (job->gen == s_gen && s_pl_list != NULL) {
            lv_obj_clean(s_pl_list);
            if (job->err != ESP_OK) {
                lv_list_add_text(s_pl_list,
                        job->err == ESP_ERR_INVALID_STATE ?
                        "WiFi or Navidrome not configured" : "server error");
            } else if (job->pls.count == 0) {
                lv_list_add_text(s_pl_list, "no playlists on server");
            } else {
                /* keep this job alive as the picker's backing data */
                if (s_pl_job != NULL) {
                    if (s_pl_job->err == ESP_OK) {
                        nav_free_playlists(&s_pl_job->pls);
                    }
                    heap_caps_free(s_pl_job);
                }
                s_pl_job = job;
                for (int i = 0; i < job->pls.count; i++) {
                    char row[96];
                    snprintf(row, sizeof(row), "%s  (%d songs)",
                             job->pls.items[i].name, job->pls.items[i].song_count);
                    utf8_trim_tail(row);
                    lv_obj_t *btn = lv_list_add_button(s_pl_list, LV_SYMBOL_AUDIO, row);
                    lv_obj_add_event_cb(btn, alarm_pl_pick_cb, LV_EVENT_CLICKED,
                                        (void *)(uintptr_t)i);
                }
                bsp_display_unlock();
                return; /* job ownership transferred */
            }
        }
        bsp_display_unlock();
    }
    if (job->err == ESP_OK) {
        nav_free_playlists(&job->pls);
    }
    heap_caps_free(job);
}

static void alarm_pl_fetch_cb(lv_event_t *e)
{
    pl_job_t *job = heap_caps_calloc(1, sizeof(*job), MALLOC_CAP_SPIRAM);
    if (job == NULL) {
        return;
    }
    job->gen = s_gen;
    if (s_pl_list != NULL) {
        lv_obj_clean(s_pl_list);
        lv_list_add_text(s_pl_list, "fetching playlists...");
    }
    if (!app_run_async(alarm_pl_fetch_job, job)) {
        heap_caps_free(job);
    }
}

static void build_alarm_page(lv_event_t *e)
{
    lv_obj_t *page = new_page("Alarms", false);
    bedside_settings_t *cfg = settings_get();

    s_alarm_next_lbl = make_label(page, "", COL_MUTED);

    for (int i = 0; i < ALARM_COUNT; i++) {
        lv_obj_t *card = lv_obj_create(page);
        lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD), 0);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_set_style_pad_all(card, 8, 0);
        lv_obj_set_style_pad_row(card, 8, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(COL_BTN), 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        s_alarm_cards[i] = card;

        /* header: [switch] Alarm N  07:00  Weekdays  [chevron] - the row is
         * the tap target; the switch takes its own clicks */
        lv_obj_t *hdr = make_row(card);
        lv_obj_set_height(hdr, 48);
        lv_obj_set_style_pad_column(hdr, 14, 0);
        lv_obj_add_event_cb(hdr, alarm_hdr_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        voice_actions_bind((va_cmd_t)(VA_SELECT_ALARM_1 + i), hdr);

        lv_obj_t *sw = lv_switch_create(hdr);
        lv_obj_set_size(sw, 80, 44);
        lv_obj_set_ext_click_area(sw, 10);
        lv_obj_add_event_cb(sw, alarm_sw_cb, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)i);
        s_alarm_sw[i] = sw;

        lv_obj_t *name = lv_label_create(hdr);
        lv_label_set_text_fmt(name, "Alarm %d", i + 1);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(COL_TEXT), 0);

        s_alarm_time_lbl[i] = lv_label_create(hdr);
        lv_obj_set_style_text_font(s_alarm_time_lbl[i], &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(s_alarm_time_lbl[i], lv_color_hex(COL_TEXT), 0);

        s_alarm_days_lbl[i] = lv_label_create(hdr);
        lv_obj_set_flex_grow(s_alarm_days_lbl[i], 1);
        lv_label_set_long_mode(s_alarm_days_lbl[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(s_alarm_days_lbl[i], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(s_alarm_days_lbl[i], lv_color_hex(COL_MUTED), 0);

        s_alarm_chev[i] = lv_label_create(hdr);
        lv_label_set_text(s_alarm_chev[i], LV_SYMBOL_DOWN);
        lv_obj_set_style_text_font(s_alarm_chev[i], &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(s_alarm_chev[i], lv_color_hex(COL_MUTED), 0);
    }

    /* the one editor, built inside the first card and moved by alarm_select */
    s_alarm_editor = make_row(s_alarm_cards[0]);
    lv_obj_set_flex_align(s_alarm_editor, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(s_alarm_editor, 16, 0);

    lv_obj_t *trow = make_row(s_alarm_editor);
    lv_obj_set_width(trow, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(trow, 12, 0);

    static char hh_opts[24 * 3 + 1];
    static char mm_opts[60 * 3 + 1];
    if (hh_opts[0] == '\0') {
        char *p = hh_opts;
        for (int i = 0; i < 24; i++) {
            p += sprintf(p, i < 23 ? "%02d\n" : "%02d", i);
        }
        p = mm_opts;
        for (int i = 0; i < 60; i++) {
            p += sprintf(p, i < 59 ? "%02d\n" : "%02d", i);
        }
    }
    /* font BEFORE the row count: the roller sizes itself from the font */
    s_alarm_hh_roller = lv_roller_create(trow);
    lv_obj_set_style_text_font(s_alarm_hh_roller, &lv_font_montserrat_28, 0);
    lv_roller_set_options(s_alarm_hh_roller, hh_opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_alarm_hh_roller, 3);

    s_alarm_mm_roller = lv_roller_create(trow);
    lv_obj_set_style_text_font(s_alarm_mm_roller, &lv_font_montserrat_28, 0);
    lv_roller_set_options(s_alarm_mm_roller, mm_opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_alarm_mm_roller, 3);

    lv_obj_add_event_cb(s_alarm_hh_roller, alarm_roller_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_alarm_mm_roller, alarm_roller_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    voice_actions_bind_roller(VA_HOUR_UP, VA_HOUR_DOWN, s_alarm_hh_roller);
    voice_actions_bind_roller(VA_MIN_UP, VA_MIN_DOWN, s_alarm_mm_roller);

    /* Day matrix beside the rollers, presets in their own row under the
     * editor. Kept SHALLOW on purpose: LVGL renders recursively and the
     * LVGL task stack is small - no wrapper containers here. */
    s_alarm_days_bm = lv_buttonmatrix_create(s_alarm_editor);
    lv_buttonmatrix_set_map(s_alarm_days_bm, k_day_map);
    for (int k = 0; k < 7; k++) {
        lv_buttonmatrix_set_button_ctrl(s_alarm_days_bm, k, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    }
    lv_obj_set_flex_grow(s_alarm_days_bm, 1);
    lv_obj_set_height(s_alarm_days_bm, 56);
    lv_obj_set_style_bg_opa(s_alarm_days_bm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_alarm_days_bm, 0, 0);
    lv_obj_set_style_pad_all(s_alarm_days_bm, 0, 0);
    lv_obj_set_style_pad_column(s_alarm_days_bm, 6, 0);
    lv_obj_set_style_bg_color(s_alarm_days_bm, lv_color_hex(COL_DAY_OFF), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_alarm_days_bm, lv_color_hex(COL_BTN),
                              LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(s_alarm_days_bm, lv_color_hex(COL_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_alarm_days_bm, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_alarm_days_bm, 6, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_alarm_days_bm, alarm_days_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_alarm_presets = make_row(s_alarm_cards[0]);
    lv_obj_set_style_pad_column(s_alarm_presets, 8, 0);
    for (int p = 0; p < 4; p++) {
        lv_obj_t *b = lv_button_create(s_alarm_presets);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_height(b, 48);
        lv_obj_set_style_bg_color(b, lv_color_hex(COL_DAY_OFF), 0);
        lv_obj_add_event_cb(b, alarm_preset_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)k_presets[p].mask);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, k_presets[p].text);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_obj_center(l);
        voice_actions_bind((va_cmd_t)(VA_DAYS_ALL + p), b);
        s_alarm_preset[p] = b;
    }

    memcpy(s_alarm_shadow, cfg->alarms, sizeof(s_alarm_shadow));
    int sel = 0;
    for (int i = 0; i < ALARM_COUNT; i++) {
        alarm_header_refresh(i);
        if (cfg->alarms[i].enabled && sel == 0 && !cfg->alarms[0].enabled) {
            sel = i; /* open the first enabled alarm */
        }
    }
    alarm_select(sel);
    alarm_next_refresh();
    s_alarm_refresh = lv_timer_create(alarm_refresh_cb, 1000, NULL);
}

/* ---------- Alarm sound page (shared by all three alarms) ---------- */

static const uint16_t k_ring_opts[] = { 60, 120, 300, 600, 900, 1800, 3600, 7200, 0 };
#define RING_OPT_COUNT ((int)(sizeof(k_ring_opts) / sizeof(k_ring_opts[0])))

static int max_ring_nearest(uint16_t v)
{
    if (v == 0) {
        return RING_OPT_COUNT - 1;
    }
    int best = 0;
    for (int i = 0; i < RING_OPT_COUNT - 1; i++) {
        if (abs((int)k_ring_opts[i] - (int)v) < abs((int)k_ring_opts[best] - (int)v)) {
            best = i;
        }
    }
    return best;
}

static void max_ring_cb(lv_event_t *e)
{
    int i = (int)lv_dropdown_get_selected(lv_event_get_target(e));
    if (i >= 0 && i < RING_OPT_COUNT) {
        settings_get()->max_alarm_seconds = k_ring_opts[i];
        mark_dirty();
    }
}

static void build_alarm_sound_page(lv_event_t *e)
{
    lv_obj_t *page = new_page("Alarm sound", false);
    bedside_settings_t *cfg = settings_get();

    make_label(page, "Stop ringing after (when nobody dismisses it)", COL_MUTED);
    lv_obj_t *rd = lv_dropdown_create(page);
    lv_obj_set_width(rd, lv_pct(100));
    lv_dropdown_set_options(rd, "1 minute\n2 minutes\n5 minutes\n10 minutes\n"
                                "15 minutes\n30 minutes\n1 hour\n2 hours\n"
                                "Never - ring until dismissed");
    lv_dropdown_set_selected(rd, max_ring_nearest(cfg->max_alarm_seconds));
    lv_obj_add_event_cb(rd, max_ring_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* sound mode */
    make_label(page, "Alarm sound", COL_MUTED);
    lv_obj_t *dd = lv_dropdown_create(page);
    lv_obj_set_width(dd, lv_pct(100));
    lv_dropdown_set_options(dd, "Beep\nPlaylist song (random)");
    lv_dropdown_set_selected(dd, cfg->alarm_mode);
    lv_obj_add_event_cb(dd, alarm_mode_cb, LV_EVENT_VALUE_CHANGED, NULL);

    char pl[96];
    snprintf(pl, sizeof(pl), "Wake playlist: %s",
             cfg->alarm_playlist_name[0] ? cfg->alarm_playlist_name : "(none)");
    s_alarm_pl_label = make_label(page, pl, COL_MUTED);
    /* playlist names come from the server: needs the Cyrillic/accent chain */
    lv_obj_set_style_text_font(s_alarm_pl_label, &app_font_20, 0);

    voice_actions_bind(VA_CHOOSE_PLAYLIST,
        make_button(page, "Choose playlist...", alarm_pl_fetch_cb, NULL));

    s_pl_list = lv_list_create(page);
    lv_obj_set_width(s_pl_list, lv_pct(100));
    lv_obj_set_style_text_font(s_pl_list, &app_font_14, 0);
    lv_obj_set_height(s_pl_list, 200);
    lv_obj_set_style_bg_color(s_pl_list, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_border_width(s_pl_list, 0, 0);
    voice_actions_set_option_list(s_pl_list);

    s_pl_status = make_label(page, "", COL_MUTED);
}

/* ---------- Timezone page ---------- */

/* setenv/tzset under the TZ mutex (and, first time, SNTP init) belong on
 * the worker, not the LVGL task */
static void tz_apply_job(void *arg)
{
    clock_sync_start(); /* re-applies TZ */
}

static void tz_roller_cb(lv_event_t *e)
{
    lv_obj_t *roller = lv_event_get_target(e);
    int idx = (int)lv_roller_get_selected(roller);
    settings_get()->tz_offset_min = (int16_t)((idx - 12) * 60);
    mark_dirty();
    if (!app_run_async(tz_apply_job, NULL) && s_side_note != NULL) {
        lv_label_set_text(s_side_note, "Busy - zone applies on Save");
    }
}

static void build_tz_page(lv_event_t *e)
{
    lv_obj_t *page = new_page("Timezone", false);
    make_label(page, "UTC offset (hours)", COL_MUTED);

    static char opts[27 * 8];
    if (opts[0] == '\0') {
        char *p = opts;
        for (int i = -12; i <= 14; i++) {
            p += sprintf(p, i < 14 ? "UTC%+d\n" : "UTC%+d", i);
        }
    }
    lv_obj_t *roller = lv_roller_create(page);
    lv_roller_set_options(roller, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller, 5);
    lv_obj_set_style_text_font(roller, &lv_font_montserrat_28, 0);
    int idx = settings_get()->tz_offset_min / 60 + 12;
    if (idx < 0) {
        idx = 0;
    }
    if (idx > 26) {
        idx = 26;
    }
    lv_roller_set_selected(roller, idx, LV_ANIM_OFF);
    lv_obj_add_event_cb(roller, tz_roller_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

/* ---------- Device Specifications page ---------- */

static void specs_run_job(void *arg)
{
    feature_specs_run();
}

static void specs_run_cb(lv_event_t *e)
{
    if (!app_run_async(specs_run_job, NULL)) {
        ui_log("Specs: worker busy - tap Refresh again");
    }
}

static void build_specs_page(lv_event_t *e)
{
    lv_obj_t *page = new_page("Device Specifications", false);
    voice_actions_bind(VA_REFRESH,
        make_button(page, "Refresh", specs_run_cb, NULL));

    lv_obj_t *cont = lv_obj_create(page);
    lv_obj_set_width(cont, lv_pct(100));
    lv_obj_set_flex_grow(cont, 1);
    lv_obj_set_height(cont, lv_pct(100)); /* was 480: the 4.3" height; follows the panel now */
    lv_obj_set_style_bg_color(cont, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);

    s_specs_label = lv_label_create(cont);
    lv_obj_set_width(s_specs_label, lv_pct(100));
    lv_label_set_long_mode(s_specs_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_specs_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_specs_label, lv_color_hex(COL_MUTED), 0);
    lv_label_set_text(s_specs_label, s_log_buf[0] ? s_log_buf : "Tap Refresh.");
}

/* ---------- menu page (with inline sliders) ---------- */

static void ui_settings_close(void)
{
    s_gen++;
    if (s_root != NULL) {
        lv_obj_delete(s_root);
        s_root = NULL;
        s_content = NULL;
        s_page = NULL;
        s_kb = NULL;
        s_side_note = NULL;
        s_specs_label = NULL;
        s_cal_label = NULL;
        s_btn_save = NULL;
        s_btn_pgup = NULL;
        s_btn_pgdn = NULL;
        s_commit = NULL;
        /* everything an async job might still target - stale pointers into
         * the deleted tree would be use-after-free */
        s_wifi_list = s_wifi_status = s_wifi_pass_ta = NULL;
        s_nav_status = s_pl_list = s_pl_status = NULL;
        s_nav_url_ta = s_nav_user_ta = s_nav_pass_ta = NULL;
        s_ai_url_ta = s_ai_key_ta = s_ai_model_ta = NULL;
        s_api_key_ta = s_api_enable_sw = NULL;
        s_alarm_pl_label = NULL;
        alarm_page_teardown();
        /* the playlist picker's backing data is only useful while open */
        if (s_pl_job != NULL) {
            if (s_pl_job->err == ESP_OK) {
                nav_free_playlists(&s_pl_job->pls);
            }
            heap_caps_free(s_pl_job);
            s_pl_job = NULL;
        }
    }
}

static void bright_min_cb(lv_event_t *e)
{
    int v = (int)lv_slider_get_value(lv_event_get_target(e));
    settings_get()->bright_min = (uint8_t)v;
    bsp_display_brightness_set(v); /* preview the floor */
    mark_dirty();
}

static void bright_max_cb(lv_event_t *e)
{
    int v = (int)lv_slider_get_value(lv_event_get_target(e));
    settings_get()->bright_max = (uint8_t)v;
    bsp_display_brightness_set(v); /* preview the ceiling */
    mark_dirty();
}

/* ambient.c reads the field on every 2 s tick, so it applies live */
static void ambient_period_cb(lv_event_t *e)
{
    int v = (int)lv_slider_get_value(lv_event_get_target(e));
    settings_get()->ambient_period_s = (uint16_t)v;
    mark_dirty();
}

static void volume_cb(lv_event_t *e)
{
    int v = (int)lv_slider_get_value(lv_event_get_target(e));
    settings_get()->volume = (uint8_t)v;
    /* LVGL task: never wait on the codec here (init mutex, or the decoder's
     * mutex while a song plays). The worker applies it; if the codec is not
     * up yet, its init applies the stored volume when it runs. */
    app_audio_set_volume_async(v);
    mark_dirty();
}

static void alarm_volume_cb(lv_event_t *e)
{
    int v = (int)lv_slider_get_value(lv_event_get_target(e));
    settings_get()->alarm_volume = (uint8_t)v;
    mark_dirty();
}

static void clock_face_cb(lv_event_t *e)
{
    settings_get()->clock_face = (uint8_t)lv_dropdown_get_selected(lv_event_get_target(e));
    mark_dirty();
    ui_clock_apply_face(); /* live - visible as soon as settings closes */
}

static void orientation_cb(lv_event_t *e)
{
    settings_get()->orientation =
        (uint8_t)lv_dropdown_get_selected(lv_event_get_target(e));
    mark_dirty();
    app_apply_orientation(); /* live - the whole UI reflows */
}

static lv_obj_t *make_slider_row(lv_obj_t *parent, const char *name, int min,
                                 int max, int value, lv_event_cb_t cb)
{
    make_label(parent, name, COL_MUTED);
    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_width(slider, lv_pct(96));
    lv_obj_set_height(slider, 16);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COL_BTN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COL_BTN), LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider, 20);
    lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, cb, LV_EVENT_RELEASED, NULL);
    return slider;
}

/* ---------- Volume page ---------- */

static void build_volume_page(lv_event_t *e)
{
    lv_obj_t *page = new_page("Volume", false);
    bedside_settings_t *cfg = settings_get();

    voice_actions_bind_slider(VA_VOL_UP, VA_VOL_DOWN,
        make_slider_row(page, "Volume", 0, 100, cfg->volume, volume_cb), 10);
    voice_actions_bind_slider(VA_AVOL_UP, VA_AVOL_DOWN,
        make_slider_row(page, "Alarm volume", 0, 100, cfg->alarm_volume,
                        alarm_volume_cb), 10);
}

/* ---------- Voice page ---------- */

static void voice_enable_cb(lv_event_t *e)
{
    bool en = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    settings_get()->voice_enabled = en ? 1 : 0;
    mark_dirty();
    voice_set_enabled(en);
}

/* I2C write under the codec's record mutex (up to 1 s if the feed task is
 * mid-read): worker, not LVGL task. A dropped job is harmless - the gain is
 * re-read from settings on every mic reclaim. */
static void voice_gain_job(void *arg)
{
    voice_apply_mic_gain();
}

static void voice_gain_cb(lv_event_t *e)
{
    int v = (int)lv_slider_get_value(lv_event_get_target(e));
    settings_get()->voice_mic_gain = (uint8_t)v;
    mark_dirty();
    app_run_async(voice_gain_job, NULL);
}

/* set_wakenet_threshold is a quick config write, but keep it off the LVGL
 * task like the gain (same worker pattern, same "dropped job is harmless":
 * voice_start re-reads the setting). */
static void voice_thr_job(void *arg)
{
    voice_apply_wake_threshold();
}

static void voice_thr_cb(lv_event_t *e)
{
    int v = (int)lv_slider_get_value(lv_event_get_target(e));
    settings_get()->voice_wake_thr = (uint8_t)v;
    mark_dirty();
    app_run_async(voice_thr_job, NULL);
}

static void voice_verify_cb(lv_event_t *e)
{
    bool en = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    settings_get()->voice_wake_verify = en ? 1 : 0;
    mark_dirty(); /* read by voice.c at the next wake */
}

/* dropdown index == registry index (wake_mww.h); the id is what persists */
static void wake_word_cb(lv_event_t *e)
{
    int i = (int)lv_dropdown_get_selected(lv_event_get_target(e));
    if (i < 0 || i >= wake_mww_model_count()) {
        return;
    }
    bedside_settings_t *cfg = settings_get();
    strlcpy(cfg->voice_wake_word, wake_mww_model_id(i), sizeof(cfg->voice_wake_word));
    mark_dirty();
    voice_apply_wake_word(); /* a single store; the detect task swaps models */
}

/* "label ........ [switch]" row */
static lv_obj_t *voice_switch_row(lv_obj_t *page, const char *text,
                                  bool checked, lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(page);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
    lv_obj_t *sw = lv_switch_create(row);
    if (checked) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sw;
}

static void build_voice_page(lv_event_t *e)
{
    lv_obj_t *page = new_page("Voice control", false);
    bedside_settings_t *cfg = settings_get();

    int wake_idx = wake_mww_model_index(cfg->voice_wake_word);
    if (wake_idx < 0) {
        wake_idx = 0; /* empty/unknown id: the engine uses the first model */
    }
    const char *wake = wake_mww_model_name(wake_idx);

    lv_label_set_text_fmt(make_label(page, "", COL_MUTED),
                          "Say \"%s\", wait for the green border, then speak "
                          "a command: \"open alarm\", \"volume up\", \"go back\", "
                          "\"option one\", \"snooze\"...", wake);

    voice_switch_row(page, "Voice control enabled", cfg->voice_enabled != 0,
                     voice_enable_cb);

    /* one entry per embedded model (BEDSIDE_WAKE_MODELS in main/CMakeLists.txt) */
    make_label(page, "Wake word", COL_MUTED);
    lv_obj_t *dd = lv_dropdown_create(page);
    lv_obj_set_width(dd, lv_pct(100));
    lv_dropdown_clear_options(dd);
    for (int i = 0; i < wake_mww_model_count(); i++) {
        lv_dropdown_add_option(dd, wake_mww_model_name(i), LV_DROPDOWN_POS_LAST);
    }
    lv_dropdown_set_selected(dd, (uint32_t)wake_idx);
    lv_obj_add_event_cb(dd, wake_word_cb, LV_EVENT_VALUE_CHANGED, NULL);

    make_slider_row(page, "Mic gain (dB, steps of 6)", 0, 42,
                    cfg->voice_mic_gain, voice_gain_cb);

    /* lower = wakes more easily (and more falsely); 50 is the tuned value */
    make_slider_row(page, "Wake threshold (%, lower = more sensitive)",
                    VOICE_WAKE_THR_MIN, VOICE_WAKE_THR_MAX,
                    cfg->voice_wake_thr, voice_thr_cb);

    voice_switch_row(page, "AI server confirms each wake",
                     cfg->voice_wake_verify != 0, voice_verify_cb);
    lv_label_set_text_fmt(make_label(page, "", COL_MUTED),
                          "With this on, the 2 s of audio around every \"%s\" "
                          "is transcribed by the AI server and the wake is ignored "
                          "when it did not contain the phrase. Needs the AI server; "
                          "commands still work if it is slow or down.", wake);

    /* Every phrase the recogniser accepts, read from the same table voice.c
     * loads into MultiNet, so this list cannot drift from what works.
     * Phrases sharing a command id (synonyms) share a line. One label: the
     * text is a few KB, built in PSRAM and copied by LVGL. */
    lv_label_set_text_fmt(make_label(page, "", COL_MUTED),
                          "Commands - say \"%s\", wait for the border, then one of:",
                          wake);
    int n_ph = 0;
    const va_phrase_t *ph = voice_actions_phrases(&n_ph);
    size_t cap = 64 + (size_t)n_ph * 48;
    char *txt = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (txt != NULL) {
        size_t len = 0;
        for (int i = 0; i < n_ph; i++) {
            bool same = i > 0 && ph[i].cmd == ph[i - 1].cmd;
            int w = snprintf(txt + len, cap - len, "%s%s",
                             same ? "  /  " : (i > 0 ? "\n" : ""), ph[i].phrase);
            if (w < 0 || (size_t)w >= cap - len) {
                break;
            }
            len += (size_t)w;
        }
        lv_obj_t *cmds = make_label(page, txt, COL_TEXT);
        lv_obj_set_style_text_font(cmds, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_line_space(cmds, 8, 0);
        heap_caps_free(txt);
    }
}

/* ---------- Clock Face page ---------- */

static void build_clockface_page(lv_event_t *e)
{
    lv_obj_t *page = new_page("Clock face", false);
    bedside_settings_t *cfg = settings_get();

    make_label(page, "Clock face", COL_MUTED);
    lv_obj_t *dd = lv_dropdown_create(page);
    lv_obj_set_width(dd, lv_pct(100));
    lv_dropdown_set_options(dd, "Large digital\nGiant digital");
    lv_dropdown_set_selected(dd, cfg->clock_face);
    lv_obj_add_event_cb(dd, clock_face_cb, LV_EVENT_VALUE_CHANGED, NULL);
    voice_actions_bind_dropdown(VA_FACE_SMALL, dd, 0);
    voice_actions_bind_dropdown(VA_FACE_BIG, dd, 1);

    make_label(page, "Orientation", COL_MUTED);
    lv_obj_t *od = lv_dropdown_create(page);
    lv_obj_set_width(od, lv_pct(100));
    lv_dropdown_set_options(od, "Landscape\nLandscape (flipped)\nPortrait");
    lv_dropdown_set_selected(od, cfg->orientation);
    lv_obj_add_event_cb(od, orientation_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

/* ---------- Brightness page ---------- */

static void brightness_cal_label_refresh(void)
{
    if (s_cal_label == NULL) {
        return;
    }
    uint8_t floor, ceil;
    ambient_get_calibration(&floor, &ceil);
    if ((int)ceil - (int)floor >= 16) {
        lv_label_set_text_fmt(s_cal_label,
                              "Measured room light: floor %u, ceiling %u "
                              "(last sample %u)",
                              floor, ceil, ambient_get_last_luma());
    } else {
        lv_label_set_text(s_cal_label, "Room light range not yet calibrated");
    }
}

/* ambient_reset_calibration() persists to NVS (flash write): worker only. */
static void reset_cal_job(void *arg)
{
    uint32_t gen = (uint32_t)(uintptr_t)arg;
    ambient_reset_calibration();
    if (bsp_display_lock(1000)) {
        if (gen == s_gen) {
            brightness_cal_label_refresh();
            if (s_side_note != NULL) {
                lv_label_set_text(s_side_note, "Calibration reset");
            }
        }
        bsp_display_unlock();
    }
}

static void reset_cal_cb(lv_event_t *e)
{
    if (s_side_note != NULL) {
        lv_label_set_text(s_side_note, "Resetting...");
    }
    if (!app_run_async(reset_cal_job, (void *)(uintptr_t)s_gen)) {
        /* never inline: NVS write on the LVGL (or voice, PSRAM) stack */
        if (s_side_note != NULL) {
            lv_label_set_text(s_side_note, "Busy - try again");
        }
    }
}

static void build_brightness_page(lv_event_t *e)
{
    lv_obj_t *page = new_page("Brightness", false);
    bedside_settings_t *cfg = settings_get();

    voice_actions_bind_slider(VA_NIGHT_UP, VA_NIGHT_DOWN,
        make_slider_row(page, "Min brightness (night)", 1, 100, cfg->bright_min,
                        bright_min_cb), 10);
    voice_actions_bind_slider(VA_BRIGHT_UP, VA_BRIGHT_DOWN,
        make_slider_row(page, "Max brightness (day)", 1, 100, cfg->bright_max,
                        bright_max_cb), 10);
    make_slider_row(page, "Light sample interval (seconds)",
                    AMBIENT_PERIOD_MIN_S, AMBIENT_PERIOD_MAX_S,
                    cfg->ambient_period_s, ambient_period_cb);

    s_cal_label = make_label(page, "", COL_MUTED);
    brightness_cal_label_refresh();

    voice_actions_bind(VA_RESET_CAL,
        make_button(page, LV_SYMBOL_REFRESH "  Reset calibration",
                    reset_cal_cb, NULL));
}

static void build_menu_page(void)
{
    lv_obj_t *page = new_page("Settings", true);

    /* the menu is a wrapping 3-wide tile grid; every other page keeps the
     * plain column flow from new_page() */
    ui_grid_config(page);
    lv_obj_set_width(lv_obj_get_child(page, 0), lv_pct(100)); /* title row */

    /* Tile order = how often each page is opened (3 per row). Everyday
     * first, one-off setup last. The voice page ids in
     * ui_settings_voice_open_page() are independent of this order. */

    /* row 1: used most days */
    make_tile(page, LV_SYMBOL_BELL, "Alarms", build_alarm_page);
    make_tile(page, LV_SYMBOL_VOLUME_MID, "Alarm sound", build_alarm_sound_page);
    /* Same glyphs as the clock face's service indicators, so the tile and the
     * thing it configures are recognisably the same feature. */
    ui_grid_tile_font(page, ICON_MIC, &app_icons_28, "Voice",
                      build_voice_page, NULL);

    /* row 2: comfort tweaks */
    make_tile(page, LV_SYMBOL_VOLUME_MAX, "Volume", build_volume_page);
    make_tile(page, LV_SYMBOL_EYE_OPEN, "Brightness", build_brightness_page);
    make_tile(page, LV_SYMBOL_IMAGE, "Clock Face", build_clockface_page);

    /* row 3: diagnostics, then setup begins */
    make_tile(page, LV_SYMBOL_LIST, "Specifications", build_specs_page);
    make_tile(page, LV_SYMBOL_WIFI, "WiFi", build_wifi_page);
    make_tile(page, LV_SYMBOL_GPS, "Timezone", build_tz_page);

    /* row 4: one-off server setup, in first-boot order */
    make_tile(page, LV_SYMBOL_AUDIO, "Music API", build_nav_page);
    ui_grid_tile_font(page, ICON_CHIP, &app_icons_28, "AI",
                      build_ai_page, NULL);
    make_tile(page, LV_SYMBOL_UPLOAD, "API", build_api_page);
}

bool ui_settings_is_open(void)
{
    return s_root != NULL;
}

void ui_settings_open(void)
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

    /* fixed sidebar: Back + Save always reachable */
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

    lv_obj_t *back = lv_button_create(side);
    lv_obj_set_size(back, lv_pct(100), 96);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x4A4F55), 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT "\nBack");
    lv_obj_set_style_text_align(bl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_20, 0);
    lv_obj_center(bl);

    s_btn_save = lv_button_create(side);
    lv_obj_set_size(s_btn_save, lv_pct(100), 96);
    lv_obj_add_event_cb(s_btn_save, save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(s_btn_save);
    lv_label_set_text(sl, LV_SYMBOL_SAVE "\nSave");
    lv_obj_set_style_text_align(sl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_20, 0);
    lv_obj_center(sl);
    /* greyed until a control marks the settings dirty */
    sidebar_btn_enable(s_btn_save, s_dirty, COL_BTN);

    s_btn_pgup = lv_button_create(side);
    lv_obj_set_size(s_btn_pgup, lv_pct(100), 96);
    lv_obj_add_event_cb(s_btn_pgup, page_up_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *upl = lv_label_create(s_btn_pgup);
    lv_label_set_text(upl, LV_SYMBOL_UP "\nPage");
    lv_obj_set_style_text_align(upl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(upl, &lv_font_montserrat_20, 0);
    lv_obj_center(upl);
    sidebar_btn_enable(s_btn_pgup, false, COL_PGBTN);

    s_btn_pgdn = lv_button_create(side);
    lv_obj_set_size(s_btn_pgdn, lv_pct(100), 96);
    lv_obj_add_event_cb(s_btn_pgdn, page_down_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dnl = lv_label_create(s_btn_pgdn);
    lv_label_set_text(dnl, LV_SYMBOL_DOWN "\nPage");
    lv_obj_set_style_text_align(dnl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(dnl, &lv_font_montserrat_20, 0);
    lv_obj_center(dnl);
    sidebar_btn_enable(s_btn_pgdn, false, COL_PGBTN);

    s_side_note = lv_label_create(side);
    lv_label_set_text(s_side_note, "");
    lv_obj_set_style_text_font(s_side_note, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_side_note, lv_color_hex(COL_GOOD), 0);

    /* content column (pages build inside this) */
    s_content = lv_obj_create(s_root);
    lv_obj_set_flex_grow(s_content, 1);
    lv_obj_set_height(s_content, lv_pct(100));
    lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, 0, 0);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);

    build_menu_page();

    /* shared keyboard floats above the row layout */
    s_kb = lv_keyboard_create(s_root);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
}

/* ---------- voice entry points (caller holds the LVGL lock) ----------
 * The build_* callbacks and sidebar callbacks all ignore their lv_event_t,
 * so voice reuses them directly: identical behavior to a touch. */

void ui_settings_voice_open_page(int page_id)
{
    static void (*const builders[])(lv_event_t *) = {
        build_volume_page,     /* 0 */
        build_wifi_page,       /* 1 */
        build_tz_page,         /* 2 */
        build_nav_page,        /* 3 */
        build_alarm_page,      /* 4 */
        build_clockface_page,  /* 5 */
        build_brightness_page, /* 6 */
        build_specs_page,      /* 7 */
        build_voice_page,      /* 8 */
        build_alarm_sound_page,/* 9 - index = VA_OPEN_* - VA_OPEN_VOLUME */
    };
    if (s_root != NULL && page_id >= 0 &&
        page_id < (int)(sizeof(builders) / sizeof(builders[0]))) {
        builders[page_id](NULL);
    }
}

void ui_settings_voice_back(void)
{
    back_cb(NULL);
}

void ui_settings_voice_save(void)
{
    save_cb(NULL);
}

void ui_settings_voice_page(bool up)
{
    if (up) {
        page_up_cb(NULL);
    } else {
        page_down_cb(NULL);
    }
}

/* ---------- ui_log (serial + Device Specifications page) ---------- */

void ui_log(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line) - 2, fmt, ap);
    va_end(ap);

    ESP_LOGI(TAG, "%s", line);
    strcat(line, "\n");

    /* s_log_buf is appended from many tasks; the display lock (recursive,
     * safe from LVGL callbacks too) serializes the mutation. On timeout the
     * line is dropped from the page only - it is already on the serial log.
     * No display at all: serial is the only log, nothing more to do. */
    if (!app_display_ready() || !bsp_display_lock(1000)) {
        return;
    }
    size_t add = strlen(line);
    size_t cur = strlen(s_log_buf);
    if (cur + add >= LOG_BUF_CAP) {
        const char *keep = s_log_buf + (cur / 2);
        const char *nl = strchr(keep, '\n');
        keep = nl != NULL ? nl + 1 : keep;
        memmove(s_log_buf, keep, strlen(keep) + 1);
        cur = strlen(s_log_buf);
    }
    memcpy(s_log_buf + cur, line, add + 1);

    if (s_specs_label != NULL) {
        lv_label_set_text(s_specs_label, s_log_buf);
    }
    bsp_display_unlock();
}

size_t ui_log_copy(char *dst, size_t cap)
{
    if (dst == NULL || cap == 0) {
        return 0;
    }
    dst[0] = '\0';
    if (!app_display_ready() || !bsp_display_lock(1000)) {
        return 0;
    }
    size_t cur = strlen(s_log_buf);
    const char *src = s_log_buf;
    if (cur >= cap) { /* keep the newest lines, starting on a line boundary */
        src = s_log_buf + (cur - (cap - 1));
        const char *nl = strchr(src, '\n');
        if (nl != NULL) {
            src = nl + 1;
        }
        cur = strlen(src);
    }
    memcpy(dst, src, cur + 1);
    bsp_display_unlock();
    return cur;
}

/* Append a whole \n-separated block in ONE locked pass with ONE label
 * re-layout. The spec sheet prints ~75 lines; doing that through ui_log()
 * means ~75 full text-wrap passes over a growing 16 KB label and seconds
 * of UI freeze. */
void ui_log_bulk(const char *text)
{
    printf("%s", text); /* serial gets the block form */

    size_t add = strlen(text);
    if (add == 0) {
        return;
    }
    if (add >= LOG_BUF_CAP - 1) { /* keep the tail of an oversized block */
        text += add - (LOG_BUF_CAP - 2);
        add = LOG_BUF_CAP - 2;
    }
    if (!app_display_ready() || !bsp_display_lock(1000)) {
        return;
    }
    size_t cur = strlen(s_log_buf);
    if (cur + add >= LOG_BUF_CAP) {
        size_t need = cur + add + 1 - LOG_BUF_CAP;
        const char *keep = s_log_buf + need;
        const char *nl = strchr(keep, '\n');
        keep = nl != NULL ? nl + 1 : keep;
        memmove(s_log_buf, keep, strlen(keep) + 1);
        cur = strlen(s_log_buf);
        if (cur + add >= LOG_BUF_CAP) { /* still too big: start fresh */
            s_log_buf[0] = '\0';
            cur = 0;
        }
    }
    memcpy(s_log_buf + cur, text, add + 1);
    if (s_specs_label != NULL) {
        lv_label_set_text(s_specs_label, s_log_buf);
    }
    bsp_display_unlock();
}
