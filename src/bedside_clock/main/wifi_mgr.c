/* WiFi via esp_wifi_remote -> esp_hosted SDIO -> ESP32-C6, driven by the
 * stored settings. Adapted from hello_world's feature_wifi.c. */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_hosted.h"

#include "app_common.h"
#include "settings.h"
#include "wifi_mgr.h"
#include "evlog.h"

static const char *TAG = "wifi_mgr";

#define MAX_RETRY          3
#define CONNECT_TIMEOUT_MS 20000

#define BIT_GOT_IP BIT0
#define BIT_FAIL   BIT1

#define RECONNECT_PERIOD_US (30 * 1000000ULL)

static EventGroupHandle_t s_ev;
static bool s_inited;
static volatile bool s_connected;
static volatile bool s_manual_disconnect; /* suppress handler on our own disconnect */
static int s_retry;
/* written by the event task, read by LVGL/worker/httpd: copy under the mux */
static char s_ip[16] = "-";
static portMUX_TYPE s_ip_mux = portMUX_INITIALIZER_UNLOCKED;
static esp_timer_handle_t s_reconnect_timer;

/* ---------- RPC serialization ----------
 * Every esp_wifi_* call here is a blocking RPC to the C6 over esp_hosted.
 * esp_hosted 2.12's rpc_core keeps its pending-response table without a
 * lock, so two tasks issuing RPCs at once (the clock's RSSI job on the
 * worker, navidrome's error diagnostics on the music/song_dl tasks, the
 * HTTP status handler) can corrupt a slot: the stored core dump of
 * 2026-09-06 was `hosted_destroy_semaphore(NULL)` asserting on the worker.
 * One recursive mutex around every RPC (nested: connect -> init -> config)
 * closes that. It is NOT held across the 20 s connect wait, so the event
 * task's retry connect can still get in. */
static SemaphoreHandle_t s_rpc_mtx;

static bool rpc_lock(uint32_t timeout_ms)
{
    if (s_rpc_mtx == NULL) {
        app_init_lock();
        if (s_rpc_mtx == NULL) {
            s_rpc_mtx = xSemaphoreCreateRecursiveMutex();
        }
        app_init_unlock();
        if (s_rpc_mtx == NULL) {
            return false; /* no RAM for a mutex: refuse the RPC */
        }
    }
    TickType_t ticks = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY
                                                      : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_rpc_mtx, ticks) == pdTRUE;
}

static void rpc_unlock(void)
{
    xSemaphoreGiveRecursive(s_rpc_mtx);
}

void wifi_mgr_ip(char *out, size_t cap)
{
    taskENTER_CRITICAL(&s_ip_mux);
    strlcpy(out, s_ip, cap);
    taskEXIT_CRITICAL(&s_ip_mux);
}

/* caller holds the RPC lock */
static bool apply_sta_config(const char *ssid, const char *pass)
{
    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    return esp_wifi_set_config(WIFI_IF_STA, &wc) == ESP_OK;
}

/* Periodic background reconnect once the quick retry budget is spent -
 * without this an AP reboot would kill WiFi until the device reboots.
 * Also covers a C6 that was not ready at boot: if the driver never came up,
 * the whole bring-up is retried (every 4th period, to keep the on-screen
 * log readable) and the stored credentials are re-applied. */
static void reconnect_job(void *arg)
{
    if (s_connected) {
        return;
    }
    if (!rpc_lock(portMAX_DELAY)) {
        return;
    }
    if (!s_inited) {
        static unsigned init_tries;
        if (init_tries++ % 4 != 0) {
            rpc_unlock();
            return;
        }
        ESP_LOGI(TAG, "background WiFi init retry");
        if (!wifi_mgr_init()) {
            rpc_unlock();
            return;
        }
        char ssid[33], pass[65];
        settings_lock();
        strlcpy(ssid, settings_get()->wifi_ssid, sizeof(ssid));
        strlcpy(pass, settings_get()->wifi_pass, sizeof(pass));
        settings_unlock();
        if (ssid[0] == '\0' || !apply_sta_config(ssid, pass)) {
            rpc_unlock();
            return;
        }
    }
    ESP_LOGI(TAG, "background reconnect attempt");
    esp_wifi_connect(); /* async; result arrives as an event */
    rpc_unlock();
}

