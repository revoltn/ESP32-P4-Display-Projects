/* Optional local HTTP control API (esp_http_server, port 80).
 *
 * Stability contract: this module must never destabilize the clock /
 * playback / display, so it
 *   - runs only when explicitly enabled (settings api_enabled + api_key),
 *   - starts/stops exclusively on the worker task via a reconcile job
 *     (event handlers and UI callbacks only post the job; a slow periodic
 *     timer re-posts it if the worker queue was full),
 *   - is capped at 2 client sockets (1 listen + 2 + 1 ctrl UDP = 4 of the
 *     10 lwIP sockets; Navidrome TLS streaming keeps plenty of headroom),
 *     with LRU purge so an idle keep-alive client can't wedge the slots,
 *   - serves at priority 3 on core 1 - strictly below LVGL (4), music (4)
 *     and the worker (5), and off the core carrying LVGL + esp_hosted,
 *   - keeps request/response buffers in PSRAM and caps bodies at 2 KB,
 *   - mutates app state only through the existing thread-safe entry points
 *     (music_* commands, settings_lock/save, alarm_voice_*, codec/backlight
 *     setters) and touches LVGL only under bsp_display_lock with a timeout,
 *   - stays down after >= 3 abnormal resets (crash-loop breaker) until the
 *     user re-enables it in Settings.
 *
 * Known quirk (accepted): a PATCH while the on-device Settings screen sits
 * on a dirty page can be overwritten by a later on-device Save, because the
 * page's commit re-reads its widgets.
 *
 * Endpoints (JSON; auth via "X-Api-Key: <key>" or "Authorization: Bearer"):
 *   GET   /                  embedded control page (static, no auth)
 *   GET   /api/v1/status     clock / wifi / playback / alarm snapshot; the
 *                            alarm object: ringing, ringing_index (1..3, 0 =
 *                            none), remaining_s (until auto-stop), enabled
 *                            (any slot), snooze_until, max_alarm_seconds,
 *                            next {index,epoch,in_s,hh,mm,wday,snooze}|null,
 *                            missed {index,epoch}|null
 *   GET   /api/v1/settings   all settings except secrets (booleans instead)
 *   PATCH /api/v1/settings   partial update, applied live + saved to NVS.
 *                            Alarms: alarm1_enabled|hh|mm|days .. alarm3_*
 *                            (days = weekday bits, bit0 Sunday .. bit6
 *                            Saturday, +128 = ring once then switch off; an
 *                            empty weekday set is stored as every day) and
 *                            max_alarm_seconds (0 = never, else 30..7200).
 *                            alarm_enabled/alarm_hh/alarm_mm are DEPRECATED
 *                            aliases of alarm 1 (alarm1_* wins if both given).
 *   GET   /api/v1/events     the persistent event journal (evlog)
 *   GET   /api/v1/log        the in-RAM ui_log text (text/plain)
 *   GET   /api/v1/usb        USB audio descriptor survey (text/plain):
 *                            what a plugged-in USB speaker advertises and
 *                            which format actually starts
 *   GET   /api/v1/songs      ?q=<text>[&limit=N] Navidrome song search
 *   GET   /api/v1/playlists  the server's playlists (for the picker)
 *   POST  /api/v1/playback   {"action":"play|play_playlist|play_random|
 *                             play_song_id|play_playlist_id|
 *                             stop|pause|next|prev","query":"...","id":"..."}
 *   POST  /api/v1/controls   {"action":"snooze|dismiss|volume_up|volume_down|
 *                             brightness_up|brightness_down"} or
 *                            {"volume":0-100} / {"brightness":1-100} or
 *                            {"action":"open_settings_page","page":0-9}
 *                            (test hook: 4 = Alarms, 9 = Alarm sound)
 *   GET   /api/v1/voice/clips        wake / recording clips held in RAM:
 *                            items [{id,kind:"wake"|"rec",t,up,ms,bytes,
 *                            verdict:"none|pending|ok|reject",text}]
 *   GET   /api/v1/voice/clips/<id>   that clip as audio/wav (16 kHz mono)
 *   POST  /api/v1/voice/clips/clear  forget them all
 *   POST  /api/v1/voice/record       {"seconds":1-8}: record through the
 *                            real mic + gain + AFE, reply audio/wav (also
 *                            kept as a clip). Blocks for the duration;
 *                            409 while the mic is paused (music playing).
 *                            Settings: voice_wake_thr (40-99 %),
 *                            voice_wake_verify, voice_wake_collect,
 *                            voice_wake_word (an id from the read-only
 *                            voice_wake_words [{id,name}] list; 400 otherwise).
 *
 * The two search endpoints call Navidrome over HTTPS from the httpd task,
 * which is why its stack matches the other TLS-calling tasks (16 KB); the
 * nav_* JSON calls serialize on their own mutex, so sharing them with the
 * music task is safe.
 */
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"
#include "cJSON.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"

#include "app_common.h"
#include "settings.h"
#include "wifi_mgr.h"
#include "clock_sync.h"
#include "music_player.h"
#include "navidrome_client.h"
#include "alarm.h"
#include "usb_audio.h"
#include "evlog.h"
#include "voice.h"
#include "wake_mww.h"
#include "ui_clock.h"
#include "ui_settings.h"
#include "boot_seq.h"
#include "api_server.h"
#include "lvgl.h"

static const char *TAG = "api_server";

#define API_BODY_CAP        2048
#define SEARCH_MAX          25    /* songs per search response; playlists
                                     are never many, so they are uncapped */
#define API_PORT            80
#define RECONCILE_PERIOD_US (60 * 1000000ULL)
#define CRASH_SUPPRESS_MIN  3

static httpd_handle_t s_httpd;          /* NULL when down */
static SemaphoreHandle_t s_mtx;         /* serializes start/stop */
static esp_timer_handle_t s_reconcile_timer;
static bool s_suppressed;               /* crash-loop breaker tripped */
static bool s_inited;

/* ---------- lifecycle ---------- */

static bool desired_up(void)
{
    if (s_suppressed) {
        return false;
    }
    bedside_settings_t *cfg = settings_get();
    if (!cfg->api_enabled) {
        return false;
    }
    settings_lock();
    bool key_set = cfg->api_key[0] != '\0';
    settings_unlock();
    return key_set;
}

static esp_err_t api_dispatch(httpd_req_t *req);

/* Embedded control page (main/www/index.html via EMBED_TXTFILES; the
 * symbols are named after the basename, and a NUL is appended). Served
 * unauthenticated: it is static and secret-free - every API call it makes
 * goes through api_dispatch's auth. */
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

static esp_err_t handle_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    /* no validator machinery, and the page must never go stale across a
     * reflash - it is ~20 KB on the LAN, just refetch */
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, index_html_start,
                           index_html_end - index_html_start - 1);
}

static void start_server(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = API_PORT;
    cfg.max_open_sockets = 2;      /* + listen + ctrl = 4 of 10 lwIP sockets */
    cfg.lru_purge_enable = true;   /* idle clients can't wedge the slots */
    cfg.stack_size = 16384;        /* TLS to Navidrome (search endpoints) */
    cfg.task_priority = 3;         /* below LVGL (4) and worker (5); level
                                    * with music/song_dl (3) */
    cfg.core_id = 1;               /* core 0 carries LVGL + esp_hosted */
    cfg.max_uri_handlers = 5;  /* three /api wildcards + GET / + spare */
    cfg.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&s_httpd, &cfg);
    if (err != ESP_OK) {
        s_httpd = NULL;
        ui_log("API server start failed: %s", esp_err_to_name(err));
        return;
    }
    httpd_uri_t u = { .uri = "/api/*", .handler = api_dispatch };
    u.method = HTTP_GET;
    httpd_register_uri_handler(s_httpd, &u);
    u.method = HTTP_POST;
    httpd_register_uri_handler(s_httpd, &u);
    u.method = HTTP_PATCH;
    httpd_register_uri_handler(s_httpd, &u);
    /* "/" has no wildcard, so the wildcard matcher requires an exact match:
     * only GET / serves the page, everything else stays 404 */
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET,
                         .handler = handle_root };
    httpd_register_uri_handler(s_httpd, &root);
    char ip[16];
    wifi_mgr_ip(ip, sizeof(ip));
    ui_log("API server up: http://%s/", ip);
}

