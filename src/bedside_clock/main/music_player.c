/* Background music engine: resolves a request (song text -> AI best match,
 * playlist text -> AI best match, or random) against Navidrome, then streams
 * it through the PSRAM song cache (song_cache.c): playback starts at a 64 KB
 * prebuffer while the downloader keeps filling, and the next track is
 * prefetched. One command at a time; a new command or music_stop() aborts
 * the current one. The alarm has priority: the play loop bails out as soon
 * as alarm_is_active(), and alarm.c calls music_stop_sync() before ringing. */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "freertos/idf_additions.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"

#include "app_common.h"
#include "settings.h"
#include "wifi_mgr.h"
#include "navidrome_client.h"
#include "localai_client.h"
#include "alarm.h"
#include "song_cache.h"
#include "music_player.h"
#include "utf8_util.h"

static const char *TAG = "music";

#define BIT_SONG_IDLE BIT0
#define BIT_NEXT      BIT1
#define BIT_PREV      BIT2

typedef enum { CMD_SONG, CMD_PLAYLIST, CMD_RANDOM,
               CMD_SONG_ID, CMD_PLAYLIST_ID } music_cmd_type_t;

typedef struct {
    music_cmd_type_t type;
    char text[96];    /* query text, or the Subsonic id for the *_ID commands */
    char name[96];    /* *_ID only: song title / playlist name, for status */
    char artist[64];  /* CMD_SONG_ID only, for status */
} music_cmd_t;

static QueueHandle_t s_cmds;          /* length 1, xQueueOverwrite */
static EventGroupHandle_t s_ev;
static volatile bool s_abort;
static volatile bool s_abort_dl;      /* aborts only the current download */
static volatile music_state_t s_state;
static char s_status[128] = "";
static char s_np_title[96] = "";      /* guarded by s_status_mtx */
static char s_np_artist[96] = "";
static SemaphoreHandle_t s_status_mtx;

