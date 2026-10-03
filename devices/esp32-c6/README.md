# ESP32-C6 as the hosted radio co-processor (both Guition P4 boards)

Scope: what the ESP32-C6 does on the 4.3" [JC4880P443C](../guition-jc4880p443c/README.md)
and the 10.1" [JC8012P4A1C](../guition-jc8012p4a1c/README.md), how its firmware couples to
the ESP32-P4 host, and how to update or recover it. Chip-level P4 facts are in
[../esp32-p4/README.md](../esp32-p4/README.md); board pins stay in the board directories.
Every claim cites a repo file or URL; *unverified* marks anything not seen on hardware.
`src/bedside_clock/components/espressif__esp_hosted/` is a local copy of the esp_hosted
2.12.12 host component (its `docs/` are the authoritative ESP-Hosted-MCU docs quoted below).

## 1. Role: ESP-Hosted-MCU slave over SDIO

- The P4 has no radio. The C6 runs Espressif's **ESP-Hosted-MCU slave** firmware (IDF project name `network_adapter`; the binary carries the string `ESP-Hosted-MCU Slave FW version :: %d.%d.%d`) and the P4 runs the host component (`devices/guition-jc8012p4a1c/vendor/INVENTORY.md` line 632; `devices/guition-jc4880p443c/community/seobaeksol-JC4880P443C_I_W/08_wireless_c6_hosted.md`).
- Transport on both boards: **SDIO, slot 1, 4-bit, streaming mode**, function 0/1 block size 512 ([10.1" factory log](../guition-jc8012p4a1c/logs/factory-boot-2026-10-02.txt) `sdio_wrapper: SDIO master: Slot 1, Data-Lines: 4-bit`, `SDIO Host operating in STREAMING MODE`; [01-hardware.md](../guition-jc4880p443c/docs/01-hardware.md)). The wiring on both boards equals the Espressif ESP32-P4-Function-EV-Board preset, so `CONFIG_ESP_HOSTED_P4_DEV_BOARD_FUNC_BOARD=y` derives the pins (`src/bedside_clock/sdkconfig.defaults`; the 10.1" log prints the same `CLK[18] CMD[19] D0[14] D1[15] D2[16] D3[17] Slave_Reset[54]`).
- What the host learns in the handshake, identical on both boards: `Identified slave [esp32c6]`, `capabilities: 0xd`, features `WLAN`, `HCI over SDIO`, `BLE only`, `ESP board type is : 13`, `Slave chip Id[12]` (10.1" log; [04-bluetooth.md](../guition-jc4880p443c/docs/04-bluetooth.md)).
- Host-side stack: `espressif/esp_hosted` (transport + RPC) and `espressif/esp_wifi_remote` (the `esp_wifi_*` API shim; Wi-Fi events arrive as `RPC_WRAP: ESP Event: ...`), plus NimBLE or Bluedroid over hosted vHCI for BLE ([03-working-components.md](../guition-jc4880p443c/docs/03-working-components.md)).
- Link bring-up takes ~1.7-1.8 s after the reset pulse (reset at 3.64 s, `Transport active` at 5.44 s in the 10.1" log; [01-hardware.md](../guition-jc4880p443c/docs/01-hardware.md)). `esp_hosted_connect_to_slave()` blocks until then; raise the task watchdog around it (ESPHome uses 60 s, [reliability-checklist.md section 2](../../docs/reliability-checklist.md)).
- Host memory discipline for P4 + big LVGL UI: `CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y` + `CONFIG_ESP_HOSTED_DFLT_TASK_FROM_SPIRAM=y` (or `USE_MEMPOOL=n` as EspControl does), else `HS_MP: mempool create failed: no mem` at boot; the PSRAM pool needs the 128 B alignment patch described in the P4 README ([reliability-checklist.md section 4](../../docs/reliability-checklist.md); `src/bedside_clock/sdkconfig.defaults`).
- Never `ESP_ERROR_CHECK` the Wi-Fi/BLE init: a dead or mismatched C6 must degrade to "wireless unavailable", not a boot loop ([reliability-checklist.md section 4](../../docs/reliability-checklist.md)).

## 2. Version coupling: host component vs slave firmware

The host compares **major.minor only** and prints the masked values, so the mismatch line
always ends in `.0` (`host/drivers/transport/transport_drv.c` lines 705-735,
`compare_fw_version`: `slave_version &= 0xFFFFFF00`). The two variants:

```
W transport: Version mismatch: Host [2.11.0] > Co-proc [2.3.0] ==> Upgrade co-proc to avoid RPC timeouts
W transport: Version mismatch: Host [2.11.0] < Co-proc [2.12.0] ==> Upgrade host to avoid compatibility issues
```

The first is the 10.1" factory image as shipped; the second was the 4.3" with the vendor demo
manifest (`esp_hosted 2.11.*`) against its factory slave, which `esp_hosted_get_coprocessor_fwversion()`
reports as **2.12.12** (the log line said 2.12.0 because of the mask)
([04-bluetooth.md](../guition-jc4880p443c/docs/04-bluetooth.md)). `CONFIG_ESP_HOSTED_FW_VERSION_MISMATCH_WARNING_SUPPRESS`
exists; do not use it.

Consequences seen or documented:

| Host vs slave | Wi-Fi | Bluetooth | Source |
|---|---|---|---|
| 2.11 host < 2.12.12 slave (4.3" as received) | worked with the warning | endless `E NimBLE: HCI wait for ack returned 19` / `BLE_HS_ETIMEOUT_HCI` until the host was pinned to `~2.12.0` | [04-bluetooth.md](../guition-jc4880p443c/docs/04-bluetooth.md) |
| 2.12 host > 2.3.0 slave (other units, community) | scan works, association loops forever (`ASSOC_LEAVE`, `H_SDIO_DRV: Dropping packet(s) from stream`, `Failed to push data to rx queue`) | not reported | [05-troubleshooting.md section 7](../guition-jc4880p443c/docs/05-troubleshooting.md) |
| 2.11.0 host > 2.3.0 slave (10.1" factory image) | `wifi station started` events arrive, then `rpc_rsp: Hosted RPC_Resp [0x21a], uid [4], resp code [12298]` | host built without BT (`hci_stub_drv: Host BT Support: Disabled`) | 10.1" log; whether the RPC error is a mismatch effect is *unverified* |

Rules that follow:

- Pin the host to the slave's major.minor (`espressif/esp_hosted: "~2.12.0"` for a 2.12.x slave); ESPHome pins `2.12.12` exactly (`reference/esphome/upstream/esphome/components/esp32_hosted/__init__.py:278`) and EspControl ships that same slave by sha256 ([07-espcontrol-findings.md](../guition-jc4880p443c/docs/07-espcontrol-findings.md)).
- Log `esp_hosted_get_coprocessor_fwversion()` at runtime and surface a mismatch to the user ([reliability-checklist.md section 4](../../docs/reliability-checklist.md)).
- **esp_hosted 2.12.12 is the last 2.x.** 3.x (3.0.6 as of Aug 2026) requires reflashing the slave to a matching 3.x image, re-applying the local 128 B alignment patch, and community reports SDIO boot loops on host/slave mismatches; deferred as its own project ([03-working-components.md](../guition-jc4880p443c/docs/03-working-components.md)).
- ESPHome refuses IDF < 5.3 for `esp32_hosted` because of a heap double free in the SDIO RX path fixed in esp_hosted 2.11.0 ([08-esphome-findings.md](../guition-jc4880p443c/docs/08-esphome-findings.md)).

## 3. Slave firmware lifecycle

### 3.1 Versions and images seen

| Where | Slave version | How known |
|---|---|---|
| 4.3" JC4880P443C, as received | **2.12.12** | `esp_hosted_get_coprocessor_fwversion()` and the handshake ([04-bluetooth.md](../guition-jc4880p443c/docs/04-bluetooth.md)) |
| 10.1" JC8012P4A1C, as received | **2.3.x** (log shows the masked `2.3.0`) | [10.1" factory log](../guition-jc8012p4a1c/logs/factory-boot-2026-10-02.txt) |
| Vendor burn files, both packages | `JC-C6-slave_v2.3.2.bin`: 1,196,480 B, identical sha256 `f9ec1205...` in both zips, `network_adapter` app `f0a63f7-dirty`, IDF v5.5, built 2025-08-26, 4 MB / 80 MHz / DIO | `devices/guition-jc8012p4a1c/vendor/INVENTORY.md` line 632; seobaeksol `07_flash_and_factory_images.md` |
| Vendor burn files, 4.3" package only | `C6-JC4880P443-2.1.10.bin`: `network_adapter` app `1`, IDF v5.4.2, 1.4 MB | seobaeksol `07_flash_and_factory_images.md` |

- The vendor images live in the vendor zips under `8-Burn operation/Burn files/`; this repo does not redistribute them.
- The hosted version number inside a `.bin` is a compiled constant and cannot be read with `strings`; the filename is the only hint, the running handshake the only proof (INVENTORY.md line 632).
- Guition batches differ: the same SKU has shipped with 2.3.x and 2.12.12 slaves, so read the handshake before pinning anything ([05 section 7](../guition-jc4880p443c/docs/05-troubleshooting.md)).
- Slave flash layout (from the vendor image, matches the OTA recipe): `nvs 0x9000/0x4000`, `otadata 0xd000/0x2000`, `phy_init 0xf000/0x1000`, `ota_0 0x10000/0x180000`, `ota_1 0x190000/0x180000`; bootloader at 0x0 (INVENTORY.md line 632; [05 section 7](../guition-jc4880p443c/docs/05-troubleshooting.md)).

### 3.2 Where slave firmware comes from

1. **Build it**: the slave project ships in https://github.com/espressif/esp-hosted-mcu; build the same major.minor as the host component. Not done in this repo (*unverified*).
2. **Prebuilt**: https://github.com/esphome/esp-hosted-firmware publishes per-version images and a manifest at https://esphome.github.io/esp-hosted-firmware/manifest/esp32c6.json; image URL pattern `https://esphome.github.io/esp-hosted-firmware/v2.12.9/network_adapter_esp32c6.bin` ([05 section 7](../guition-jc4880p443c/docs/05-troubleshooting.md)). EspControl pins 2.12.12 from there with sha256 `bad97ce81e7fcf5f3365898f633b80941ae863db9c754d87a78f89c8f61f2e94` (`reference/espcontrol/upstream/common/device/esp32_c6_recovery.yaml` lines 14-16).
3. **Vendor burn files** (3.1), the restore path a Guition unit was born with.

### 3.3 Flashing paths

**A. OTA over the SDIO link (no wires).** Host API `esp_hosted_slave_ota_begin()` /
`_write()` / `_end()` / `_activate()` (`host/api/include/esp_hosted_ota.h` lines 56-79;
`esp_hosted_slave_ota(url)` is deprecated); worked example `examples/host_performs_slave_ota/`
in the component (SDIO/SPI/UART transports, version check). Production implementations:
EspControl `components/c6_recovery/` embeds a sha256-verified image in the P4 app and flashes it
before Wi-Fi starts; ESPHome `esp32_hosted` has an `update:` platform (`type: embedded` or
`type: http` against the manifest above) ([07-espcontrol-findings.md](../guition-jc4880p443c/docs/07-espcontrol-findings.md),
[08-esphome-findings.md](../guition-jc4880p443c/docs/08-esphome-findings.md)). Untested in this
repo. One community report of the ESPHome-mirrored image failing the slave's `ota_end` with
`OTA_VALIDATE_FAILED`; fall back to B ([05 section 7](../guition-jc4880p443c/docs/05-troubleshooting.md)).

**B. Direct UART with esptool (last resort).** The C6's UART0, `IO9` (boot) and `CHIP_PU`
(reset) are on the 4.3" expansion header (`C6_U0RXD`/`C6_U0TXD`/`C6_IO9`/`C6_CHIP_PU`,
[01-hardware.md](../guition-jc4880p443c/docs/01-hardware.md)); on the 10.1" the vendor schematic
sheet `5_ESP32-C6.png` exists but the header mapping is *unverified*. Recipe (community-verified,
not run by us): 3.3 V USB-TTL, TX to `C6_U0RXD`, RX to `C6_U0TXD`, GND, **no 3V3** (the board
powers the C6); pull `IO9` low and pulse `CHIP_PU` for download mode. **Park the P4 in download
mode first**: a running host keeps resetting the C6 over GPIO54 on its Wi-Fi retry and kills the
write (`The chip stopped responding`). Then `write_flash 0x10000 network_adapter_esp32c6.bin`,
`erase_region 0xd000 0x2000` so otadata points at `ota_0`, hard-reset both chips
([05 section 7](../guition-jc4880p443c/docs/05-troubleshooting.md), full command lines there).
The slave image lives in the C6's own flash and survives any P4 reflash.

**C. Vendor factory restore.** The P4 merged image goes to offset 0x0 with the Flash Download
Tool over the USB port; the C6 image is a separate file in the same folder. Which port and
procedure the vendor uses for the C6 is *unverified* (seobaeksol `07_flash_and_factory_images.md`).

Keep P4 and C6 versioning separate; a P4 app update must not silently overwrite the C6
(seobaeksol `08_wireless_c6_hosted.md`).

## 4. BLE over the C6 (summary; full write-up in [04-bluetooth.md](../guition-jc4880p443c/docs/04-bluetooth.md))

- Architecture: NimBLE (or Bluedroid) host on the P4, HCI tunnelled by esp_hosted ("vHCI") over the same SDIO link, BLE controller on the C6. `CONFIG_BT_CONTROLLER_DISABLED=y` because the P4 has no controller.
- sdkconfig: `CONFIG_BT_ENABLED=y`, `CONFIG_BT_NIMBLE_ENABLED=y`, `CONFIG_BT_CONTROLLER_DISABLED=y`, `CONFIG_BT_NIMBLE_TRANSPORT_UART=n`, `CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE=y`, `CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI=y`; put NimBLE in PSRAM with `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y` (`src/sample_hello_world/sdkconfig.defaults`).
- Runtime order, mandatory: `esp_hosted_connect_to_slave()` (blocks until the transport is up) -> `esp_hosted_get_coprocessor_fwversion()` (log it) -> `esp_hosted_bt_controller_init()` -> `esp_hosted_bt_controller_enable()` -> short settle -> `nimble_port_init()`. **Since ESP-Hosted-MCU v2.5.2 the co-processor's BT controller starts disabled** so the host can set the BT MAC first (`docs/bluetooth_design.md` section 3.1); older samples that skip the enable call time out forever on current slaves. The MAC set this way reverts on reset.
- Three stacked causes produced one identical symptom (`BLE_HS_ETIMEOUT_HCI`): version mismatch, NimBLE started before the link was up, controller never enabled. Bound the sync wait (~5 s) and degrade with a message naming those causes ([reliability-checklist.md section 5](../../docs/reliability-checklist.md)).
- Verified here: NimBLE sync, active scan, name parsing (first scan ~7 s because of the C6 reset + enable). Advertising, GATT, connections, Wi-Fi + BLE coexistence under load: *unverified* here; ESPHome's `ble.cpp` runs the same sequence (Bluedroid flavour) with tracker/client/server/proxy on P4 ([08-esphome-findings.md](../guition-jc4880p443c/docs/08-esphome-findings.md)).
- A host log line `hci_stub_drv: Host BT Support: Disabled` means the P4 build has no BT (`CONFIG_BT_ENABLED=n`), not that the slave lacks it (10.1" log; `src/bedside_clock/sdkconfig.defaults`).

## 5. Wi-Fi notes

- The C6 is a **2.4 GHz-only** Wi-Fi 6 (802.11ax, 20 MHz) radio, b/g/n compatible; no 5 GHz band exists in the silicon (Espressif C6 datasheet, section 10; [device-specifications-log.md](../guition-jc4880p443c/docs/device-specifications-log.md)). A "_5g"-named SSID can only work if the AP also broadcasts it on 2.4 GHz; never exercised ([01-hardware.md](../guition-jc4880p443c/docs/01-hardware.md), listed as an assumption).
- Verified: STA connect to WPA2 and DHCP via `esp_wifi_remote`; untested: AP mode, throughput ceilings, coexistence with BLE under load ([06-status-and-assumptions.md](../guition-jc4880p443c/docs/06-status-and-assumptions.md)).
- Observed data rate: 2-3 MB/s raw HTTPS streaming at both 20 and 40 MHz SDIO; with software AES (the P4 AES-DMA workaround) TLS tops out near 0.6 MB/s ([11-tls-stream-corruption-investigation.md](../guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md) section 7).
- Buffers in use: `CONFIG_WIFI_RMT_STATIC_RX_BUFFER_NUM=10`, `CONFIG_WIFI_RMT_RX_BA_WIN=6` (vendor demo values), lwIP `TCP_WND 32768` / `SND_BUF 16384` from PSRAM, `CONFIG_LWIP_L2_TO_L3_COPY=y` to release hosted RX buffers immediately (+35 KB internal RAM during playback) (`src/bedside_clock/sdkconfig.defaults`). ESP-Hosted's "high performance" P4+C6 block (`docs/sdio.md` section 7.3: RX buffers 16/64/64, BA windows 32, 64 KB TCP windows, SACK) has not been tried here (*unverified*).
- Connection discipline that prevents reconnect storms: bounded retries per attempt, ~90 s debounce before opening a fallback AP, stage heavy fetches 1 s+ after connect ([reliability-checklist.md section 6](../../docs/reliability-checklist.md)).
- The host resets the C6 on Wi-Fi init retry; this is what breaks a concurrent UART flash (section 3.3 B).
- Optional SDIO payload checksum `CONFIG_ESP_HOSTED_SDIO_CHECKSUM` is off by default; lwIP's TCP checksum is always verified, which is how the TLS corruption was localised to the P4 side ([11 section 4](../guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md)).

## 6. What the C6 silicon has that the hosted setup does not give you

| Capability | C6 silicon | Over esp_hosted on these boards | Source |
|---|---|---|---|
| Bluetooth Classic (BR/EDR, A2DP, SPP) | **absent**: the C6 is Bluetooth 5 LE only | never; the capability flag says `BLE only` | C6 datasheet; [04-bluetooth.md](../guition-jc4880p443c/docs/04-bluetooth.md) |
| 802.15.4 Thread / Zigbee | present | not with the factory `network_adapter` slave. ESP-Hosted runs the OpenThread/Zigbee host on the P4 and an **RCP build** on the C6, but "a dedicated UART channel is required" and "OpenThread over the current ESP-Hosted Transport [is] to be supported in the future"; Zigbee is UART-only | `docs/openthread_zigbee.md` section 1 (component copy); [device-specifications-log.md](../guition-jc4880p443c/docs/device-specifications-log.md) |
| 5 GHz Wi-Fi | absent | n/a | C6 datasheet |
| C6 GPIOs | present | only through hosted's GPIO-expander RPC (`docs/gpio_expander.md`); *unverified* here | component docs |
| iTWT, host power save, network split | slave features | listed in `docs/features.md`, `feature_host_power_save.md`, `feature_network_split.md`; none exercised here (*unverified*) | component docs |

Reflashing the slave to an RCP image to get Thread would also remove Wi-Fi/BLE from the same
chip unless the image is a border-router build, and needs the C6 UART wired to the P4; the 4.3"
header exposes the C6 UART0 for recovery but whether it is routed to a P4 UART is *unverified*.

## 7. Power and reset

- The host resets the C6 through **GPIO54, active high**, on every boot (`CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE=54`, `CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH=y`; 10.1" log `Reset slave using GPIO[54]`; EspControl `devices/guition-esp32-p4-jc8012p4a1/device/device.yaml` lines 194-203 `reset_pin: GPIO54`, `active_high: true`; same in the p1ngb4ck ESPHome example for the 10.1"). Identical to the Espressif EV-board preset, which is why both boards use `CONFIG_ESP_HOSTED_P4_DEV_BOARD_FUNC_BOARD`.
- Reset on every host boot is deliberate (`SLAVE_RESET_ON_EVERY_HOST_BOOTUP`): a host-only reset otherwise leaves stale slave state and undefined transport behaviour ([reliability-checklist.md section 4](../../docs/reliability-checklist.md)).
- The C6 is powered from the board's 3V3 rail; an external UART adapter must not feed it 3V3 ([05 section 7](../guition-jc4880p443c/docs/05-troubleshooting.md)). `C6_CHIP_PU` on the 4.3" header allows a manual reset.
- Shutting the slave down for host power save is a documented hosted feature (`examples/host_shuts_down_slave_to_power_save`); *unverified* here.

## 8. SDIO clock choice

- Component default is **40 MHz** (`Kconfig` line 776-778, `ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ` default 40000); the docs say start at 400 kHz-20 MHz for bring-up and that all SDIO co-processors work up to 50 MHz (`docs/sdio.md` lines 84, 409-414).
- 40 MHz works on both boards: the 4.3" ran it for the whole bring-up and the 10.1" factory image prints `Freq(KHz)[40000 KHz]`; EspControl ships the 10.1" at `sdio_frequency: 40MHz` but the 4.3" at **20 MHz** "for reliable ESP32-C6 communication across production panels" ([07-espcontrol-findings.md](../guition-jc4880p443c/docs/07-espcontrol-findings.md)).
- The clock app now ships 20 MHz on the 4.3": streaming throughput was unchanged (2-3 MB/s at both) and the lower clock cut the P4 AES-DMA TLS error rate ~10x through less bus contention, though it was not the fix ([11 section 5](../guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md); `src/bedside_clock/sdkconfig.defaults`).
- Rule: treat 40 MHz as provisional; on any link flakiness drop to 20 MHz first ([reliability-checklist.md section 4](../../docs/reliability-checklist.md)).
- Slot 1 is used on the P4 for its flexible GPIO matrix routing (`docs/sdio.md` lines 142-144). Streaming mode buys throughput at the cost of heap; packet mode is the low-memory alternative (`docs/sdio.md` section 9).
- `CONFIG_FREERTOS_HZ` below hosted's expectation triggers a boot warning about bus jitter (`port_esp_hosted_host_os.c`, `hosted_init_hook`); both apps use 1000 Hz.

## 9. When the C6 is bricked or misbehaving

1. **Read the handshake block first** (`transport: capabilities ...`): it tells you the slave chip, the (masked) version, and whether HCI-over-SDIO is compiled in, before touching the host ([05 diagnostic techniques](../guition-jc4880p443c/docs/05-troubleshooting.md)). `strings` on a vendor slave `.bin` shows the same feature set offline.
2. No `Received INIT event from ESP32 peripheral` and `Attempt connection with slave: retry[n]` climbing means the slave is not talking: check the reset pin polarity and SDIO clock, then suspect the slave image.
3. `Version mismatch` with Wi-Fi association loops or BLE HCI timeouts: fix the version pairing (section 2) before anything else.
4. `Unrecoverable host sdio state` reboots under sustained traffic with a PSRAM mempool: the 128 B alignment patch (P4 README section 7).
5. Recovery order: OTA over SDIO if the link comes up at all (3.3 A), else UART (3.3 B) with the P4 parked in download mode, else the vendor restore (3.3 C). The P4 image is never the problem; the slave has its own flash.

## 10. Links (URLs only)

- ESP-Hosted-MCU (slave + host source, docs): https://github.com/espressif/esp-hosted-mcu
- Host component on the registry: https://components.espressif.com/components/espressif/esp_hosted
- Wi-Fi API shim: https://components.espressif.com/components/espressif/esp_wifi_remote
- Prebuilt slave images and manifest: https://github.com/esphome/esp-hosted-firmware and https://esphome.github.io/esp-hosted-firmware/manifest/esp32c6.json
- ESPHome `esp32_hosted` component (update platform, `use_psram`): https://esphome.io/components/esp32_hosted.html
- ESP32-C6 datasheet (radio capabilities): https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf

## 11. Verified on

- 4.3" JC4880P443C: slave 2.12.12 with host 2.12.12 at 40 MHz and 20 MHz SDIO; Wi-Fi STA + DHCP, BLE active scan, HTTPS streaming for weeks (2026-08-30 onward). Slave OTA and UART reflash not performed.
- 10.1" JC8012P4A1C: factory firmware boot log only (slave 2.3.x, host 2.11.0, 40 MHz, Wi-Fi station start events). Nothing flashed, no BLE attempted, no slave update attempted.
- Not verified anywhere in this repo: esp_hosted 3.x, Thread/Zigbee RCP mode, host power save, any slave firmware build.
