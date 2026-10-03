/* OpenAI-compatible chat client for LocalAI: one job, pick the best match
 * for a free-text query from a numbered list (songs, playlists).
 *
 * POST {ai_url}/v1/chat/completions with Bearer {ai_key}; the prompt asks
 * for a bare number, temperature 0. Runs on the caller's task (worker, music
 * or STT task - it needs a real stack for TLS if the URL is https).
 *
 * The AI settings strings are edited on the LVGL task while a request may
 * be in flight here, so every public entry point snapshots url/key/model
 * under settings_lock() first and never reads the live struct again. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

#include "app_common.h"
#include "settings.h"
#include "boot_seq.h"
#include "localai_client.h"

static const char *TAG = "localai";

/* Outcome of the most recent request: 1 = the server answered 200, 0 =
 * connect/HTTP failure, -1 = nothing tried yet. Written by every HTTP path
 * below so ordinary use keeps the clock screen's AI indicator fresh. */
static volatile int s_ai_link = -1;
static volatile int64_t s_ai_last_ok_us; /* esp_timer time of the last 200 */

static void ai_note_ok(void)
{
    s_ai_last_ok_us = esp_timer_get_time();
    s_ai_link = 1;
}

int64_t ai_last_ok_us(void)
{
    return s_ai_last_ok_us;
}

#define AI_HTTP_TIMEOUT_MS 45000   /* LLM inference can be slow */
#define AI_PING_TIMEOUT_MS 5000    /* the boot probe must not stall on a dead server */
#define AI_MAX_RESP        (64 * 1024)
/* whisper model name on the LocalAI server (change if yours differs) */
#define AI_STT_MODEL       "whisper-1"

typedef struct {
    char url[sizeof(((bedside_settings_t *)0)->ai_url)];
    char key[sizeof(((bedside_settings_t *)0)->ai_key)];
    char model[sizeof(((bedside_settings_t *)0)->ai_model)];
} ai_creds_t;

static void ai_snapshot(ai_creds_t *out)
{
    const bedside_settings_t *cfg = settings_get();
    settings_lock();
    strlcpy(out->url, cfg->ai_url, sizeof(out->url));
    strlcpy(out->key, cfg->ai_key, sizeof(out->key));
    strlcpy(out->model, cfg->ai_model, sizeof(out->model));
    settings_unlock();
}

bool ai_is_configured(void)
{
    return settings_get()->ai_url[0] != '\0'; /* single byte: no lock needed */
}

/* POST json body, return the response body as a SPIRAM string (caller
 * frees) or NULL with the reason logged. */
static char *ai_http_post(const char *url, const char *key, const char *body)
{
    esp_http_client_config_t hc = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = AI_HTTP_TIMEOUT_MS,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t client = esp_http_client_init(&hc);
    if (client == NULL) {
        ui_log("AI: http client init failed");
        return NULL;
    }

    char auth[80];
    snprintf(auth, sizeof auth, "Bearer %s", key);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (key[0] != '\0') {
        esp_http_client_set_header(client, "Authorization", auth);
    }

    char *buf = NULL;
    esp_err_t err = esp_http_client_open(client, strlen(body));
    if (err != ESP_OK) {
        ui_log("AI: connect failed: %s", esp_err_to_name(err));
        goto fail;
    }
    if (esp_http_client_write(client, body, strlen(body)) < 0) {
        ui_log("AI: request write failed");
        goto fail;
    }
    if (esp_http_client_fetch_headers(client) < 0) {
        ui_log("AI: no response headers (server silent?)");
        goto fail;
    }
    int status = esp_http_client_get_status_code(client);

    size_t cap = 4096, len = 0;
    buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        goto fail;
    }
    for (;;) {
        if (len + 2048 + 1 > cap) {
            if (cap >= AI_MAX_RESP) {
                ui_log("AI: response too large");
                goto fail;
            }
            cap *= 2;
            char *nb = heap_caps_realloc(buf, cap, MALLOC_CAP_SPIRAM);
            if (nb == NULL) {
                goto fail;
            }
            buf = nb;
        }
        int n = esp_http_client_read(client, buf + len, 2048);
        if (n < 0) {
            ui_log("AI: read error (%d)", n);
            goto fail;
        }
        if (n == 0) {
            break;
        }
        len += n;
    }
    buf[len] = '\0';

    if (status != 200) {
        ui_log("AI: HTTP %d", status);
        ESP_LOGW(TAG, "HTTP %d body: %.256s", status, buf);
        goto fail;
    }
    ai_note_ok();
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return buf;

fail:
    s_ai_link = 0;
    heap_caps_free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return NULL;
}

svc_state_t ai_service_state(void)
{
    if (!ai_is_configured()) {
        return SVC_OFF;
    }
    switch (s_ai_link) {
    case 1:
        /* same aging rule as the music indicator: green = answered within
         * the check window, older than that = dim */
        return (esp_timer_get_time() - s_ai_last_ok_us <= boot_svc_fresh_window_us())
                   ? SVC_UP : SVC_WAIT;
    case 0:  return SVC_FAIL;
    default: return SVC_WAIT;
    }
}