static void set_status(const char *fmt, ...)
{
    char line[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    utf8_trim_tail(line); /* "Playing: title - artist" can overflow 128 */
    if (s_status_mtx != NULL &&
        xSemaphoreTake(s_status_mtx, pdMS_TO_TICKS(200)) == pdTRUE) {
        strlcpy(s_status, line, sizeof s_status);
        xSemaphoreGive(s_status_mtx);
    }
    ESP_LOGI(TAG, "%s", line);
}

/* Non-blocking (the LVGL task polls this): false = contended, buf untouched. */
bool music_status(char *buf, int cap)
{
    if (s_status_mtx != NULL && xSemaphoreTake(s_status_mtx, 0) == pdTRUE) {
        strlcpy(buf, s_status, cap);
        xSemaphoreGive(s_status_mtx);
        return true;
    }
    if (s_status_mtx == NULL) {
        strlcpy(buf, "", cap);
        return true; /* engine not started yet: genuinely empty */
    }
    return false;
}

static void set_now_playing(const char *title, const char *artist)
{
    if (xSemaphoreTake(s_status_mtx, pdMS_TO_TICKS(200)) == pdTRUE) {
        strlcpy(s_np_title, title != NULL ? title : "", sizeof s_np_title);
        strlcpy(s_np_artist, artist != NULL ? artist : "", sizeof s_np_artist);
        /* byte-bounded copies: don't leave half a UTF-8 sequence for LVGL
         * or the JSON status API */
        utf8_trim_tail(s_np_title);
        utf8_trim_tail(s_np_artist);
        xSemaphoreGive(s_status_mtx);
    }
}

bool music_now_playing(char *title, int tcap, char *artist, int acap)
{
    music_state_t st = s_state;
    bool active = (st == MUSIC_STATE_PLAYING || st == MUSIC_STATE_PAUSED);
    /* 0-tick take: the LVGL task polls this and must never block here */
    if (!active || s_status_mtx == NULL ||
        xSemaphoreTake(s_status_mtx, 0) != pdTRUE) {
        strlcpy(title, "", tcap);
        strlcpy(artist, "", acap);
        return false;
    }
    strlcpy(title, s_np_title, tcap);
    strlcpy(artist, s_np_artist, acap);
    xSemaphoreGive(s_status_mtx);
    return title[0] != '\0';
}

music_state_t music_state(void)
{
    return s_state;
}

/* ---------- playback ---------- */

static void player_cb(audio_player_cb_ctx_t *ctx)
{
    switch (ctx->audio_event) {
    case AUDIO_PLAYER_CALLBACK_EVENT_IDLE:
    case AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN_FILE_TYPE:
        xEventGroupSetBits(s_ev, BIT_SONG_IDLE);
        break;
    default:
        break;
    }
}

static bool player_ensure_init(void)
{
    static bool s_inited;
    if (s_inited) {
        return true;
    }
    app_init_lock(); /* LVGL, worker and voice tasks all reach this lazily */
    if (!s_inited) {
        if (!app_audio_ensure_init() || bsp_extra_player_init() != ESP_OK) {
            app_init_unlock();
            ui_log("audio player init failed");
            return false;
        }
        s_inited = true;
    }
    app_init_unlock();
    return true;
}

/* ---------- deferred song-buffer free (shared with alarm.c) ----------
 *
 * The decoder task reads the fmemopen()ed PSRAM buffer until it reports
 * IDLE. When a halt wait times out we must NOT free that buffer - the
 * decoder may still be inside a read. Park it instead; it becomes safe
 * once the player accepts new work or confirms idle, so the next call
 * reaps it. */

static uint8_t *s_condemned_buf;
static portMUX_TYPE s_condemned_mux = portMUX_INITIALIZER_UNLOCKED;

void audio_song_buf_release(uint8_t *buf, bool decoder_idle)
{
    uint8_t *free_now = NULL;
    uint8_t *old = NULL;
    taskENTER_CRITICAL(&s_condemned_mux);
    old = s_condemned_buf;
    if (decoder_idle) {
        /* idle confirmed: buf and any earlier zombie are both finished */
        free_now = buf;
        s_condemned_buf = NULL;
    } else {
        /* decoder may still read buf; park it. The previous zombie (if
         * any) is safe: the player went idle before accepting the song
         * that produced this buf. */
        s_condemned_buf = buf;
    }
    taskEXIT_CRITICAL(&s_condemned_mux);
    if (free_now != NULL) {
        heap_caps_free(free_now);
    }
    if (old != NULL) {
        heap_caps_free(old);
    }
    if (!decoder_idle) {
        ESP_LOGW(TAG, "decoder still busy - song buffer free deferred");
    }
}

/* Free a parked buffer once the decoder is provably idle. Without this a
 * zombie can sit on up to 16 MB of PSRAM until the NEXT release call - which
 * may never come (stop, alarm takeover, play failure). */
void audio_song_buf_reap(void)
{
    if (audio_player_get_state() != AUDIO_PLAYER_STATE_IDLE) {
        return;
    }
    uint8_t *old;
    taskENTER_CRITICAL(&s_condemned_mux);
    old = s_condemned_buf;
    s_condemned_buf = NULL;
    taskEXIT_CRITICAL(&s_condemned_mux);
    heap_caps_free(old);
}

static bool should_stop(void)
{
    return s_abort || alarm_is_active() ||
           uxQueueMessagesWaiting(s_cmds) > 0; /* newer command queued */
}

/* Stop the player (works while paused too) and wait for it to go idle,
 * at which point it has fclosed the song's FILE*. Returns true when idle
 * was confirmed - only then is the song buffer safe to free directly. */
static bool player_halt(void)
{
    if (audio_player_get_state() == AUDIO_PLAYER_STATE_IDLE) {
        return true; /* already idle: stop() would emit no IDLE event and
                      * the wait below would stall the caller for 3 s */
    }
    audio_player_stop();
    EventBits_t bits = xEventGroupWaitBits(s_ev, BIT_SONG_IDLE, pdTRUE,
                                           pdFALSE, pdMS_TO_TICKS(3000));
    return (bits & BIT_SONG_IDLE) != 0;
}

typedef enum {
    PLAY_OK,     /* song played to the end */
    PLAY_FAIL,   /* download/decode setup failed */
    PLAY_HALTED, /* stop/alarm/new command - caller's should_stop() confirms */
    PLAY_NEXT,   /* user skipped forward */
    PLAY_PREV,   /* user skipped backward */
} play_result_t;

/* Playback starts once this much of the song has arrived (or it completes).
 * 64 KB ~= 4 s of 128 kbps audio - plenty ahead of the ~6 KB decoder input
 * buffer, and it arrives in well under a second on a LAN. */
#define PLAY_PREBUFFER (64 * 1024)

/* Every download / decode-setup failure funnels through here so the Play
 * screen never sits on a stale "Loading: ..." - the navidrome client has
 * already written the diagnosed cause to the Specifications log. */
static play_result_t play_fail(const nav_song_t *song, song_stream_t *st)
{
    if (st != NULL) {
        song_stream_release(st);
    }
    set_status("Could not load: %s - see Specifications log", song->title);
    return PLAY_FAIL;
}

/* Stream and play one song via the cache (playback starts at the prebuffer
 * mark while the downloader keeps filling); prefetches `next` once this
 * song's download completes. Returns when the song ends, is skipped, or
 * should_stop(). */
static play_result_t play_one(const nav_song_t *song, const nav_song_t *next)
{
    /* a Next/Prev that raced the end of the previous song must not skip
     * this one before it has even played (real stops also set s_abort, so
     * resetting s_abort_dl here loses nothing) */
    xEventGroupClearBits(s_ev, BIT_NEXT | BIT_PREV);
    s_abort_dl = false;
    audio_song_buf_reap(); /* the alarm path still parks buffers */
    set_status("Loading: %s", song->title);

    song_stream_t *st = song_cache_request(song->id);
    if (st == NULL) {
        return play_fail(song, NULL);
    }

    /* wait for the prebuffer (instant when the song is already cached) */
    for (;;) {
        if (song_stream_failed(st)) {
            return play_fail(song, st);
        }
        if (song_stream_complete(st) || song_stream_len(st) >= PLAY_PREBUFFER) {
            break;
        }
        EventBits_t bits = xEventGroupWaitBits(s_ev, BIT_NEXT | BIT_PREV,
                                               pdTRUE, pdFALSE,
                                               pdMS_TO_TICKS(100));
        if (bits & (BIT_NEXT | BIT_PREV)) {
            song_cache_abort_stream(st);
            song_stream_release(st);
            s_abort_dl = false;
            return (bits & BIT_PREV) ? PLAY_PREV : PLAY_NEXT;
        }
        if (should_stop() || s_abort_dl) {
            song_cache_abort_stream(st);
            song_stream_release(st);
            return PLAY_HALTED;
        }
    }

    FILE *fp = song_stream_fopen(st);
    if (fp == NULL) {
        return play_fail(song, st);
    }
    xEventGroupClearBits(s_ev, BIT_SONG_IDLE);
    if (audio_player_play(fp) != ESP_OK) {
        fclose(fp); /* drops the FILE*'s own ref */
        return play_fail(song, st);
    }
    s_state = MUSIC_STATE_PLAYING;
    set_now_playing(song->title, song->artist);
    if (song->artist[0] != '\0') {
        set_status("Playing: %s - %s", song->title, song->artist);
    } else {
        set_status("Playing: %s", song->title);
    }

    /* poll: song end and skips are events, stop conditions are flags */
    bool prefetched = (next == NULL);
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(s_ev,
                BIT_SONG_IDLE | BIT_NEXT | BIT_PREV,
                pdTRUE, pdFALSE, pdMS_TO_TICKS(250));
        /* the downloader is free once this song is in: line up the next
         * track so the inter-song gap disappears */
        if (!prefetched && song_stream_complete(st)) {
            song_cache_prefetch(next->id);
            prefetched = true;
        }
        if (bits & BIT_SONG_IDLE) {
            /* song finished (player fcloses the FILE*) - unless the decoder
             * stopped because the download died under it */
            if (!song_stream_complete(st) && song_stream_failed(st)) {
                set_status("Stream of %s failed mid-song - see Specifications log",
                           song->title);
                song_stream_release(st);
                return PLAY_FAIL;
            }
            break;
        }
        if (bits & (BIT_NEXT | BIT_PREV)) {
            song_cache_abort_stream(st); /* also unblocks a starving decoder */
            player_halt();
            song_stream_release(st);
            s_abort_dl = false;
            return (bits & BIT_PREV) ? PLAY_PREV : PLAY_NEXT;
        }
        if (should_stop()) {
            song_cache_abort_stream(st);
            player_halt();
            song_stream_release(st);
            return PLAY_HALTED;
        }
    }
    song_stream_release(st);
    return PLAY_OK;
}

