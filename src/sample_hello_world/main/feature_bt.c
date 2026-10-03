/* BLE device scan — best-effort on this board.
 *
 * The ESP32-P4 has no radio. Bluetooth requires NimBLE on the P4 with HCI
 * tunneled by esp_hosted over SDIO to the ESP32-C6, AND a C6 firmware built
 * with BT support. The factory C6 firmware may not provide it. The whole
 * implementation is compile-time gated so the rest of the app always ships. */
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "app_common.h"

#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED

#include <stdio.h>
#include <string.h>
#include "esp_hosted.h"
#include "esp_hosted_misc.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nimble/hci_common.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "ble_adv_fmt.h"

static const char *TAG = "ble";

#define SCAN_MS       8000
#define SYNC_WAIT_MS  5000
#define MAX_SEEN      48

#define BIT_SYNC     BIT0
#define BIT_DONE     BIT1

static EventGroupHandle_t s_ev;
static bool s_stack_started;

/* Info accumulated per device across advertisements + scan responses. */
typedef struct {
    ble_addr_t  addr;
    char        name[30];
    const char *mfg;         /* company name, NULL if unknown */
    uint16_t    cid;         /* SIG company id from mfg data */
    bool        has_mfg;
    bool        connectable;
    int8_t      rssi;        /* strongest seen */
} dev_rec_t;

static dev_rec_t s_devs[MAX_SEEN];
static int s_dev_count;

static void on_sync(void)
{
    xEventGroupSetBits(s_ev, BIT_SYNC);
}

static dev_rec_t *find_or_add(const ble_addr_t *addr, bool *is_new)
{
    *is_new = false;
    for (int i = 0; i < s_dev_count; i++) {
        if (memcmp(s_devs[i].addr.val, addr->val, 6) == 0) {
            return &s_devs[i];
        }
    }
    if (s_dev_count >= MAX_SEEN) {
        return NULL;
    }
    dev_rec_t *rec = &s_devs[s_dev_count++];
    memset(rec, 0, sizeof *rec);
    rec->addr = *addr;
    *is_new = true;
    return rec;
}

/* Runs on the NimBLE host task: just record, print nothing here. */
static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        bool is_new;
        dev_rec_t *rec = find_or_add(&event->disc.addr, &is_new);
        if (rec == NULL) {
            break;
        }
        if (is_new || event->disc.rssi > rec->rssi) {
            rec->rssi = event->disc.rssi;
        }
        if (event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
            event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) {
            rec->connectable = true;
        }
        struct ble_hs_adv_fields f;
        if (ble_hs_adv_parse_fields(&f, event->disc.data,
                                    event->disc.length_data) == 0) {
            if (rec->name[0] == '\0' && f.name != NULL && f.name_len > 0) {
                ble_adv_fmt_name(rec->name, sizeof rec->name,
                                 f.name, f.name_len);
            }
            if (!rec->has_mfg && f.mfg_data != NULL && f.mfg_data_len >= 2) {
                rec->has_mfg = true;
                rec->cid = f.mfg_data[0] | ((uint16_t)f.mfg_data[1] << 8);
                rec->mfg = ble_adv_fmt_company(rec->cid);
            }
        }
        break;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        xEventGroupSetBits(s_ev, BIT_DONE);
        break;
    default:
        break;
    }
    return 0;
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* Bring up esp_hosted + the C6 BT controller + NimBLE once, then wait for
 * HCI sync. Returns false (with the reason logged) when BLE is unusable. */