static void stop_server(void)
{
    httpd_handle_t h = s_httpd;
    s_httpd = NULL;
    httpd_stop(h); /* blocks until the server task exits; worker task only */
    ui_log("API server stopped");
}

/* Worker-task job: make reality match the settings. Idempotent. */
static void api_reconcile_job(void *arg)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    bool want = desired_up();
    if (want && s_httpd == NULL && wifi_mgr_is_connected()) {
        start_server();
    } else if (!want && s_httpd != NULL) {
        stop_server();
    }
    xSemaphoreGive(s_mtx);
}

static void post_reconcile(void)
{
    /* Drop-tolerant: the periodic timer re-posts on mismatch. */
    if (!app_run_async(api_reconcile_job, NULL)) {
        ESP_LOGW(TAG, "reconcile deferred: job queue full");
    }
}

/* esp_timer task: stay short, decide cheaply, hand work to the worker. */
static void reconcile_timer_cb(void *arg)
{
    bedside_settings_t *cfg = settings_get();
    bool want = !s_suppressed && cfg->api_enabled && cfg->api_key[0] != '\0';
    bool up = s_httpd != NULL;
    if (want != up) {
        post_reconcile();
    }
}

/* Event task: just kick a reconcile once WiFi has an IP. */
static void got_ip_handler(void *arg, esp_event_base_t base, int32_t id,
                           void *data)
{
    post_reconcile();
}

void api_server_init(uint32_t crash_count)
{
    if (s_inited) {
        return;
    }
    s_mtx = xSemaphoreCreateMutex();
    if (s_mtx == NULL) {
        ESP_LOGE(TAG, "mutex alloc failed, API unavailable");
        return;
    }
    s_suppressed = crash_count >= CRASH_SUPPRESS_MIN;
    if (s_suppressed && settings_get()->api_enabled) {
        ui_log("API auto-start suppressed after %lu abnormal resets",
               (unsigned long)crash_count);
    }

    /* One boot-time line saying why the server will (not) come up, so a
     * closed port 80 is never mistaken for a start failure (start_server
     * logs those separately). Mirrors desired_up(). */
    {
        bedside_settings_t *cfg = settings_get();
        settings_lock();
        bool key_set = cfg->api_key[0] != '\0';
        settings_unlock();
        if (!cfg->api_enabled) {
            ui_log("API: disabled in Settings");
        } else if (!key_set) {
            ui_log("API: no key set, disabled");
        } else if (!s_suppressed) {
            ui_log("API: enabled, starts when WiFi is up");
        }
    }

    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        got_ip_handler, NULL, NULL);

    const esp_timer_create_args_t targs = {
        .callback = reconcile_timer_cb,
        .name = "api_reconcile",
    };
    if (esp_timer_create(&targs, &s_reconcile_timer) == ESP_OK) {
        esp_timer_start_periodic(s_reconcile_timer, RECONCILE_PERIOD_US);
    }
    s_inited = true;
    api_reconcile_job(NULL); /* already on the worker task (boot_job) */
}

void api_server_set_enabled(bool en)
{
    if (!s_inited) {
        return;
    }
    if (en) {
        s_suppressed = false; /* explicit user action re-arms after crashes */
    }
    post_reconcile();
}

bool api_server_is_running(void)
{
    return s_httpd != NULL;
}

/* Derived entirely from existing state - the reconcile job is the single
 * writer, so there is nothing extra to keep in sync. A crash-loop suppression
 * reads OFF, not FAIL: the server is deliberately down, not broken.
 *
 * Called from the 1 s LVGL tick, so it must not take settings_lock() the way
 * desired_up() does; the single-byte reads are the same ones reconcile_timer_cb
 * already makes unlocked from the esp_timer task. */
svc_state_t api_service_state(void)
{
    bedside_settings_t *cfg = settings_get();
    if (s_suppressed || !cfg->api_enabled || cfg->api_key[0] == '\0') {
        return SVC_OFF;
    }
    if (s_httpd != NULL) {
        return SVC_UP;
    }
    return wifi_mgr_is_connected() ? SVC_FAIL : SVC_WAIT;
}

/* ---------- request helpers ---------- */

static void log_client(httpd_req_t *req, const char *what)
{
    char ip[46] = "?";
    int fd = httpd_req_to_sockfd(req);
    struct sockaddr_storage addr;
    socklen_t alen = sizeof(addr);
    if (fd >= 0 && getpeername(fd, (struct sockaddr *)&addr, &alen) == 0) {
        if (addr.ss_family == AF_INET) {
            inet_ntop(AF_INET, &((struct sockaddr_in *)&addr)->sin_addr,
                      ip, sizeof(ip));
        }
#if LWIP_IPV6
        else if (addr.ss_family == AF_INET6) {
            inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&addr)->sin6_addr,
                      ip, sizeof(ip));
        }
#endif
    }
    ESP_LOGW(TAG, "%s (client %s)", what, ip);
}