/* Play `songs` (owned by caller) until stopped or the list ends; `shuffle`
 * plays full passes in a shuffled order (reshuffling between passes) so
 * Prev/Next stay meaningful; `once` plays exactly the first song (Prev
 * restarts it, Next ends it). */
static void play_list(nav_song_list_t *songs, bool shuffle, bool once)
{
    if (alarm_is_active()) {
        return; /* the alarm owns the codec and the player callback */
    }
    bsp_extra_codec_volume_set(settings_get()->volume, NULL);
    bsp_extra_player_register_callback(player_cb, NULL);
    xEventGroupClearBits(s_ev, BIT_SONG_IDLE | BIT_NEXT | BIT_PREV);
    s_abort_dl = false;

    int *order = heap_caps_malloc(songs->count * sizeof(int),
                                  MALLOC_CAP_SPIRAM);
    if (order != NULL) {
        for (int i = 0; i < songs->count; i++) {
            order[i] = i;
        }
        if (shuffle) {
            for (int i = songs->count - 1; i > 0; i--) {
                int j = esp_random() % (i + 1);
                int tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
        }
    }

    int pos = 0;
    int fails = 0;
    while (!should_stop() && fails < 3) {
        int idx = (order != NULL) ? order[pos] : pos;
        const nav_song_t *next = NULL;
        if (!once && pos + 1 < songs->count) {
            int nidx = (order != NULL) ? order[pos + 1] : pos + 1;
            next = &songs->items[nidx];
        }
        play_result_t r = play_one(&songs->items[idx], next);
        if (r == PLAY_HALTED) {
            break;
        }
        if (once && r != PLAY_PREV) {
            break;
        }
        switch (r) {
        case PLAY_OK:   fails = 0; pos++; break;
        case PLAY_FAIL: fails++;   pos++; break;
        case PLAY_NEXT:            pos++; break;
        case PLAY_PREV: pos = (pos > 0) ? pos - 1 : 0; break;
        default: break;
        }
        if (pos >= songs->count) {
            if (shuffle && order != NULL) {
                for (int i = songs->count - 1; i > 0; i--) {
                    int j = esp_random() % (i + 1);
                    int tmp = order[i];
                    order[i] = order[j];
                    order[j] = tmp;
                }
                pos = 0;
            } else {
                break; /* sequential list finished */
            }
        }
    }
    heap_caps_free(order);
    /* If the alarm fired mid-list it has re-registered the callback as its
     * own - clearing it here would strand the alarm waiting for SONG_IDLE.
     * Also stop background downloads so they don't compete with the alarm's
     * own playlist fetch for bandwidth. */
    if (!alarm_is_active()) {
        bsp_extra_player_register_callback(NULL, NULL);
    } else {
        song_cache_cancel_downloads();
    }
}

/* ---------- resolving ---------- */

static bool network_ready(void)
{
    if (!wifi_mgr_is_connected() || !nav_is_configured()) {
        set_status("WiFi or music server not configured");
        return false;
    }
    return player_ensure_init() && song_cache_init();
}

/* The AI models are small: never send more than this many options, and
 * pre-narrow big candidate lists with a local fuzzy scorer first. Tested
 * against the real library + LocalAI by tools/test_ai_matching.py - keep
 * that script's logic in sync with this file and localai_client.c. */
#define SHORTLIST_MAX 25

/* lowercase, alnum only, spaces dropped - "red gum" scores vs "redgum" */
static void squash(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    for (; *in != '\0' && o + 1 < cap; in++) {
        unsigned char c = (unsigned char)*in;
        if (isalnum(c)) {
            out[o++] = (char)tolower(c);
        }
    }
    out[o] = '\0';
}

/* Cheap sounds-like fold of an already-squashed string: silent-ish h
 * dropped, soft c ("tenacious") -> s else k, q->k, z->s, vowels+y -> a,
 * repeats collapsed. "toccata"/"tocata"/"tokata" -> "takata". */
static void phonetic_fold(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; in[i] != '\0' && o + 1 < cap; i++) {
        char c = in[i];
        if (c == 'h') {
            continue;
        }
        if (c == 'c') {
            char nx = in[i + 1];
            c = (nx == 'e' || nx == 'i' || nx == 'y') ? 's' : 'k';
        }
        switch (c) {
        case 'q': c = 'k'; break;
        case 'z': c = 's'; break;
        case 'e': case 'i': case 'o': case 'u': case 'y': c = 'a'; break;
        default: break;
        }
        if (o > 0 && out[o - 1] == c) {
            continue;
        }
        out[o++] = c;
    }
    out[o] = '\0';
}

