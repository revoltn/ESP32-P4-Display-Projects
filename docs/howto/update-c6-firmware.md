# Update the ESP32-C6 firmware

The ESP32-P4 has no radio. WiFi and BLE run on the ESP32-C6, which executes Espressif's
esp_hosted "slave" firmware and talks to the P4 over SDIO. The host component compiled into
the P4 firmware (`espressif/esp_hosted`, pinned `~2.12.0` here) and the slave firmware are
**version coupled**: WiFi tolerates a mismatch with a warning, Bluetooth fails on every HCI
command, and an old slave (2.3.0) loops on association.

This page is a pointer. The authoritative documents are:

| Document | Covers |
|---|---|
| [devices/esp32-c6/README.md](../../devices/esp32-c6/README.md) | The C6 as a component: firmware lineage, which versions ship on which board, the official image mirror |
| [devices/guition-jc8012p4a1c/docs/c6-firmware.md](../../devices/guition-jc8012p4a1c/docs/c6-firmware.md) | The 10.1" board, which ships slave 2.3.0 and needs the update before the host used here is usable |
| [05-troubleshooting.md section 7](../../devices/guition-jc4880p443c/docs/05-troubleshooting.md) | The community-verified UART reflash recipe with exact commands and the "halt the P4 first" warning |
| [04-bluetooth.md](../../devices/guition-jc4880p443c/docs/04-bluetooth.md) | Why the version matters for BLE and the controller-enable sequence |

## How to tell what you have

Read the esp_hosted handshake in the boot log:

```
I transport: Identified slave [esp32c6]
W transport: Version mismatch: Host [2.12.0] > Co-proc [2.3.0] ==> Upgrade co-proc to avoid RPC timeouts
```

`Co-proc` is the slave. The 4.3" boards seen here shipped 2.12.12 (no update needed); the
10.1" shipped 2.3.0. `bedside_clock` also logs `C6 co-processor: esp_hosted X.Y.Z` at WiFi start.

## The two update paths

| Path | When | Summary |
|---|---|---|
| **OTA over SDIO** (no wires) | Preferred | The P4 firmware embeds or downloads a slave image and writes it through `esp_hosted_slave_ota_*` before WiFi starts. Working implementations exist in EspControl (`c6_recovery`) and ESPHome (`esp32_hosted/update`); official images are published at `https://esphome.github.io/esp-hosted-firmware/`. Not yet implemented in this repository's apps |
| **Direct UART** (last resort) | OTA validation fails, or the slave is bricked | 3.3 V USB-TTL adapter on the C6's UART0, BOOT and reset pins (JP1 expand header on the 4.3"), `esptool --chip esp32c6 write_flash 0x10000 network_adapter_esp32c6.bin`, then reset `otadata`. **Park the P4 in download mode first**: a running P4 resets the C6 over GPIO54 and kills the flash mid-write (`The chip stopped responding`) |

The update lives in the C6's own flash and survives any later reflash of the P4.

## After updating

- Keep the host pin (`espressif/esp_hosted: "~2.12.0"` in `main/idf_component.yml`) in step
  with the slave. esp_hosted 3.x is a separate project: it requires a matching 3.x slave and a
  re-port of the local `hosted_malloc_align` patch.
- Since esp_hosted 2.5.2 the slave's BT controller starts **disabled**; the host must call
  `esp_hosted_bt_controller_init()` and `_enable()` after `esp_hosted_connect_to_slave()` and
  before the host stack. Only `sample_hello_world` uses BLE; `bedside_clock` compiles it out.

## Worked example: the 10.1" board, 2026-10-03

`src/tool_c6_ota` flashes an embedded slave image over the existing SDIO link with no extra
hardware. It took the JC8012P4A1C from 2.3.2 to 2.12.12 in about 11 s; log and details in
`devices/guition-jc8012p4a1c/docs/c6-firmware.md` section 7.