static void reconnect_timer_cb(void *arg)
{
    /* esp_wifi_connect over esp_hosted is a blocking SDIO RPC to the C6 -
     * far too slow for the shared esp_timer task. Run it on the worker;
     * a full queue just means we try again next period. */
    if (!s_connected) {
        app_run_async(reconnect_job, NULL);
    }
}

static void reconnect_timer_set(bool on)
{
    if (s_reconnect_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = reconnect_timer_cb,
            .name = "wifi_reconn",
        };
        if (esp_timer_create(&args, &s_reconnect_timer) != ESP_OK) {
            return;
        }
    }
    if (on && !esp_timer_is_active(s_reconnect_timer)) {
        esp_timer_start_periodic(s_reconnect_timer, RECONNECT_PERIOD_US);
    } else if (!on && esp_timer_is_active(s_reconnect_timer)) {
        esp_timer_stop(s_reconnect_timer);
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        bool was_up = s_connected;
        s_connected = false;
        /* the reason code is the one field that separates wrong-password
         * (AUTH_FAIL/HANDSHAKE_TIMEOUT) from AP-not-found / AP-rebooted */
        const wifi_event_sta_disconnected_t *d = data;
        ESP_LOGI(TAG, "disconnected, reason %d%s", d != NULL ? d->reason : -1,
                 s_manual_disconnect ? " (ours)" : "");
        if (was_up && !s_manual_disconnect) {
            /* a real drop, not a failed attempt (those retry every 30 s) */
            evlog(EV_WIFI, "dropped, reason %d", d != NULL ? d->reason : -1);
        }
        if (s_manual_disconnect) {
            s_manual_disconnect = false;
            return;
        }
        if (s_retry < MAX_RETRY) {
            s_retry++;
            ESP_LOGI(TAG, "retry %d", s_retry);
            /* system event task: never park it behind a long scan on the
             * worker - if the RPC lock is busy, skip this retry (the
             * background timer or the caller's timeout picks it up) */
            if (rpc_lock(2000)) {
                esp_wifi_connect();
                rpc_unlock();
            } else {
                ESP_LOGW(TAG, "retry skipped: RPC busy");
            }
        } else {
            xEventGroupSetBits(s_ev, BIT_FAIL);
            /* keep trying in the background if we have stored credentials */
            if (settings_get()->wifi_ssid[0] != '\0') {
                reconnect_timer_set(true);
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ev->ip_info.ip));
        taskENTER_CRITICAL(&s_ip_mux);
        strlcpy(s_ip, ip, sizeof(s_ip));
        taskEXIT_CRITICAL(&s_ip_mux);
        s_connected = true;
        s_retry = 0;
        reconnect_timer_set(false);
        evlog(EV_WIFI, "connected, IP %s", ip);
        xEventGroupSetBits(s_ev, BIT_GOT_IP);
    }
}

/* One-shot resources that must survive a failed init: a retry after a
 * mid-init failure must not leak event groups, create a second default
 * netif (that aborts) or double-register the event handlers (that would
 * double every reconnect attempt). */
static esp_netif_t *s_netif;
static bool s_handlers_registered;

static bool wifi_mgr_init_locked(void);

bool wifi_mgr_init(void)
{
    if (s_inited) {
        return true;
    }
    if (!rpc_lock(portMAX_DELAY)) {
        return false;
    }
    bool ok = wifi_mgr_init_locked();
    rpc_unlock();
    return ok;
}

