/* ui_watch.c - LVGL stall detector + touch trace (see ui_watch.h).
 *
 * Heartbeat: a 250 ms lv_timer bumps s_beat on the LVGL task. Checker: a
 * 2 s periodic esp_timer (esp_timer task, prio 22, core 0 - it keeps
 * running whatever the LVGL task is doing) compares s_beat with the last
 * sample. A stall is logged once when detected with the LVGL task's
 * FreeRTOS state, every STALL_REPEAT_MS while it lasts, and once more when
 * the heartbeat resumes. The first report also asks the worker to print
 * vTaskGetRunTimeStats() so a starved UI names the task eating the CPU.
 *
 * Escalation: a stall that outlives STALL_ABORT_MS is a dead clock (touch is
 * polled by the LVGL task, so the alarm overlay could not even be dismissed)
 * and nothing in the system recovers it - the 2026-09-06 freeze sat for
 * 7+ minutes with taskLVGL blocked inside the PPA driver. We abort: with
 * core dumps to flash that records every task's backtrace (read it with
 * `idf.py coredump-info`) and reboots into a working clock. */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "lvgl.h"

#include "app_common.h"
#include "ui_watch.h"
#include "evlog.h"

static const char *TAG = "ui_watch";

#define BEAT_MS         250
#define CHECK_MS        2000
#define STALL_REPEAT_MS 10000
#define STALL_ABORT_MS  30000
#define STATS_BUF_CAP   4096

static volatile uint32_t s_beat;
static TaskHandle_t s_lvgl_task;      /* recorded on the first heartbeat */
static uint32_t s_last_beat;
static int64_t s_stall_since_us;      /* 0 = healthy */
static int64_t s_last_report_us;
static bool s_stats_job_pending;

static void beat_cb(lv_timer_t *t)
{
    if (s_lvgl_task == NULL) {
        s_lvgl_task = xTaskGetCurrentTaskHandle();
    }
    s_beat++;
}

static char task_state_char(TaskHandle_t h)
{
    if (h == NULL) {
        return '?';
    }
    switch (eTaskGetState(h)) {
    case eRunning:   return 'X'; /* running right now (on another core) */
    case eReady:     return 'R'; /* ready but not scheduled: starved */
    case eBlocked:   return 'B'; /* waiting on a mutex/semaphore/delay */
    case eSuspended: return 'S';
    case eDeleted:   return 'D';
    default:         return '?';
    }
}

/* worker task: the formatted table is a few KB - keep it off the esp_timer
 * task's 3.5 KB stack and out of internal RAM */
static void stats_job(void *arg)
{
    char *buf = heap_caps_malloc(STATS_BUF_CAP, MALLOC_CAP_SPIRAM);
    if (buf != NULL) {
        buf[0] = '\0';
        vTaskGetRunTimeStats(buf);
        buf[STATS_BUF_CAP - 1] = '\0';
        ESP_LOGW(TAG, "run-time stats (name, abs time, %%):\n%s", buf);
        heap_caps_free(buf);
    }
    s_stats_job_pending = false;
}

static void check_cb(void *arg)
{
    uint32_t beat = s_beat;
    int64_t now = esp_timer_get_time();

    if (beat != s_last_beat) {
        s_last_beat = beat;
        if (s_stall_since_us != 0) {
            int ms = (int)((now - s_stall_since_us) / 1000);
            ESP_LOGW(TAG, "LVGL task resumed after %d ms", ms);
            if (ms >= STALL_REPEAT_MS) {
                evlog(EV_UI, "LVGL resumed after %d ms", ms);
            }
            s_stall_since_us = 0;
        }
        return;
    }

    /* no heartbeat since the last check (>= CHECK_MS, normally 8 beats) */
    bool first = (s_stall_since_us == 0);
    if (first) {
        s_stall_since_us = now - CHECK_MS * 1000;
    }
    int stalled_ms = (int)((now - s_stall_since_us) / 1000);
    if (stalled_ms >= STALL_ABORT_MS) {
        ESP_LOGE(TAG, "LVGL task stalled %d ms, state=%c - aborting for a core "
                 "dump and reboot", stalled_ms, task_state_char(s_lvgl_task));
        /* noinit ring survives the abort; the next boot mirrors it to NVS */
        evlog(EV_UI, "LVGL stalled %d ms state=%c - abort+reboot", stalled_ms,
              task_state_char(s_lvgl_task));
        esp_system_abort("LVGL task stalled");
    }
    if (!first && now - s_last_report_us < STALL_REPEAT_MS * 1000) {
        return;
    }
    s_last_report_us = now;
    if (stalled_ms >= STALL_REPEAT_MS) {
        /* the first (2 s) report is usually a long flush; 10 s+ is a wedge */
        evlog(EV_UI, "LVGL stalled %d ms state=%c", stalled_ms,
              task_state_char(s_lvgl_task));
    }
    ESP_LOGW(TAG, "LVGL task stalled %d ms, state=%c, internal free %u, "
             "largest %u",
             stalled_ms,
             task_state_char(s_lvgl_task),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    if (first && !s_stats_job_pending) {
        s_stats_job_pending = true;
        if (!app_run_async(stats_job, NULL)) {
            s_stats_job_pending = false; /* worker busy: state char must do */
        }
    }
}

static void touch_trace_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_current_target(e);
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    ESP_LOGI(TAG, "touch %s at %d,%d",
             lv_event_get_code(e) == LV_EVENT_PRESSED ? "PRESSED" : "RELEASED",
             (int)p.x, (int)p.y);
}

void ui_watch_start(void)
{
    /* caller holds the LVGL lock */
    lv_timer_create(beat_cb, BEAT_MS, NULL);

    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (indev != NULL) {
        lv_indev_add_event_cb(indev, touch_trace_cb, LV_EVENT_PRESSED, NULL);
        lv_indev_add_event_cb(indev, touch_trace_cb, LV_EVENT_RELEASED, NULL);
    } else {
        ESP_LOGW(TAG, "no input device - touch trace off");
    }

    const esp_timer_create_args_t args = {
        .callback = check_cb,
        .name = "ui_watch",
    };
    esp_timer_handle_t timer;
    if (esp_timer_create(&args, &timer) != ESP_OK ||
        esp_timer_start_periodic(timer, (uint64_t)CHECK_MS * 1000) != ESP_OK) {
        ESP_LOGW(TAG, "stall detector not started");
        return;
    }
    ESP_LOGI(TAG, "LVGL stall detector armed (%d ms check)", CHECK_MS);
}
