#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Background music engine (music_player.c). Commands return immediately;
 * resolving (Navidrome + AI matching) and playback run on the music task.
 * Progress lands in music_status() - the Play screen polls it. */

typedef enum {
    MUSIC_STATE_IDLE = 0,
    MUSIC_STATE_BUSY,      /* resolving a request (search / AI / download) */
    MUSIC_STATE_PLAYING,
    MUSIC_STATE_PAUSED,
} music_state_t;

/* Find the song best matching `text` (AI-assisted) and play it. */
void music_play_song_query(const char *text);

/* Find the playlist best matching `text` and play it, shuffled, until
 * stopped. */
void music_play_playlist_query(const char *text);

/* Random songs from the whole library, one after another, until stopped. */
void music_play_random(void);

/* Play one exact song / one exact playlist by Subsonic id, skipping the
 * search + AI matching entirely (the web UI lets the user pick a specific
 * row from a search result list). title/artist/name are used for the
 * status line only and may be NULL. */
void music_play_song_id(const char *id, const char *title, const char *artist);
void music_play_playlist_id(const char *id, const char *name);

/* Stop playback/resolving (async; state goes IDLE shortly after). */
void music_stop(void);

/* Pause <-> resume the current song. No-op unless a song is audibly
 * playing or paused (resolving/loading windows are ignored). */
void music_toggle_pause(void);

/* Skip within the current list session (playlist pass, random batch; on
 * a single song Prev restarts it, Next ends it). Aborts an in-flight
 * download without ending the session. No-op unless PLAYING/PAUSED. */
void music_next(void);
void music_prev(void);

/* Snapshot of the current song. Returns false (and empties both buffers)
 * when nothing is playing or paused. Thread-safe like music_status(). */
bool music_now_playing(char *title, int tcap, char *artist, int acap);

/* Stop and wait up to timeout_ms for the engine to go idle. Call before
 * anything else takes over the audio player (the alarm). Returns false if
 * the engine was still busy when the timeout expired. */
bool music_stop_sync(int timeout_ms);

music_state_t music_state(void);

/* Current status line ("Playing: x - y", "Thinking...", ...). Non-blocking:
 * returns false (buf untouched) when the status lock is contended. */
bool music_status(char *buf, int cap);

/* Free a PSRAM song buffer that was handed to the audio player via
 * fmemopen. Pass decoder_idle=false when the halt wait timed out - the
 * buffer is then parked and reaped later instead of freed under the
 * decoder. Shared by the music engine and the alarm ringer. */
void audio_song_buf_release(uint8_t *buf, bool decoder_idle);

/* Free a parked (deferred) song buffer now if the decoder is idle. Cheap;
 * call whenever playback has provably stopped. */
void audio_song_buf_reap(void);

#ifdef __cplusplus
}
#endif