static bool wifi_mgr_init_locked(void)
{
    if (s_inited) {
        return true;
    }
    if (s_ev == NULL) {
        s_ev = xEventGroupCreate();
        if (s_ev == NULL) {
            return false;
        }
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ui_log("esp_netif_init failed: %s", esp_err_to_name(err));
        return false;
    }
    if (s_netif == NULL) {
        s_netif = esp_netif_create_default_wifi_sta();
        if (s_netif == NULL) {
            ui_log("WiFi netif create failed");
            return false;
        }
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    /* Transparently brings up the esp_hosted SDIO link to the C6. */
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ui_log("WiFi init FAILED (%s) - C6 co-processor not responding",
               esp_err_to_name(err));
        return false;
    }

    /* A bedside clock must keep showing the time: none of these failures
     * may abort the firmware (no ESP_ERROR_CHECK here). */
    if (!s_handlers_registered) {
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                  wifi_event_handler, NULL, NULL);
        if (err == ESP_OK) {
            err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                      wifi_event_handler, NULL, NULL);
        }
        if (err != ESP_OK) {
            ui_log("WiFi event handler register failed: %s", esp_err_to_name(err));
            return false;
        }
        s_handlers_registered = true;
    }
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ui_log("esp_wifi_set_mode failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ui_log("esp_wifi_start failed: %s", esp_err_to_name(err));
        return false;
    }

    esp_hosted_coprocessor_fwver_t fwver;
    if (esp_hosted_get_coprocessor_fwversion(&fwver) == ESP_OK) {
        ESP_LOGI(TAG, "C6 co-processor: esp_hosted %lu.%lu.%lu",
                 (unsigned long)fwver.major1, (unsigned long)fwver.minor1,
                 (unsigned long)fwver.patch1);
    }

    s_inited = true;
    return true;
}

