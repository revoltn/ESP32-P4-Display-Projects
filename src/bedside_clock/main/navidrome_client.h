#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "app_common.h"

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

/* True when a server URL and user are stored in settings. */
bool nav_is_configured(void);

/* Outcome of the most recent Subsonic API call: 1 = server answered "ok",
 * 0 = connect/HTTP/auth failure, -1 = no call made yet. Lock-free read for
 * the clock screen's Navidrome indicator. */
int nav_link_state(void);

/* esp_timer_get_time() of the last call the server answered "ok" (probe,
 * search, playlist, or a song stream that returned headers); 0 = never.
 * boot_seq uses it to skip a probe when real traffic already proved the
 * server within the check period. */
int64_t nav_last_ok_us(void);

/* Subsonic error code the server returned on the most recent API call
 * (70 = requested object not found, 40 = wrong credentials, ...); 0 when
 * that call succeeded or failed before an answer arrived (connect/TLS/HTTP).
 * Lets a caller tell "the id is stale" from "the server is unreachable". */
int nav_last_api_error(void);

/* nav_link_state() folded together with nav_is_configured(), for the clock
 * screen's music-server indicator. SVC_UP only while the last "ok" is younger
 * than boot_svc_fresh_window_us(); older than that reads SVC_WAIT (dim). */
svc_state_t nav_service_state(void);

/* Subsonic "ping": prove URL + credentials + server in one cheap call and
 * update nav_link_state(). 5 s timeout, no retry - the boot sequence probes
 * with this and must not stall on a dead server. Blocking (TLS): worker task
 * only. Requires WiFi; check wifi_mgr_is_connected() first. */
bool nav_ping(void);

esp_err_t nav_get_playlists(nav_playlist_list_t *out);
esp_err_t nav_get_playlist_songs(const char *playlist_id, nav_song_list_t *out);

/* Subsonic search3, songs only. Navidrome returns the whole library for an
 * empty query, so query="" + a generous max doubles as "list all songs". */
esp_err_t nav_search_songs(const char *query, int max, nav_song_list_t *out);

/* Subsonic getRandomSongs. */
esp_err_t nav_get_random_songs(int max, nav_song_list_t *out);

/* Download one song as MP3 (server-side transcode, 128 kbps cap) into a
 * SPIRAM buffer the caller frees with heap_caps_free(). Polls *abort_flag
 * between reads and returns ESP_ERR_NOT_FINISHED when it goes true. Does
 * NOT retry or wait out a silent server: it serves the alarm, which must
 * fall back to the chime promptly (alarm.c retries with another song).
 * `why` (optional, ~24 chars is plenty) receives a short failure reason
 * ("timeout 0KB/15s", "HTTP 404", "TLS 512KB/3s", "no PSRAM", ...) for the
 * caller's journal line. */
esp_err_t nav_download_song(const char *song_id, uint8_t **out_buf,
                            size_t *out_len, volatile bool *abort_flag,
                            char *why, size_t why_cap);

/* Streaming download for the song cache. Tries format=raw first (no
 * server-side ffmpeg pass, Content-Length available, resumable) and sniffs
 * the first bytes; anything that isn't MP3/WAV is fetched again as a
 * 128 kbps MP3 transcode.
 *
 * Resilience: a silent server is waited out once more (2 x 15 s); a dropped
 * or truncated connection is reconnected with "Range: bytes=<got>-" up to
 * 2 times within a 45 s budget from the first failure (backoff 1 s, 3 s,
 * abort_flag polled throughout). On HTTP 206 the bytes are appended; if the
 * server answers 200 instead (live transcodes cannot resume) on_restart is
 * asked whether the sink can discard what it holds and start from byte 0
 * (bytes_identical is true for raw files, which re-send the same bytes).
 * Returning false from on_restart fails the transfer instead of splicing.
 *
 * on_start fires after the headers (content_len 0 = unknown) and again after
 * an accepted on_restart; returning false from on_start/on_data aborts the
 * transfer. Every failure is diagnosed to ui_log (cause, errno, TLS error,
 * bytes, ms, attempt, RSSI). Uses ONE persistent keep-alive connection:
 * call only from the song_cache downloader task. */
typedef struct {
    void *arg;
    bool (*on_start)(void *arg, int64_t content_len);
    bool (*on_data)(void *arg, const uint8_t *data, size_t len);
    bool (*on_restart)(void *arg, bool bytes_identical); /* may be NULL = refuse */
} nav_stream_sink_t;

esp_err_t nav_stream_song(const char *song_id, const nav_stream_sink_t *sink,
                          volatile bool *abort_flag);

void nav_free_playlists(nav_playlist_list_t *l);
void nav_free_songs(nav_song_list_t *l);

#ifdef __cplusplus
}
#endif
