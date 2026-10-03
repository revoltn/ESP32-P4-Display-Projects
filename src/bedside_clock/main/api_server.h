#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "app_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Optional local HTTP control API (esp_http_server on port 80).
 *
 * Runs only while settings api_enabled is set, api_key is non-empty and
 * WiFi has an IP. Start/stop always happens on the worker task via a
 * reconcile job (never inline in a UI callback or event handler); a slow
 * periodic reconcile heals dropped jobs, so callers just fire-and-forget. */

/* Register the GOT_IP kick and the reconcile timer, then reconcile once.
 * Call from boot_job (worker task) before wifi_mgr_autoconnect(). If the
 * crash-loop breaker reports >= 3 abnormal resets, auto-start is suppressed
 * until the user re-enables the API in Settings. */
void api_server_init(uint32_t crash_count);

/* Request the server to match settings_get()->api_enabled (posts a
 * reconcile job; safe from any task, including the LVGL task). Enabling
 * clears a crash-loop suppression. */
void api_server_set_enabled(bool en);

/* True while the HTTP server is up (for the settings page status line). */
bool api_server_is_running(void);

/* The same fact folded together with "should it be up?", for the clock
 * screen's API indicator. Lock-free: safe from the LVGL tick. */
svc_state_t api_service_state(void);

#ifdef __cplusplus
}
#endif