/* Plain Levenshtein, early-out on hopeless length gap. */
static int lev(const char *a, const char *b)
{
    int la = (int)strlen(a);
    int lb = (int)strlen(b);
    if (la - lb > 3 || lb - la > 3 || lb >= 63 || la >= 63) {
        return 99;
    }
    int prev[64], cur[64];
    for (int j = 0; j <= lb; j++) {
        prev[j] = j;
    }
    for (int i = 1; i <= la; i++) {
        cur[0] = i;
        for (int j = 1; j <= lb; j++) {
            int d = prev[j - 1] + (a[i - 1] != b[j - 1]);
            if (prev[j] + 1 < d) {
                d = prev[j] + 1;
            }
            if (cur[j - 1] + 1 < d) {
                d = cur[j - 1] + 1;
            }
            cur[j] = d;
        }
        memcpy(prev, cur, (lb + 1) * sizeof(int));
    }
    return prev[lb];
}

#define FS_MAX_WORDS 16

/* Cheap token/substring scorer, punctuation- and word-join-blind ("red
 * gum" hits "Redgum", "barbers" hits "Barber's"), phonetic + edit-distance
 * aware so typos and sound-alikes ("tokata", "tenashus") surface and rank
 * correctly. EXACT mirror of tools/test_ai_matching.py fuzzy_score - keep
 * them in sync and re-run that suite after any change here. */
