/* song_cache.c - PSRAM song cache + single downloader task.
 *
 * See song_cache.h for the API contract. Key invariants:
 *  - s_lock guards the stream list, every stream's buf/cap/len/flags/refs,
 *    and s_held_bytes. Readers copy out under the lock, so the downloader's
 *    realloc can never move a buffer under an in-flight read.
 *  - A stream's buffer is freed ONLY by eviction, which requires refs == 0
 *    and the stream neither queued nor actively downloading. The decoder
 *    holds a ref through its FILE* until audio_task fcloses it, so there is
 *    no use-after-free window (this replaces the old fmemopen +
 *    audio_song_buf_release dance for the music path).
 *  - Budget (recomputed under s_lock before every allocation/growth):
 *    the cache may hold min(CACHE_MAX_BYTES, held + max(0, free PSRAM -
 *    CACHE_PSRAM_FLOOR)); LRU unreferenced complete entries and all failed
 *    entries are evicted until the newcomer fits. */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_err.h"

#include "app_common.h"
#include "navidrome_client.h"
#include "song_cache.h"

static const char *TAG = "song_cache";

#define CACHE_MAX_BYTES     (20u * 1024 * 1024) /* hard ceiling on held bytes */
#define CACHE_PSRAM_FLOOR   (6u * 1024 * 1024)  /* PSRAM the rest of the app keeps */
#define STREAM_INITIAL_CAP  (512u * 1024)       /* no Content-Length: start here */
#define STREAM_MAX_BYTES    (16u * 1024 * 1024) /* per-song cap (matches old code) */
#define READ_POLL_MS        20
/* Reader patience while the downloader is still working on the stream. Must
 * exceed the downloader's worst case (nav_stream_song: 2 x 15 s silent-server
 * wait + 45 s reconnect budget) or the decoder gives up on a song the
 * downloader is about to rescue. */
#define READ_STALL_MS       90000
/* Reader patience when NOBODY is filling the stream (not queued, not active,
 * not complete, not failed - e.g. a stale queue entry was skipped). */
#define READ_ORPHAN_MS      5000
#define DL_TASK_STACK       16384               /* TLS runs on it; PSRAM stack
                                                 * (precedent: voice_detect) */

struct song_stream {
    struct song_stream *next;
    char id[48];
    uint8_t *buf;            /* PSRAM; may move on growth (only under lock) */
    size_t cap;
    size_t len;              /* bytes downloaded so far */
    size_t expected;         /* Content-Length when known, else 0 */
    bool complete;           /* download finished OK */
    bool failed;             /* download errored/aborted */
    bool abort;              /* reader/downloader should stop now */
    bool queued;             /* sitting in the request queue */
    bool active;             /* the downloader is filling it right now */
    int refs;                /* request handles + open FILE*s */
    size_t max_read_pos;     /* furthest byte any reader has consumed */
    int64_t last_use;        /* LRU stamp */
};

typedef struct {
    char id[48];
} dl_req_t;

static SemaphoreHandle_t s_lock;
static QueueHandle_t s_reqs;             /* dl_req_t, depth 4 */
static song_stream_t *s_streams;
static size_t s_held_bytes;
static volatile bool s_dl_abort;         /* aborts the in-flight download */
static bool s_inited;

static void lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

static song_stream_t *find_locked(const char *id)
{
    for (song_stream_t *s = s_streams; s != NULL; s = s->next) {
        if (strcmp(s->id, id) == 0) {
            return s;
        }
    }
    return NULL;
}

static void evict_locked(song_stream_t *victim)
{
    for (song_stream_t **pp = &s_streams; *pp != NULL; pp = &(*pp)->next) {
        if (*pp == victim) {
            *pp = victim->next;
            break;
        }
    }
    s_held_bytes -= victim->cap;
    heap_caps_free(victim->buf);
    heap_caps_free(victim);
}

static bool evictable_locked(const song_stream_t *s)
{
    return s->refs == 0 && !s->queued && !s->active &&
           (s->complete || s->failed);
}

