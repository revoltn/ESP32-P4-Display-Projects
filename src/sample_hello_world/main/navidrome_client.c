/* Subsonic REST client for Navidrome over HTTPS (esp_http_client + cert
 * bundle). Auth is per-request: t = md5(password + salt) with a fresh salt,
 * per the Subsonic API spec. Runs on the worker task (needs its 16 KB stack
 * for the TLS handshake). */
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_rom_md5.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

#include "app_common.h"
#include "navidrome_client.h"

static const char *TAG = "navidrome";

/* Server and credentials come from Kconfig (menu "Sample: hello world").
 * All three default to empty; nav_configured() then says so and every
 * entry point refuses to run instead of calling https://(nothing). */
#define NAV_BASE_URL        CONFIG_SAMPLE_NAVIDROME_URL
#define NAV_USER            CONFIG_SAMPLE_NAVIDROME_USER  /* no chars needing URL-encoding */
#define NAV_PASSWORD        CONFIG_SAMPLE_NAVIDROME_PASSWORD
#define NAV_CLIENT          "jc4880demo"
#define NAV_API_VER         "1.16.1"
#define NAV_HTTP_TIMEOUT_MS 15000
#define NAV_SONG_CAP        (16 * 1024 * 1024)
#define NAV_SONG_MIN        4096              /* smaller than this = not a song */
#define NAV_READ_CHUNK      8192

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

bool nav_configured(void)
{
    if (NAV_BASE_URL[0] != '\0') {
        return true;
    }
    ui_log("Navidrome not configured: set SAMPLE_NAVIDROME_URL/USER/PASSWORD via "
           "idf.py menuconfig (or sdkconfig.local.defaults)");
    return false;
}

const char *nav_server_url(void)
{
    return NAV_BASE_URL;
}

/* Build "<base>/rest/<endpoint>?u=..&t=..&s=..&v=..&c=..&f=json<extra_qs>". */
static void nav_build_url(char *out, size_t cap, const char *endpoint,
                          const char *extra_qs)
{
    uint8_t salt_raw[8];
    esp_fill_random(salt_raw, sizeof salt_raw);
    char salt[17];
    hex_encode(salt_raw, sizeof salt_raw, salt);

    md5_context_t ctx;
    uint8_t digest[16];
    esp_rom_md5_init(&ctx);
    esp_rom_md5_update(&ctx, (const uint8_t *)NAV_PASSWORD, strlen(NAV_PASSWORD));
    esp_rom_md5_update(&ctx, (const uint8_t *)salt, strlen(salt));
    esp_rom_md5_final(digest, &ctx);
    char token[33];
    hex_encode(digest, sizeof digest, token);

    snprintf(out, cap, "%s/rest/%s?u=%s&t=%s&s=%s&v=%s&c=%s&f=json%s",
             NAV_BASE_URL, endpoint, NAV_USER, token, salt, NAV_API_VER,
             NAV_CLIENT, extra_qs != NULL ? extra_qs : "");
}

/* GET url, return the body as a NUL-terminated SPIRAM string (caller frees),
 * or NULL with the reason logged. */
static char *nav_http_get_json(const char *url)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = NAV_HTTP_TIMEOUT_MS,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ui_log("Navidrome: http client init failed");
        return NULL;
    }

    char *buf = NULL;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ui_log("Navidrome: connect failed: %s", esp_err_to_name(err));
        goto fail;
    }
    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    size_t cap = 8192, len = 0;
    buf = psram_alloc(cap);
    if (buf == NULL) {
        goto fail;
    }
    for (;;) {
        if (len + NAV_READ_CHUNK + 1 > cap) {
            cap *= 2;
            char *nb = heap_caps_realloc(buf, cap, MALLOC_CAP_SPIRAM);
            if (nb == NULL) {
                goto fail;
            }
            buf = nb;
        }
        int n = esp_http_client_read(client, buf + len, NAV_READ_CHUNK);
        if (n < 0) {
            ui_log("Navidrome: read error");
            goto fail;
        }
        if (n == 0) {
            break;
        }
        len += n;
    }
    buf[len] = '\0';

    if (status != 200) {
        ui_log("Navidrome: HTTP %d", status);
        ESP_LOGW(TAG, "HTTP %d body: %.256s", status, buf);
        goto fail;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return buf;

fail:
    heap_caps_free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
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
        return NULL;
    }
    cJSON *resp = cJSON_GetObjectItem(root, "subsonic-response");
    const char *status =
        cJSON_GetStringValue(cJSON_GetObjectItem(resp, "status"));
    if (status == NULL || strcmp(status, "ok") != 0) {
        cJSON *error = cJSON_GetObjectItem(resp, "error");
        cJSON *msg = cJSON_GetObjectItem(error, "message");
        cJSON *code = cJSON_GetObjectItem(error, "code");
        ui_log("Navidrome error %d: %s",
               cJSON_IsNumber(code) ? (int)code->valuedouble : -1,
               cJSON_IsString(msg) ? msg->valuestring : "unknown");
        cJSON_Delete(root);
        return NULL;
    }
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

esp_err_t nav_get_playlists(nav_playlist_list_t *out)
{
    memset(out, 0, sizeof *out);
    if (!nav_configured()) {
        return ESP_ERR_INVALID_STATE;
    }

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
        }
        out->count = n;
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t nav_get_playlist_songs(const char *playlist_id, nav_song_list_t *out)
{
    memset(out, 0, sizeof *out);
    if (!nav_configured()) {
        return ESP_ERR_INVALID_STATE;
    }

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
        }
        out->count = n;
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t nav_download_song(const char *song_id, uint8_t **out_buf,
                            size_t *out_len, volatile bool *abort_flag)
{
    *out_buf = NULL;
    *out_len = 0;
    if (!nav_configured()) {
        return ESP_ERR_INVALID_STATE;
    }

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

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ui_log("Navidrome: stream connect failed: %s", esp_err_to_name(err));
        goto out;
    }
    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ui_log("Navidrome: stream HTTP %d", status);
        goto out;
    }

    /* Content-Length is usually absent when Navidrome transcodes. */
    int64_t clen = esp_http_client_get_content_length(client);
    size_t cap = (clen > 0 && clen <= NAV_SONG_CAP) ? (size_t)clen + 4096
                                                    : 1024 * 1024;
    buf = psram_alloc(cap);
    if (buf == NULL) {
        ui_log("Navidrome: out of PSRAM for song buffer");
        goto out;
    }

    size_t len = 0;
    for (;;) {
        if (abort_flag != NULL && *abort_flag) {
            ret = ESP_ERR_NOT_FINISHED;
            goto out;
        }
        if (len + NAV_READ_CHUNK > cap) {
            if (cap >= NAV_SONG_CAP) {
                ui_log("Navidrome: song exceeds %d MB cap, skipping",
                       NAV_SONG_CAP / (1024 * 1024));
                goto out;
            }
            cap *= 2;
            uint8_t *nb = heap_caps_realloc(buf, cap, MALLOC_CAP_SPIRAM);
            if (nb == NULL) {
                ui_log("Navidrome: out of PSRAM growing song buffer");
                goto out;
            }
            buf = nb;
        }
        int n = esp_http_client_read(client, (char *)buf + len, NAV_READ_CHUNK);
        if (n < 0) {
            ui_log("Navidrome: stream read error");
            goto out;
        }
        if (n == 0) {
            break;
        }
        len += n;
    }

    if (len < NAV_SONG_MIN) {
        ui_log("Navidrome: stream too short (%u bytes)", (unsigned)len);
        goto out;
    }

    *out_buf = buf;
    *out_len = len;
    ret = ESP_OK;
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