static int fuzzy_score(const char *query, const char *title,
                       const char *artist)
{
    char tmp[192];
    char haysq[192];
    char hayph[192];
    snprintf(tmp, sizeof tmp, "%s %s", title, artist);
    squash(tmp, haysq, sizeof haysq);
    phonetic_fold(haysq, hayph, sizeof hayph);

    /* phonetic folds of the individual title/artist words (len >= 3) */
    char words[FS_MAX_WORDS][24];
    int nwords = 0;
    const char *w = tmp;
    while (*w != '\0' && nwords < FS_MAX_WORDS) {
        while (*w != '\0' && !isalnum((unsigned char)*w)) {
            w++;
        }
        char wp[32];
        size_t wo = 0;
        while (*w != '\0' && isalnum((unsigned char)*w)) {
            if (wo + 1 < sizeof wp) {
                wp[wo++] = (char)tolower((unsigned char)*w);
            }
            w++;
        }
        wp[wo] = '\0';
        if (wo >= 3) {
            phonetic_fold(wp, words[nwords++], sizeof words[0]);
        }
    }

    int score = 0;
    const char *p = query;
    while (*p != '\0') {
        while (*p != '\0' && !isalnum((unsigned char)*p)) {
            p++;
        }
        char tok[64];
        size_t o = 0;
        while (*p != '\0' && isalnum((unsigned char)*p)) {
            if (o + 1 < sizeof tok) {
                tok[o++] = (char)tolower((unsigned char)*p);
            }
            p++;
        }
        tok[o] = '\0';
        if (o < 2) {
            continue;
        }
        if (strstr(haysq, tok) != NULL) {
            score += 2 * (int)o;        /* exact substring: strong */
            continue;
        }
        char ph[64];
        phonetic_fold(tok, ph, sizeof ph);
        int phl = (int)strlen(ph);
        if (phl >= 3) {
            /* edit distance between PHONETIC folds: "tokata" is 0 folded
             * edits from "toccata" but 2 from "takatukka" */
            int best_pct = 0;
            for (int i = 0; i < nwords; i++) {
                int wl = (int)strlen(words[i]);
                int m = (phl > wl) ? phl : wl;
                int d = lev(ph, words[i]);
                int pct = (d >= m) ? 0 : 100 - (100 * d) / m;
                if (pct > best_pct) {
                    best_pct = pct;
                }
            }
            if (best_pct >= 80) {
                score += ((int)o * best_pct + 50) / 100;
            }
        }
        if (phl >= 3 && strstr(hayph, ph) != NULL) {
            score += (int)o + (int)o / 2;   /* sounds-like: medium */
            continue;
        }
        if (o >= 4) {
            int grams = (int)o - 1;
            int hits = 0;
            for (int i = 0; i < grams; i++) {
                char g[3] = { tok[i], tok[i + 1], '\0' };
                if (strstr(haysq, g) != NULL) {
                    hits++;
                }
            }
            if (hits * 4 >= grams * 3) { /* >= 75% of bigrams present */
                score += ((int)o * hits) / grams;
                continue;
            }
            if (phl >= 4) {
                int pg = phl - 1;
                int phits = 0;
                for (int i = 0; i < pg; i++) {
                    char g[3] = { ph[i], ph[i + 1], '\0' };
                    if (strstr(hayph, g) != NULL) {
                        phits++;
                    }
                }
                if (phits * 4 >= pg * 3) {
                    score += ((int)o * phits) / (2 * pg);
                }
            }
        }
    }
    char qsq[96];
    squash(query, qsq, sizeof qsq);
    if (strlen(qsq) >= 3 && strstr(haysq, qsq) != NULL) {
        score += 2 * (int)strlen(qsq);
    } else {
        char qph[96];
        phonetic_fold(qsq, qph, sizeof qph);
        if (strlen(qph) >= 4 && strstr(hayph, qph) != NULL) {
            score += (int)strlen(qsq);  /* joined sounds-like: "ram stein" */
        }
    }
    return score;
}

/* Keep the best `cap` fuzzy matches (score > 0) at the front of `l`,
 * freeing the dropped entries; the kept scores land in out_scores[cap]
 * (zeros when unranked). When nothing scores: keep the first `cap` as-is
 * if keep_first_on_zero, else empty the list. */
static void shortlist_songs(const char *query, nav_song_list_t *l, int cap,
                            bool keep_first_on_zero, int *out_scores)
{
    memset(out_scores, 0, cap * sizeof(int));
    if (l->count == 0) {
        return;
    }
    int n = l->count;
    int *score = heap_caps_malloc(n * sizeof(int), MALLOC_CAP_SPIRAM);
    if (score == NULL) {
        goto truncate; /* no memory to rank: just cap the list */
    }
    int positive = 0;
    for (int i = 0; i < n; i++) {
        score[i] = fuzzy_score(query, l->items[i].title, l->items[i].artist);
        if (score[i] > 0) {
            positive++;
        }
    }
    if (positive == 0) {
        heap_caps_free(score);
        if (!keep_first_on_zero) {
            nav_free_songs(l); /* zeroes the struct */
            return;
        }
        goto truncate;
    }
    /* selection of the top `cap` by score, stable for ties */
    int kept = (positive < cap) ? positive : cap;
    for (int k = 0; k < kept; k++) {
        int bestj = k;
        for (int j = k + 1; j < n; j++) {
            if (score[j] > score[bestj]) {
                bestj = j;
            }
        }
        if (bestj != k) {
            nav_song_t ts = l->items[k];
            l->items[k] = l->items[bestj];
            l->items[bestj] = ts;
            int s = score[k];
            score[k] = score[bestj];
            score[bestj] = s;
        }
        out_scores[k] = score[k];
    }
    heap_caps_free(score);
    for (int i = kept; i < n; i++) {
        heap_caps_free(l->items[i].id);
        heap_caps_free(l->items[i].title);
        heap_caps_free(l->items[i].artist);
    }
    l->count = kept;
    return;

truncate:
    if (l->count > cap) {
        for (int i = cap; i < n; i++) {
            heap_caps_free(l->items[i].id);
            heap_caps_free(l->items[i].title);
            heap_caps_free(l->items[i].artist);
        }
        l->count = cap;
    }
}

