/* WiFi via esp_wifi_remote -> esp_hosted SDIO -> ESP32-C6.
 * Credentials come from Kconfig (menu "Sample: hello world"):
 * CONFIG_SAMPLE_WIFI_SSID is tried first, then the optional
 * CONFIG_SAMPLE_WIFI_SSID_ALT, both with CONFIG_SAMPLE_WIFI_PASSWORD. */
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_hosted.h"

#include "app_common.h"

static const char *TAG = "wifi";

#define MAX_RETRY      3
#define CONNECT_TIMEOUT_MS 20000

/* Primary first, then the optional fallback; empty entries are skipped. */
static const char *SSIDS[] = { CONFIG_SAMPLE_WIFI_SSID, CONFIG_SAMPLE_WIFI_SSID_ALT };

#define BIT_GOT_IP BIT0
#define BIT_FAIL   BIT1

static EventGroupHandle_t s_ev;
static bool s_inited;
static bool s_connected;
static int s_retry;
static char s_ip[16];

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry < MAX_RETRY) {
            s_retry++;
            ESP_LOGI(TAG, "retry %d", s_retry);
            esp_wifi_connect();
        } else {
            s_connected = false;
            xEventGroupSetBits(s_ev, BIT_FAIL);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupSetBits(s_ev, BIT_GOT_IP);
    }
}

/* No SSID in the build = nothing to connect to. Says so on every attempt so
 * the menu tile explains itself instead of failing silently. */
static bool wifi_configured(void)
{
    if (CONFIG_SAMPLE_WIFI_SSID[0] != '\0') {
        return true;
    }
    ui_log("WiFi not configured: set SAMPLE_WIFI_SSID/PASSWORD via idf.py menuconfig "
           "(or sdkconfig.local.defaults)");
    ui_set_status("WiFi: not configured");
    return false;
}

static bool wifi_init_once(void)
{
    if (s_inited) {
        return true;
    }
    s_ev = xEventGroupCreate();

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        ui_log("esp_netif_init failed: %s", esp_err_to_name(err));
        return false;
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    /* This transparently brings up the esp_hosted SDIO link to the C6.
     * Failure here means the co-processor is not responding. */
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ui_log("WiFi init FAILED (%s) - ESP32-C6 co-processor not responding over SDIO",
               esp_err_to_name(err));
        return false;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ui_log("esp_wifi_start failed: %s", esp_err_to_name(err));
        return false;
    }

    /* MUST-IMPLEMENT §4: log the co-processor firmware version at runtime —
     * a host/slave esp_hosted version mismatch silently breaks Bluetooth. */
    esp_hosted_coprocessor_fwver_t fwver;
    if (esp_hosted_get_coprocessor_fwversion(&fwver) == ESP_OK) {
        ui_log("C6 co-processor: esp_hosted %lu.%lu.%lu",
               (unsigned long)fwver.major1, (unsigned long)fwver.minor1,
               (unsigned long)fwver.patch1);
    }

    s_inited = true;
    return true;
}

static bool try_connect(const char *ssid)
{
    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, CONFIG_SAMPLE_WIFI_PASSWORD, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = (CONFIG_SAMPLE_WIFI_PASSWORD[0] != '\0')
                                ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    s_retry = 0;
    xEventGroupClearBits(s_ev, BIT_GOT_IP | BIT_FAIL);

    ui_log("Connecting to \"%s\"...", ssid);
    ui_set_status("WiFi: connecting to %s", ssid);

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (err != ESP_OK) {
        ui_log("set_config failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ui_log("connect failed: %s", esp_err_to_name(err));
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(s_ev, BIT_GOT_IP | BIT_FAIL,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));
    if (bits & BIT_GOT_IP) {
        ui_log("Connected to \"%s\", IP %s", ssid, s_ip);
        ui_set_status("WiFi: %s  IP: %s", ssid, s_ip);
        return true;
    }
    ui_log("Could not connect to \"%s\" (%s)", ssid,
           (bits & BIT_FAIL) ? "gave up after retries" : "timeout");
    esp_wifi_disconnect();
    return false;
}

bool wifi_ensure_connected(void)
{
    if (s_connected) {
        return true;
    }
    if (!wifi_configured()) {
        return false;
    }
    if (!wifi_init_once()) {
        ui_set_status("WiFi unavailable");
        return false;
    }
    for (size_t i = 0; i < sizeof(SSIDS) / sizeof(SSIDS[0]); i++) {
        if (SSIDS[i][0] == '\0') {
            continue;
        }
        if (try_connect(SSIDS[i])) {
            s_connected = true;
            return true;
        }
    }
    return false;
}

void feature_wifi_run(void)
{
    if (!wifi_configured()) {
        return;
    }
    if (!wifi_init_once()) {
        ui_set_status("WiFi unavailable");
        return;
    }
    if (s_connected) {
        ui_log("Reconnecting: dropping current connection first");
        esp_wifi_disconnect();
        s_connected = false;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!wifi_ensure_connected()) {
        ui_set_status("WiFi: failed");
        ui_log("WiFi failed: no configured SSID reachable");
    }
}
