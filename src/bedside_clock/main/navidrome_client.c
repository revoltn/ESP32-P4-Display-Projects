/* Subsonic REST client for Navidrome over HTTPS (esp_http_client + cert
 * bundle). Auth is per-request: t = md5(password + salt) with a fresh salt,
 * per the Subsonic API spec. Runs on the worker task (needs its 16 KB stack
 * for the TLS handshake).
 *
 * Failure handling (2026-09-02): every HTTP path goes through the same
 * read/fetch-headers wrappers, which tolerate a "server silent" timeout
 * (-ESP_ERR_HTTP_EAGAIN: the connection is still valid, IDF just gave up
 * waiting for NAV_HTTP_TIMEOUT_MS) and, on any real failure, log ONE
 * diagnostic line with the classified cause, errno, TLS error, byte count,
 * elapsed time, attempt number and WiFi RSSI to both serial and the Device
 * Specifications page. The streaming path additionally reconnects and
 * resumes with an HTTP Range request. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_rom_md5.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_tls_errors.h"
#include "mbedtls/error.h"
#include "cJSON.h"

#include "app_common.h"
#include "settings.h"
#include "wifi_mgr.h"
#include "boot_seq.h"
#include "navidrome_client.h"
#include "evlog.h"

static const char *TAG = "navidrome";

static volatile int s_link_state = -1;   /* see nav_link_state() */
static volatile int64_t s_last_ok_us;    /* esp_timer time of the last "ok" */

int nav_link_state(void)
{
    return s_link_state;
}

int64_t nav_last_ok_us(void)
{
    return s_last_ok_us;
}

/* Every path that learns the server's state reports it through these two,
 * so the indicator's "seen within the check window" clock is kept by real
 * traffic (searches, playlists, song streams) as well as by the probe. */
static volatile int s_last_api_error;    /* see nav_last_api_error() */

int nav_last_api_error(void)
{
    return s_last_api_error;
}

static void nav_note_ok(void)
{
    s_last_ok_us = esp_timer_get_time();
    s_link_state = 1;
    s_last_api_error = 0;
}

static void nav_note_fail(void)
{
    s_link_state = 0;
    s_last_api_error = 0; /* nav_parse_and_check overwrites with the code */
}

/* Server URL and credentials come from the persisted settings. */
#define NAV_CLIENT          "bedside"
#define NAV_API_VER         "1.16.1"
#define NAV_HTTP_TIMEOUT_MS 15000
#define NAV_SONG_CAP        (16 * 1024 * 1024)
#define NAV_SONG_MIN        4096              /* smaller than this = not a song */
#define NAV_SONG_START      (4 * 1024 * 1024) /* no Content-Length: covers a
                                                 typical 128 kbps track without
                                                 any realloc-and-copy */
#define NAV_JSON_CAP        (2 * 1024 * 1024) /* sanity cap for API responses */
#define NAV_READ_CHUNK      8192

/* Retry policy. An EAGAIN re-read costs another NAV_HTTP_TIMEOUT_MS of
 * blocking, so the alarm path (which must fall back to the chime promptly)
 * gets none. The streaming path may also reconnect: bounded by a count AND
 * a wall-clock budget measured from the first failure, so the song_cache
 * reader (READ_STALL_MS there) always outlives the downloader's retries. */
#define NAV_EAGAIN_RETRIES_STREAM 1
#define NAV_EAGAIN_RETRIES_JSON   1
#define NAV_EAGAIN_RETRIES_ALARM  0
#define NAV_RETRY_BUDGET_MS       45000
#define NAV_API_MUTEX_WAIT_MS     10000 /* wait for another task's API call */
#define NAV_MAX_RECONNECTS        2
static const int NAV_BACKOFF_MS[NAV_MAX_RECONNECTS] = { 1000, 3000 };

/* ---------- small helpers ---------- */

static void *psram_alloc(size_t n)
{
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
}

static char *psram_strdup(const char *s)
{
    if (s == NULL) {
        s = "";
    }
    size_t n = strlen(s) + 1;
    char *d = psram_alloc(n);
    if (d != NULL) {
        memcpy(d, s, n);
    }
    return d;
}

static void hex_encode(const uint8_t *in, size_t n, char *out)
{
    static const char HEX[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i]     = HEX[in[i] >> 4];
        out[2 * i + 1] = HEX[in[i] & 0x0F];
    }
    out[2 * n] = '\0';
}

bool nav_is_configured(void)
{
    const bedside_settings_t *cfg = settings_get();
    return cfg->nav_url[0] != '\0' && cfg->nav_user[0] != '\0';
}

/* Build "<base>/rest/<endpoint>?u=..&t=..&s=..&v=..&c=..&f=json<extra_qs>". */
static void nav_build_url(char *out, size_t cap, const char *endpoint,
                          const char *extra_qs)
{
    const bedside_settings_t *cfg = settings_get();

    uint8_t salt_raw[8];
    esp_fill_random(salt_raw, sizeof salt_raw);
    char salt[17];
    hex_encode(salt_raw, sizeof salt_raw, salt);

    md5_context_t ctx;
    uint8_t digest[16];
    esp_rom_md5_init(&ctx);
    esp_rom_md5_update(&ctx, (const uint8_t *)cfg->nav_pass, strlen(cfg->nav_pass));
    esp_rom_md5_update(&ctx, (const uint8_t *)salt, strlen(salt));
    esp_rom_md5_final(digest, &ctx);
    char token[33];
    hex_encode(digest, sizeof digest, token);

    snprintf(out, cap, "%s/rest/%s?u=%s&t=%s&s=%s&v=%s&c=%s&f=json%s",
             cfg->nav_url, endpoint, cfg->nav_user, token, salt, NAV_API_VER,
             NAV_CLIENT, extra_qs != NULL ? extra_qs : "");
}

