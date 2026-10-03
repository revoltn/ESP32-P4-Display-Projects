/* voice_actions.c - central voice-command registry and dispatch.
 *
 * Widget actions synthesize LVGL events on the REAL widgets under
 * bsp_display_lock(), so the existing callbacks stay the single source of
 * truth.  Widgets are bound at creation (LVGL task) and auto-unbind via an
 * LV_EVENT_DELETE hook, so page teardown needs no extra bookkeeping.
 * Navigation and snooze/dismiss are plain functions (see ui_settings.h /
 * alarm.h voice entry points).  Dispatch runs on the voice task. */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"

#include "app_common.h"
#include "ui_settings.h"
#include "ui_play.h"
#include "music_player.h"
#include "alarm.h"
#include "voice_actions.h"

static const char *TAG = "voice_act";

typedef enum {
    K_UNBOUND = 0,
    K_CLICK,      /* click the widget */
    K_SLIDER,     /* nudge slider by param */
    K_SWITCH,     /* set checked = param, send VALUE_CHANGED */
    K_DROPDOWN,   /* select option param, send VALUE_CHANGED */
    K_ROLLER,     /* nudge selection by param */
} va_kind_t;

static lv_obj_t *s_obj[VA_CMD_MAX];
static int8_t    s_kind[VA_CMD_MAX];
static int16_t   s_param[VA_CMD_MAX];
static lv_obj_t *s_option_list;

/* ---------- phrase table (lowercase; g2p runs on-device) ---------- */

static const va_phrase_t s_phrases[] = {
    { VA_SNOOZE,          "snooze" },
    { VA_SNOOZE,          "snooze alarm" },
    { VA_DISMISS,         "stop alarm" },
    { VA_DISMISS,         "dismiss alarm" },
    { VA_OPEN_SETTINGS,   "open settings" },
    { VA_GO_BACK,         "go back" },
    { VA_SAVE,            "save settings" },
    { VA_PAGE_UP,         "page up" },
    { VA_PAGE_DOWN,       "page down" },
    { VA_OPEN_VOLUME,     "open volume" },
    { VA_OPEN_WIFI,       "open wifi" },
    { VA_OPEN_WIFI,       "open network" },
    { VA_OPEN_TZ,         "open time zone" },
    { VA_OPEN_MUSIC,      "open music" },
    { VA_OPEN_ALARM,      "open alarm" },
    { VA_OPEN_FACE,       "open clock face" },
    { VA_OPEN_BRIGHT,     "open brightness" },
    { VA_OPEN_SPECS,      "open specifications" },
    { VA_OPEN_SPECS,      "device info" },
    { VA_OPEN_VOICE,      "open voice control" },
    { VA_OPEN_ALARM_SOUND, "open alarm sound" },
    { VA_PLAY_SONG,       "play song" },
    { VA_PLAY_SONG,       "play a song" },
    { VA_PLAY_PLAYLIST,   "playlist" },
    { VA_PLAY_PLAYLIST,   "play playlist" },
    { VA_PLAY_RANDOM,     "play random" },
    { VA_PLAY_RANDOM,     "play random music" },
    { VA_STOP_MUSIC,      "stop music" },
    { VA_STOP_MUSIC,      "stop playing" },
    { VA_SCAN,            "scan networks" },
    { VA_CONNECT,         "connect now" },
    { VA_SAVE_TEST,       "test server" },
    { VA_ALARM_ON,        "alarm on" },
    { VA_ALARM_ON,        "turn alarm on" },
    { VA_ALARM_OFF,       "alarm off" },
    { VA_ALARM_OFF,       "turn alarm off" },
    { VA_CHOOSE_PLAYLIST, "choose playlist" },
    { VA_HOUR_UP,         "hour up" },
    { VA_HOUR_DOWN,       "hour down" },
    { VA_MIN_UP,          "minute up" },
    { VA_MIN_DOWN,        "minute down" },
    { VA_VOL_UP,          "volume up" },
    { VA_VOL_DOWN,        "volume down" },
    { VA_AVOL_UP,         "alarm volume up" },
    { VA_AVOL_DOWN,       "alarm volume down" },
    { VA_BRIGHT_UP,       "brightness up" },
    { VA_BRIGHT_DOWN,     "brightness down" },
    { VA_NIGHT_UP,        "night brightness up" },
    { VA_NIGHT_DOWN,      "night brightness down" },
    { VA_FACE_BIG,        "giant clock" },
    { VA_FACE_SMALL,      "large clock" },
    { VA_RESET_CAL,       "reset calibration" },
    { VA_REFRESH,         "refresh page" },
    { VA_OPT_1,           "option one" },
    { VA_OPT_2,           "option two" },
    { VA_OPT_3,           "option three" },
    { VA_OPT_4,           "option four" },
    { VA_OPT_5,           "option five" },
    { VA_OPT_6,           "option six" },
    { VA_OPT_7,           "option seven" },
    { VA_OPT_8,           "option eight" },
    { VA_OPT_9,           "option nine" },
    { VA_OPT_10,          "option ten" },
    /* "first alarm", not "alarm one": one phoneme away from "alarm on" */
    { VA_SELECT_ALARM_1,  "first alarm" },
    { VA_SELECT_ALARM_2,  "second alarm" },
    { VA_SELECT_ALARM_3,  "third alarm" },
    { VA_DAYS_ALL,        "every day" },
    { VA_DAYS_WEEKDAYS,   "weekdays" },
    { VA_DAYS_WEEKENDS,   "weekends" },
    { VA_DAYS_ONCE,       "just once" },
};