int wifi_mgr_scan(wifi_scan_result_t *out, int max)
{
    if (!wifi_mgr_init()) {
        return -1;
    }
    if (!rpc_lock(portMAX_DELAY)) {
        return -1;
    }

    wifi_scan_config_t sc = { 0 };
    sc.show_hidden = false;
    esp_err_t err = esp_wifi_scan_start(&sc, true /* block */);
    if (err != ESP_OK) {
        rpc_unlock();
        ui_log("WiFi scan failed: %s", esp_err_to_name(err));
        return -1;
    }

    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n == 0) {
        esp_wifi_clear_ap_list(); /* frees the driver-side scan list */
        rpc_unlock();
        return 0;
    }
    if (n > WIFI_SCAN_MAX) {
        n = WIFI_SCAN_MAX;
    }
    wifi_ap_record_t recs[WIFI_SCAN_MAX];
    err = esp_wifi_scan_get_ap_records(&n, recs);
    rpc_unlock();
    if (err != ESP_OK) {
        return -1;
    }

    int count = 0;
    for (int i = 0; i < n && count < max; i++) {
        if (recs[i].ssid[0] == '\0') {
            continue;
        }
        /* de-duplicate SSIDs (multi-AP networks) */
        bool dup = false;
        for (int j = 0; j < count; j++) {
            if (strcmp(out[j].ssid, (const char *)recs[i].ssid) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        strlcpy(out[count].ssid, (const char *)recs[i].ssid, sizeof(out[count].ssid));
        out[count].rssi = recs[i].rssi;
        out[count].secure = (recs[i].authmode != WIFI_AUTH_OPEN);
        count++;
    }
    return count;
}

/* Blocking connect. `stored_creds` = the credentials are the persisted ones
 * that worked before (boot autoconnect): a failure then means the AP is not
 * there YET (router still booting after a power cut, C6 not ready), so the
 * background reconnect stays armed. User-typed credentials (`false`) may
 * simply be wrong, so those are not retried in the background. Before this
 * distinction existed the boot autoconnect's failure path cancelled the
 * timer the event handler had just armed - a clock that lost power with its
 * router never got back online without a reboot. */
static bool connect_impl(const char *ssid, const char *pass, bool stored_creds)
{
    if (!wifi_mgr_init()) {
        if (stored_creds) {
            ui_set_status("WiFi unavailable - retrying in background");
            reconnect_timer_set(true); /* reconnect_job retries the init too */
        }
        return false;
    }
    reconnect_timer_set(false); /* this attempt supersedes background retry */
    if (s_connected) {
        s_manual_disconnect = true;
        if (rpc_lock(portMAX_DELAY)) {
            esp_wifi_disconnect();
            rpc_unlock();
        }
        s_connected = false;
        vTaskDelay(pdMS_TO_TICKS(500));
        s_manual_disconnect = false;
    }

    s_retry = 0;
    xEventGroupClearBits(s_ev, BIT_GOT_IP | BIT_FAIL);

    ui_set_status("WiFi: connecting to %s", ssid);
    esp_err_t err = ESP_FAIL;
    if (rpc_lock(portMAX_DELAY)) {
        err = apply_sta_config(ssid, pass) ? ESP_OK : ESP_FAIL;
        if (err == ESP_OK) {
            err = esp_wifi_connect();
        }
        rpc_unlock(); /* not held across the wait: the event task retries */
    }
    if (err != ESP_OK) {
        ui_log("WiFi connect failed: %s", esp_err_to_name(err));
        if (stored_creds) {
            reconnect_timer_set(true);
        }
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(s_ev, BIT_GOT_IP | BIT_FAIL,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));
    if (bits & BIT_GOT_IP) {
        s_connected = true;
        char ip[16];
        wifi_mgr_ip(ip, sizeof(ip));
        ui_log("WiFi connected to \"%s\", IP %s", ssid, ip);
        ui_set_status("WiFi: %s (%s)", ssid, ip);
        return true;
    }
    if (stored_creds) {
        /* leave the driver's own retry alone and keep trying every 30 s */
        ui_log("WiFi: \"%s\" not reachable - retrying in background", ssid);
        ui_set_status("WiFi: not connected - retrying");
        reconnect_timer_set(true);
        return false;
    }
    ui_log("WiFi: could not connect to \"%s\"", ssid);
    ui_set_status("WiFi: connect failed");
    /* Give up cleanly - no background retry of possibly-wrong credentials. */
    s_manual_disconnect = true;
    if (rpc_lock(portMAX_DELAY)) {
        esp_wifi_disconnect();
        rpc_unlock();
    }
    reconnect_timer_set(false);
    /* The flag must not outlive OUR disconnect event: left set, it would
     * swallow the next genuine AP drop and kill reconnection until reboot. */
    vTaskDelay(pdMS_TO_TICKS(500));
    s_manual_disconnect = false;
    return false;
}

bool wifi_mgr_connect(const char *ssid, const char *pass)
{
    return connect_impl(ssid, pass, false);
}

bool wifi_mgr_is_connected(void)
{
    return s_connected;
}

bool wifi_mgr_get_info(wifi_conn_info_t *out)
{
    if (!s_connected) {
        return false;
    }
    wifi_ap_record_t ap;
    /* Short bound: every caller is a diagnostic (RSSI bars, the Navidrome
     * error log, the settings page) and each keeps its previous reading on
     * failure, so a miss costs nothing while a long wait here would park the
     * worker or the music task behind a scan or a connect. */
    if (!rpc_lock(1000)) {
        return false;
    }
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap);
    rpc_unlock();
    if (err != ESP_OK) {
        return false;
    }
    strlcpy(out->ssid, (const char *)ap.ssid, sizeof(out->ssid));
    wifi_mgr_ip(out->ip, sizeof(out->ip));
    out->rssi = ap.rssi;
    out->channel = ap.primary;
    switch (ap.authmode) {
    case WIFI_AUTH_OPEN:            out->security = "open";      break;
    case WIFI_AUTH_WEP:             out->security = "WEP";       break;
    case WIFI_AUTH_WPA_PSK:         out->security = "WPA";       break;
    case WIFI_AUTH_WPA2_PSK:        out->security = "WPA2";      break;
    case WIFI_AUTH_WPA_WPA2_PSK:    out->security = "WPA/WPA2";  break;
    case WIFI_AUTH_WPA3_PSK:        out->security = "WPA3";      break;
    case WIFI_AUTH_WPA2_WPA3_PSK:   out->security = "WPA2/WPA3"; break;
    default:                        out->security = "secured";   break;
    }
    return true;
}

bool wifi_mgr_autoconnect(void)
{
    bedside_settings_t *cfg = settings_get();
    if (cfg->wifi_ssid[0] == '\0') {
        ui_set_status("WiFi not configured - open settings");
        return false;
    }
    if (s_connected) {
        return true;
    }
    return connect_impl(cfg->wifi_ssid, cfg->wifi_pass, true);
}