/* Drop every failed entry nobody holds - they can never play. */
static void sweep_failed_locked(void)
{
    song_stream_t *s = s_streams;
    while (s != NULL) {
        song_stream_t *nx = s->next;
        if (s->failed && evictable_locked(s)) {
            ESP_LOGI(TAG, "evict failed \"%s\" (%u KB)", s->id,
                     (unsigned)(s->cap / 1024));
            evict_locked(s);
        }
        s = nx;
    }
}

/* Make room for `extra` more cache bytes, evicting LRU unreferenced entries
 * until held+extra fits the budget. The budget is recomputed each pass:
 * min(CACHE_MAX_BYTES, held + free PSRAM above the floor) - i.e. the cache
 * may never push free PSRAM below what the rest of the app needs. */
static bool ensure_room_locked(size_t extra)
{
    for (;;) {
        size_t free_now = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t spare = free_now > CACHE_PSRAM_FLOOR
                     ? free_now - CACHE_PSRAM_FLOOR : 0;
        size_t budget = s_held_bytes + spare;
        if (budget > CACHE_MAX_BYTES) {
            budget = CACHE_MAX_BYTES;
        }
        if (s_held_bytes + extra <= budget) {
            return true;
        }
        song_stream_t *victim = NULL;
        for (song_stream_t *s = s_streams; s != NULL; s = s->next) {
            if (evictable_locked(s) &&
                (victim == NULL || s->last_use < victim->last_use)) {
                victim = s;
            }
        }
        if (victim == NULL) {
            return false; /* everything left is in use */
        }
        ESP_LOGI(TAG, "evict LRU \"%s\" (%u KB) for %u KB request",
                 victim->id, (unsigned)(victim->cap / 1024),
                 (unsigned)(extra / 1024));
        evict_locked(victim);
    }
}

/* ---------- downloader sink (nav_stream_song callbacks) ---------- */

static bool dl_on_start(void *arg, int64_t content_len)
{
    song_stream_t *st = arg;
    size_t want = STREAM_INITIAL_CAP;
    if (content_len > 0) {
        if ((size_t)content_len > STREAM_MAX_BYTES) {
            ESP_LOGW(TAG, "\"%s\": %lld bytes exceeds per-song cap", st->id,
                     (long long)content_len);
            return false;
        }
        want = (size_t)content_len;
    }
    lock();
    st->expected = (content_len > 0) ? (size_t)content_len : 0;
    st->len = 0; /* a (re)start always fills from byte 0 */
    bool ok;
    if (st->abort) {
        ok = false;
    } else if (st->buf != NULL && st->cap >= want) {
        ok = true; /* retry of a failed download: reuse the old buffer */
    } else {
        if (st->buf != NULL) {
            s_held_bytes -= st->cap;
            heap_caps_free(st->buf);
            st->buf = NULL;
            st->cap = 0;
        }
        ok = ensure_room_locked(want);
        if (ok) {
            st->buf = heap_caps_malloc(want, MALLOC_CAP_SPIRAM);
            ok = (st->buf != NULL);
            if (ok) {
                st->cap = want;
                s_held_bytes += want;
            }
        }
    }
    unlock();
    if (!ok) {
        ESP_LOGW(TAG, "\"%s\": no room in cache (held %u KB)", st->id,
                 (unsigned)(s_held_bytes / 1024));
    }
    return ok;
}

static bool dl_on_data(void *arg, const uint8_t *data, size_t n)
{
    song_stream_t *st = arg;
    lock();
    if (st->abort) {
        unlock();
        return false;
    }
    if (st->len + n > st->cap) {
        size_t ncap = st->cap * 2;
        if (ncap < st->len + n) {
            ncap = st->len + n;
        }
        if (ncap > STREAM_MAX_BYTES) {
            ncap = STREAM_MAX_BYTES;
        }
        if (st->len + n > ncap || !ensure_room_locked(ncap - st->cap)) {
            unlock();
            ESP_LOGW(TAG, "\"%s\": song exceeds cache room, aborting", st->id);
            return false;
        }
        uint8_t *nb = heap_caps_realloc(st->buf, ncap, MALLOC_CAP_SPIRAM);
        if (nb == NULL) {
            unlock();
            return false;
        }
        s_held_bytes += ncap - st->cap;
        st->buf = nb;
        st->cap = ncap;
    }
    memcpy(st->buf + st->len, data, n);
    st->len += n; /* publish after the copy; readers copy under this lock */
    unlock();
    return true;
}