const va_phrase_t *voice_actions_phrases(int *count)
{
    *count = (int)(sizeof(s_phrases) / sizeof(s_phrases[0]));
    return s_phrases;
}

/* short label used for "heard" feedback */
static const char *va_label(va_cmd_t cmd)
{
    for (size_t i = 0; i < sizeof(s_phrases) / sizeof(s_phrases[0]); i++) {
        if (s_phrases[i].cmd == cmd) {
            return s_phrases[i].phrase;
        }
    }
    return "?";
}

/* ---------- binding (LVGL task) ---------- */

static void unbind_on_delete_cb(lv_event_t *e)
{
    va_cmd_t cmd = (va_cmd_t)(uintptr_t)lv_event_get_user_data(e);
    if (cmd > VA_NONE && cmd < VA_CMD_MAX &&
        s_obj[cmd] == lv_event_get_target(e)) {
        s_obj[cmd] = NULL;
        s_kind[cmd] = K_UNBOUND;
    }
}

static void bind(va_cmd_t cmd, lv_obj_t *obj, va_kind_t kind, int param)
{
    if (cmd <= VA_NONE || cmd >= VA_CMD_MAX || obj == NULL) {
        return;
    }
    s_kind[cmd] = (int8_t)kind;
    s_param[cmd] = (int16_t)param;
    if (s_obj[cmd] == obj) {
        return; /* re-bound to the same widget (e.g. the Alarm page moving a
                 * command between alarms and back): no second delete hook */
    }
    s_obj[cmd] = obj;
    /* a delete hook left on a previously bound widget is harmless: it only
     * clears the slot if that widget is still the bound one */
    lv_obj_add_event_cb(obj, unbind_on_delete_cb, LV_EVENT_DELETE,
                        (void *)(uintptr_t)cmd);
}

void voice_actions_bind(va_cmd_t cmd, lv_obj_t *target)
{
    bind(cmd, target, K_CLICK, 0);
}

void voice_actions_bind_slider(va_cmd_t up_cmd, va_cmd_t down_cmd,
                               lv_obj_t *slider, int step)
{
    bind(up_cmd, slider, K_SLIDER, step);
    bind(down_cmd, slider, K_SLIDER, -step);
}

