/* SNTP time sync (hourly) + timezone handling. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

#include "app_common.h"
#include "settings.h"
#include "clock_sync.h"
#include "evlog.h"

static const char *TAG = "clock_sync";

/* Anything before this is "the RTC was never set" (power-on reset). */
#define RTC_PLAUSIBLE_EPOCH 1735689600 /* 2025-01-01T00:00:00Z */

static bool s_sntp_started;
static volatile bool s_time_valid;   /* wall clock usable (RTC or SNTP) */
static volatile bool s_synced;       /* at least one SNTP sync this boot */
/* newlib's tzset() mutates global TZ state; alarm/ui_clock read it via
 * localtime_r on other tasks, so both sides go through this mutex. */
static SemaphoreHandle_t s_tz_mtx;

static void apply_timezone(void);

void clock_sync_early_init(void)
{
    if (s_tz_mtx == NULL) {
        s_tz_mtx = xSemaphoreCreateMutex();
    }
    /* Settings are loaded by now: apply the zone before any task computes a
     * local time (the alarm scheduler starts before WiFi/SNTP). */
    apply_timezone();
    /* The RTC keeps wall time across every reset except power-on
     * (CONFIG_ESP_TIME_FUNCS_USE_RTC_TIMER). After a crash reboot with the
     * network down the alarm must still fire and the clock must still show
     * the time, so trust a plausible RTC value until SNTP corrects it. */
    time_t now = time(NULL);
    if (now > RTC_PLAUSIBLE_EPOCH) {
        s_time_valid = true;
        ESP_LOGI(TAG, "time restored from RTC (unsynced): %lld",
                 (long long)now);
        evlog(EV_TIME, "time kept by RTC (unsynced)");
    } else {
        evlog(EV_TIME, "no valid time - waiting for SNTP");
    }
}

void clock_sync_localtime(const time_t *t, struct tm *out)
{
    if (s_tz_mtx != NULL) {
        xSemaphoreTake(s_tz_mtx, portMAX_DELAY);
    }
    localtime_r(t, out);
    if (s_tz_mtx != NULL) {
        xSemaphoreGive(s_tz_mtx);
    }
}

time_t clock_sync_mktime(struct tm *tm)
{
    if (s_tz_mtx != NULL) {
        xSemaphoreTake(s_tz_mtx, portMAX_DELAY);
    }
    time_t t = mktime(tm);
    if (s_tz_mtx != NULL) {
        xSemaphoreGive(s_tz_mtx);
    }
    return t;
}

static void time_synced_job(void *arg)
{
    ui_set_status("Time synced");
}

static void on_time_sync(struct timeval *tv)
{
    /* Runs on the SNTP/tcpip task: never take the LVGL lock here, the UI
     * update goes through the worker. */
    if (!s_synced) {
        evlog(EV_TIME, "SNTP synced"); /* first sync only: hourly is noise */
    }
    s_time_valid = true;
    s_synced = true;
    ESP_LOGI(TAG, "time synced");
    app_run_async(time_synced_job, NULL);
}

static void apply_timezone(void)
{
    /* POSIX TZ offsets are west-positive, hence the sign flip:
     * UTC+10 -> "UTC-10". */
    int mins = settings_get()->tz_offset_min;
    char tz[24];
    int sign_flipped = -mins;
    snprintf(tz, sizeof(tz), "UTC%+03d:%02d", sign_flipped / 60,
             abs(sign_flipped) % 60);
    if (s_tz_mtx != NULL) {
        xSemaphoreTake(s_tz_mtx, portMAX_DELAY);
    }
    setenv("TZ", tz, 1);
    tzset();
    if (s_tz_mtx != NULL) {
        xSemaphoreGive(s_tz_mtx);
    }
    ESP_LOGI(TAG, "TZ set to %s", tz);
}

void clock_sync_start(void)
{
    apply_timezone();

    if (s_sntp_started) {
        return;
    }

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.start = true;
    cfg.sync_cb = on_time_sync;
    /* Default IMMED mode: every sync HARD-STEPS the clock. The alarm task
     * copes via its catch-up window (see alarm.c) - do not rely on the
     * wall clock passing through every minute. */
    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sntp init failed: %s", esp_err_to_name(err));
        return;
    }
    /* hourly resync */
    sntp_set_sync_interval(60 * 60 * 1000);
    s_sntp_started = true;
}

bool clock_sync_time_valid(void)
{
    return s_time_valid;
}

bool clock_sync_is_synced(void)
{
    return s_synced;
}