static bool bt_ensure_ready(void)
{
    if (!s_stack_started) {
        s_ev = xEventGroupCreate();

        /* The hosted transport MUST be up before NimBLE binds its HCI
         * driver (per the esp_hosted host_nimble_*_vhci example) —
         * otherwise the first HCI commands go into a dead link. */
        ui_log("Connecting to C6 co-processor...");
        int rc = esp_hosted_connect_to_slave();
        if (rc != 0) {
            ui_log("esp_hosted_connect_to_slave failed (%d)", rc);
            return false;
        }
        esp_hosted_coprocessor_fwver_t fwver;
        if (esp_hosted_get_coprocessor_fwversion(&fwver) == ESP_OK) {
            ui_log("C6 firmware: esp_hosted %lu.%lu.%lu",
                   (unsigned long)fwver.major1, (unsigned long)fwver.minor1,
                   (unsigned long)fwver.patch1);
        }
        /* Since esp_hosted 2.5.2 the co-processor's BT controller is
         * DISABLED by default — it must be switched on from the host
         * before the BT host stack starts (docs/bluetooth_design.md 3.1). */
        esp_err_t err = esp_hosted_bt_controller_init();
        if (err != ESP_OK) {
            ui_log("C6 BT controller init failed: %s", esp_err_to_name(err));
            return false;
        }
        err = esp_hosted_bt_controller_enable();
        if (err != ESP_OK) {
            ui_log("C6 BT controller enable failed: %s", esp_err_to_name(err));
            return false;
        }
        ui_log("C6 BT controller enabled");
        vTaskDelay(pdMS_TO_TICKS(500)); /* let vHCI settle */

        ble_hs_cfg.sync_cb = on_sync;
        ui_log("Starting NimBLE (HCI over esp_hosted SDIO to C6)...");
        rc = nimble_port_init();
        if (rc != 0) {
            ui_log("nimble_port_init failed (%d) - BLE unavailable", rc);
            return false;
        }
        nimble_port_freertos_init(host_task);
        s_stack_started = true;
    }

    EventBits_t bits = xEventGroupWaitBits(s_ev, BIT_SYNC, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(SYNC_WAIT_MS));
    if (!(bits & BIT_SYNC)) {
        ui_log("BLE unavailable: no HCI sync from the C6 co-processor.");
        ui_log("The factory C6 firmware likely lacks Bluetooth over esp_hosted.");
        ui_log("Fix: reflash the C6 with an esp_hosted 2.11 slave image built with BT.");
        ui_set_status("BLE: not supported by C6 firmware");
        return false;
    }
    return true;
}

/* Active scan into s_devs; returns false when the scan couldn't start. */
static bool scan_run(void)
{
    s_dev_count = 0;
    xEventGroupClearBits(s_ev, BIT_DONE);

    /* active scan: also collects scan responses, so more device names */
    struct ble_gap_disc_params params = { 0 };
    params.passive = 0;

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, SCAN_MS, &params,
                          gap_event_cb, NULL);
    if (rc != 0) {
        ui_log("ble_gap_disc failed (%d)", rc);
        ui_set_status("BLE: scan error");
        return false;
    }
    xEventGroupWaitBits(s_ev, BIT_DONE, pdTRUE, pdFALSE,
                        pdMS_TO_TICKS(SCAN_MS + 2000));
    return true;
}

/* One line per device, strongest signal first:
 *   Someone's iPhone (Apple) -45dBm connectable
 * MAC stands in for the name when the device doesn't advertise one. */
static void print_results(void)
{
    for (int i = 1; i < s_dev_count; i++) {
        dev_rec_t tmp = s_devs[i];
        int j = i - 1;
        while (j >= 0 && s_devs[j].rssi < tmp.rssi) {
            s_devs[j + 1] = s_devs[j];
            j--;
        }
        s_devs[j + 1] = tmp;
    }

    for (int i = 0; i < s_dev_count; i++) {
        const dev_rec_t *r = &s_devs[i];
        char mac[18];
        const char *name = r->name;
        if (name[0] == '\0') {
            snprintf(mac, sizeof mac, "%02x:%02x:%02x:%02x:%02x:%02x",
                     r->addr.val[5], r->addr.val[4], r->addr.val[3],
                     r->addr.val[2], r->addr.val[1], r->addr.val[0]);
            name = mac;
        }
        const char *avail = r->connectable ? "connectable" : "non-conn";
        if (r->mfg != NULL) {
            ui_log("%s (%s) %ddBm %s", name, r->mfg, r->rssi, avail);
        } else if (r->has_mfg) {
            ui_log("%s (mfg 0x%04x) %ddBm %s", name, (unsigned)r->cid,
                   r->rssi, avail);
        } else {
            ui_log("%s %ddBm %s", name, r->rssi, avail);
        }
    }
}

void feature_bt_run(void)
{
    if (!bt_ensure_ready()) {
        return;
    }
    ui_log("Scanning for BLE devices (%d s)...", SCAN_MS / 1000);
    ui_set_status("BLE: scanning...");
    if (!scan_run()) {
        return;
    }
    print_results();
    ui_log("BLE scan complete: %d device(s) found", s_dev_count);
    ui_set_status("BLE: %d device(s) found", s_dev_count);
}

#else /* !CONFIG_BT_ENABLED */

void feature_bt_run(void)
{
    ui_log("BLE is not enabled in this build.");
    ui_log("The ESP32-P4 has no radio; Bluetooth needs the C6 co-processor");
    ui_log("running BT-capable esp_hosted firmware. See feature_bt.c for the");
    ui_log("config to try: CONFIG_BT_ENABLED + CONFIG_BT_NIMBLE_ENABLED +");
    ui_log("esp_hosted BT-over-SDIO support.");
    ui_set_status("BLE: not enabled in this build");
}

#endif