static esp_err_t send_err(httpd_req_t *req, const char *status,
                          const char *msg)
{
    char body[112];
    snprintf(body, sizeof(body), "{\"error\":\"%s\"}", msg);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

/* Sends and deletes obj (even on failure). */
static esp_err_t send_json(httpd_req_t *req, cJSON *obj)
{
    char *out = obj != NULL ? cJSON_PrintUnformatted(obj) : NULL;
    cJSON_Delete(obj);
    if (out == NULL) {
        return send_err(req, "500 Internal Server Error", "out of memory");
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t r = httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
    free(out);
    return r;
}

/* Constant-time key comparison over fixed-size zero-padded buffers. */
static bool key_equal_ct(const char *presented, const char *expected)
{
    uint8_t pa[64] = {0};
    uint8_t pb[64] = {0};
    size_t la = strlen(presented);
    size_t lb = strlen(expected);
    if (la > sizeof(pa) - 1) {
        la = sizeof(pa) - 1;
    }
    if (lb > sizeof(pb) - 1) {
        lb = sizeof(pb) - 1;
    }
    memcpy(pa, presented, la);
    memcpy(pb, expected, lb);
    unsigned diff = (unsigned)(la ^ lb);
    for (size_t i = 0; i < sizeof(pa); i++) {
        diff |= (unsigned)(pa[i] ^ pb[i]);
    }
    return diff == 0;
}

static bool check_auth(httpd_req_t *req)
{
    char presented[80] = "";
    if (httpd_req_get_hdr_value_str(req, "X-Api-Key", presented,
                                    sizeof(presented)) != ESP_OK) {
        char auth[96] = "";
        if (httpd_req_get_hdr_value_str(req, "Authorization", auth,
                                        sizeof(auth)) == ESP_OK &&
            strncasecmp(auth, "Bearer ", 7) == 0) {
            strlcpy(presented, auth + 7, sizeof(presented));
        }
    }

    char expected[sizeof(((bedside_settings_t *)0)->api_key)];
    settings_lock();
    strlcpy(expected, settings_get()->api_key, sizeof(expected));
    settings_unlock();

    /* Empty stored key = locked out, even if the server is somehow up. */
    if (expected[0] == '\0' || presented[0] == '\0') {
        return false;
    }
    return key_equal_ct(presented, expected);
}

/* Reads the request body into a PSRAM buffer (NUL-terminated). On any
 * problem an error response has already been sent and NULL is returned. */
static char *read_body(httpd_req_t *req)
{
    int len = req->content_len;
    if (len <= 0) {
        send_err(req, "400 Bad Request", "missing body");
        return NULL;
    }
    if (len > API_BODY_CAP) {
        send_err(req, "413 Content Too Large", "body too large");
        return NULL;
    }
    char *buf = heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        send_err(req, "500 Internal Server Error", "out of memory");
        return NULL;
    }
    int got = 0;
    while (got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        if (r <= 0) {
            heap_caps_free(buf);
            if (r != HTTPD_SOCK_ERR_TIMEOUT) {
                return NULL; /* socket gone; nothing to respond to */
            }
            send_err(req, "408 Request Timeout", "body read timeout");
            return NULL;
        }
        got += r;
    }
    buf[len] = '\0';
    return buf;
}

static const char *music_state_str(music_state_t st)
{
    switch (st) {
    case MUSIC_STATE_BUSY:    return "busy";
    case MUSIC_STATE_PLAYING: return "playing";
    case MUSIC_STATE_PAUSED:  return "paused";
    case MUSIC_STATE_IDLE:
    default:                  return "idle";
    }
}

/* Shared with the Play screen's slider semantics (ui_play.c vol_cb). */
static void apply_volume(int v)
{
    settings_get()->volume = (uint8_t)v;
    /* httpd task: never wait on the codec here (init mutex, or the decoder's
     * mutex while a song plays) - that could park one of our two sockets.
     * The worker applies it; the stored value is applied by the codec init
     * itself when it is not up yet. */
    app_audio_set_volume_async(v);
}

/* ---------- GET /api/v1/status ---------- */

static esp_err_t handle_status(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return send_err(req, "500 Internal Server Error", "out of memory");
    }
    bedside_settings_t *cfg = settings_get();

    time_t now = time(NULL);
    struct tm tm;
    clock_sync_localtime(&now, &tm);
    char tstr[32];
    strftime(tstr, sizeof(tstr), "%Y-%m-%d %H:%M:%S", &tm);
    cJSON *jt = cJSON_AddObjectToObject(root, "time");
    cJSON_AddNumberToObject(jt, "epoch", (double)now);
    cJSON_AddStringToObject(jt, "local", tstr);
    cJSON_AddBoolToObject(jt, "valid", clock_sync_time_valid());

    cJSON *jw = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddBoolToObject(jw, "connected", wifi_mgr_is_connected());
    char ip[16];
    wifi_mgr_ip(ip, sizeof(ip));
    cJSON_AddStringToObject(jw, "ip", ip);
    cJSON_AddStringToObject(jw, "ssid", cfg->wifi_ssid);

    cJSON *jp = cJSON_AddObjectToObject(root, "playback");
    cJSON_AddStringToObject(jp, "state", music_state_str(music_state()));
    char title[96] = "", artist[96] = "", status[128] = "";
    if (music_now_playing(title, sizeof(title), artist, sizeof(artist))) {
        cJSON_AddStringToObject(jp, "title", title);
        cJSON_AddStringToObject(jp, "artist", artist);
    } else {
        cJSON_AddNullToObject(jp, "title");
        cJSON_AddNullToObject(jp, "artist");
    }
    if (music_status(status, sizeof(status))) {
        cJSON_AddStringToObject(jp, "status", status);
    } else {
        cJSON_AddNullToObject(jp, "status"); /* status lock contended */
    }

    cJSON *ja = cJSON_AddObjectToObject(root, "alarm");
    {
        bool any = false;
        for (int i = 0; i < ALARM_COUNT; i++) {
            any = any || cfg->alarms[i].enabled;
        }
        int ring_idx = alarm_ringing_index();
        cJSON_AddBoolToObject(ja, "enabled", any);
        cJSON_AddBoolToObject(ja, "ringing", ring_idx >= 0);
        cJSON_AddNumberToObject(ja, "ringing_index", ring_idx >= 0 ? ring_idx + 1 : 0);
        cJSON_AddNumberToObject(ja, "remaining_s", alarm_ring_remaining_s());
        cJSON_AddNumberToObject(ja, "max_alarm_seconds", cfg->max_alarm_seconds);
        alarm_next_t nx;
        if (alarm_next(&nx)) {
            struct tm ntm;
            clock_sync_localtime(&nx.at, &ntm);
            cJSON *jn = cJSON_AddObjectToObject(ja, "next");
            cJSON_AddNumberToObject(jn, "index", nx.index + 1);
            cJSON_AddNumberToObject(jn, "epoch", (double)nx.at);
            cJSON_AddNumberToObject(jn, "in_s", (double)(nx.at - now));
            cJSON_AddNumberToObject(jn, "hh", ntm.tm_hour);
            cJSON_AddNumberToObject(jn, "mm", ntm.tm_min);
            cJSON_AddNumberToObject(jn, "wday", ntm.tm_wday);
            cJSON_AddBoolToObject(jn, "snooze", nx.snooze);
            cJSON_AddNumberToObject(ja, "snooze_until",
                                    nx.snooze ? (double)nx.at : 0);
        } else {
            cJSON_AddNullToObject(ja, "next");
            cJSON_AddNumberToObject(ja, "snooze_until", 0);
        }
        alarm_next_t ms;
        if (alarm_last_missed(&ms)) {
            cJSON *jm = cJSON_AddObjectToObject(ja, "missed");
            cJSON_AddNumberToObject(jm, "index", ms.index + 1);
            cJSON_AddNumberToObject(jm, "epoch", (double)ms.at);
        } else {
            cJSON_AddNullToObject(ja, "missed");
        }
    }

    cJSON_AddNumberToObject(root, "volume", cfg->volume);

    /* USB audio (phase 1: detection only - nothing is routed there yet).
     * Every accessor is lock-free or bounded; this runs on the httpd task. */
    cJSON *ju = cJSON_AddObjectToObject(root, "usb_audio");
    cJSON_AddNumberToObject(ju, "state", usb_audio_service_state());
    cJSON_AddBoolToObject(ju, "present", usb_audio_present());
    char uprod[40];
    uint16_t uvid = 0, upid = 0;
    uint32_t urate = 0;
    int uch = 0;
    if (usb_audio_device_info(uprod, sizeof uprod, &uvid, &upid, &urate, &uch)) {
        cJSON_AddStringToObject(ju, "product", uprod);
        cJSON_AddNumberToObject(ju, "vid", uvid);
        cJSON_AddNumberToObject(ju, "pid", upid);
        cJSON_AddNumberToObject(ju, "rate", urate);
        cJSON_AddNumberToObject(ju, "channels", uch);
    } else {
        cJSON_AddNullToObject(ju, "product");
        cJSON_AddNullToObject(ju, "rate");
        cJSON_AddNullToObject(ju, "channels");
    }

    cJSON *jh = cJSON_AddObjectToObject(root, "heap");
    cJSON_AddNumberToObject(jh, "internal_free",
                            heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(jh, "internal_largest",
                            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    /* DMA-capable internal RAM: what I2S/SDIO/camera descriptors need */
    cJSON_AddNumberToObject(jh, "dma_free",
                            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    cJSON_AddNumberToObject(jh, "dma_largest",
                            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    cJSON_AddNumberToObject(jh, "psram_free",
                            heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    return send_json(req, root);
}

/* ---------- GET/PATCH /api/v1/settings ---------- */

/* Everything except secrets; secrets appear as *_set booleans only. */
static cJSON *settings_to_json(void)
{
    bedside_settings_t snap;
    settings_lock();
    snap = *settings_get();
    settings_unlock();

    cJSON *j = cJSON_CreateObject();
    if (j == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(j, "volume", snap.volume);
    cJSON_AddNumberToObject(j, "alarm_volume", snap.alarm_volume);
    cJSON_AddNumberToObject(j, "bright_min", snap.bright_min);
    cJSON_AddNumberToObject(j, "bright_max", snap.bright_max);
    cJSON_AddNumberToObject(j, "ambient_period_s", snap.ambient_period_s);
    for (int i = 0; i < ALARM_COUNT; i++) {
        char key[24];
        snprintf(key, sizeof(key), "alarm%d_enabled", i + 1);
        cJSON_AddBoolToObject(j, key, snap.alarms[i].enabled != 0);
        snprintf(key, sizeof(key), "alarm%d_hh", i + 1);
        cJSON_AddNumberToObject(j, key, snap.alarms[i].hh);
        snprintf(key, sizeof(key), "alarm%d_mm", i + 1);
        cJSON_AddNumberToObject(j, key, snap.alarms[i].mm);
        snprintf(key, sizeof(key), "alarm%d_days", i + 1);
        cJSON_AddNumberToObject(j, key, snap.alarms[i].days);
    }
    cJSON_AddNumberToObject(j, "max_alarm_seconds", snap.max_alarm_seconds);
    /* deprecated pre-v9 names: read-only mirror of alarm 1 */
    cJSON_AddBoolToObject(j, "alarm_enabled", snap.alarms[0].enabled != 0);
    cJSON_AddNumberToObject(j, "alarm_hh", snap.alarms[0].hh);
    cJSON_AddNumberToObject(j, "alarm_mm", snap.alarms[0].mm);
    cJSON_AddNumberToObject(j, "alarm_mode", snap.alarm_mode);
    cJSON_AddStringToObject(j, "alarm_playlist_name", snap.alarm_playlist_name);
    cJSON_AddNumberToObject(j, "clock_face", snap.clock_face);
    cJSON_AddNumberToObject(j, "orientation", snap.orientation);
    cJSON_AddNumberToObject(j, "tz_offset_min", snap.tz_offset_min);
    cJSON_AddBoolToObject(j, "voice_enabled", snap.voice_enabled != 0);
    cJSON_AddNumberToObject(j, "voice_mic_gain", snap.voice_mic_gain);
    cJSON_AddNumberToObject(j, "voice_wake_thr", snap.voice_wake_thr);
    cJSON_AddBoolToObject(j, "voice_wake_verify", snap.voice_wake_verify != 0);
    cJSON_AddBoolToObject(j, "voice_wake_collect", snap.voice_wake_collect != 0);
    cJSON_AddStringToObject(j, "voice_wake_word", snap.voice_wake_word);
    /* the embedded models a client may pick from (read-only) */
    cJSON *words = cJSON_AddArrayToObject(j, "voice_wake_words");
    for (int i = 0; words != NULL && i < wake_mww_model_count(); i++) {
        cJSON *w = cJSON_CreateObject();
        if (w == NULL) {
            break;
        }
        cJSON_AddStringToObject(w, "id", wake_mww_model_id(i));
        cJSON_AddStringToObject(w, "name", wake_mww_model_name(i));
        cJSON_AddItemToArray(words, w);
    }
    cJSON_AddStringToObject(j, "wifi_ssid", snap.wifi_ssid);
    cJSON_AddBoolToObject(j, "wifi_pass_set", snap.wifi_pass[0] != '\0');
    cJSON_AddStringToObject(j, "nav_url", snap.nav_url);
    cJSON_AddStringToObject(j, "nav_user", snap.nav_user);
    cJSON_AddBoolToObject(j, "nav_pass_set", snap.nav_pass[0] != '\0');
    cJSON_AddStringToObject(j, "ai_url", snap.ai_url);
    cJSON_AddStringToObject(j, "ai_model", snap.ai_model);
    cJSON_AddBoolToObject(j, "ai_key_set", snap.ai_key[0] != '\0');
    cJSON_AddBoolToObject(j, "api_enabled", snap.api_enabled != 0);
    cJSON_AddBoolToObject(j, "api_key_set", snap.api_key[0] != '\0');
    cJSON_AddNumberToObject(j, "svc_poll_s", snap.svc_poll_s);
    return j;
}

static esp_err_t handle_settings_get(httpd_req_t *req)
{
    cJSON *j = settings_to_json();
    if (j == NULL) {
        return send_err(req, "500 Internal Server Error", "out of memory");
    }
    return send_json(req, j);
}

/* Validated staging for PATCH: absent fields keep *out untouched. The
 * returned false means an invalid value; *bad names the field. */
static bool stage_int(cJSON *root, const char *name, int lo, int hi,
                      bool *has, int *out, const char **bad)
{
    cJSON *it = cJSON_GetObjectItemCaseSensitive(root, name);
    if (it == NULL) {
        return true;
    }
    int v;
    if (cJSON_IsNumber(it)) {
        v = (int)it->valuedouble;
    } else if (cJSON_IsBool(it)) {
        v = cJSON_IsTrue(it) ? 1 : 0;
    } else {
        *bad = name;
        return false;
    }
    if (v < lo || v > hi) {
        *bad = name;
        return false;
    }
    *has = true;
    *out = v;
    return true;
}

static bool stage_str(cJSON *root, const char *name, size_t cap,
                      bool *has, const char **out, const char **bad)
{
    cJSON *it = cJSON_GetObjectItemCaseSensitive(root, name);
    if (it == NULL) {
        return true;
    }
    if (!cJSON_IsString(it) || strlen(it->valuestring) >= cap) {
        *bad = name;
        return false;
    }
    *has = true;
    *out = it->valuestring;
    return true;
}

static esp_err_t handle_settings_patch(httpd_req_t *req)
{
    char *body = read_body(req);
    if (body == NULL) {
        return ESP_OK; /* error response already sent (or socket gone) */
    }
    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return send_err(req, "400 Bad Request", "invalid JSON object");
    }

    bedside_settings_t *cfg = settings_get();
    const char *bad = NULL;
    bool ok = true;

    /* stage ints/bools */
    struct {
        const char *name;
        int lo, hi;
        bool has;
        int v;
    } ints[] = {
        { .name = "volume",         .lo = 0,    .hi = 100 },
        { .name = "alarm_volume",   .lo = 0,    .hi = 100 },
        { .name = "bright_min",     .lo = 1,    .hi = 100 },
        { .name = "bright_max",     .lo = 1,    .hi = 100 },
        { .name = "alarm_enabled",  .lo = 0,    .hi = 1   },
        { .name = "alarm_hh",       .lo = 0,    .hi = 23  },
        { .name = "alarm_mm",       .lo = 0,    .hi = 59  },
        { .name = "alarm_mode",     .lo = 0,    .hi = 1   },
        { .name = "clock_face",     .lo = 0,    .hi = 1   },
        { .name = "orientation",    .lo = 0,    .hi = 2   },
        { .name = "tz_offset_min",  .lo = -720, .hi = 840 },
        { .name = "voice_enabled",  .lo = 0,    .hi = 1   },
        { .name = "voice_mic_gain", .lo = 0,    .hi = 42  },
        { .name = "api_enabled",    .lo = 0,    .hi = 1   },
        /* 0 = off; 1..MIN-1 is accepted and normalised up to MIN */
        { .name = "svc_poll_s",     .lo = 0,    .hi = SVC_POLL_MAX_S },
        { .name = "ambient_period_s", .lo = AMBIENT_PERIOD_MIN_S, .hi = AMBIENT_PERIOD_MAX_S },
        /* per-alarm slots; days = weekday bits (+128 once), any byte value
         * is accepted and normalised by settings_norm_days */
#define ALARM_ROWS(n) \
        { .name = "alarm" #n "_enabled", .lo = 0, .hi = 1   }, \
        { .name = "alarm" #n "_hh",      .lo = 0, .hi = 23  }, \
        { .name = "alarm" #n "_mm",      .lo = 0, .hi = 59  }, \
        { .name = "alarm" #n "_days",    .lo = 0, .hi = 255 }
        ALARM_ROWS(1), ALARM_ROWS(2), ALARM_ROWS(3),
#undef ALARM_ROWS
        /* 0 = never; 1..MIN-1 normalised up to MIN */
        { .name = "max_alarm_seconds", .lo = 0, .hi = ALARM_MAX_RING_MAX_S },
        { .name = "voice_wake_thr",     .lo = VOICE_WAKE_THR_MIN, .hi = VOICE_WAKE_THR_MAX },
        { .name = "voice_wake_verify",  .lo = 0, .hi = 1 },
        { .name = "voice_wake_collect", .lo = 0, .hi = 1 },
    };
    enum { I_VOLUME, I_AVOL, I_BMIN, I_BMAX, I_AEN, I_AHH, I_AMM, I_AMODE,
           I_FACE, I_ORIENT, I_TZ, I_VEN, I_VGAIN, I_APIEN, I_SVCPOLL, I_APER,
           I_A1EN, I_A1HH, I_A1MM, I_A1DAYS,
           I_A2EN, I_A2HH, I_A2MM, I_A2DAYS,
           I_A3EN, I_A3HH, I_A3MM, I_A3DAYS,
           I_MAXRING,
           I_VTHR, I_VVERIFY, I_VCOLLECT,
           I_COUNT };
    _Static_assert(I_A1EN + 4 * ALARM_COUNT == I_MAXRING, "alarm rows/enum drift");
    for (int i = 0; ok && i < I_COUNT; i++) {
        ok = stage_int(root, ints[i].name, ints[i].lo, ints[i].hi,
                       &ints[i].has, &ints[i].v, &bad);
    }

    /* stage strings (secrets included: write-only) */
    struct {
        const char *name;
        size_t cap;
        bool has;
        const char *v;
    } strs[] = {
        { .name = "wifi_ssid", .cap = sizeof(cfg->wifi_ssid) },
        { .name = "wifi_pass", .cap = sizeof(cfg->wifi_pass) },
        { .name = "nav_url",   .cap = sizeof(cfg->nav_url)   },
        { .name = "nav_user",  .cap = sizeof(cfg->nav_user)  },
        { .name = "nav_pass",  .cap = sizeof(cfg->nav_pass)  },
        { .name = "ai_url",    .cap = sizeof(cfg->ai_url)    },
        { .name = "ai_key",    .cap = sizeof(cfg->ai_key)    },
        { .name = "ai_model",  .cap = sizeof(cfg->ai_model)  },
        { .name = "api_key",   .cap = sizeof(cfg->api_key)   },
        { .name = "voice_wake_word", .cap = sizeof(cfg->voice_wake_word) },
    };
    enum { S_SSID, S_WPASS, S_NURL, S_NUSER, S_NPASS, S_AIURL, S_AIKEY,
           S_AIMODEL, S_APIKEY, S_VWAKE, S_COUNT };
    for (int i = 0; ok && i < S_COUNT; i++) {
        ok = stage_str(root, strs[i].name, strs[i].cap,
                       &strs[i].has, &strs[i].v, &bad);
    }
    /* must name an embedded model (GET lists them as voice_wake_words) */
    if (ok && strs[S_VWAKE].has && wake_mww_model_index(strs[S_VWAKE].v) < 0) {
        ok = false;
        bad = "voice_wake_word";
    }

    if (!ok) {
        cJSON_Delete(root);
        char msg[64];
        snprintf(msg, sizeof(msg), "invalid value for %s", bad);
        return send_err(req, "400 Bad Request", msg);
    }

    /* write everything in one locked batch */
    settings_lock();
    if (ints[I_VOLUME].has) cfg->volume       = (uint8_t)ints[I_VOLUME].v;
    if (ints[I_AVOL].has)   cfg->alarm_volume = (uint8_t)ints[I_AVOL].v;
    if (ints[I_BMIN].has)   cfg->bright_min   = (uint8_t)ints[I_BMIN].v;
    if (ints[I_BMAX].has)   cfg->bright_max   = (uint8_t)ints[I_BMAX].v;
    if (cfg->bright_max < cfg->bright_min) { /* keep the pair sane */
        cfg->bright_max = cfg->bright_min;
    }
    /* deprecated single-alarm names map onto alarm 1; the alarm1_* keys
     * below win when both are present in one request */
    if (ints[I_AEN].has)    cfg->alarms[0].enabled = ints[I_AEN].v != 0;
    if (ints[I_AHH].has)    cfg->alarms[0].hh = (uint8_t)ints[I_AHH].v;
    if (ints[I_AMM].has)    cfg->alarms[0].mm = (uint8_t)ints[I_AMM].v;
    for (int a = 0; a < ALARM_COUNT; a++) {
        int base = I_A1EN + 4 * a;
        if (ints[base + 0].has) cfg->alarms[a].enabled = (uint8_t)ints[base + 0].v;
        if (ints[base + 1].has) cfg->alarms[a].hh = (uint8_t)ints[base + 1].v;
        if (ints[base + 2].has) cfg->alarms[a].mm = (uint8_t)ints[base + 2].v;
        if (ints[base + 3].has) cfg->alarms[a].days = settings_norm_days(ints[base + 3].v);
    }
    if (ints[I_MAXRING].has) cfg->max_alarm_seconds = settings_norm_max_ring(ints[I_MAXRING].v);
    if (ints[I_AMODE].has)  cfg->alarm_mode   = (uint8_t)ints[I_AMODE].v;
    if (ints[I_FACE].has)   cfg->clock_face   = (uint8_t)ints[I_FACE].v;
    if (ints[I_ORIENT].has) cfg->orientation  = (uint8_t)ints[I_ORIENT].v;
    if (ints[I_TZ].has)     cfg->tz_offset_min = (int16_t)ints[I_TZ].v;
    if (ints[I_VEN].has)    cfg->voice_enabled = (uint8_t)ints[I_VEN].v;
    if (ints[I_VGAIN].has)  cfg->voice_mic_gain = (uint8_t)ints[I_VGAIN].v;
    if (ints[I_VTHR].has)   cfg->voice_wake_thr = (uint8_t)ints[I_VTHR].v;
    if (ints[I_VVERIFY].has) cfg->voice_wake_verify = (uint8_t)ints[I_VVERIFY].v;
    if (ints[I_VCOLLECT].has) cfg->voice_wake_collect = (uint8_t)ints[I_VCOLLECT].v;
    if (ints[I_APIEN].has)  cfg->api_enabled  = (uint8_t)ints[I_APIEN].v;
    if (ints[I_APER].has)   cfg->ambient_period_s = (uint16_t)ints[I_APER].v;
    if (ints[I_SVCPOLL].has) cfg->svc_poll_s  = settings_norm_poll(ints[I_SVCPOLL].v);
    if (strs[S_SSID].has)   strlcpy(cfg->wifi_ssid, strs[S_SSID].v, sizeof(cfg->wifi_ssid));
    if (strs[S_WPASS].has)  strlcpy(cfg->wifi_pass, strs[S_WPASS].v, sizeof(cfg->wifi_pass));
    if (strs[S_NURL].has)   strlcpy(cfg->nav_url,   strs[S_NURL].v,  sizeof(cfg->nav_url));
    if (strs[S_NUSER].has)  strlcpy(cfg->nav_user,  strs[S_NUSER].v, sizeof(cfg->nav_user));
    if (strs[S_NPASS].has)  strlcpy(cfg->nav_pass,  strs[S_NPASS].v, sizeof(cfg->nav_pass));
    if (strs[S_AIURL].has)  strlcpy(cfg->ai_url,    strs[S_AIURL].v, sizeof(cfg->ai_url));
    if (strs[S_AIKEY].has)  strlcpy(cfg->ai_key,    strs[S_AIKEY].v, sizeof(cfg->ai_key));
    if (strs[S_AIMODEL].has) strlcpy(cfg->ai_model, strs[S_AIMODEL].v, sizeof(cfg->ai_model));
    if (strs[S_APIKEY].has) strlcpy(cfg->api_key,   strs[S_APIKEY].v, sizeof(cfg->api_key));
    if (strs[S_VWAKE].has)  strlcpy(cfg->voice_wake_word, strs[S_VWAKE].v, sizeof(cfg->voice_wake_word));
    settings_unlock();
    cJSON_Delete(root);

    /* live-apply, same calls the settings UI makes */
    bool ui_applied = true;
    if (ints[I_VOLUME].has) {
        apply_volume(cfg->volume);
    }
    if (ints[I_BMIN].has || ints[I_BMAX].has) {
        /* preview; the ambient task re-glides within the new range */
        bsp_display_brightness_set(cfg->bright_max);
    }
    if (ints[I_VEN].has) {
        voice_set_enabled(cfg->voice_enabled != 0);
    }
    if (ints[I_VGAIN].has) {
        voice_apply_mic_gain();
    }
    if (ints[I_VTHR].has) {
        voice_apply_wake_threshold();
    }
    if (strs[S_VWAKE].has) {
        voice_apply_wake_word();
    }
    if (ints[I_TZ].has) {
        clock_sync_start(); /* re-applies TZ; SNTP already running */
    }
    if (ints[I_FACE].has || ints[I_ORIENT].has) {
        if (app_display_ready() && bsp_display_lock(500)) {
            if (ints[I_FACE].has) {
                ui_clock_apply_face();
            }
            if (ints[I_ORIENT].has) {
                app_apply_orientation();
            }
            bsp_display_unlock();
        } else {
            ui_applied = false; /* saved; takes effect on next boot/UI touch */
        }
    }
    /* alarm*_* / max_alarm_seconds: the alarm task re-reads cfg every second
     * and re-arms on any slot change - no apply call */

    settings_save();

    cJSON *resp = cJSON_CreateObject();
    if (resp == NULL) {
        return send_err(req, "500 Internal Server Error", "out of memory");
    }
    cJSON_AddBoolToObject(resp, "ok", true);
    if (!ui_applied) {
        cJSON_AddBoolToObject(resp, "ui_applied", false);
    }
    if (strs[S_SSID].has || strs[S_WPASS].has) {
        cJSON_AddStringToObject(resp, "note",
                                "wifi changes apply on next reconnect/boot");
    }
    cJSON *jset = settings_to_json();
    if (jset != NULL) {
        cJSON_AddItemToObject(resp, "settings", jset);
    }
    esp_err_t r = send_json(req, resp);

    /* After the response is on the wire: the server may need to stop or
     * (re)start itself. Runs on the worker task - no self-deadlock. */
    if (ints[I_APIEN].has || strs[S_APIKEY].has) {
        post_reconcile();
    }
    /* New server credentials or a new check interval: refresh the clock
     * face's Music/AI indicators now rather than at the next sweep. */
    if (ints[I_SVCPOLL].has || strs[S_NURL].has || strs[S_NUSER].has ||
        strs[S_NPASS].has || strs[S_AIURL].has || strs[S_AIKEY].has) {
        boot_net_probe_kick();
    }
    return r;
}

/* ---------- POST /api/v1/playback ---------- */

static esp_err_t handle_playback(httpd_req_t *req)
{
    char *body = read_body(req);
    if (body == NULL) {
        return ESP_OK;
    }
    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return send_err(req, "400 Bad Request", "invalid JSON object");
    }
    cJSON *ja = cJSON_GetObjectItemCaseSensitive(root, "action");
    cJSON *jq = cJSON_GetObjectItemCaseSensitive(root, "query");
    const char *action = cJSON_IsString(ja) ? ja->valuestring : NULL;
    const char *query = cJSON_IsString(jq) ? jq->valuestring : NULL;

    esp_err_t r;
    if (action == NULL) {
        r = send_err(req, "400 Bad Request", "missing action");
    } else if (strcmp(action, "play_random") == 0) {
        music_play_random();
        r = ESP_FAIL; /* sentinel: send ok below */
    } else if (strcmp(action, "play") == 0 ||
               strcmp(action, "play_playlist") == 0) {
        if (query == NULL || query[0] == '\0') {
            r = send_err(req, "400 Bad Request", "missing query");
        } else {
            if (action[4] == '\0') { /* "play" */
                music_play_song_query(query);   /* copies into a 96B cmd */
            } else {
                music_play_playlist_query(query);
            }
            r = ESP_FAIL;
        }
    } else if (strcmp(action, "play_song_id") == 0 ||
               strcmp(action, "play_playlist_id") == 0) {
        /* exact pick from the search/list endpoints: no AI matching */
        cJSON *jid = cJSON_GetObjectItemCaseSensitive(root, "id");
        if (!cJSON_IsString(jid) || jid->valuestring[0] == '\0') {
            r = send_err(req, "400 Bad Request", "missing id");
        } else {
            cJSON *jn = cJSON_GetObjectItemCaseSensitive(root, "title");
            if (jn == NULL) {
                jn = cJSON_GetObjectItemCaseSensitive(root, "name");
            }
            cJSON *ja2 = cJSON_GetObjectItemCaseSensitive(root, "artist");
            const char *name = cJSON_IsString(jn) ? jn->valuestring : NULL;
            const char *artist = cJSON_IsString(ja2) ? ja2->valuestring : NULL;
            if (strcmp(action, "play_song_id") == 0) {
                music_play_song_id(jid->valuestring, name, artist);
            } else {
                music_play_playlist_id(jid->valuestring, name);
            }
            r = ESP_FAIL;
        }
    } else if (strcmp(action, "stop") == 0) {
        music_stop();
        r = ESP_FAIL;
    } else if (strcmp(action, "pause") == 0) {
        music_toggle_pause();
        r = ESP_FAIL;
    } else if (strcmp(action, "next") == 0) {
        music_next();
        r = ESP_FAIL;
    } else if (strcmp(action, "prev") == 0) {
        music_prev();
        r = ESP_FAIL;
    } else {
        r = send_err(req, "400 Bad Request", "unknown action");
    }
    cJSON_Delete(root);
    if (r != ESP_FAIL) {
        return r;
    }

    cJSON *resp = cJSON_CreateObject();
    if (resp == NULL) {
        return send_err(req, "500 Internal Server Error", "out of memory");
    }
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "state", music_state_str(music_state()));
    return send_json(req, resp);
}

/* ---------- POST /api/v1/controls ---------- */

/* LVGL task (lv_async_call): same calls the voice "open <page>" path makes */
static void open_page_async_cb(void *arg)
{
    int page = (int)(intptr_t)arg;
    if (!ui_settings_is_open()) {
        ui_settings_open();
    }
    ui_settings_voice_open_page(page);
}

static esp_err_t handle_controls(httpd_req_t *req)
{
    char *body = read_body(req);
    if (body == NULL) {
        return ESP_OK;
    }
    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return send_err(req, "400 Bad Request", "invalid JSON object");
    }
    cJSON *ja = cJSON_GetObjectItemCaseSensitive(root, "action");
    cJSON *jv = cJSON_GetObjectItemCaseSensitive(root, "volume");
    cJSON *jb = cJSON_GetObjectItemCaseSensitive(root, "brightness");
    const char *action = cJSON_IsString(ja) ? ja->valuestring : NULL;
    bedside_settings_t *cfg = settings_get();

    esp_err_t r = ESP_FAIL; /* sentinel: send the ok response below */
    if (cJSON_IsNumber(jv)) {
        int v = (int)jv->valuedouble;
        if (v < 0 || v > 100) {
            r = send_err(req, "400 Bad Request", "invalid value for volume");
        } else {
            apply_volume(v);
        }
    } else if (cJSON_IsNumber(jb)) {
        int v = (int)jb->valuedouble;
        if (v < 1 || v > 100) {
            r = send_err(req, "400 Bad Request",
                         "invalid value for brightness");
        } else {
            /* same semantics as the Brightness page's day ceiling: ambient
             * dimming re-adjusts within the new range */
            if (v < cfg->bright_min) {
                v = cfg->bright_min;
            }
            cfg->bright_max = (uint8_t)v;
            bsp_display_brightness_set(v);
        }
    } else if (action == NULL) {
        r = send_err(req, "400 Bad Request", "missing action");
    } else if (strcmp(action, "open_settings_page") == 0) {
        /* Test/automation hook: open Settings on page N exactly as a tap
         * would - the builder runs on the LVGL task via lv_async_call, not
         * on the httpd task (page ids as in ui_settings_voice_open_page;
         * -1 = close/back to the clock is not offered). */
        cJSON *jp = cJSON_GetObjectItemCaseSensitive(root, "page");
        int page = cJSON_IsNumber(jp) ? (int)jp->valuedouble : -1;
        if (page < 0 || page > 9) {
            r = send_err(req, "400 Bad Request", "page must be 0..9");
        } else if (!app_display_ready() || !bsp_display_lock(500)) {
            r = send_err(req, "503 Service Unavailable", "display busy");
        } else {
            lv_async_call(open_page_async_cb, (void *)(intptr_t)page);
            bsp_display_unlock();
        }
    } else if (strcmp(action, "snooze") == 0 ||
               strcmp(action, "dismiss") == 0) {
        if (!alarm_is_active()) {
            r = send_err(req, "409 Conflict", "no alarm ringing");
        } else if (action[0] == 's') {
            alarm_voice_snooze();
        } else {
            alarm_voice_dismiss();
        }
    } else if (strcmp(action, "volume_up") == 0 ||
               strcmp(action, "volume_down") == 0) {
        int v = cfg->volume + (strcmp(action, "volume_up") == 0 ? 10 : -10);
        v = v < 0 ? 0 : (v > 100 ? 100 : v);
        apply_volume(v);
    } else if (strcmp(action, "brightness_up") == 0 ||
               strcmp(action, "brightness_down") == 0) {
        int v = cfg->bright_max +
                (strcmp(action, "brightness_up") == 0 ? 10 : -10);
        v = v < cfg->bright_min ? cfg->bright_min : (v > 100 ? 100 : v);
        cfg->bright_max = (uint8_t)v;
        bsp_display_brightness_set(v);
    } else {
        r = send_err(req, "400 Bad Request", "unknown action");
    }
    cJSON_Delete(root);
    if (r != ESP_FAIL) {
        return r;
    }

    /* live like the side-panel controls: not persisted to flash here;
     * use PATCH /settings to persist */
    cJSON *resp = cJSON_CreateObject();
    if (resp == NULL) {
        return send_err(req, "500 Internal Server Error", "out of memory");
    }
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddNumberToObject(resp, "volume", cfg->volume);
    cJSON_AddNumberToObject(resp, "brightness", cfg->bright_max);
    return send_json(req, resp);
}

/* ---------- GET /api/v1/songs, GET /api/v1/playlists ---------- */

/* In-place %XX / '+' decoding for query-string values. */
static void url_decode(char *s)
{
    char *w = s;
    for (const char *r = s; *r != '\0'; r++) {
        if (*r == '+') {
            *w++ = ' ';
        } else if (*r == '%' && isxdigit((unsigned char)r[1]) &&
                   isxdigit((unsigned char)r[2])) {
            char hex[3] = { r[1], r[2], '\0' };
            *w++ = (char)strtol(hex, NULL, 16);
            r += 2;
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';
}

static esp_err_t handle_songs(httpd_req_t *req)
{
    char qs[256] = "";
    char q[96] = "";
    int limit = 10;
    if (httpd_req_get_url_query_str(req, qs, sizeof(qs)) == ESP_OK) {
        char val[128];
        if (httpd_query_key_value(qs, "q", val, sizeof(val)) == ESP_OK) {
            url_decode(val);
            strlcpy(q, val, sizeof(q));
        }
        if (httpd_query_key_value(qs, "limit", val, sizeof(val)) == ESP_OK) {
            limit = atoi(val);
        }
    }
    if (limit < 1) {
        limit = 1;
    } else if (limit > SEARCH_MAX) {
        limit = SEARCH_MAX;
    }
    /* an empty search3 query is "the whole library" on Navidrome - refuse
     * rather than pull hundreds of rows for a keystroke */
    if (q[0] == '\0') {
        return send_err(req, "400 Bad Request", "missing q");
    }
    if (!nav_is_configured()) {
        return send_err(req, "503 Service Unavailable",
                        "music server not configured");
    }

    nav_song_list_t list;
    if (nav_search_songs(q, limit, &list) != ESP_OK) {
        return send_err(req, "502 Bad Gateway", "search failed");
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *items = root ? cJSON_AddArrayToObject(root, "items") : NULL;
    if (items == NULL) {
        nav_free_songs(&list);
        cJSON_Delete(root);
        return send_err(req, "500 Internal Server Error", "out of memory");
    }
    int n = list.count < limit ? list.count : limit;
    for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_CreateObject();
        if (it == NULL) {
            break;
        }
        cJSON_AddStringToObject(it, "id", list.items[i].id);
        cJSON_AddStringToObject(it, "title", list.items[i].title);
        cJSON_AddStringToObject(it, "artist", list.items[i].artist);
        cJSON_AddNumberToObject(it, "duration", list.items[i].duration);
        cJSON_AddItemToArray(items, it);
    }
    cJSON_AddStringToObject(root, "query", q);
    nav_free_songs(&list);
    return send_json(req, root);
}

static esp_err_t handle_playlists(httpd_req_t *req)
{
    if (!nav_is_configured()) {
        return send_err(req, "503 Service Unavailable",
                        "music server not configured");
    }
    nav_playlist_list_t list;
    if (nav_get_playlists(&list) != ESP_OK) {
        return send_err(req, "502 Bad Gateway", "playlist fetch failed");
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *items = root ? cJSON_AddArrayToObject(root, "items") : NULL;
    if (items == NULL) {
        nav_free_playlists(&list);
        cJSON_Delete(root);
        return send_err(req, "500 Internal Server Error", "out of memory");
    }
    /* all of them: a library has tens of playlists, and the page shows the
     * complete list (only songs are capped) */
    for (int i = 0; i < list.count; i++) {
        cJSON *it = cJSON_CreateObject();
        if (it == NULL) {
            break;
        }
        cJSON_AddStringToObject(it, "id", list.items[i].id);
        cJSON_AddStringToObject(it, "name", list.items[i].name);
        cJSON_AddNumberToObject(it, "song_count", list.items[i].song_count);
        cJSON_AddNumberToObject(it, "duration", list.items[i].duration);
        cJSON_AddItemToArray(items, it);
    }
    cJSON_AddNumberToObject(root, "total", list.count);
    nav_free_playlists(&list);
    return send_json(req, root);
}

/* GET /api/v1/events: the persistent event journal, oldest first.
 * t = epoch seconds (0 = clock was not valid), up = uptime seconds. */
static esp_err_t handle_events(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *items = root ? cJSON_AddArrayToObject(root, "items") : NULL;
    if (items == NULL) {
        cJSON_Delete(root);
        return send_err(req, "500 Internal Server Error", "out of memory");
    }
    int n = evlog_count();
    for (int i = 0; i < n; i++) {
        evlog_entry_t e;
        if (!evlog_get(i, &e)) {
            break;
        }
        cJSON *it = cJSON_CreateObject();
        if (it == NULL) {
            break;
        }
        cJSON_AddNumberToObject(it, "t", (double)e.epoch);
        cJSON_AddNumberToObject(it, "up", e.uptime_s);
        cJSON_AddStringToObject(it, "code", evlog_code_str(e.code));
        cJSON_AddStringToObject(it, "text", e.text);
        cJSON_AddItemToArray(items, it);
    }
    cJSON_AddNumberToObject(root, "total", n);
    return send_json(req, root);
}

/* GET /api/v1/log: the in-RAM ui_log text (what the Specs page shows, ~16 KB
 * of the newest lines, lost on reboot). Complements /events: the journal
 * says WHAT happened, this has the detailed lines around it. */
#define LOG_COPY_CAP (16 * 1024 + 64)
static esp_err_t handle_log(httpd_req_t *req)
{
    char *buf = heap_caps_malloc(LOG_COPY_CAP, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        return send_err(req, "500 Internal Server Error", "out of memory");
    }
    size_t n = ui_log_copy(buf, LOG_COPY_CAP);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t r = httpd_resp_send(req, buf, (ssize_t)n);
    heap_caps_free(buf);
    return r;
}

/* GET /api/v1/usb: the USB audio descriptor survey as plain text - identity,
 * every advertised format, and which one actually started. This exists
 * because the board's USB port is the port the PC uses, so a speaker can only
 * be attached with the serial console gone; uac_host_printf_device_param()
 * writes to that console and is therefore useless here. */
#define USB_REPORT_CAP 2048
static esp_err_t handle_usb(httpd_req_t *req)
{
    char *buf = heap_caps_malloc(USB_REPORT_CAP, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        return send_err(req, "500 Internal Server Error", "out of memory");
    }
    size_t n = usb_audio_report(buf, USB_REPORT_CAP);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t r = httpd_resp_send(req, buf, (ssize_t)n);
    heap_caps_free(buf);
    return r;
}

/* ---------- /api/v1/voice/... : wake clips + on-demand recording ---------- */

static const char *verdict_str(voice_verdict_t v)
{
    switch (v) {
    case VOICE_VERDICT_PENDING: return "pending";
    case VOICE_VERDICT_OK:      return "ok";
    case VOICE_VERDICT_REJECT:  return "reject";
    case VOICE_VERDICT_NONE:
    default:                    return "none";
    }
}

static esp_err_t handle_voice_clips(httpd_req_t *req)
{
    voice_clip_info_t *list = heap_caps_malloc(sizeof(*list) * 16, MALLOC_CAP_SPIRAM);
    cJSON *root = cJSON_CreateObject();
    cJSON *items = root ? cJSON_AddArrayToObject(root, "items") : NULL;
    if (list == NULL || items == NULL) {
        heap_caps_free(list);
        cJSON_Delete(root);
        return send_err(req, "500 Internal Server Error", "out of memory");
    }
    int n = voice_clips_list(list, 16);
    for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_CreateObject();
        if (it == NULL) {
            break;
        }
        cJSON_AddNumberToObject(it, "id", list[i].id);
        cJSON_AddStringToObject(it, "kind", list[i].kind == VOICE_CLIP_REC ? "rec" : "wake");
        cJSON_AddNumberToObject(it, "t", (double)list[i].epoch);
        cJSON_AddNumberToObject(it, "up", list[i].uptime_s);
        cJSON_AddNumberToObject(it, "ms", list[i].ms);
        cJSON_AddNumberToObject(it, "bytes", (double)list[i].len);
        cJSON_AddStringToObject(it, "verdict", verdict_str(list[i].verdict));
        cJSON_AddStringToObject(it, "text", list[i].text);
        cJSON_AddItemToArray(items, it);
    }
    heap_caps_free(list);
    cJSON_AddNumberToObject(root, "total", n);
    cJSON_AddNumberToObject(root, "max", 16);
    return send_json(req, root);
}

/* Whole WAV from PSRAM in one send; clips are <= ~256 KB. */
static esp_err_t send_wav(httpd_req_t *req, uint8_t *wav, size_t len)
{
    httpd_resp_set_type(req, "audio/wav");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t r = httpd_resp_send(req, (const char *)wav, (ssize_t)len);
    heap_caps_free(wav);
    return r;
}

static esp_err_t handle_voice_clip_wav(httpd_req_t *req, uint32_t id)
{
    size_t len = voice_clip_copy(id, NULL, 0);
    if (len == 0) {
        return send_err(req, "404 Not Found", "no such clip");
    }
    uint8_t *wav = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (wav == NULL) {
        return send_err(req, "500 Internal Server Error", "out of memory");
    }
    if (voice_clip_copy(id, wav, len) != len) {
        heap_caps_free(wav);
        return send_err(req, "404 Not Found", "clip evicted");
    }
    return send_wav(req, wav, len);
}

static esp_err_t handle_voice_record(httpd_req_t *req)
{
    int seconds = 3;
    if (req->content_len > 0) {
        char *body = read_body(req);
        if (body == NULL) {
            return ESP_OK;
        }
        cJSON *root = cJSON_Parse(body);
        heap_caps_free(body);
        cJSON *s = root ? cJSON_GetObjectItemCaseSensitive(root, "seconds") : NULL;
        if (cJSON_IsNumber(s)) {
            seconds = (int)s->valuedouble;
        }
        cJSON_Delete(root);
    }
    if (seconds < 1 || seconds * 1000 > VOICE_REC_MAX_MS) {
        return send_err(req, "400 Bad Request", "seconds must be 1..8");
    }
    if (voice_service_state() != SVC_UP) {
        return send_err(req, "409 Conflict", "voice pipeline not listening");
    }
    log_client(req, "voice recording requested");
    uint32_t id = voice_record_clip(seconds * 1000);
    if (id == 0) {
        return send_err(req, "503 Service Unavailable", "recording failed or busy");
    }
    size_t len = voice_clip_copy(id, NULL, 0);
    uint8_t *wav = len ? heap_caps_malloc(len, MALLOC_CAP_SPIRAM) : NULL;
    if (wav == NULL || voice_clip_copy(id, wav, len) != len) {
        heap_caps_free(wav);
        return send_err(req, "500 Internal Server Error", "clip vanished");
    }
    char hdr[16];
    snprintf(hdr, sizeof(hdr), "%lu", (unsigned long)id);
    httpd_resp_set_hdr(req, "X-Clip-Id", hdr);
    return send_wav(req, wav, len);
}

static esp_err_t dispatch_voice(httpd_req_t *req, const char *path)
{
    if (strcmp(path, "/api/v1/voice/clips") == 0) {
        if (req->method == HTTP_GET) {
            return handle_voice_clips(req);
        }
        return send_err(req, "405 Method Not Allowed", "use GET");
    }
    if (strcmp(path, "/api/v1/voice/clips/clear") == 0) {
        if (req->method == HTTP_POST) {
            voice_clips_clear();
            cJSON *ok = cJSON_CreateObject();
            if (ok != NULL) {
                cJSON_AddBoolToObject(ok, "ok", true);
            }
            return send_json(req, ok);
        }
        return send_err(req, "405 Method Not Allowed", "use POST");
    }
    if (strncmp(path, "/api/v1/voice/clips/", 20) == 0) {
        char *end = NULL;
        unsigned long id = strtoul(path + 20, &end, 10);
        if (end == path + 20 || *end != '\0' || id == 0) {
            return send_err(req, "404 Not Found", "bad clip id");
        }
        if (req->method == HTTP_GET) {
            return handle_voice_clip_wav(req, (uint32_t)id);
        }
        return send_err(req, "405 Method Not Allowed", "use GET");
    }
    if (strcmp(path, "/api/v1/voice/record") == 0) {
        if (req->method == HTTP_POST) {
            return handle_voice_record(req);
        }
        return send_err(req, "405 Method Not Allowed", "use POST");
    }
    return send_err(req, "404 Not Found", "unknown endpoint");
}

/* ---------- dispatch (single auth choke point) ---------- */

static esp_err_t api_dispatch(httpd_req_t *req)
{
    if (!check_auth(req)) {
        log_client(req, "unauthorized API request");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
        return send_err(req, "401 Unauthorized", "unauthorized");
    }

    char path[64];
    strlcpy(path, req->uri, sizeof(path));
    char *q = strchr(path, '?');
    if (q != NULL) {
        *q = '\0';
    }

    if (strcmp(path, "/api/v1/status") == 0) {
        if (req->method == HTTP_GET) {
            return handle_status(req);
        }
        return send_err(req, "405 Method Not Allowed", "use GET");
    }
    if (strcmp(path, "/api/v1/settings") == 0) {
        if (req->method == HTTP_GET) {
            return handle_settings_get(req);
        }
        if (req->method == HTTP_PATCH) {
            return handle_settings_patch(req);
        }
        return send_err(req, "405 Method Not Allowed", "use GET or PATCH");
    }
    if (strcmp(path, "/api/v1/playback") == 0) {
        if (req->method == HTTP_POST) {
            return handle_playback(req);
        }
        return send_err(req, "405 Method Not Allowed", "use POST");
    }
    if (strcmp(path, "/api/v1/songs") == 0) {
        if (req->method == HTTP_GET) {
            return handle_songs(req);
        }
        return send_err(req, "405 Method Not Allowed", "use GET");
    }
    if (strcmp(path, "/api/v1/playlists") == 0) {
        if (req->method == HTTP_GET) {
            return handle_playlists(req);
        }
        return send_err(req, "405 Method Not Allowed", "use GET");
    }
    if (strcmp(path, "/api/v1/controls") == 0) {
        if (req->method == HTTP_POST) {
            return handle_controls(req);
        }
        return send_err(req, "405 Method Not Allowed", "use POST");
    }
    if (strcmp(path, "/api/v1/events") == 0) {
        if (req->method == HTTP_GET) {
            return handle_events(req);
        }
        return send_err(req, "405 Method Not Allowed", "use GET");
    }
    if (strcmp(path, "/api/v1/usb") == 0) {
        if (req->method == HTTP_GET) {
            return handle_usb(req);
        }
        return send_err(req, "405 Method Not Allowed", "use GET");
    }
    if (strcmp(path, "/api/v1/log") == 0) {
        if (req->method == HTTP_GET) {
            return handle_log(req);
        }
        return send_err(req, "405 Method Not Allowed", "use GET");
    }
    if (strncmp(path, "/api/v1/voice/", 14) == 0) {
        return dispatch_voice(req, path);
    }
    return send_err(req, "404 Not Found", "unknown endpoint");
}
