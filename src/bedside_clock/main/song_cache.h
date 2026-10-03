#pragma once

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PSRAM song cache + downloader task (song_cache.c).
 *
 * A "stream" is one song's bytes, filling progressively while a single
 * downloader task pulls it from Navidrome (keep-alive connection, format=raw
 * with transcode fallback). Playback can begin as soon as a prebuffer is in:
 * song_stream_fopen() returns a FILE* whose reads block until the bytes have
 * arrived. Completed downloads stay cached (LRU) so replays and prefetched
 * next-tracks start instantly.
 *
 * Capacity is budgeted on EVERY new request: the cache may hold at most
 * min(hard cap, bytes already held + free PSRAM above the app reserve), and
 * least-recently-used unreferenced entries (plus all failed ones) are
 * evicted until the newcomer fits. */

typedef struct song_stream song_stream_t;

/* Lazy one-time init (queue + downloader task). Safe from any task. */
bool song_cache_init(void);

/* Snapshot for the UI: bytes currently held and the budget the next request
 * would be allowed (min(hard cap, held + free PSRAM above the floor)).
 * Lock-free, so a value may be a moment stale - display use only. */
void song_cache_stats(size_t *held, size_t *budget);

/* Get a stream for `song_id`, starting/queueing its download at the FRONT
 * of the queue (an in-flight prefetch of a different song is aborted so the
 * user's request never waits behind it). Returns a referenced handle -
 * always pair with song_stream_release(). NULL on OOM/not-initialized. */
song_stream_t *song_cache_request(const char *song_id);

/* Queue a background download of `song_id` (back of the queue) so it is
 * already cached when requested. No handle is returned. */
void song_cache_prefetch(const char *song_id);

/* Drop the reference from song_cache_request. */
void song_stream_release(song_stream_t *s);

/* Abort this stream's download if it is queued or in flight, and unblock
 * any reader waiting on it. No-op on a completed stream. */
void song_cache_abort_stream(song_stream_t *s);

/* Abort the in-flight download and drop everything queued. Cached data
 * stays. */
void song_cache_cancel_downloads(void);

/* Progress. len grows monotonically; complete/failed are terminal. */
size_t song_stream_len(song_stream_t *s);
bool song_stream_complete(song_stream_t *s);
bool song_stream_failed(song_stream_t *s);

/* Blocking-read FILE* over the stream for audio_player (funopen). Takes its
 * own reference, dropped when the player fcloses it. NULL on OOM. */
FILE *song_stream_fopen(song_stream_t *s);

#ifdef __cplusplus
}
#endif