void voice_actions_bind_switch(va_cmd_t on_cmd, va_cmd_t off_cmd, lv_obj_t *sw)
{
    bind(on_cmd, sw, K_SWITCH, 1);
    bind(off_cmd, sw, K_SWITCH, 0);
}

void voice_actions_bind_dropdown(va_cmd_t cmd, lv_obj_t *dd, int option)
{
    bind(cmd, dd, K_DROPDOWN, option);
}

void voice_actions_bind_roller(va_cmd_t up_cmd, va_cmd_t down_cmd,
                               lv_obj_t *roller)
{
    bind(up_cmd, roller, K_ROLLER, 1);
    bind(down_cmd, roller, K_ROLLER, -1);
}

static void option_list_delete_cb(lv_event_t *e)
{
    if (s_option_list == lv_event_get_target(e)) {
        s_option_list = NULL;
    }
}

void voice_actions_set_option_list(lv_obj_t *list)
{
    s_option_list = list;
    if (list != NULL) {
        lv_obj_add_event_cb(list, option_list_delete_cb, LV_EVENT_DELETE, NULL);
    }
}

/* ---------- dispatch (voice task) ---------- */

static bool click_option_row(int idx, char *fb, size_t cap)
{
    if (s_option_list == NULL) {
        snprintf(fb, cap, "No list on screen");
        return false;
    }
    int seen = 0;
    uint32_t n = lv_obj_get_child_count(s_option_list);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(s_option_list, (int32_t)i);
        if (!lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE)) {
            continue; /* lv_list_add_text rows are plain labels */
        }
        if (seen++ == idx) {
            lv_obj_send_event(child, LV_EVENT_CLICKED, NULL);
            snprintf(fb, cap, "Option %d", idx + 1);
            return true;
        }
    }
    snprintf(fb, cap, "Only %d options", seen);
    return false;
}

/* navigation / plain-function commands; caller holds the LVGL lock */
static bool dispatch_fn_locked(va_cmd_t cmd, char *fb, size_t cap)
{
    switch (cmd) {
    case VA_OPEN_SETTINGS:
        ui_settings_open();
        snprintf(fb, cap, "Settings");
        return true;
    case VA_GO_BACK:
        if (!ui_settings_is_open()) {
            snprintf(fb, cap, "Nothing to go back from");
            return false;
        }
        ui_settings_voice_back();
        snprintf(fb, cap, "Back");
        return true;
    case VA_SAVE:
        if (!ui_settings_is_open()) {
            snprintf(fb, cap, "Settings not open");
            return false;
        }
        ui_settings_voice_save();
        snprintf(fb, cap, "Saved");
        return true;
    case VA_PAGE_UP:
    case VA_PAGE_DOWN:
        if (!ui_settings_is_open()) {
            snprintf(fb, cap, "Settings not open");
            return false;
        }
        ui_settings_voice_page(cmd == VA_PAGE_UP);
        snprintf(fb, cap, cmd == VA_PAGE_UP ? "Page up" : "Page down");
        return true;
    case VA_OPEN_VOLUME:
    case VA_OPEN_WIFI:
    case VA_OPEN_TZ:
    case VA_OPEN_MUSIC:
    case VA_OPEN_ALARM:
    case VA_OPEN_FACE:
    case VA_OPEN_BRIGHT:
    case VA_OPEN_SPECS:
    case VA_OPEN_VOICE:
    case VA_OPEN_ALARM_SOUND:
        if (!ui_settings_is_open()) {
            ui_settings_open();
        }
        ui_settings_voice_open_page((int)(cmd - VA_OPEN_VOLUME));
        snprintf(fb, cap, "%s", va_label(cmd));
        return true;
    case VA_PLAY_RANDOM:
        ui_play_open();
        music_play_random();
        snprintf(fb, cap, "Random play");
        return true;
    case VA_STOP_MUSIC:
        music_stop();
        snprintf(fb, cap, "Music stopped");
        return true;
    default:
        return false;
    }
}