/* ---------- transfer diagnostics + retry primitives ----------
 *
 * esp_http_client_read() / _fetch_headers() return (IDF 5.5.5):
 *   > 0                  bytes
 *   0                    body framing complete (Content-Length met / last chunk)
 *   -ESP_ERR_HTTP_EAGAIN timeout_ms elapsed with NOTHING buffered; the
 *                        connection and parser state are intact - just call
 *                        again (a stall while Navidrome starts ffmpeg looks
 *                        exactly like this)
 *   ESP_FAIL (-1)        transport/TLS error, or the server closed the
 *                        connection (FIN) with the body incomplete
 * The details of a hard failure live in the transport error handle and are
 * READ-ONCE (get_errno clears; get_and_clear_last_tls_error clears), so
 * nav_diag_log() is called exactly once per failure. */

typedef struct {
    const char *what;      /* "api" | "download" | "stream" */
    const char *song_id;   /* NULL for API calls */
    const char *format;    /* "raw" | "mp3" | "-" */
    int64_t t_open_us;     /* esp_timer_get_time() right after open */
    size_t bytes;          /* body bytes received in this attempt */
    int attempt;           /* 1-based connection attempt */
} nav_xfer_t;

/* Sleep `ms` in 100 ms slices; false as soon as *abort_flag goes true.
 * Every retry path sleeps through here, so the task watchdog's idle task
 * always gets CPU (this task runs at priority 4). */
