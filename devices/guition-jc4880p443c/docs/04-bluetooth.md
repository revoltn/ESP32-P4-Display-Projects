# Bluetooth on the JC4880P443C — Verified Working Solution

**Status: BLE scanning verified on hardware 2026-08-30** (active NimBLE scan found 10
devices). No C6 reflash was needed — the factory C6 firmware supports BLE.

## The architecture

The ESP32-P4 has **no radio**. Bluetooth = NimBLE host stack running on the P4, with HCI
tunneled by esp_hosted ("vHCI") over the same SDIO link used for WiFi, to the BLE controller
on the ESP32-C6. The factory C6 firmware (esp_hosted slave **2.12.12**) advertises:

```
capabilities: 0xd
  * WLAN
    - HCI over SDIO
    - BLE only
```

"**BLE only**" is a hard fact from the capability handshake: **Bluetooth Classic is not
available** on this board, period.

## The three stacked problems (each masked the next)

Symptom in all three cases was identical — endless:
```
E NimBLE: HCI wait for ack returned 19
I NimBLE: ogf=0x03, ocf=0x0003 : BLE_HS_ETIMEOUT_HCI (HCI request timed out; controller unresponsive)
```

### 1. Host/slave esp_hosted version mismatch
Host component was `2.11.*` (as in the vendor demo manifests); the C6 actually runs
**2.12.12**. The boot log warned:
`Version mismatch: Host [2.11.0] < Co-proc [2.12.0] ==> Upgrade host`.
**Fix**: pin `espressif/esp_hosted: "~2.12.0"` in `main/idf_component.yml`.
(WiFi worked despite the mismatch; BT did not.)

### 2. NimBLE started before the SDIO transport was up
`nimble_port_init()` triggers the hosted transport lazily and immediately sends HCI
commands — ~1.7 s before the link goes active, so its HCI binding starts against a dead
link. esp_hosted's own example (`examples/host_nimble_bleprph_host_only_vhci`) connects
first. **Fix**: call `esp_hosted_connect_to_slave()` (blocks until transport ready) before
`nimble_port_init()`.

### 3. The C6's BT controller is DISABLED by default ← the actual killer
Documented in the esp_hosted component, `docs/bluetooth_design.md` §3.1: **since
ESP-Hosted-MCU v2.5.2 the co-processor's Bluetooth controller starts disabled** (so the
host can set the BT MAC first). The host must enable it explicitly. Older examples (and the
community sample now at
`devices/guition-jc4880p443c/community/bigbag-JC4880P443C-examples/archived-examples/examples/07_bluetooth`)
predate this change and
never call it — they would hit the same timeout on current C6 firmware.

## The working recipe

sdkconfig (see [src/sample_hello_world/sdkconfig.defaults](../../../src/sample_hello_world/sdkconfig.defaults)) —
identical to esp_hosted's official vHCI example:

```
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_CONTROLLER_DISABLED=y        # P4 has no local controller
CONFIG_BT_NIMBLE_TRANSPORT_UART=n      # hosted vHCI provides the transport
CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE=y
CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI=y
```

Runtime order (see [src/sample_hello_world/main/feature_bt.c](../../../src/sample_hello_world/main/feature_bt.c)):

```c
esp_hosted_connect_to_slave();          // 1. bring SDIO transport up (blocking)
esp_hosted_get_coprocessor_fwversion(&v); // (optional sanity: prints 2.12.12)
esp_hosted_bt_controller_init();        // 2. REQUIRED since esp_hosted 2.5.2
esp_hosted_bt_controller_enable();      // 3. REQUIRED — else HCI times out forever
vTaskDelay(pdMS_TO_TICKS(500));         //    small settle
ble_hs_cfg.sync_cb = on_sync;
nimble_port_init();                     // 4. now the host stack syncs immediately
nimble_port_freertos_init(host_task);
// wait for sync_cb, then ble_gap_disc(...) — active scan gets more device names
```

First press of "BLE Scan" takes ~7 s (C6 reset+connect ~2 s, controller enable, 500 ms
settle) before the 8 s scan starts; subsequent scans are immediate.

## NimBLE vs Bluedroid on this board

Both work over hosted vHCI. NimBLE chosen: smaller footprint, Espressif's recommendation
for BLE-only — and the controller *is* BLE-only, so Bluedroid's Classic support buys
nothing. The Bluedroid route (as in the community sample) needs
`CONFIG_BT_BLUEDROID_ENABLED` + `CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID` and the
`esp_hosted_bluedroid.h` attach APIs instead. **Assumption: the Bluedroid path also works
once the controller-enable call is added — not validated here.**

## Verified / not verified

- ✅ NimBLE host sync over vHCI, active BLE scan, advertisement + scan-response name parsing.
- ❓ **Assumptions, not validated**: BLE advertising/peripheral role, GATT client/server,
  connections, WiFi+BLE coexistence under load (scan ran with WiFi idle).
- ❌ Bluetooth Classic: confirmed unavailable (C6 capability = "BLE only").

## C6 firmware lifecycle (for when the version dance matters again)

The C6's esp_hosted slave firmware has an official distribution channel:
**https://esphome.github.io/esp-hosted-firmware/** — the EspControl project pins exactly the
version our board runs (2.12.12) by sha256 from there. If the C6 ever needs updating or
recovery, there is a **wire-free path**: embed the slave binary in the P4 image and flash it
over the SDIO link with the `esp_hosted_slave_ota_*` API before WiFi starts (working
implementation to crib: `reference/espcontrol/upstream/components/c6_recovery/`, local copy,
PolyForm Noncommercial, see [reference/espcontrol/README.md](../../../reference/espcontrol/README.md)).
Untested by us —
see [07-espcontrol-findings.md](07-espcontrol-findings.md). Note: EspControl itself never
uses Bluetooth, so our NimBLE recipe above has no counterpart there.

**ESPHome core, however, implements the same recipe** (Bluedroid flavor) in
`reference/esphome/upstream/esphome/components/esp32_ble/ble.cpp` (GPLv3 C++, local copy, see
[reference/esphome/README.md](../../../reference/esphome/README.md)) — same connect → fwversion →
controller init/enable → attach-vHCI order, wrapped in a 60 s WDT raise, with BLE
tracker/client/server/proxy all P4-tested. Independent confirmation of the sequence, and
proof that BLE roles beyond scanning work on this hardware path — see
[08-esphome-findings.md](08-esphome-findings.md).
