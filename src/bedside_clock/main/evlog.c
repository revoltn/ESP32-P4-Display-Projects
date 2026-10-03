/* evlog.c - persistent event journal (see evlog.h). */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "nvs.h"

#include "app_common.h"
#include "clock_sync.h"
#include "evlog.h"

static const char *TAG = "evlog";

#define EVLOG_MAGIC     0x45564C31u /* "EVL1" */
#define NVS_NS          "bedside"
#define NVS_KEY         "evlog"
#define FLUSH_DELAY_US  (2 * 1000000ULL)
#define RETRY_DELAY_US  (30 * 1000000ULL)
/* same bar as clock_sync's RTC plausibility check */
#define TIME_PLAUSIBLE  1735689600 /* 2025-01-01T00:00:00Z */

typedef struct {
    uint32_t magic;
    uint16_t head;              /* next slot to write */
    uint16_t count;             /* valid entries, <= EVLOG_CAP */
    evlog_entry_t e[EVLOG_CAP];
    uint32_t crc;               /* over everything above */
} ring_t;

/* .noinit: internal DRAM the startup code does not zero. ~2.3 KB. */
static __NOINIT_ATTR ring_t s_ring;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static esp_timer_handle_t s_flush_timer;
static volatile bool s_flush_pending; /* a worker job is queued/running */
static bool s_dirty;                  /* ring newer than the NVS copy */

static const char *const s_code_str[] = {
    [EV_BOOT] = "boot", [EV_TIME] = "time", [EV_WIFI] = "wifi",
    [EV_ALARM] = "alarm", [EV_UI] = "ui", [EV_ERR] = "err",
};

const char *evlog_code_str(uint8_t code)
{
    if (code < sizeof(s_code_str) / sizeof(s_code_str[0]) &&
        s_code_str[code] != NULL) {
        return s_code_str[code];
    }
    return "?";
}

static uint32_t ring_crc(const ring_t *r)
{
    return esp_rom_crc32_le(0, (const uint8_t *)r, offsetof(ring_t, crc));
}

static bool ring_valid(const ring_t *r)
{
    return r->magic == EVLOG_MAGIC && r->head < EVLOG_CAP &&
           r->count <= EVLOG_CAP && r->crc == ring_crc(r);
}

static void ring_reset(void)
{
    memset(&s_ring, 0, sizeof(s_ring));
    s_ring.magic = EVLOG_MAGIC;
    s_ring.crc = ring_crc(&s_ring);
}

/* ---------- NVS mirror (worker task only) ---------- */

static void flush_timer_cb(void *arg);

static void arm_flush(uint64_t delay_us)
{
    if (s_flush_timer == NULL) {
        return;
    }
    /* already armed: keep the earlier deadline (debounce, not postpone) */
    esp_timer_start_once(s_flush_timer, delay_us);
}

static void flush_job(void *arg)
{
    ring_t *snap = heap_caps_malloc(sizeof(ring_t), MALLOC_CAP_SPIRAM);
    if (snap == NULL) {
        snap = malloc(sizeof(ring_t));
    }
    bool ok = false;
    if (snap != NULL) {
        taskENTER_CRITICAL(&s_mux);
        memcpy(snap, &s_ring, sizeof(ring_t));
        s_dirty = false;
        taskEXIT_CRITICAL(&s_mux);

        nvs_handle_t nvs;
        if (nvs_open(NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
            ok = nvs_set_blob(nvs, NVS_KEY, snap, sizeof(ring_t)) == ESP_OK &&
                 nvs_commit(nvs) == ESP_OK;
            nvs_close(nvs);
        }
        free(snap);
    }
    s_flush_pending = false;
    if (!ok) {
        ESP_LOGW(TAG, "NVS mirror write failed - retrying");
        taskENTER_CRITICAL(&s_mux);
        s_dirty = true;
        taskEXIT_CRITICAL(&s_mux);
        arm_flush(RETRY_DELAY_US);
        return;
    }
    /* an append landed while we wrote: go again after the usual delay */
    bool again;
    taskENTER_CRITICAL(&s_mux);
    again = s_dirty;
    taskEXIT_CRITICAL(&s_mux);
    if (again) {
        arm_flush(FLUSH_DELAY_US);
    }
}

/* esp_timer task: hand the flash write to the worker (internal stack, and
 * never the caller's task) */
static void flush_timer_cb(void *arg)
{
    if (s_flush_pending) {
        return; /* the running job re-arms us if the ring changed under it */
    }
    s_flush_pending = true;
    if (!app_run_async(flush_job, NULL)) {
        s_flush_pending = false;
        arm_flush(RETRY_DELAY_US); /* queue full (or no worker yet) */
    }
}

/* ---------- append ---------- */

void evlog(uint8_t code, const char *fmt, ...)
{
    evlog_entry_t ent;
    memset(&ent, 0, sizeof(ent));
    time_t now = time(NULL);
    ent.epoch = (now > TIME_PLAUSIBLE) ? (int64_t)now : 0;
    ent.uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    ent.code = code;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ent.text, sizeof(ent.text), fmt, ap);
    va_end(ap);

    ESP_LOGI(TAG, "[%s] %s", evlog_code_str(code), ent.text);

    taskENTER_CRITICAL(&s_mux);
    if (!ring_valid(&s_ring)) {
        ring_reset(); /* called before init, or the ring got clobbered */
    }
    s_ring.e[s_ring.head] = ent;
    s_ring.head = (uint16_t)((s_ring.head + 1) % EVLOG_CAP);
    if (s_ring.count < EVLOG_CAP) {
        s_ring.count++;
    }
    s_ring.crc = ring_crc(&s_ring);
    s_dirty = true;
    taskEXIT_CRITICAL(&s_mux);

    arm_flush(FLUSH_DELAY_US);
}

