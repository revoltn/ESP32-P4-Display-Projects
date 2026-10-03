#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Voice command ids double as MultiNet command ids (0 is invalid there,
 * so the enum starts at 1). */
typedef enum {
    VA_NONE = 0,
    /* plain-function actions (no widget) */
    VA_SNOOZE,
    VA_DISMISS,
    VA_OPEN_SETTINGS,
    VA_GO_BACK,
    VA_SAVE,
    VA_PAGE_UP,
    VA_PAGE_DOWN,
    VA_OPEN_VOLUME,
    VA_OPEN_WIFI,
    VA_OPEN_TZ,
    VA_OPEN_MUSIC,
    VA_OPEN_ALARM,
    VA_OPEN_FACE,
    VA_OPEN_BRIGHT,
    VA_OPEN_SPECS,
    VA_OPEN_VOICE,
    VA_OPEN_ALARM_SOUND,   /* keep the VA_OPEN_* block contiguous: the page
                            * index is (cmd - VA_OPEN_VOLUME) */
    VA_PLAY_RANDOM,
    VA_STOP_MUSIC,
    /* handled inside voice.c: these open a free-speech capture window and
     * route the transcript to music_player (never reach dispatch) */
    VA_PLAY_SONG,
    VA_PLAY_PLAYLIST,
    /* widget actions (bound by the UI at widget creation) */
    VA_SCAN,
    VA_CONNECT,
    VA_SAVE_TEST,
    VA_ALARM_ON,
    VA_ALARM_OFF,
    VA_SET_TIME,
    VA_CHOOSE_PLAYLIST,
    VA_HOUR_UP,
    VA_HOUR_DOWN,
    VA_MIN_UP,
    VA_MIN_DOWN,
    VA_VOL_UP,
    VA_VOL_DOWN,
    VA_AVOL_UP,
    VA_AVOL_DOWN,
    VA_BRIGHT_UP,
    VA_BRIGHT_DOWN,
    VA_NIGHT_UP,
    VA_NIGHT_DOWN,
    VA_FACE_BIG,
    VA_FACE_SMALL,
    VA_RESET_CAL,
    VA_REFRESH,
    /* numbered rows of the currently bound list (wifi scan / playlists) */
    VA_OPT_1, VA_OPT_2, VA_OPT_3, VA_OPT_4, VA_OPT_5,
    VA_OPT_6, VA_OPT_7, VA_OPT_8, VA_OPT_9, VA_OPT_10,
    /* Alarm page: pick which of the three alarms the editor targets, and
     * the weekday presets of the one being edited */
    VA_SELECT_ALARM_1,
    VA_SELECT_ALARM_2,
    VA_SELECT_ALARM_3,
    VA_DAYS_ALL,
    VA_DAYS_WEEKDAYS,
    VA_DAYS_WEEKENDS,
    VA_DAYS_ONCE,
    VA_CMD_MAX
} va_cmd_t;

/* --- called by UI code on the LVGL task at widget creation ---
 * A bound widget auto-unbinds when it is deleted (page teardown). */

/* Click `target` when `cmd` is heard. */
void voice_actions_bind(va_cmd_t cmd, lv_obj_t *target);

/* Nudge a slider by +/-step (sends VALUE_CHANGED then RELEASED). */
void voice_actions_bind_slider(va_cmd_t up_cmd, va_cmd_t down_cmd,
                               lv_obj_t *slider, int step);

/* Set a switch on/off (sends VALUE_CHANGED). */
void voice_actions_bind_switch(va_cmd_t on_cmd, va_cmd_t off_cmd,
                               lv_obj_t *sw);

/* Select a dropdown option (sends VALUE_CHANGED). */
void voice_actions_bind_dropdown(va_cmd_t cmd, lv_obj_t *dd, int option);

/* Nudge a roller selection by +/-1 (sends VALUE_CHANGED). */
void voice_actions_bind_roller(va_cmd_t up_cmd, va_cmd_t down_cmd,
                               lv_obj_t *roller);

/* "option one".."option ten" click the Nth clickable row of this list. */
void voice_actions_set_option_list(lv_obj_t *list);

/* --- called by voice.c --- */

/* Phrase table for esp_mn_commands_add(); several phrases may share an id. */
typedef struct {
    va_cmd_t cmd;
    const char *phrase;
} va_phrase_t;
const va_phrase_t *voice_actions_phrases(int *count);

/* Execute a recognized command (called from the voice task; takes the LVGL
 * lock internally where needed).  Writes user feedback into fb.  Returns
 * true if the command did something. */
bool voice_actions_dispatch(int command_id, char *fb, size_t cap);

#ifdef __cplusplus
}
#endif