static bool nav_sleep_abortable(int ms, volatile bool *abort_flag)
{
    for (int waited = 0; waited < ms; waited += 100) {
        if (abort_flag != NULL && *abort_flag) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return !(abort_flag != NULL && *abort_flag);
}

static bool nav_aborted(volatile bool *abort_flag)
{
    return abort_flag != NULL && *abort_flag;
}

/* esp_http_client_read() that rides out up to `eagain_max` silent timeouts.
 * Returns bytes (>0), 0 at end of body, -ESP_ERR_HTTP_EAGAIN if the server
 * stayed silent through every retry, -ESP_ERR_NOT_FINISHED if aborted, or
 * ESP_FAIL for a hard error. */
static int nav_read_retry(esp_http_client_handle_t c, void *buf, size_t cap,
                          int eagain_max, volatile bool *abort_flag)
{
    for (int tries = 0;; tries++) {
        int n = esp_http_client_read(c, (char *)buf, (int)cap);
        if (n != -ESP_ERR_HTTP_EAGAIN || tries >= eagain_max) {
            return n;
        }
        if (nav_aborted(abort_flag)) {
            return -ESP_ERR_NOT_FINISHED;
        }
        ESP_LOGW(TAG, "server silent for %d ms - waiting again (%d/%d)",
                 NAV_HTTP_TIMEOUT_MS, tries + 1, eagain_max);
    }
}

/* esp_http_client_fetch_headers() with the same EAGAIN policy. Returns the
 * HTTP status code (>0) or a negative error as above. The bare IDF call was
 * previously ignored everywhere, so a header timeout surfaced as "HTTP -1". */
static int nav_fetch_headers_retry(esp_http_client_handle_t c, int eagain_max,
                                   volatile bool *abort_flag)
{
    for (int tries = 0;; tries++) {
        int64_t r = esp_http_client_fetch_headers(c);
        if (r >= 0) {
            return esp_http_client_get_status_code(c);
        }
        if (r != -ESP_ERR_HTTP_EAGAIN || tries >= eagain_max) {
            return (int)r;
        }
        if (nav_aborted(abort_flag)) {
            return -ESP_ERR_NOT_FINISHED;
        }
        ESP_LOGW(TAG, "no response headers after %d ms - waiting again (%d/%d)",
                 NAV_HTTP_TIMEOUT_MS, tries + 1, eagain_max);
    }
}

/* Hygiene before every open() on a REUSED handle:
 *  - esp_http_client_close() does not drop body bytes that arrived together
 *    with the headers of an abandoned response; they would be returned by
 *    the next request's first read.
 *  - request headers persist across requests, so a Range set for a resume
 *    would leak into the next song. */
static void nav_handle_reset(esp_http_client_handle_t c)
{
    esp_http_client_clear_response_buffer(c);
    esp_http_client_delete_header(c, "Range");
}

/* Called once right after a successful open: ssl_read() stores a TLS code
 * even on the benign WANT_READ->timeout path, so a stale code from an
 * earlier request must not be attributed to this one. */
static void nav_clear_tls_error(esp_http_client_handle_t c)
{
    esp_http_client_get_and_clear_last_tls_error(c, NULL, NULL);
}

/* One-shot failure report. Short line to ui_log (serial + on-screen log),
 * everything else to ESP_LOGW. Never logs the URL: it carries credentials.
 * `why` (optional) receives a compact "<class> <KB>KB/<ms>ms" for callers
 * that journal the failure (the alarm). */
static void nav_diag_log(esp_http_client_handle_t c, int rc, const nav_xfer_t *x,
                         char *why, size_t why_cap)
{
    int sock_errno = esp_http_client_get_errno(c);
    int tls_code = 0, tls_flags = 0;
    /* the return value IS the last esp-tls error (ESP_OK if none); reading
     * clears it, so exactly one call */
    esp_err_t tls_last =
        esp_http_client_get_and_clear_last_tls_error(c, &tls_code, &tls_flags);
    if (tls_last == ESP_ERR_INVALID_STATE) {
        tls_last = ESP_OK; /* no error handle (never connected) */
    }
    bool complete = esp_http_client_is_complete_data_received(c);
    bool chunked = esp_http_client_is_chunked_response(c);
    int64_t clen = esp_http_client_get_content_length(c);
    int elapsed_ms = (int)((esp_timer_get_time() - x->t_open_us) / 1000);

    const char *cls;
    if (rc == -ESP_ERR_HTTP_EAGAIN) {
        cls = "timeout (server silent)";
    } else if (rc == -ESP_ERR_NOT_FINISHED) {
        cls = "aborted";
    } else if (tls_last == ESP_ERR_ESP_TLS_TCP_CLOSED_FIN ||
               (!complete && sock_errno == 0 && tls_code == 0)) {
        cls = "server closed early (truncated)";
    } else if (tls_last == ESP_ERR_MBEDTLS_SSL_READ_FAILED || tls_code != 0) {
        cls = "TLS error";
    } else if (sock_errno != 0) {
        cls = "transport error";
    } else {
        cls = "read error";
    }
    if (why != NULL && why_cap > 0) {
        const char *short_cls =
            rc == -ESP_ERR_HTTP_EAGAIN ? "timeout" :
            rc == -ESP_ERR_NOT_FINISHED ? "aborted" :
            (tls_last == ESP_ERR_ESP_TLS_TCP_CLOSED_FIN ||
             (!complete && sock_errno == 0 && tls_code == 0)) ? "closed early" :
            (tls_last == ESP_ERR_MBEDTLS_SSL_READ_FAILED || tls_code != 0) ? "TLS" :
            sock_errno != 0 ? "transport" : "read err";
        snprintf(why, why_cap, "%s %uKB/%ds", short_cls,
                 (unsigned)(x->bytes / 1024), (elapsed_ms + 500) / 1000);
    }

    int rssi = 0;
    wifi_conn_info_t wi;
    if (wifi_mgr_is_connected() && wifi_mgr_get_info(&wi)) {
        rssi = wi.rssi;
    }

    ui_log("Navidrome %s %s: %s after %uKB/%dms try%d fmt=%s rc=%d errno=%d rssi=%d",
           x->what, x->song_id != NULL ? x->song_id : "-", cls,
           (unsigned)(x->bytes / 1024), elapsed_ms, x->attempt, x->format,
           rc, sock_errno, rssi);

    char tls_str[96] = "-";
    if (tls_code != 0) {
        mbedtls_strerror(tls_code, tls_str, sizeof tls_str);
    }
    ESP_LOGW(TAG, "%s %s: host=%s tls_last=%s tls_code=%d (%s) tls_flags=0x%x "
             "errno=%d (%s) body_complete=%d chunked=%d content_len=%lld "
             "bytes=%u elapsed=%dms free_internal=%u",
             x->what, x->song_id != NULL ? x->song_id : "-",
             settings_get()->nav_url, esp_err_to_name(tls_last), tls_code,
             tls_str, (unsigned)tls_flags, sock_errno,
             sock_errno != 0 ? strerror(sock_errno) : "-", (int)complete,
             (int)chunked, (long long)clen, (unsigned)x->bytes, elapsed_ms,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

/* ---------- persistent keep-alive API client ----------
 *
 * One handle serves every JSON call (search, playlists, random) under a
 * mutex - the music task, worker and alarm task all come through here. A
 * fresh TCP+TLS handshake per request used to cost 200-500 ms each; a
 * kept-alive connection makes repeat calls near-free. On any error the
 * connection is closed (never reused half-read) but the handle is kept. */

static esp_http_client_handle_t s_api_client;
static SemaphoreHandle_t s_api_mtx;

static esp_http_client_handle_t api_client_take(const char *url)
{
    if (s_api_mtx == NULL) {
        app_init_lock();
        if (s_api_mtx == NULL) {
            s_api_mtx = xSemaphoreCreateMutex();
        }
        app_init_unlock();
        if (s_api_mtx == NULL) {
            return NULL;
        }
    }
    /* Bounded: a request holds the mutex for up to ~60 s (2 attempts x
     * timeout + EAGAIN re-read). The alarm's playlist fetch must not sit
     * behind a browser search on the httpd task for that long before it
     * falls back to the chime - fail fast instead. */
    if (xSemaphoreTake(s_api_mtx, pdMS_TO_TICKS(NAV_API_MUTEX_WAIT_MS)) != pdTRUE) {
        ui_log("Navidrome: API busy - request skipped");
        return NULL;
    }
    if (s_api_client == NULL) {
        esp_http_client_config_t cfg = {
            .url = url,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = NAV_HTTP_TIMEOUT_MS,
            .buffer_size = 4096,
            .keep_alive_enable = true,
        };
        s_api_client = esp_http_client_init(&cfg);
    } else {
        esp_http_client_set_url(s_api_client, url);
    }
    if (s_api_client == NULL) {
        xSemaphoreGive(s_api_mtx);
        return NULL;
    }
    return s_api_client;
}

static void api_client_give(bool healthy)
{
    if (!healthy && s_api_client != NULL) {
        esp_http_client_close(s_api_client);
    }
    xSemaphoreGive(s_api_mtx);
}

/* GET url, return the body as a NUL-terminated SPIRAM string (caller frees),
 * or NULL with the reason logged. API calls are small and idempotent, so a
 * failed attempt (stale keep-alive, silent server, dropped connection) is
 * retried once on a fresh connection. */
static char *nav_http_get_json(const char *url)
{
    esp_http_client_handle_t client = api_client_take(url);
    if (client == NULL) {
        ui_log("Navidrome: http client init failed");
        return NULL;
    }

    char *buf = NULL;
    size_t cap = 0, len = 0;
    int status = 0;
    nav_xfer_t x = { .what = "api", .song_id = NULL, .format = "-" };

    for (int attempt = 1;; attempt++) {
        x.attempt = attempt;
        x.bytes = 0;
        len = 0;

        nav_handle_reset(client);
        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            /* the kept-alive connection may have gone stale: reconnect once */
            esp_http_client_close(client);
            err = esp_http_client_open(client, 0);
        }
        if (err != ESP_OK) {
            ui_log("Navidrome: connect failed: %s", esp_err_to_name(err));
            goto fail;
        }
        x.t_open_us = esp_timer_get_time();
        nav_clear_tls_error(client);

        int rc = nav_fetch_headers_retry(client, NAV_EAGAIN_RETRIES_JSON, NULL);
        if (rc < 0) {
            goto attempt_failed;
        }
        status = rc;

        if (buf == NULL) {
            cap = 8192;
            buf = psram_alloc(cap);
            if (buf == NULL) {
                goto fail;
            }
        }
        for (;;) {
            if (len + NAV_READ_CHUNK + 1 > cap) {
                if (cap >= NAV_JSON_CAP) {
                    ui_log("Navidrome: response exceeds %d MB cap",
                           NAV_JSON_CAP / (1024 * 1024));
                    goto fail;
                }
                cap *= 2;
                char *nb = heap_caps_realloc(buf, cap, MALLOC_CAP_SPIRAM);
                if (nb == NULL) {
                    goto fail;
                }
                buf = nb;
            }
            rc = nav_read_retry(client, buf + len, NAV_READ_CHUNK,
                                NAV_EAGAIN_RETRIES_JSON, NULL);
            if (rc <= 0) {
                break;
            }
            len += (size_t)rc;
            x.bytes = len;
        }
        if (rc == 0 && !esp_http_client_is_complete_data_received(client)) {
            rc = ESP_FAIL; /* FIN before the body was complete */
        }
        if (rc == 0) {
            break; /* success */
        }

attempt_failed:
        nav_diag_log(client, rc, &x, NULL, 0);
        esp_http_client_close(client);
        if (attempt >= 2) {
            goto fail;
        }
        ui_log("Navidrome: retrying API call");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    buf[len] = '\0';

    if (status != 200) {
        ui_log("Navidrome: HTTP %d", status);
        ESP_LOGW(TAG, "HTTP %d body: %.256s", status, buf);
        goto fail;
    }

    /* body fully read: leave the connection open for the next call */
    api_client_give(true);
    return buf;

fail:
    nav_note_fail();
    heap_caps_free(buf);
    api_client_give(false);
    return NULL;
}

/* Parse body, return the "subsonic-response" object on status "ok".
 * Caller must cJSON_Delete the returned tree's root, kept in *root_out. */
static cJSON *nav_parse_and_check(char *body, cJSON **root_out)
{
    *root_out = NULL;
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        ui_log("Navidrome: bad JSON from server");
        nav_note_fail();
        return NULL;
    }
    cJSON *resp = cJSON_GetObjectItem(root, "subsonic-response");
    const char *status =
        cJSON_GetStringValue(cJSON_GetObjectItem(resp, "status"));
    if (status == NULL || strcmp(status, "ok") != 0) {
        cJSON *error = cJSON_GetObjectItem(resp, "error");
        cJSON *msg = cJSON_GetObjectItem(error, "message");
        cJSON *code = cJSON_GetObjectItem(error, "code");
        int ecode = cJSON_IsNumber(code) ? (int)code->valuedouble : -1;
        const char *emsg = cJSON_IsString(msg) ? msg->valuestring : "unknown";
        ui_log("Navidrome error %d: %s", ecode, emsg);
        /* Rare, and the only place a stale id (70 "not found") or bad
         * credentials (40) surface - worth a journal line. */
        evlog(EV_ERR, "Navidrome err %d: %.24s", ecode, emsg);
        cJSON_Delete(root);
        nav_note_fail();
        s_last_api_error = ecode; /* after note_fail, which clears it */
        return NULL;
    }
    nav_note_ok();
    *root_out = root;
    return resp;
}

static int json_int(const cJSON *obj, const char *key)
{
    const cJSON *it = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(it) ? (int)it->valuedouble : 0;
}

static char *json_strdup(const cJSON *obj, const char *key)
{
    return psram_strdup(cJSON_GetStringValue(cJSON_GetObjectItem(obj, key)));
}

/* ---------- public API ---------- */

svc_state_t nav_service_state(void)
{
    if (!nav_is_configured()) {
        return SVC_OFF;
    }
    switch (s_link_state) {
    case 1:
        /* Green means "answered within the check window", not "nothing has
         * failed yet": with WiFi gone or the probe off the last success ages
         * out to dim rather than lying green for hours. */
        return (esp_timer_get_time() - s_last_ok_us <= boot_svc_fresh_window_us())
                   ? SVC_UP : SVC_WAIT;
    case 0:  return SVC_FAIL;
    default: return SVC_WAIT;
    }
}

/* Subsonic "ping": the cheapest call that proves URL + credentials + server.
 *
 * Deliberately NOT routed through nav_http_get_json(): that shares the
 * keep-alive API connection and waits NAV_HTTP_TIMEOUT_MS (15 s) twice over
 * before giving up, and this runs in the boot path where a dead server must
 * not hold the worker. Own short-lived client, PING_TIMEOUT_MS, no retry -
 * the boot sequence re-probes periodically anyway. */
#define NAV_PING_TIMEOUT_MS 5000

bool nav_ping(void)
{
    if (!nav_is_configured()) {
        s_link_state = -1;
        return false;
    }

    char url[512];
    nav_build_url(url, sizeof url, "ping", NULL);

    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = NAV_PING_TIMEOUT_MS,
        .buffer_size = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ui_log("Navidrome: ping client init failed");
        nav_note_fail();
        return false;
    }

    /* open + fetch_headers + read, not perform(): with no event handler
     * perform() reads the body and throws it away, and the body is exactly
     * what distinguishes "server up" from "bad password" here. */
    bool ok = false;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ui_log("Music server unreachable: %s", esp_err_to_name(err));
    } else if (esp_http_client_fetch_headers(client) < 0) {
        ui_log("Music server: no response headers");
    } else {
        int status = esp_http_client_get_status_code(client);
        /* Subsonic answers 200 even for a bad password (the failure is in the
         * body), so a 200 alone is not proof. The body is tiny; read it and
         * let the shared parser decide - it sets s_link_state for us. */
        if (status != 200) {
            ui_log("Music server: HTTP %d", status);
        } else {
            char body[512];
            int n = esp_http_client_read(client, body, sizeof(body) - 1);
            if (n <= 0) {
                ui_log("Music server: empty ping response");
            } else {
                body[n] = '\0';
                cJSON *root;
                ok = (nav_parse_and_check(body, &root) != NULL);
                cJSON_Delete(root);
            }
        }
    }
    if (!ok) {
        nav_note_fail();
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

esp_err_t nav_get_playlists(nav_playlist_list_t *out)
{
    memset(out, 0, sizeof *out);

    char url[512];
    nav_build_url(url, sizeof url, "getPlaylists", NULL);
    char *body = nav_http_get_json(url);
    if (body == NULL) {
        return ESP_FAIL;
    }

    cJSON *root;
    cJSON *resp = nav_parse_and_check(body, &root);
    heap_caps_free(body);
    if (resp == NULL) {
        return ESP_FAIL;
    }

    cJSON *arr = cJSON_GetObjectItem(cJSON_GetObjectItem(resp, "playlists"),
                                     "playlist");
    int n = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
    if (n > 0) {
        out->items = psram_alloc(n * sizeof(nav_playlist_t));
        if (out->items == NULL) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        for (int i = 0; i < n; i++) {
            cJSON *pl = cJSON_GetArrayItem(arr, i);
            out->items[i].id         = json_strdup(pl, "id");
            out->items[i].name       = json_strdup(pl, "name");
            out->items[i].song_count = json_int(pl, "songCount");
            out->items[i].duration   = json_int(pl, "duration");
            if (out->items[i].id == NULL || out->items[i].name == NULL) {
                /* OOM: fail whole call - consumers assume non-NULL fields */
                out->count = i + 1; /* free() tolerates the NULL member */
                nav_free_playlists(out);
                cJSON_Delete(root);
                return ESP_ERR_NO_MEM;
            }
        }
        out->count = n;
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t nav_get_playlist_songs(const char *playlist_id, nav_song_list_t *out)
{
    memset(out, 0, sizeof *out);

    char extra[96];
    snprintf(extra, sizeof extra, "&id=%s", playlist_id);
    char url[512];
    nav_build_url(url, sizeof url, "getPlaylist", extra);
    char *body = nav_http_get_json(url);
    if (body == NULL) {
        return ESP_FAIL;
    }

    cJSON *root;
    cJSON *resp = nav_parse_and_check(body, &root);
    heap_caps_free(body);
    if (resp == NULL) {
        return ESP_FAIL;
    }

    cJSON *arr = cJSON_GetObjectItem(cJSON_GetObjectItem(resp, "playlist"),
                                     "entry");
    int n = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
    if (n > 0) {
        out->items = psram_alloc(n * sizeof(nav_song_t));
        if (out->items == NULL) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        for (int i = 0; i < n; i++) {
            cJSON *e = cJSON_GetArrayItem(arr, i);
            out->items[i].id       = json_strdup(e, "id");
            out->items[i].title    = json_strdup(e, "title");
            out->items[i].artist   = json_strdup(e, "artist");
            out->items[i].duration = json_int(e, "duration");
            if (out->items[i].id == NULL || out->items[i].title == NULL ||
                out->items[i].artist == NULL) {
                out->count = i + 1;
                nav_free_songs(out);
                cJSON_Delete(root);
                return ESP_ERR_NO_MEM;
            }
        }
        out->count = n;
    }
    cJSON_Delete(root);
    return ESP_OK;
}

/* Copy a JSON "song"/"entry" array into a nav_song_list_t. All-or-nothing:
 * on OOM the partial list is freed and no NULL fields ever escape (the
 * fuzzy matcher and the UI dereference title/artist unconditionally). */
static esp_err_t nav_copy_song_array(cJSON *arr, nav_song_list_t *out)
{
    int n = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
    if (n <= 0) {
        return ESP_OK;
    }
    out->items = psram_alloc(n * sizeof(nav_song_t));
    if (out->items == NULL) {
        return ESP_ERR_NO_MEM;
    }
    for (int i = 0; i < n; i++) {
        cJSON *e = cJSON_GetArrayItem(arr, i);
        out->items[i].id       = json_strdup(e, "id");
        out->items[i].title    = json_strdup(e, "title");
        out->items[i].artist   = json_strdup(e, "artist");
        out->items[i].duration = json_int(e, "duration");
        if (out->items[i].id == NULL || out->items[i].title == NULL ||
            out->items[i].artist == NULL) {
            out->count = i + 1;
            nav_free_songs(out);
            return ESP_ERR_NO_MEM;
        }
    }
    out->count = n;
    return ESP_OK;
}

/* Percent-encode `in` into `out` (query-string value). */
static void url_encode(const char *in, char *out, size_t cap)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *in != '\0' && o + 4 < cap; in++) {
        unsigned char c = (unsigned char)*in;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~') {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = HEX[c >> 4];
            out[o++] = HEX[c & 0x0F];
        }
    }
    out[o] = '\0';
}

esp_err_t nav_search_songs(const char *query, int max, nav_song_list_t *out)
{
    memset(out, 0, sizeof *out);

    char enc[192];
    url_encode(query != NULL ? query : "", enc, sizeof enc);
    char extra[256];
    snprintf(extra, sizeof extra,
             "&query=%s&songCount=%d&artistCount=0&albumCount=0", enc, max);
    char url[512];
    nav_build_url(url, sizeof url, "search3", extra);
    char *body = nav_http_get_json(url);
    if (body == NULL) {
        return ESP_FAIL;
    }

    cJSON *root;
    cJSON *resp = nav_parse_and_check(body, &root);
    heap_caps_free(body);
    if (resp == NULL) {
        return ESP_FAIL;
    }
    cJSON *arr = cJSON_GetObjectItem(
            cJSON_GetObjectItem(resp, "searchResult3"), "song");
    esp_err_t ret = nav_copy_song_array(arr, out);
    cJSON_Delete(root);
    return ret;
}

esp_err_t nav_get_random_songs(int max, nav_song_list_t *out)
{
    memset(out, 0, sizeof *out);

    char extra[48];
    snprintf(extra, sizeof extra, "&size=%d", max);
    char url[512];
    nav_build_url(url, sizeof url, "getRandomSongs", extra);
    char *body = nav_http_get_json(url);
    if (body == NULL) {
        return ESP_FAIL;
    }

    cJSON *root;
    cJSON *resp = nav_parse_and_check(body, &root);
    heap_caps_free(body);
    if (resp == NULL) {
        return ESP_FAIL;
    }
    cJSON *arr = cJSON_GetObjectItem(
            cJSON_GetObjectItem(resp, "randomSongs"), "song");
    esp_err_t ret = nav_copy_song_array(arr, out);
    cJSON_Delete(root);
    return ret;
}

/* Alarm path: whole-song download on a fresh connection. No EAGAIN re-reads
 * and no reconnect here on purpose - alarm.c retries with another song and
 * must reach the chime fallback quickly if the server is unwell. */
esp_err_t nav_download_song(const char *song_id, uint8_t **out_buf,
                            size_t *out_len, volatile bool *abort_flag,
                            char *why, size_t why_cap)
{
#define WHY(...) do { if (why != NULL && why_cap > 0) { \
                          snprintf(why, why_cap, __VA_ARGS__); } } while (0)
    *out_buf = NULL;
    *out_len = 0;
    WHY("init failed");

    char extra[128];
    snprintf(extra, sizeof extra, "&id=%s&format=mp3&maxBitRate=128", song_id);
    char url[512];
    nav_build_url(url, sizeof url, "stream", extra);

    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = NAV_HTTP_TIMEOUT_MS,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return ESP_FAIL;
    }

    uint8_t *buf = NULL;
    esp_err_t ret = ESP_FAIL;
    nav_xfer_t x = { .what = "download", .song_id = song_id, .format = "mp3",
                     .attempt = 1 };

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ui_log("Navidrome: stream connect failed: %s", esp_err_to_name(err));
        WHY("connect %s", esp_err_to_name(err));
        goto out;
    }
    x.t_open_us = esp_timer_get_time();
    nav_clear_tls_error(client);

    int status = nav_fetch_headers_retry(client, NAV_EAGAIN_RETRIES_ALARM,
                                         abort_flag);
    if (status == -ESP_ERR_NOT_FINISHED) {
        ret = ESP_ERR_NOT_FINISHED;
        WHY("aborted");
        goto out;
    }
    if (status < 0) {
        nav_diag_log(client, status, &x, why, why_cap);
        goto out;
    }
    if (status != 200) {
        ui_log("Navidrome: stream HTTP %d", status);
        WHY("HTTP %d", status);
        goto out;
    }

    /* Content-Length is usually absent when Navidrome transcodes. */
    int64_t clen = esp_http_client_get_content_length(client);
    size_t cap = (clen > 0 && clen <= NAV_SONG_CAP) ? (size_t)clen + 4096
                                                    : NAV_SONG_START;
    buf = psram_alloc(cap);
    if (buf == NULL) {
        ui_log("Navidrome: out of PSRAM for song buffer");
        WHY("no PSRAM");
        goto out;
    }

    size_t len = 0;
    for (;;) {
        if (nav_aborted(abort_flag)) {
            ret = ESP_ERR_NOT_FINISHED;
            WHY("aborted");
            goto out;
        }
        if (len + NAV_READ_CHUNK > cap) {
            if (cap >= NAV_SONG_CAP) {
                ui_log("Navidrome: song exceeds %d MB cap, skipping",
                       NAV_SONG_CAP / (1024 * 1024));
                WHY("over %dMB cap", NAV_SONG_CAP / (1024 * 1024));
                goto out;
            }
            cap *= 2;
            uint8_t *nb = heap_caps_realloc(buf, cap, MALLOC_CAP_SPIRAM);
            if (nb == NULL) {
                ui_log("Navidrome: out of PSRAM growing song buffer");
                WHY("no PSRAM at %uKB", (unsigned)(len / 1024));
                goto out;
            }
            buf = nb;
        }
        int n = nav_read_retry(client, buf + len, NAV_READ_CHUNK,
                               NAV_EAGAIN_RETRIES_ALARM, abort_flag);
        if (n == -ESP_ERR_NOT_FINISHED) {
            ret = ESP_ERR_NOT_FINISHED;
            WHY("aborted");
            goto out;
        }
        if (n < 0) {
            x.bytes = len;
            nav_diag_log(client, n, &x, why, why_cap);
            goto out;
        }
        if (n == 0) {
            break;
        }
        len += (size_t)n;
    }

    if (len < NAV_SONG_MIN) {
        ui_log("Navidrome: stream too short (%u bytes)", (unsigned)len);
        WHY("too short %uB", (unsigned)len);
        goto out;
    }
    /* A dropped connection can surface as read()==0 - when the server
     * declared a length, hold it to it, and for chunked bodies ask the
     * parser whether the last chunk arrived - or a truncated file "plays"
     * as a mysteriously short song. */
    if ((clen > 0 && len < (size_t)clen) ||
        !esp_http_client_is_complete_data_received(client)) {
        ui_log("Navidrome: truncated download (%u of %lld bytes)",
               (unsigned)len, (long long)clen);
        WHY("truncated %uKB", (unsigned)(len / 1024));
        goto out;
    }

    *out_buf = buf;
    *out_len = len;
    ret = ESP_OK;
    WHY("ok");
    /* First bytes identify what the server really sent (MP3 sync / ID3 tag /
     * something else entirely) - key diagnostic for undecodable streams. */
    ESP_LOGI(TAG, "downloaded %u bytes, head %02x %02x %02x %02x %02x %02x %02x %02x",
             (unsigned)len, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
    buf = NULL;

out:
    heap_caps_free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ret;
#undef WHY
}