bool voice_actions_dispatch(int command_id, char *fb, size_t cap)
{
    va_cmd_t cmd = (va_cmd_t)command_id;
    if (cmd <= VA_NONE || cmd >= VA_CMD_MAX) {
        snprintf(fb, cap, "?");
        return false;
    }

    /* alarm overlay: never touches LVGL, works even mid-render */
    if (cmd == VA_SNOOZE || cmd == VA_DISMISS) {
        if (!alarm_is_active()) {
            snprintf(fb, cap, "No alarm ringing");
            return false;
        }
        if (cmd == VA_SNOOZE) {
            alarm_voice_snooze();
            snprintf(fb, cap, "Snoozed");
        } else {
            alarm_voice_dismiss();
            snprintf(fb, cap, "Alarm dismissed");
        }
        return true;
    }

    if (!app_display_ready() || !bsp_display_lock(1000)) {
        snprintf(fb, cap, app_display_ready() ? "UI busy" : "No display");
        return false;
    }

    bool ok;
    if (cmd >= VA_OPT_1 && cmd <= VA_OPT_10) {
        ok = click_option_row((int)(cmd - VA_OPT_1), fb, cap);
    } else if (cmd <= VA_STOP_MUSIC) {
        ok = dispatch_fn_locked(cmd, fb, cap);
    } else {
        lv_obj_t *obj = s_obj[cmd];
        if (obj == NULL) {
            snprintf(fb, cap, "Not on this screen");
            ok = false;
        } else {
            ok = true;
            switch (s_kind[cmd]) {
            case K_CLICK:
                lv_obj_send_event(obj, LV_EVENT_CLICKED, NULL);
                snprintf(fb, cap, "%s", va_label(cmd));
                break;
            case K_SLIDER: {
                int32_t v = lv_slider_get_value(obj) + s_param[cmd];
                int32_t lo = lv_slider_get_min_value(obj);
                int32_t hi = lv_slider_get_max_value(obj);
                if (v < lo) v = lo;
                if (v > hi) v = hi;
                lv_slider_set_value(obj, v, LV_ANIM_ON);
                lv_obj_send_event(obj, LV_EVENT_VALUE_CHANGED, NULL);
                lv_obj_send_event(obj, LV_EVENT_RELEASED, NULL);
                snprintf(fb, cap, "%s: %d", va_label(cmd), (int)v);
                break;
            }
            case K_SWITCH:
                if (s_param[cmd]) {
                    lv_obj_add_state(obj, LV_STATE_CHECKED);
                } else {
                    lv_obj_remove_state(obj, LV_STATE_CHECKED);
                }
                lv_obj_send_event(obj, LV_EVENT_VALUE_CHANGED, NULL);
                snprintf(fb, cap, "%s", va_label(cmd));
                break;
            case K_DROPDOWN:
                lv_dropdown_set_selected(obj, (uint32_t)s_param[cmd]);
                lv_obj_send_event(obj, LV_EVENT_VALUE_CHANGED, NULL);
                snprintf(fb, cap, "%s", va_label(cmd));
                break;
            case K_ROLLER: {
                int32_t sel = (int32_t)lv_roller_get_selected(obj) + s_param[cmd];
                int32_t n = (int32_t)lv_roller_get_option_count(obj);
                if (n > 0) {
                    sel = (sel % n + n) % n; /* wrap */
                    lv_roller_set_selected(obj, (uint32_t)sel, LV_ANIM_ON);
                    lv_obj_send_event(obj, LV_EVENT_VALUE_CHANGED, NULL);
                }
                snprintf(fb, cap, "%s", va_label(cmd));
                break;
            }
            default:
                snprintf(fb, cap, "Not available");
                ok = false;
                break;
            }
        }
    }
    bsp_display_unlock();
    if (!ok) {
        ESP_LOGW(TAG, "cmd %d (%s): %s", command_id, va_label(cmd), fb);
    }
    return ok;
}