/* The server could not resume a broken download and is sending the song
 * from byte 0 again. Safe to accept when no decoder has consumed anything
 * yet, or when the bytes are guaranteed identical (raw file) - the reader
 * simply waits for len to pass its position again. Otherwise refuse: a
 * fresh transcode is not byte-identical and would splice the audio. */
static bool dl_on_restart(void *arg, bool bytes_identical)
{
    song_stream_t *st = arg;
    lock();
    size_t consumed = st->max_read_pos;
    bool ok = !st->abort && (consumed == 0 || bytes_identical);
    if (ok) {
        st->len = 0;
        st->expected = 0;
    }
    unlock();
    if (!ok && consumed > 0) {
        ESP_LOGW(TAG, "\"%s\": cannot restart from 0 - decoder already consumed "
                 "%u KB of a transcode", st->id, (unsigned)(consumed / 1024));
    }
    return ok;
}

static void dl_task(void *arg)
{
    dl_req_t req;
    for (;;) {
        if (xQueueReceive(s_reqs, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        lock();
        song_stream_t *st = find_locked(req.id);
        if (st == NULL || !st->queued || st->complete || st->failed) {
            if (st != NULL) {
                st->queued = false;
            }
            unlock();
            continue;
        }
        st->queued = false;
        st->active = true;
        s_dl_abort = false;
        unlock();

        int64_t t0 = esp_timer_get_time();
        nav_stream_sink_t sink = {
            .arg = st,
            .on_start = dl_on_start,
            .on_data = dl_on_data,
            .on_restart = dl_on_restart,
        };
        esp_err_t err = nav_stream_song(req.id, &sink, &s_dl_abort);

        lock();
        st->active = false;
        if (err == ESP_OK && !st->abort) {
            st->complete = true;
            /* trim a start-size overshoot so the cache accounting is honest */
            if (st->cap > st->len && st->len > 0) {
                uint8_t *nb = heap_caps_realloc(st->buf, st->len,
                                                MALLOC_CAP_SPIRAM);
                if (nb != NULL) {
                    s_held_bytes -= st->cap - st->len;
                    st->buf = nb;
                    st->cap = st->len;
                }
            }
        } else {
            st->failed = true;
        }
        size_t held = s_held_bytes;
        size_t got = st->len; /* still under the lock: st may be evicted
                               * the moment we let go */
        unlock();
        ESP_LOGI(TAG, "\"%s\": %s (%s), %u KB in %d ms (cache holds %u KB)",
                 req.id, err == ESP_OK ? "complete" : "failed",
                 esp_err_to_name(err), (unsigned)(got / 1024),
                 (int)((esp_timer_get_time() - t0) / 1000),
                 (unsigned)(held / 1024));
    }
}

/* ---------- public API ---------- */

void song_cache_stats(size_t *held, size_t *budget)
{
    size_t h = s_held_bytes; /* single aligned word: no lock for a readout */
    size_t free_now = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t spare = free_now > CACHE_PSRAM_FLOOR ? free_now - CACHE_PSRAM_FLOOR : 0;
    size_t b = h + spare;
    if (b > CACHE_MAX_BYTES) {
        b = CACHE_MAX_BYTES;
    }
    if (held != NULL) {
        *held = h;
    }
    if (budget != NULL) {
        *budget = b;
    }
}

bool song_cache_init(void)
{
    if (s_inited) {
        return true;
    }
    app_init_lock();
    if (!s_inited) {
        s_lock = xSemaphoreCreateMutex();
        s_reqs = xQueueCreate(4, sizeof(dl_req_t));
        if (s_lock == NULL || s_reqs == NULL ||
            /* prio 3: below the LVGL task (4). Software-AES TLS makes this
             * task CPU-bound for the first seconds of every song; at equal
             * priority it time-sliced 50/50 with the UI. */
            xTaskCreateWithCaps(dl_task, "song_dl", DL_TASK_STACK, NULL, 3,
                                NULL, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                != pdPASS) {
            app_init_unlock();
            ui_log("song cache init failed");
            return false;
        }
        s_inited = true;
    }
    app_init_unlock();
    return true;
}

/* Create-or-find under the lock; bumps refs and the LRU stamp. */
static song_stream_t *get_locked(const char *id)
{
    song_stream_t *st = find_locked(id);
    if (st == NULL) {
        st = heap_caps_calloc(1, sizeof(*st), MALLOC_CAP_SPIRAM);
        if (st == NULL) {
            return NULL;
        }
        strlcpy(st->id, id, sizeof(st->id));
        st->next = s_streams;
        s_streams = st;
    }
    st->last_use = esp_timer_get_time();
    return st;
}

song_stream_t *song_cache_request(const char *song_id)
{
    if (!s_inited || song_id == NULL || song_id[0] == '\0') {
        return NULL;
    }
    lock();
    /* every new request cleans house first (user-facing contract) */
    sweep_failed_locked();
    ensure_room_locked(STREAM_INITIAL_CAP);

    song_stream_t *st = get_locked(song_id);
    if (st == NULL) {
        unlock();
        return NULL;
    }
    st->refs++;
    bool need_download = !st->complete && !st->active && !st->queued;
    if (need_download) {
        /* a fresh attempt on a previously failed stream */
        st->failed = false;
        st->abort = false;
        st->len = 0;
        st->max_read_pos = 0;
        st->queued = true;
    }
    /* the user is waiting: an in-flight PREFETCH of another song yields */
    for (song_stream_t *s = s_streams; s != NULL; s = s->next) {
        if (s->active && s != st) {
            s->abort = true;
            s_dl_abort = true;
        }
    }
    unlock();

    if (need_download) {
        dl_req_t req;
        strlcpy(req.id, song_id, sizeof(req.id));
        if (xQueueSendToFront(s_reqs, &req, 0) != pdTRUE) {
            lock();
            st->queued = false;
            st->failed = true;
            unlock();
        }
    }
    return st;
}

void song_cache_prefetch(const char *song_id)
{
    if (!s_inited || song_id == NULL || song_id[0] == '\0') {
        return;
    }
    lock();
    sweep_failed_locked();
    ensure_room_locked(STREAM_INITIAL_CAP);
    song_stream_t *st = get_locked(song_id);
    bool queue_it = (st != NULL) && !st->complete && !st->active && !st->queued;
    if (queue_it) {
        st->failed = false;
        st->abort = false;
        st->len = 0;
        st->max_read_pos = 0;
        st->queued = true;
    }
    unlock();
    if (queue_it) {
        dl_req_t req;
        strlcpy(req.id, song_id, sizeof(req.id));
        if (xQueueSend(s_reqs, &req, 0) != pdTRUE) {
            lock();
            st->queued = false;
            unlock();
        } else {
            ESP_LOGI(TAG, "prefetching \"%s\"", song_id);
        }
    }
}

void song_stream_release(song_stream_t *s)
{
    if (s == NULL) {
        return;
    }
    lock();
    if (s->refs > 0) {
        s->refs--;
    }
    unlock();
}

void song_cache_abort_stream(song_stream_t *s)
{
    if (s == NULL) {
        return;
    }
    lock();
    if (!s->complete) {
        s->abort = true;   /* unblocks readers immediately */
        s->queued = false; /* the downloader will skip a stale queue entry */
        if (s->active) {
            s_dl_abort = true;
        }
    }
    unlock();
}

void song_cache_cancel_downloads(void)
{
    if (!s_inited) {
        return;
    }
    lock();
    xQueueReset(s_reqs);
    for (song_stream_t *s = s_streams; s != NULL; s = s->next) {
        if (s->queued || s->active) {
            s->queued = false;
            s->abort = true;
        }
    }
    s_dl_abort = true;
    unlock();
}

size_t song_stream_len(song_stream_t *s)
{
    lock();
    size_t n = s->len;
    unlock();
    return n;
}

bool song_stream_complete(song_stream_t *s)
{
    lock();
    bool v = s->complete;
    unlock();
    return v;
}

bool song_stream_failed(song_stream_t *s)
{
    lock();
    bool v = s->failed || s->abort;
    unlock();
    return v;
}

/* ---------- funopen FILE* over a stream ---------- */

typedef struct {
    song_stream_t *st;
    size_t pos;
} stream_file_t;

static int stream_readfn(void *cookie, char *dst, int size)
{
    stream_file_t *f = cookie;
    song_stream_t *st = f->st;
    int waited = 0;
    for (;;) {
        lock();
        size_t avail = (st->len > f->pos) ? st->len - f->pos : 0;
        if (avail > 0) {
            size_t n = ((size_t)size < avail) ? (size_t)size : avail;
            memcpy(dst, st->buf + f->pos, n);
            f->pos += n;
            if (f->pos > st->max_read_pos) {
                st->max_read_pos = f->pos;
            }
            st->last_use = esp_timer_get_time();
            unlock();
            return (int)n;
        }
        bool eof = st->complete;
        bool dead = st->failed || st->abort;
        bool filling = st->active || st->queued;
        unlock();
        if (eof) {
            return 0;
        }
        if (dead) {
            return -1;
        }
        if (waited >= (filling ? READ_STALL_MS : READ_ORPHAN_MS)) {
            ESP_LOGW(TAG, "read stalled %d ms at %u (%s) - giving up", waited,
                     (unsigned)f->pos,
                     filling ? "downloader still busy" : "nobody downloading");
            return -1;
        }
        vTaskDelay(pdMS_TO_TICKS(READ_POLL_MS));
        waited += READ_POLL_MS;
    }
}

static fpos_t stream_seekfn(void *cookie, fpos_t off, int whence)
{
    stream_file_t *f = cookie;
    song_stream_t *st = f->st;

    long target;
    lock();
    size_t len = st->len;
    size_t expected = st->expected;
    bool complete = st->complete;
    unlock();

    switch (whence) {
    case SEEK_SET: target = (long)off; break;
    case SEEK_CUR: target = (long)f->pos + (long)off; break;
    case SEEK_END:
        /* only answerable when the size is known (Content-Length or done) */
        if (complete) {
            target = (long)len + (long)off;
        } else if (expected > 0) {
            target = (long)expected + (long)off;
        } else {
            return -1;
        }
        break;
    default:
        return -1;
    }
    if (target < 0) {
        return -1;
    }

    /* forward seek past the download frontier (large ID3 tag skip): wait
     * for the bytes rather than fail - the downloader is filling behind us */
    int waited = 0;
    for (;;) {
        lock();
        len = st->len;
        complete = st->complete;
        bool dead = st->failed || st->abort;
        bool filling = st->active || st->queued;
        unlock();
        if ((size_t)target <= len) {
            break;
        }
        if (complete || dead ||
            waited >= (filling ? READ_STALL_MS : READ_ORPHAN_MS)) {
            return -1;
        }
        vTaskDelay(pdMS_TO_TICKS(READ_POLL_MS));
        waited += READ_POLL_MS;
    }
    f->pos = (size_t)target;
    lock();
    if (f->pos > st->max_read_pos) {
        st->max_read_pos = f->pos;
    }
    unlock();
    return (fpos_t)target;
}

static int stream_closefn(void *cookie)
{
    stream_file_t *f = cookie;
    song_stream_release(f->st);
    heap_caps_free(f);
    return 0;
}

FILE *song_stream_fopen(song_stream_t *s)
{
    if (s == NULL) {
        return NULL;
    }
    stream_file_t *f = heap_caps_calloc(1, sizeof(*f), MALLOC_CAP_SPIRAM);
    if (f == NULL) {
        return NULL;
    }
    f->st = s;
    FILE *fp = funopen(f, stream_readfn, NULL, stream_seekfn, stream_closefn);
    if (fp == NULL) {
        heap_caps_free(f);
        return NULL;
    }
    lock();
    s->refs++;
    unlock();
    return fp;
}