/* GET {ai_url}/v1/models - the OpenAI-compatible "are you there" call. The
 * body is ignored: a 200 proves the server is up and the key is accepted,
 * which is all the indicator claims. */
bool ai_ping(void)
{
    if (!ai_is_configured()) {
        s_ai_link = -1;
        return false;
    }
    ai_creds_t cr;
    ai_snapshot(&cr);

    char url[160];
    snprintf(url, sizeof url, "%s/v1/models", cr.url);

    esp_http_client_config_t hc = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = AI_PING_TIMEOUT_MS,
        .buffer_size = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&hc);
    if (client == NULL) {
        ui_log("AI: ping client init failed");
        s_ai_link = 0;
        return false;
    }
    char auth[80];
    if (cr.key[0] != '\0') {
        snprintf(auth, sizeof auth, "Bearer %s", cr.key);
        esp_http_client_set_header(client, "Authorization", auth);
    }

    bool ok = false;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ui_log("AI server unreachable: %s", esp_err_to_name(err));
    } else if (esp_http_client_fetch_headers(client) < 0) {
        ui_log("AI server: no response headers");
    } else {
        int status = esp_http_client_get_status_code(client);
        ok = (status == 200);
        if (!ok) {
            ui_log("AI server: HTTP %d", status);
        }
    }
    if (ok) {
        ai_note_ok();
    } else {
        s_ai_link = 0;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

/* snprintf that reports the number of bytes actually in the buffer */
static int snprintf_len(char *dst, size_t cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(dst, cap, fmt, ap);
    va_end(ap);
    if (w < 0) {
        return 0;
    }
    return (size_t)w >= cap ? (int)cap - 1 : w;
}

esp_err_t ai_transcribe_wav(const uint8_t *wav, size_t wav_len,
                            char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    if (!ai_is_configured()) {
        ui_log("AI: server not configured (Settings -> AI)");
        return ESP_ERR_INVALID_STATE;
    }
    ai_creds_t cr;
    ai_snapshot(&cr);

    /* multipart/form-data: model field + the WAV file */
    static const char *BOUND = "esp32voicecap";
    char head[256];
    int head_len = snprintf_len(head, sizeof head,
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
        "%s\r\n"
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"cmd.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n",
        BOUND, AI_STT_MODEL, BOUND);
    char tail[48];
    int tail_len = snprintf_len(tail, sizeof tail, "\r\n--%s--\r\n", BOUND);

    char url[160];
    snprintf(url, sizeof url, "%s/v1/audio/transcriptions", cr.url);

    esp_http_client_config_t hc = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = AI_HTTP_TIMEOUT_MS,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t client = esp_http_client_init(&hc);
    if (client == NULL) {
        ui_log("AI: http client init failed");
        return ESP_FAIL;
    }
    char ctype[80];
    snprintf(ctype, sizeof ctype, "multipart/form-data; boundary=%s", BOUND);
    esp_http_client_set_header(client, "Content-Type", ctype);
    char auth[80];
    snprintf(auth, sizeof auth, "Bearer %s", cr.key);
    if (cr.key[0] != '\0') {
        esp_http_client_set_header(client, "Authorization", auth);
    }

    esp_err_t ret = ESP_FAIL;
    char *resp = NULL;
    esp_err_t err = esp_http_client_open(client,
                                         head_len + (int)wav_len + tail_len);
    if (err != ESP_OK) {
        ui_log("AI: connect failed: %s", esp_err_to_name(err));
        goto out;
    }
    if (esp_http_client_write(client, head, head_len) < 0 ||
        esp_http_client_write(client, (const char *)wav, wav_len) < 0 ||
        esp_http_client_write(client, tail, tail_len) < 0) {
        ui_log("AI: upload failed");
        goto out;
    }
    if (esp_http_client_fetch_headers(client) < 0) {
        ui_log("AI: STT no response headers (server silent?)");
        goto out;
    }
    int status = esp_http_client_get_status_code(client);

    resp = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    if (resp == NULL) {
        goto out;
    }
    int len = 0, n;
    while (len < 8191 &&
           (n = esp_http_client_read(client, resp + len, 8191 - len)) > 0) {
        len += n;
    }
    resp[len] = '\0';

    if (status != 200) {
        ui_log("AI: STT HTTP %d (model \"%s\")", status, AI_STT_MODEL);
        ESP_LOGW(TAG, "STT %d body: %.256s", status, resp);
        goto out;
    }

    cJSON *jr = cJSON_Parse(resp);
    if (jr != NULL) {
        const char *text =
            cJSON_GetStringValue(cJSON_GetObjectItem(jr, "text"));
        if (text != NULL) {
            /* trim surrounding whitespace and a trailing period */
            while (*text == ' ' || *text == '\n') {
                text++;
            }
            snprintf(out, out_cap, "%s", text);
            size_t l = strlen(out);
            while (l > 0 && (out[l - 1] == ' ' || out[l - 1] == '\n' ||
                             out[l - 1] == '.')) {
                out[--l] = '\0';
            }
            ret = (l > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
            if (l == 0) {
                ui_log("AI: empty transcript (whisper heard nothing)");
            }
        } else {
            ui_log("AI: no \"text\" in STT response");
        }
        cJSON_Delete(jr);
    } else {
        ui_log("AI: bad JSON from STT");
    }

out:
    heap_caps_free(resp);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ret;
}

esp_err_t ai_pick_best(const char *query, const char *const *options,
                       int count, int *best_idx)
{
    *best_idx = -1;
    if (!ai_is_configured()) {
        ui_log("AI: server not configured (Settings -> AI)");
        return ESP_ERR_INVALID_STATE;
    }
    if (count <= 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (count == 1) {
        *best_idx = 0; /* nothing to disambiguate */
        return ESP_OK;
    }
    ai_creds_t cr;
    ai_snapshot(&cr);

    /* numbered list for the prompt (a NULL option - the caller ran out of
     * memory building it - degrades to an empty line rather than a crash) */
    size_t lcap = 256;
    for (int i = 0; i < count; i++) {
        lcap += (options[i] != NULL ? strlen(options[i]) : 0) + 16;
    }
    char *list = heap_caps_malloc(lcap, MALLOC_CAP_SPIRAM);
    if (list == NULL) {
        return ESP_ERR_NO_MEM;
    }
    size_t off = 0;
    for (int i = 0; i < count && off < lcap; i++) {
        int w = snprintf(list + off, lcap - off, "%d. %s\n", i + 1,
                         options[i] != NULL ? options[i] : "");
        if (w < 0 || (size_t)w >= lcap - off) {
            break; /* clamp: never let off run past lcap (size_t underflow) */
        }
        off += (size_t)w;
    }

    /* Wording tuned against the real library + model by
     * tools/test_ai_matching.py - keep in sync with that script. */
    char prompt[768];
    snprintf(prompt, sizeof prompt,
             "Each list item is a song, written \"title - artist\". "
             "The request names a song or an artist, and may be misspelled "
             "or written how it sounds. The list is ordered with the most "
             "likely match first. Pick the item that best matches; when "
             "torn between similar items prefer the earlier one. "
             "Reply with ONLY its number, nothing else. "
             "Reply 0 only if no item shares any words or sounds with the "
             "request.\n\nRequest: %s\n\nList:\n",
             query);

    /* every cJSON allocation checked: on OOM nothing leaks and the caller
     * falls back to the top local hit */
    esp_err_t ret = ESP_ERR_NO_MEM;
    char *body = NULL;
    char *content = NULL;
    cJSON *root = cJSON_CreateObject();
    cJSON *msgs = root != NULL ? cJSON_AddArrayToObject(root, "messages") : NULL;
    cJSON *m = cJSON_CreateObject();
    if (root == NULL || msgs == NULL || m == NULL) {
        cJSON_Delete(m); /* not yet attached */
        goto build_done;
    }
    cJSON_AddItemToArray(msgs, m); /* owned by root from here on */
    cJSON_AddStringToObject(root, "model", cr.model);
    cJSON_AddNumberToObject(root, "temperature", 0);
    cJSON_AddNumberToObject(root, "max_tokens", 8);
    cJSON_AddStringToObject(m, "role", "user");
    size_t clen = strlen(prompt) + off + 1;
    content = heap_caps_malloc(clen, MALLOC_CAP_SPIRAM);
    if (content == NULL) {
        goto build_done;
    }
    strlcpy(content, prompt, clen);
    strlcat(content, list, clen);
    cJSON_AddStringToObject(m, "content", content);
    body = cJSON_PrintUnformatted(root);

build_done:
    heap_caps_free(content);
    heap_caps_free(list);
    cJSON_Delete(root);
    if (body == NULL) {
        return ret;
    }

    char url[160];
    snprintf(url, sizeof url, "%s/v1/chat/completions", cr.url);
    char *resp = ai_http_post(url, cr.key, body);
    cJSON_free(body);
    if (resp == NULL) {
        return ESP_FAIL;
    }

    ret = ESP_FAIL;
    cJSON *jr = cJSON_Parse(resp);
    heap_caps_free(resp);
    if (jr == NULL) {
        ui_log("AI: bad JSON from server");
        return ESP_FAIL;
    }
    cJSON *choices = cJSON_GetObjectItem(jr, "choices");
    cJSON *msg = cJSON_GetObjectItem(
            cJSON_GetObjectItem(cJSON_GetArrayItem(choices, 0), "message"),
            "content");
    const char *text = cJSON_GetStringValue(msg);
    if (text != NULL) {
        /* tolerate whitespace/markdown around the number */
        while (*text != '\0' && (*text < '0' || *text > '9')) {
            text++;
        }
        int n = atoi(text);
        ESP_LOGI(TAG, "model picked %d of %d", n, count);
        if (n >= 1 && n <= count) {
            *best_idx = n - 1;
            ret = ESP_OK;
        } else {
            ret = ESP_ERR_NOT_FOUND;
        }
    } else {
        ui_log("AI: no completion in response");
    }
    cJSON_Delete(jr);
    return ret;
}
