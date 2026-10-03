#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SCAN_MAX 20

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
} wifi_scan_result_t;

/* Bring up esp_hosted + esp_wifi once. False when the C6 is unresponsive. */
bool wifi_mgr_init(void);

/* Blocking scan (call from worker task). Returns found count, -1 on error. */
int wifi_mgr_scan(wifi_scan_result_t *out, int max);

/* Blocking connect (call from worker task). */
bool wifi_mgr_connect(const char *ssid, const char *pass);

bool wifi_mgr_is_connected(void);
/* Copy of the current IP ("-" when none); safe from any task. */
void wifi_mgr_ip(char *out, size_t cap);

typedef struct {
    char ssid[33];
    char ip[16];
    int8_t rssi;           /* dBm */
    uint8_t channel;
    const char *security;  /* static string, e.g. "WPA2" */
} wifi_conn_info_t;

/* Details of the current connection (RPC to the C6). False when not
 * connected or the query fails. */
bool wifi_mgr_get_info(wifi_conn_info_t *out);

/* Connect using stored settings if an SSID is configured. */
bool wifi_mgr_autoconnect(void);

#ifdef __cplusplus
}
#endif