/* ---------- read ---------- */

int evlog_count(void)
{
    int n;
    taskENTER_CRITICAL(&s_mux);
    n = ring_valid(&s_ring) ? s_ring.count : 0;
    taskEXIT_CRITICAL(&s_mux);
    return n;
}

bool evlog_get(int idx, evlog_entry_t *out)
{
    bool ok = false;
    taskENTER_CRITICAL(&s_mux);
    if (ring_valid(&s_ring) && idx >= 0 && idx < s_ring.count) {
        /* oldest = head - count (mod CAP) */
        int slot = (s_ring.head + EVLOG_CAP - s_ring.count + idx) % EVLOG_CAP;
        *out = s_ring.e[slot];
        ok = true;
    }
    taskEXIT_CRITICAL(&s_mux);
    if (ok) {
        out->text[EVLOG_TEXT_LEN - 1] = '\0';
    }
    return ok;
}

bool evlog_format(int idx, char *buf, size_t cap)
{
    evlog_entry_t e;
    if (!evlog_get(idx, &e)) {
        return false;
    }
    char when[20];
    if (e.epoch != 0) {
        time_t t = (time_t)e.epoch;
        struct tm tm;
        clock_sync_localtime(&t, &tm);
        snprintf(when, sizeof(when), "%02d-%02d %02d:%02d:%02d",
                 tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    } else {
        snprintf(when, sizeof(when), "+%06lus", (unsigned long)e.uptime_s);
    }
    snprintf(buf, cap, "%s %s: %s", when, evlog_code_str(e.code), e.text);
    return true;
}

void evlog_echo_ui(int last_n)
{
    int n = evlog_count();
    int from = n - last_n;
    if (from < 0) {
        from = 0;
    }
    if (from >= n) {
        return;
    }
    /* ~80 chars per line */
    size_t cap = (size_t)(n - from) * 96 + 64;
    char *block = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (block == NULL) {
        return;
    }
    size_t len = (size_t)snprintf(block, cap, "Event journal (last %d of %d):\n",
                                  n - from, n);
    for (int i = from; i < n && len + 2 < cap; i++) {
        char line[96];
        if (evlog_format(i, line, sizeof(line))) {
            len += (size_t)snprintf(block + len, cap - len, "  %s\n", line);
        }
    }
    /* block ends with '\n' like the spec sheet does, so the next ui_log line
     * starts fresh (ui_log_bulk appends verbatim) */
    ui_log_bulk(block);
    heap_caps_free(block);
}

/* ---------- init ---------- */

void evlog_init(const char *reset_reason, uint32_t crash_count, bool coredump)
{
    bool kept = ring_valid(&s_ring);
    if (!kept) {
        /* power-on (or corrupted): fall back to the NVS mirror */
        bool loaded = false;
        nvs_handle_t nvs;
        if (nvs_open(NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
            size_t sz = sizeof(ring_t);
            ring_t *tmp = heap_caps_malloc(sizeof(ring_t), MALLOC_CAP_SPIRAM);
            if (tmp != NULL &&
                nvs_get_blob(nvs, NVS_KEY, tmp, &sz) == ESP_OK &&
                sz == sizeof(ring_t) && ring_valid(tmp)) {
                memcpy(&s_ring, tmp, sizeof(ring_t));
                loaded = true;
            }
            free(tmp);
            nvs_close(nvs);
        }
        if (!loaded) {
            ring_reset();
        }
        ESP_LOGI(TAG, "journal %s (%u entries)",
                 loaded ? "loaded from NVS" : "started empty", s_ring.count);
    } else {
        ESP_LOGI(TAG, "journal kept across reset (%u entries)", s_ring.count);
    }

    const esp_timer_create_args_t args = {
        .callback = flush_timer_cb,
        .name = "evlog_flush",
    };
    if (esp_timer_create(&args, &s_flush_timer) != ESP_OK) {
        s_flush_timer = NULL;
        ESP_LOGW(TAG, "flush timer create failed - journal is RAM-only");
    }

    evlog(EV_BOOT, "boot: %s%s%s", reset_reason != NULL ? reset_reason : "?",
          crash_count > 0 ? ", crash loop" : "",
          coredump ? ", core dump stored" : "");
    if (crash_count > 0) {
        evlog(EV_BOOT, "consecutive crashes: %lu", (unsigned long)crash_count);
    }
    /* a ring that survived a reset was never mirrored for its last entries */
    if (kept) {
        arm_flush(FLUSH_DELAY_US);
    }
}
