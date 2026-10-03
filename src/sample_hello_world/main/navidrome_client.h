#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal Subsonic REST client for the Navidrome server (navidrome_client.c).
 * All strings and arrays live in SPIRAM; free with the nav_free_* helpers.
 * Every call logs its failure reason (including the server's error message
 * on bad credentials) through ui_log(). */

typedef struct {
    char *id;
    char *name;
    int   song_count;
    int   duration;    /* seconds */
} nav_playlist_t;

typedef struct {
    nav_playlist_t *items;
    int count;
} nav_playlist_list_t;

typedef struct {
    char *id;
    char *title;
    char *artist;      /* "" when the server omits it */
    int   duration;    /* seconds */
} nav_song_t;

typedef struct {
    nav_song_t *items;
    int count;
} nav_song_list_t;

/* False (with a ui_log explaining which Kconfig options to set) when the
 * build carries no server URL; every call below then returns
 * ESP_ERR_INVALID_STATE without touching the network. */
bool nav_configured(void);
/* The configured base URL, for status lines. */
const char *nav_server_url(void);

esp_err_t nav_get_playlists(nav_playlist_list_t *out);
esp_err_t nav_get_playlist_songs(const char *playlist_id, nav_song_list_t *out);

/* Download one song as MP3 (server-side transcode, 128 kbps cap) into a
 * SPIRAM buffer the caller frees with heap_caps_free(). Polls *abort_flag
 * between reads and returns ESP_ERR_NOT_FINISHED when it goes true. */
esp_err_t nav_download_song(const char *song_id, uint8_t **out_buf,
                            size_t *out_len, volatile bool *abort_flag);

void nav_free_playlists(nav_playlist_list_t *l);
void nav_free_songs(nav_song_list_t *l);

#ifdef __cplusplus
}
#endif
