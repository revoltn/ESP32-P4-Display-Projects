#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Play screen (ui_play.c): music overlay opened from the clock's play
 * button. Song / Playlist / Random tiles; resolving and playback run on
 * the music task (music_player.h). Call on the LVGL task. */
void ui_play_open(void);
bool ui_play_is_open(void);

#ifdef __cplusplus
}
#endif