/* Same artist and one folded title contains the other: remaster/remix
 * duplicates of the same song. */
static bool is_dup_song(const nav_song_t *a, const nav_song_t *b)
{
    char sq[192];
    char fa[96], fb[96];
    squash(a->artist, sq, sizeof sq);
    phonetic_fold(sq, fa, sizeof fa);
    squash(b->artist, sq, sizeof sq);
    phonetic_fold(sq, fb, sizeof fb);
    if (strcmp(fa, fb) != 0) {
        return false;
    }
    char ta[96], tb[96];
    squash(a->title, sq, sizeof sq);
    phonetic_fold(sq, ta, sizeof ta);
    squash(b->title, sq, sizeof sq);
    phonetic_fold(sq, tb, sizeof tb);
    return strstr(ta, tb) != NULL || strstr(tb, ta) != NULL;
}

#define DOMINANT_MIN 8

/* True when items[0] outscores the best NON-duplicate rival 2:1 - a
 * clear local win needs no AI referee (mirror of the python harness). */
static bool top_is_dominant(const nav_song_list_t *l, const int *scores)
{
    if (l->count == 0 || scores[0] < DOMINANT_MIN) {
        return false;
    }
    for (int i = 1; i < l->count; i++) {
        if (is_dup_song(&l->items[0], &l->items[i])) {
            continue;
        }
        return scores[0] >= 2 * scores[i];
    }
    return true; /* nothing but duplicates of the winner */
}

/* Best-matching song: server text search first; when that finds nothing,
 * fuzzy-shortlist the whole library so the AI never sees a huge list. */
static void do_song(const char *text)
{
    if (!network_ready()) {
        return;
    }
    set_status("Searching: %s", text);
    nav_song_list_t songs;
    int scores[SHORTLIST_MAX] = { 0 };
    if (nav_search_songs(text, 40, &songs) != ESP_OK) {
        set_status("Search failed");
        return;
    }
    if (should_stop()) {
        nav_free_songs(&songs);
        return;
    }
    /* Always rank locally - top_is_dominant() needs the scores to skip the
     * AI round-trip, and small result sets are where a clear winner is most
     * likely. (Previously only lists > SHORTLIST_MAX were scored, so the AI
     * was consulted on virtually every request.) */
    shortlist_songs(text, &songs, SHORTLIST_MAX, true, scores);
    if (songs.count == 0) {
        nav_free_songs(&songs);
        set_status("No direct hits - checking the whole library...");
        if (nav_search_songs("", 400, &songs) != ESP_OK) {
            nav_free_songs(&songs);
            set_status("No songs on the server");
            return;
        }
        shortlist_songs(text, &songs, SHORTLIST_MAX, false, scores);
        if (songs.count == 0) {
            set_status("Nothing in the library like \"%s\"", text);
            return;
        }
    }
    if (should_stop()) {
        nav_free_songs(&songs);
        return;
    }

    /* runaway local winner: skip the AI round-trip entirely */
    if (top_is_dominant(&songs, scores)) {
        ESP_LOGI(TAG, "dominant match \"%s\" (score %d), skipping AI",
                 songs.items[0].title, scores[0]);
        nav_song_list_t one = { .items = &songs.items[0], .count = 1 };
        play_list(&one, false, true);
        nav_free_songs(&songs);
        return;
    }

    int best = 0;
    if (songs.count > 1) {
        /* "title - artist" lines for the model */
        char **opts = heap_caps_malloc(songs.count * sizeof(char *),
                                       MALLOC_CAP_SPIRAM);
        if (opts == NULL) {
            nav_free_songs(&songs);
            return;
        }
        for (int i = 0; i < songs.count; i++) {
            size_t n = strlen(songs.items[i].title) +
                       strlen(songs.items[i].artist) + 4;
            opts[i] = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
            if (opts[i] != NULL) {
                snprintf(opts[i], n, "%s - %s", songs.items[i].title,
                         songs.items[i].artist);
            }
        }
        set_status("Asking AI for the best match...");
        esp_err_t err = ai_pick_best(text, (const char *const *)opts,
                                     songs.count, &best);
        for (int i = 0; i < songs.count; i++) {
            heap_caps_free(opts[i]);
        }
        heap_caps_free(opts);
        if (err == ESP_ERR_NOT_FOUND) {
            set_status("AI found no match for \"%s\"", text);
            nav_free_songs(&songs);
            return;
        }
        if (err != ESP_OK) {
            best = 0; /* AI down: fall back to the top search hit */
            set_status("AI unavailable - playing top search hit");
        }
    }

    nav_song_list_t one = { .items = &songs.items[best], .count = 1 };
    play_list(&one, false, true);
    nav_free_songs(&songs);
}

static void play_playlist_by_id(const char *id, const char *name);