/* ---------- streaming download (song cache) ---------- */

/* MP3 (ID3 tag or frame sync) or WAV (RIFF)? Anything else the decoder
 * cannot play, so a raw stream of it must fall back to transcoding. */
static bool nav_looks_playable(const uint8_t *d, size_t n)
{
    if (n >= 3 && memcmp(d, "ID3", 3) == 0) {
        return true;
    }
    if (n >= 2 && d[0] == 0xFF && (d[1] & 0xE0) == 0xE0) {
        return true;
    }
    if (n >= 4 && memcmp(d, "RIFF", 4) == 0) {
        return true;
    }
    return false;
}

/* One persistent keep-alive connection for song bytes. Only the song_cache
 * downloader task calls nav_stream_song, so no mutex is needed here. */
static esp_http_client_handle_t s_stream_client;

/* One format pass (raw or transcode) of nav_stream_song, including the
 * reconnect-and-resume loop. Sets *try_transcode when a raw pass failed for
 * a reason the transcode pass can fix (server refused raw / not MP3/WAV). */
static esp_err_t nav_stream_pass(esp_http_client_handle_t c, const char *song_id,
                                 bool raw, const nav_stream_sink_t *sink,
                                 volatile bool *abort_flag, bool *try_transcode)
{
    /* the read buffer lives off the stack; single caller by contract. PSRAM:
     * esp_http_client_read copies into it, no DMA - internal RAM is scarce */
    EXT_RAM_BSS_ATTR static uint8_t chunk[NAV_READ_CHUNK];

    nav_xfer_t x = { .what = "stream", .song_id = song_id,
                     .format = raw ? "raw" : "mp3" };
    size_t total = 0;          /* bytes delivered to the sink so far */
    size_t expected = 0;       /* total body size when the server told us */
    int64_t first_fail_us = 0; /* start of the retry budget */
    int reconnects = 0;
    *try_transcode = false;

    for (;;) {
        char extra[128];
        snprintf(extra, sizeof extra,
                 raw ? "&id=%s&format=raw"
                     : "&id=%s&format=mp3&maxBitRate=128",
                 song_id);
        char url[512];
        nav_build_url(url, sizeof url, "stream", extra); /* fresh salt */
        esp_http_client_set_url(c, url);
        nav_handle_reset(c);
        if (total > 0) {
            char range[48];
            snprintf(range, sizeof range, "bytes=%u-", (unsigned)total);
            esp_http_client_set_header(c, "Range", range);
        }
        x.attempt++;
        x.bytes = total;

        esp_err_t err = esp_http_client_open(c, 0);
        if (err != ESP_OK) {
            /* stale keep-alive connection: reconnect once */
            esp_http_client_close(c);
            err = esp_http_client_open(c, 0);
        }
        if (err != ESP_OK) {
            ui_log("Navidrome: stream connect failed: %s (try %d)",
                   esp_err_to_name(err), x.attempt);
            esp_http_client_close(c);
            goto retry_or_fail;
        }
        x.t_open_us = esp_timer_get_time();
        nav_clear_tls_error(c);

        int status = nav_fetch_headers_retry(c, NAV_EAGAIN_RETRIES_STREAM,
                                             abort_flag);
        if (status == -ESP_ERR_NOT_FINISHED) {
            esp_http_client_close(c);
            return ESP_ERR_NOT_FINISHED;
        }
        if (status < 0) {
            nav_diag_log(c, status, &x, NULL, 0);
            esp_http_client_close(c);
            goto retry_or_fail;
        }
        int64_t clen = esp_http_client_get_content_length(c);

        if (total > 0) {
            /* resuming after a failure */
            if (status == 206) {
                nav_note_ok(); /* a served Range request: server alive */
                expected = (clen > 0) ? total + (size_t)clen : 0;
                ui_log("Navidrome: resumed %s at %u KB (HTTP 206)", song_id,
                       (unsigned)(total / 1024));
            } else if (status == 200) {
                /* The server cannot resume (live transcode): it is sending
                 * the whole song again. Only acceptable if the sink can throw
                 * away what it has (nothing consumed yet, or byte-identical
                 * raw content) - otherwise a decoder mid-song would get a
                 * spliced stream. */
                if (sink->on_restart == NULL ||
                    !sink->on_restart(sink->arg, raw)) {
                    ui_log("Navidrome: server cannot resume %s at %u KB - "
                           "giving up", song_id, (unsigned)(total / 1024));
                    esp_http_client_close(c);
                    return ESP_FAIL;
                }
                ui_log("Navidrome: server cannot resume - restarting %s from 0",
                       song_id);
                total = 0;
                expected = 0;
            } else {
                ui_log("Navidrome: resume of %s refused: HTTP %d", song_id,
                       status);
                esp_http_client_close(c);
                return ESP_FAIL;
            }
        }
        if (total == 0) {
            if (status != 200) {
                ui_log("Navidrome: stream HTTP %d%s", status,
                       raw ? " (raw)" : "");
                esp_http_client_close(c); /* unread body: don't reuse */
                *try_transcode = raw; /* some servers refuse raw */
                return ESP_FAIL;
            }
            /* 200 with headers: the server is alive, so playback keeps the
             * clock-face indicator green without a separate probe */
            nav_note_ok();
            /* sniff the first chunk before committing the sink */
            int first = nav_read_retry(c, chunk, NAV_READ_CHUNK,
                                       NAV_EAGAIN_RETRIES_STREAM, abort_flag);
            if (first == -ESP_ERR_NOT_FINISHED) {
                esp_http_client_close(c);
                return ESP_ERR_NOT_FINISHED;
            }
            if (first < 0) {
                nav_diag_log(c, first, &x, NULL, 0);
                esp_http_client_close(c);
                goto retry_or_fail;
            }
            if (first == 0) {
                ui_log("Navidrome: empty stream response%s",
                       raw ? " (raw)" : "");
                esp_http_client_close(c);
                *try_transcode = raw;
                return ESP_FAIL;
            }
            if (raw && !nav_looks_playable(chunk, (size_t)first)) {
                ESP_LOGI(TAG,
                         "raw stream not MP3/WAV (%02x %02x %02x %02x) - "
                         "falling back to transcode",
                         chunk[0], chunk[1], chunk[2], chunk[3]);
                esp_http_client_close(c);
                *try_transcode = true;
                return ESP_FAIL;
            }
            if (!sink->on_start(sink->arg, clen > 0 ? clen : 0)) {
                /* The sink refused the size (song_cache budget / per-song
                 * cap). A raw FLAC or 320 kbps file can be 3-6x the 128 kbps
                 * transcode, so try that before giving up. */
                esp_http_client_close(c);
                if (raw) {
                    ui_log("Navidrome: raw %s too big for the cache (%lld bytes) "
                           "- trying transcode", song_id, (long long)clen);
                    *try_transcode = true;
                    return ESP_FAIL;
                }
                return ESP_ERR_NO_MEM;
            }
            if (!sink->on_data(sink->arg, chunk, (size_t)first)) {
                esp_http_client_close(c);
                return ESP_ERR_NOT_FINISHED;
            }
            total = (size_t)first;
            expected = (clen > 0) ? (size_t)clen : 0;
        }

        for (;;) {
            if (nav_aborted(abort_flag)) {
                esp_http_client_close(c);
                return ESP_ERR_NOT_FINISHED;
            }
            int n = nav_read_retry(c, chunk, NAV_READ_CHUNK,
                                   NAV_EAGAIN_RETRIES_STREAM, abort_flag);
            if (n == -ESP_ERR_NOT_FINISHED) {
                esp_http_client_close(c);
                return ESP_ERR_NOT_FINISHED;
            }
            if (n < 0) {
                x.bytes = total;
                nav_diag_log(c, n, &x, NULL, 0);
                esp_http_client_close(c);
                goto retry_or_fail;
            }
            if (n == 0) {
                break;
            }
            if (!sink->on_data(sink->arg, chunk, (size_t)n)) {
                esp_http_client_close(c);
                return ESP_ERR_NOT_FINISHED;
            }
            total += (size_t)n;
        }
        x.bytes = total;

        /* read()==0 with the body incomplete = the server closed early.
         * Content-Length catches it for raw files; for chunked transcodes
         * only the parser knows whether the last chunk arrived. */
        bool complete = esp_http_client_is_complete_data_received(c);
        if ((expected > 0 && total < expected) || !complete) {
            ui_log("Navidrome: truncated stream %s (%u of %u bytes%s)", song_id,
                   (unsigned)total, (unsigned)expected,
                   complete ? "" : ", body incomplete");
            esp_http_client_close(c);
            goto retry_or_fail; /* resume from `total` */
        }
        if (total < NAV_SONG_MIN) {
            ui_log("Navidrome: stream too short (%u bytes)", (unsigned)total);
            esp_http_client_close(c);
            return ESP_FAIL;
        }
        /* fully read: leave the connection open for the next song */
        ESP_LOGI(TAG, "streamed %u bytes (%s, %d attempt%s)", (unsigned)total,
                 raw ? "raw" : "transcode", x.attempt,
                 x.attempt == 1 ? "" : "s");
        return ESP_OK;

retry_or_fail:
        {
            int64_t now = esp_timer_get_time();
            if (first_fail_us == 0) {
                first_fail_us = now;
            }
            int since_ms = (int)((now - first_fail_us) / 1000);
            if (reconnects >= NAV_MAX_RECONNECTS ||
                since_ms > NAV_RETRY_BUDGET_MS) {
                ui_log("Navidrome: giving up on %s after %d attempt%s / %d ms "
                       "(%u KB received)", song_id, x.attempt,
                       x.attempt == 1 ? "" : "s", since_ms,
                       (unsigned)(total / 1024));
                return ESP_FAIL;
            }
            int backoff = NAV_BACKOFF_MS[reconnects++];
            ui_log("Navidrome: reconnecting for %s at %u KB in %d ms "
                   "(retry %d/%d)", song_id, (unsigned)(total / 1024),
                   backoff, reconnects, NAV_MAX_RECONNECTS);
            if (!nav_sleep_abortable(backoff, abort_flag)) {
                return ESP_ERR_NOT_FINISHED;
            }
        }
    }
}

