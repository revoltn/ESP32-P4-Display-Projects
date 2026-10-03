/* Ordered service startup for bedside_clock.
 *
 * Everything here runs on the worker task (app_main posts boot_seq_job), so
 * each stage may block. The order is dependency-driven, cheapest and most
 * essential first, so that a missing network or a dead server can only ever
 * delay the things that genuinely need them:
 *
 *   Stage 0  platform          (app_main, before this file runs)
 *            NVS -> crash guard -> settings -> timezone -> display,
 *            brightness, orientation, clock face -> clock UI -> worker task
 *   Stage 1  local hardware    codec + player, alarm scheduler, ambient sensing
 *   Stage 2  network root      API server registration, WiFi autoconnect
 *   Stage 3  network services  SNTP, music server probe, AI probe
 *   Stage 4  heavy local       voice (esp-sr models, ~MB PSRAM, two tasks)
 *
 * Stage 1 comes before the (up to ~20 s) WiFi connect because a bedside clock
 * must ring its alarm whether or not the network ever arrives. Stage 3 is
 * gated on stage 2 and is re-runnable: WiFi can turn up late (wifi_mgr's
 * background reconnect retries every 30 s) and a server can be booted after
 * the clock is. The re-run is driven by a GOT_IP handler plus a periodic
 * timer paced by the user's svc_poll_s setting, both posting the same
 * idempotent job - the pattern api_server.c already uses for its own
 * start/stop reconcile.
 *
 * Each stage that has an indicator on the clock face updates it simply by
 * making the state true; ui_clock polls the <mod>_service_state() accessors
 * on its 1 s tick. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "bsp_board_extra.h"

#include "app_common.h"
#include "settings.h"
#include "wifi_mgr.h"
#include "clock_sync.h"
#include "alarm.h"
#include "ambient.h"
#include "usb_audio.h"
#include "voice.h"
#include "api_server.h"
#include "navidrome_client.h"
#include "localai_client.h"
#include "boot_seq.h"

static const char *TAG = "boot_seq";

/* At this many consecutive abnormal resets the two heaviest, most
 * hardware-dependent subsystems (voice, camera) stay off: safe mode. */
#define CRASH_SAFE_MODE_MIN 3

/* The re-probe cadence is the user's svc_poll_s setting (0 = off). A fast
 * fixed tick compares "time since the last probe" against the live setting,
 * so a changed interval takes effect without restarting any timer. */
#define NET_REPROBE_TICK_US   (10 * 1000000ULL)
/* Slack added to two poll periods when deciding that a last "ok" is stale:
 * one missed probe is a hiccup, two plus the probe timeout is a dead link. */
#define SVC_FRESH_SLACK_US    (15 * 1000000LL)

static uint32_t s_crash_count;
static esp_timer_handle_t s_reprobe_timer;
static volatile int64_t s_last_probe_us;   /* when net_probe_job last ran */
/* Set by boot_net_probe_kick (GOT_IP, settings save): probe both servers
 * even if real traffic recently proved them. The timer path leaves it clear
 * so a device that is streaming music never pings the server on top. */
static volatile bool s_probe_force;

int64_t boot_svc_fresh_window_us(void)
{
    uint16_t poll_s = settings_get()->svc_poll_s; /* u16: lock-free read */
    if (poll_s == 0) {
        return INT64_MAX; /* polling off: nothing would refresh it, stay sticky */
    }
    return 2 * (int64_t)poll_s * 1000000LL + SVC_FRESH_SLACK_US;
}

/* Request/served counters, not a simple "queued" flag: several kicks can be
 * in flight at once (the boot path's GOT_IP fires while boot_seq_job is still
 * running, and it then probes inline). Each probe serves whatever the latest
 * request number was when it started, so any job that finds its request
 * already served is a duplicate and returns. Without this every boot probed
 * both servers twice. */
static volatile uint32_t s_probe_req;
static volatile uint32_t s_probe_done;

/* ---------- stage 3: network services ---------- */

/* Idempotent, and safe to call when the network is down: each probe reports
 * its own failure and leaves its indicator red. */
static void net_probe_job(void *arg)
{
    uint32_t req = s_probe_req;
    if (s_probe_done == req) {
        return; /* an earlier job already covered this request */
    }
    if (!wifi_mgr_is_connected()) {
        /* Leave the request outstanding so the GOT_IP handler re-runs it. */
        return;
    }
    s_probe_done = req;
    bool force = s_probe_force;
    s_probe_force = false;
    int64_t now = esp_timer_get_time();
    s_last_probe_us = now;

    clock_sync_start(); /* SNTP; no-op after the first call */

    /* A periodic (unforced) run skips a server that real traffic - a search,
     * a playlist fetch, a song stream - already answered within the last
     * poll period: the ping would prove nothing and only keep the server
     * busy. A never-seen server (last_ok == 0) is always probed. */
    int64_t period_us = (int64_t)settings_get()->svc_poll_s * 1000000LL;

    /* Both probes are bounded at 5 s so a dead server cannot hold the worker
     * (and, on the boot path, cannot delay the voice bring-up behind us).
     * The result is logged only when it changes: at a 30 s cadence a line
     * per probe would drown the device log. */
    if (nav_is_configured() &&
        (force || nav_last_ok_us() == 0 || now - nav_last_ok_us() >= period_us)) {
        int before = nav_link_state();
        bool up = nav_ping();
        if (force || (up ? 1 : 0) != before) {
            ui_log("Music server: %s", up ? "connected" : "NOT reachable");
        }
    }
    if (ai_is_configured() &&
        (force || ai_last_ok_us() == 0 || now - ai_last_ok_us() >= period_us)) {
        svc_state_t before = ai_service_state();
        bool up = ai_ping();
        if (force || (up ? SVC_UP : SVC_FAIL) != before) {
            ui_log("AI server: %s", up ? "connected" : "NOT reachable");
        }
    }
}

