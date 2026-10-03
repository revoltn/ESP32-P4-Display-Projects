#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ordered service startup. Stage 0 (NVS, settings, timezone, display, clock
 * UI, worker task) happens in app_main before this is called; boot_seq owns
 * everything after it. See boot_seq.c for the stage list and the reasoning
 * behind the order. */

/* Post the boot sequence to the worker task. `minimal` = the worker could not
 * be created and the sequence will run inline on the main task, which has a
 * smaller stack: the heavy voice bring-up is skipped. Call once, last, from
 * app_main. */
void boot_seq_start(uint32_t crash_count, bool minimal);

/* Re-run the network probes (music server, AI) and refresh their indicators.
 * Fire-and-forget from any task: the work is posted to the worker and a
 * dropped job is picked up by the periodic re-probe. Called on GOT_IP and
 * after a settings save changes credentials or the check interval. Forced:
 * both servers are pinged even if recent traffic already proved them. */
void boot_net_probe_kick(void);

/* How old a server's last successful answer may be for its indicator to
 * still read "up" (green): two svc_poll_s periods plus slack. INT64_MAX when
 * polling is off (svc_poll_s == 0), so the indicator then stays sticky on
 * the outcome of the last real call. Lock-free; the 1 s LVGL tick calls it. */
int64_t boot_svc_fresh_window_us(void);

#ifdef __cplusplus
}
#endif