esp_err_t nav_stream_song(const char *song_id, const nav_stream_sink_t *sink,
                          volatile bool *abort_flag)
{
    if (s_stream_client == NULL) {
        char url[512];
        nav_build_url(url, sizeof url, "stream", NULL);
        esp_http_client_config_t cfg = {
            .url = url,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = NAV_HTTP_TIMEOUT_MS,
            .buffer_size = 4096,
            .keep_alive_enable = true,
        };
        s_stream_client = esp_http_client_init(&cfg);
        if (s_stream_client == NULL) {
            ui_log("Navidrome: stream client init failed");
            return ESP_FAIL;
        }
    }

    /* format=raw first (no server-side ffmpeg, Content-Length available,
     * resumable); anything that is not MP3/WAV is fetched again as a
     * 128 kbps MP3 transcode. */
    for (int pass = 0; pass < 2; pass++) {
        bool try_transcode = false;
        esp_err_t err = nav_stream_pass(s_stream_client, song_id, pass == 0,
                                        sink, abort_flag, &try_transcode);
        if (err == ESP_OK || !try_transcode) {
            return err;
        }
    }
    return ESP_FAIL;
}

void nav_free_playlists(nav_playlist_list_t *l)
{
    for (int i = 0; i < l->count; i++) {
        heap_caps_free(l->items[i].id);
        heap_caps_free(l->items[i].name);
    }
    heap_caps_free(l->items);
    memset(l, 0, sizeof *l);
}

void nav_free_songs(nav_song_list_t *l)
{
    for (int i = 0; i < l->count; i++) {
        heap_caps_free(l->items[i].id);
        heap_caps_free(l->items[i].title);
        heap_caps_free(l->items[i].artist);
    }
    heap_caps_free(l->items);
    memset(l, 0, sizeof *l);
}