static void do_playlist(const char *text)
{
    if (!network_ready()) {
        return;
    }
    set_status("Fetching playlists...");
    nav_playlist_list_t pls;
    if (nav_get_playlists(&pls) != ESP_OK || pls.count == 0) {
        set_status("No playlists on the server");
        return;
    }
    if (should_stop()) {
        nav_free_playlists(&pls);
        return;
    }

    int best = 0;
    if (pls.count > 1) {
        const char **opts = heap_caps_malloc(pls.count * sizeof(char *),
                                             MALLOC_CAP_SPIRAM);
        if (opts == NULL) {
            nav_free_playlists(&pls);
            return;
        }
        for (int i = 0; i < pls.count; i++) {
            opts[i] = pls.items[i].name;
        }
        set_status("Asking AI for the best match...");
        esp_err_t err = ai_pick_best(text, opts, pls.count, &best);
        heap_caps_free(opts);
        if (err == ESP_ERR_NOT_FOUND) {
            set_status("AI found no playlist like \"%s\"", text);
            nav_free_playlists(&pls);
            return;
        }
        if (err != ESP_OK) {
            best = 0;
            set_status("AI unavailable - using first playlist");
        }
    }

    /* copy out before freeing the list the strings live in */
    char id[96], name[96];
    strlcpy(id, pls.items[best].id, sizeof id);
    strlcpy(name, pls.items[best].name, sizeof name);
    nav_free_playlists(&pls);
    play_playlist_by_id(id, name);
}

/* Play one exact playlist - shared by the AI-matched path above and the
 * CMD_PLAYLIST_ID command the web UI uses to play a hand-picked row. */
static void play_playlist_by_id(const char *id, const char *name)
{
    set_status("Playlist: %s", (name != NULL && name[0]) ? name : id);
    nav_song_list_t songs;
    if (nav_get_playlist_songs(id, &songs) != ESP_OK || songs.count == 0) {
        set_status("Playlist is empty");
        return;
    }
    play_list(&songs, true, false);
    nav_free_songs(&songs);
}

static void do_playlist_id(const char *id, const char *name)
{
    if (!network_ready()) {
        return;
    }
    play_playlist_by_id(id, name);
}

/* One exact song: no search, no AI. The title/artist are only for the
 * status line, so a stack-allocated borrowed list is enough (play_list
 * reads the items and never frees them - same pattern as do_song). */
static void do_song_id(const char *id, const char *title, const char *artist)
{
    if (!network_ready()) {
        return;
    }
    nav_song_t song = {
        .id = (char *)id,
        .title = (char *)((title != NULL && title[0]) ? title : id),
        .artist = (char *)((artist != NULL) ? artist : ""),
        .duration = 0,
    };
    nav_song_list_t one = { .items = &song, .count = 1 };
    play_list(&one, false, true);
}

