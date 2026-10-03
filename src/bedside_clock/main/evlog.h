#pragma once
/* evlog - persistent event journal.
 *
 * A small ring of one-line events (boot, time, WiFi edges, every alarm
 * decision, UI stalls) that survives reboots, so "the alarm did not ring
 * last night" can be answered from the device instead of from a serial
 * capture nobody was running. Two copies:
 *   - a __NOINIT_ATTR ring in internal DRAM: survives every reset except
 *     power-on (crash, watchdog, esp_system_abort) at zero flash cost, so
 *     the entries written seconds before a panic are still there at boot;
 *   - one NVS blob mirror written by the worker task ~2 s after the last
 *     append, so a power cut loses at most the final couple of seconds.
 *
 * evlog() itself never blocks and never touches flash: a portMUX copy into
 * the ring plus a timer kick. Safe from any task (alarm, esp_timer, event
 * loop, LVGL). Not ISR-safe. Read-out: Settings -> Specs, GET
 * /api/v1/events, and the last few lines echoed into ui_log at boot. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EVLOG_CAP      40
#define EVLOG_TEXT_LEN 43

enum {
    EV_BOOT = 0,
    EV_TIME,
    EV_WIFI,
    EV_ALARM,
    EV_UI,
    EV_ERR,
};

typedef struct {
    int64_t  epoch;     /* wall time, 0 = clock not valid yet */
    uint32_t uptime_s;  /* seconds since that boot */
    uint8_t  code;      /* EV_* */
    char     text[EVLOG_TEXT_LEN];
} evlog_entry_t;

/* app_main, right after NVS init: adopt the noinit ring if intact, else the
 * NVS copy, else start empty; then journal this boot. */
void evlog_init(const char *reset_reason, uint32_t crash_count, bool coredump);

/* Append one event (printf-style, truncated to EVLOG_TEXT_LEN-1). Also goes
 * to the serial log at INFO. */
void evlog(uint8_t code, const char *fmt, ...);

/* Oldest-first access for the API and the spec sheet. */
int  evlog_count(void);
bool evlog_get(int idx, evlog_entry_t *out);
const char *evlog_code_str(uint8_t code);

/* "09-19 06:30:02 alarm: ringing 06:30 (mode 1)" - or "+000012s" when the
 * entry predates a valid clock. false when idx is out of range. */
bool evlog_format(int idx, char *buf, size_t cap);

/* Echo the newest `last_n` entries into ui_log (one ui_log_bulk call). */
void evlog_echo_ui(int last_n);

#ifdef __cplusplus
}
#endif