static void probe_post(bool force)
{
    if (force) {
        s_probe_force = true;
    }
    s_probe_req++;
    if (!app_run_async(net_probe_job, NULL)) {
        /* queue full: the request stays outstanding and the periodic timer
         * (or the next kick) re-posts it */
        ESP_LOGW(TAG, "net probe deferred: job queue full");
    }
}

void boot_net_probe_kick(void)
{
    probe_post(true);
}

/* Event task and esp_timer task: both must stay short, so they only post. */
static void got_ip_handler(void *arg, esp_event_base_t base, int32_t id,
                           void *data)
{
    boot_net_probe_kick();
}

static void reprobe_timer_cb(void *arg)
{
    uint16_t poll_s = settings_get()->svc_poll_s; /* u16: lock-free read */
    if (poll_s == 0 || !wifi_mgr_is_connected()) {
        return;
    }
    if (s_probe_req != s_probe_done) {
        return; /* a probe is already queued or running */
    }
    if (esp_timer_get_time() - s_last_probe_us >= (int64_t)poll_s * 1000000LL) {
        probe_post(false);
    }
}

static void net_watch_start(void)
{
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        got_ip_handler, NULL, NULL);
    const esp_timer_create_args_t targs = {
        .callback = reprobe_timer_cb,
        .name = "net_reprobe",
    };
    if (esp_timer_create(&targs, &s_reprobe_timer) == ESP_OK) {
        esp_timer_start_periodic(s_reprobe_timer, NET_REPROBE_TICK_US);
    }
}

/* ---------- the sequence ---------- */

/* arg != NULL = degraded inline run on the main task (worker creation
 * failed): skip the heavy voice bring-up, keep everything the alarm needs. */
static void boot_seq_job(void *arg)
{
    bool minimal = (arg != NULL);
    /* Safe mode (MUST-IMPLEMENT §1): after repeated crashes bring up only
     * what a bedside clock needs - time, WiFi/SNTP, alarm, on-demand music.
     * Voice (esp-sr, ~MB of PSRAM, two tasks) and the camera stay off. The
     * counter clears after 30 s of stable uptime, so the next clean boot is
     * a normal one. */
    bool safe_mode = (s_crash_count >= CRASH_SAFE_MODE_MIN);
    if (safe_mode) {
        ui_log("SAFE MODE after %lu consecutive crashes: voice and auto-dim off "
               "this boot", (unsigned long)s_crash_count);
        ui_set_status("Safe mode (%lu crashes): voice/auto-dim off",
                      (unsigned long)s_crash_count);
    }

    /* --- stage 1: local hardware, needs neither network nor time --- */

    /* Codec + player up-front: the 200 ms codec settle and the audio task
     * creation must never land on the user's first play or volume touch
     * (via the LVGL task, that used to freeze the whole UI). */
    /* USB audio host: a USB speaker takes over from the built-in mono one
     * when it is plugged in. Brought up before the codec so the route is
     * known by the time the alarm arms, and skipped in safe mode for the same
     * reason the camera is - it is the newest hardware path here, and a
     * bedside clock that boots is worth more than one that has stereo. */
    if (!safe_mode) {
        usb_audio_init();
    }
    if (app_audio_ensure_init()) {
        bsp_extra_player_init();
    }
    /* The alarm is the one function this device must never lose, so its
     * scheduler comes up before anything that can block on the network. The
     * TZ was applied in clock_sync_early_init, so it computes the right
     * instant from its first tick even before SNTP. */
    alarm_init();
    if (!safe_mode) {
        ambient_init();
    }

    /* --- stage 2: network root; everything below depends on this --- */

    /* Registered before autoconnect so the GOT_IP kicks (the API server's own
     * and ours) cannot be missed; both also reconcile at init for the
     * already-connected case. */
    api_server_init(s_crash_count);
    net_watch_start();
    bool online = wifi_mgr_autoconnect();

    /* --- stage 3: network services --- */

    if (online) {
        /* Already on the worker, so run it inline rather than queueing: the
         * probes must land before the voice bring-up below, not behind it.
         * The counter bump makes the GOT_IP kick that autoconnect just
         * triggered a no-op when its job is finally dequeued. */
        s_probe_req++;
        net_probe_job(NULL);
    } else {
        /* Not an error: wifi_mgr keeps retrying every 30 s and got_ip_handler
         * runs stage 3 the moment it succeeds. SNTP is started regardless so
         * it syncs by itself as soon as there is a route. */
        clock_sync_start();
        ui_log("Network services waiting for WiFi");
    }

    /* --- stage 4: heavy local, nothing waits on it --- */

    if (!minimal && !safe_mode) {
        voice_init();
    }

    ui_log("bedside_clock ready%s%s", minimal ? " (degraded: no worker task)" : "",
           safe_mode ? " (safe mode)" : "");
}

void boot_seq_start(uint32_t crash_count, bool minimal)
{
    s_crash_count = crash_count;
    if (minimal) {
        /* Without the worker nothing async ever runs: WiFi, SNTP, alarm,
         * ambient would all silently never start. Run inline on the main task
         * (8 KB: no TLS on this path) minus the voice stack. */
        ESP_LOGE(TAG, "no worker task - booting inline");
        ui_set_status("Low memory: running without background worker");
        boot_seq_job((void *)1);
    } else if (!app_run_async(boot_seq_job, NULL)) {
        boot_seq_job((void *)1); /* queue is fresh; cannot happen, but never drop */
    }
}