static void do_random(void)
{
    if (!network_ready()) {
        return;
    }
    int fetch_fails = 0;
    while (!should_stop()) {
        set_status("Fetching random songs...");
        nav_song_list_t songs;
        if (nav_get_random_songs(30, &songs) != ESP_OK) {
            /* a transient server/network hiccup must not end random play */
            if (++fetch_fails >= 3) {
                set_status("Music server not responding");
                return;
            }
            set_status("Music server not responding - retrying (%d/3)",
                       fetch_fails);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        if (songs.count == 0) {
            set_status("No songs on the server");
            return;
        }
        fetch_fails = 0;
        /* sequential pass through one random batch, then fetch a new one */
        play_list(&songs, false, false);
        nav_free_songs(&songs);
    }
}

/* ---------- task / API ---------- */

static void music_task(void *arg)
{
    music_cmd_t cmd;
    for (;;) {
        s_state = MUSIC_STATE_IDLE;
        if (xQueueReceive(s_cmds, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        s_abort = false;
        s_state = MUSIC_STATE_BUSY;
        switch (cmd.type) {
        case CMD_SONG:     do_song(cmd.text);     break;
        case CMD_PLAYLIST: do_playlist(cmd.text); break;
        case CMD_RANDOM:   do_random();           break;
        case CMD_SONG_ID:
            do_song_id(cmd.text, cmd.name, cmd.artist);
            break;
        case CMD_PLAYLIST_ID:
            do_playlist_id(cmd.text, cmd.name);
            break;
        }
        if (s_abort) {
            set_status("Stopped");
        } else if (s_state == MUSIC_STATE_PLAYING ||
                   s_state == MUSIC_STATE_PAUSED) {
            set_status("Done");
        }
        set_now_playing("", "");
        audio_song_buf_reap(); /* don't strand a parked buffer while idle */
    }
}

/* Lazy engine bring-up. Returns false (and logs) when any piece could not be
 * allocated - internal RAM is the scarce resource on this board and this is
 * reached from the LVGL, httpd and voice tasks, where a NULL queue handed to
 * xQueueOverwrite would be a configASSERT panic. Nothing is marked inited
 * until everything exists, so the next command retries. */
static bool ensure_task(void)
{
    static bool s_inited;
    if (s_inited) {
        return true;
    }
    app_init_lock(); /* reachable from LVGL, worker and voice_detect tasks */
    if (!s_inited) {
        if (s_ev == NULL) {
            s_ev = xEventGroupCreate();
        }
        if (s_status_mtx == NULL) {
            s_status_mtx = xSemaphoreCreateMutex();
        }
        if (s_cmds == NULL) {
            s_cmds = xQueueCreate(1, sizeof(music_cmd_t));
        }
        bool ok = (s_ev != NULL && s_status_mtx != NULL && s_cmds != NULL);
        if (ok) {
            /* Big stack: TLS to Navidrome + LocalAI run on this task. It
             * lives in PSRAM: this task must NEVER write flash/NVS (no
             * settings_save, no nvs_*) - spi_flash asserts that the calling
             * task's stack is internal. Its call tree (navidrome_client,
             * localai_client, song_cache, ui_log) is network + heap only. */
            /* prio 3: below the LVGL task (4) - TLS search/AI must never
             * time-slice with the UI. Playback itself runs on the separate
             * prio-5 audio task; this task only waits on events. */
            ok = xTaskCreateWithCaps(music_task, "music", 16384, NULL, 3, NULL,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS;
        }
        if (ok) {
            s_inited = true;
        } else {
            /* keep whatever was created for the retry; the task is the only
             * piece that must not exist twice and it is created last */
            ui_log("Music engine init failed (internal RAM?) - try again later");
        }
    }
    app_init_unlock();
    return s_inited;
}

static void submit_full(music_cmd_type_t type, const char *text,
                        const char *name, const char *artist)
{
    if (!ensure_task()) {
        return; /* logged; s_cmds may be NULL */
    }
    music_cmd_t cmd = { .type = type };
    if (text != NULL) {
        strlcpy(cmd.text, text, sizeof cmd.text);
    }
    if (name != NULL) {
        strlcpy(cmd.name, name, sizeof cmd.name);
        utf8_trim_tail(cmd.name);
    }
    if (artist != NULL) {
        strlcpy(cmd.artist, artist, sizeof cmd.artist);
        utf8_trim_tail(cmd.artist);
    }
    s_abort = true;              /* end the current activity... */
    s_abort_dl = true;           /* ...including an in-flight download... */
    xQueueOverwrite(s_cmds, &cmd); /* ...and replace any queued one */
}

static void submit(music_cmd_type_t type, const char *text)
{
    submit_full(type, text, NULL, NULL);
}

void music_play_song_query(const char *text)
{
    submit(CMD_SONG, text);
}

void music_play_playlist_query(const char *text)
{
    submit(CMD_PLAYLIST, text);
}

void music_play_random(void)
{
    submit(CMD_RANDOM, NULL);
}

void music_play_song_id(const char *id, const char *title, const char *artist)
{
    if (id == NULL || id[0] == '\0') {
        return;
    }
    submit_full(CMD_SONG_ID, id, title, artist);
}

void music_play_playlist_id(const char *id, const char *name)
{
    if (id == NULL || id[0] == '\0') {
        return;
    }
    submit_full(CMD_PLAYLIST_ID, id, name, NULL);
}

void music_stop(void)
{
    if (s_cmds != NULL) {
        xQueueReset(s_cmds);
    }
    s_abort = true;
    s_abort_dl = true;
}

void music_toggle_pause(void)
{
    if (s_state == MUSIC_STATE_PLAYING &&
        audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING) {
        if (audio_player_pause() == ESP_OK) {
            s_state = MUSIC_STATE_PAUSED;
            set_status("Paused");
        }
    } else if (s_state == MUSIC_STATE_PAUSED) {
        if (audio_player_resume() == ESP_OK) {
            s_state = MUSIC_STATE_PLAYING;
            char title[96], artist[96];
            if (music_now_playing(title, sizeof title, artist, sizeof artist)) {
                if (artist[0] != '\0') {
                    set_status("Playing: %s - %s", title, artist);
                } else {
                    set_status("Playing: %s", title);
                }
            }
        }
    }
    /* anything else (resolving, loading between songs, idle): no-op */
}

void music_next(void)
{
    if (s_ev == NULL ||
        (s_state != MUSIC_STATE_PLAYING && s_state != MUSIC_STATE_PAUSED)) {
        return;
    }
    s_abort_dl = true; /* kills an in-flight download */
    xEventGroupSetBits(s_ev, BIT_NEXT);
}

void music_prev(void)
{
    if (s_ev == NULL ||
        (s_state != MUSIC_STATE_PLAYING && s_state != MUSIC_STATE_PAUSED)) {
        return;
    }
    s_abort_dl = true;
    xEventGroupSetBits(s_ev, BIT_PREV);
}

bool music_stop_sync(int timeout_ms)
{
    music_stop();
    for (int waited = 0; s_state != MUSIC_STATE_IDLE && waited < timeout_ms;
         waited += 100) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return s_state == MUSIC_STATE_IDLE;
}
