# ESP32-C6 co-processor firmware on the JC8012P4A1C

**Status 2026-10-03: option (b), slave OTA over SDIO, was executed on this unit and worked**
(section 7). Sections 1 to 6 below were written on 2026-10-02 before that and are kept as
written; options (a) and (c) remain untested. Every procedure below is assembled
from the vendor package, Espressif's esp_hosted 2.12.12 documentation (the copy under
`src/bedside_clock/components/espressif__esp_hosted/`), the schematic, EspControl and
the 4.3" notes in this repo, and is marked untested.

## 1. The problem

Factory boot log, esp_hosted handshake:

```
I transport: Identified slave [esp32c6]
I transport: capabilities: 0xd
I transport:   * WLAN
I transport:     - HCI over SDIO
I transport:     - BLE only
W transport: Version mismatch: Host [2.11.0] > Co-proc [2.3.0] ==> Upgrade co-proc to avoid RPC timeouts
I hci_stub_drv: Host BT Support: Disabled
```

The C6 runs esp_hosted slave **2.3.0**; the factory P4 app was built with host **2.11.0**;
this repo's bedside_clock pins host `espressif/esp_hosted: "~2.12.0"`
(`src/bedside_clock/main/idf_component.yml`), which resolves to **2.12.12** on disk
(`components/espressif__esp_hosted/idf_component.yml`). The 4.3" unit shipped with slave
2.12.12, which is why its notes never had to deal with this.

What the warning means: host and slave talk protobuf RPC over SDIO; Espressif's own
troubleshooting guide says "When switching Hosted versions, use the same version for master
and slave. Implementation changes can make mismatched master/slave versions incompatible."
Observed consequences across versions, from this repo's 4.3" notes (troubleshooting
section 7, community reports for slave 2.3.0 boards): WiFi scans work but association loops
with `ASSOC_LEAVE`, `H_SDIO_DRV: Dropping packet(s) from stream`; and vHCI Bluetooth fails
outright across a mismatch (comment in bedside_clock's `idf_component.yml`). The factory
log already ends with a failed RPC (`rpc_rsp: Hosted RPC_Resp [0x21a] ... resp code
[12298]`) after `wifi station started`; whether that is the mismatch or simply missing
credentials is [unverified].

Conclusion: before any serious WiFi or BLE work with a 2.12.x host, the C6 must run a
2.12.x slave.

## 2. Which version to target

| Candidate | Why / why not |
|---|---|
| **2.12.12** | Matches the host version resolved by bedside_clock today; the exact version the 4.3" unit runs and that EspControl pinned by SHA256; published at `https://esphome.github.io/esp-hosted-firmware/v2.12.12/network_adapter_esp32c6.bin` (sha256 `bad97ce81e7fcf5f3365898f633b80941ae863db9c754d87a78f89c8f61f2e94` per the manifest fetched 2026-10-02) |
| 2.12.13 | Latest 2.12.x in both `espressif/esp-hosted-mcu` tags and the esphome manifest (2026-09-25); fine if the host is bumped to 2.12.13 at the same time |
| 3.0.x | Exists (3.0.9, 2026-09-28) but requires a 3.x host; the owner's deferred-upgrade list keeps esp_hosted 3.x out of scope for now |
| 2.3.x (stay) | Only if the host is pinned down to 2.3.x, which would lose the fixes the 4.3" bring-up depended on; not recommended |

Rule: change host and slave together, and keep the two numbers equal.

## 3. Options

### (a) Build the slave yourself and flash it over the C6 UART header (untested)

Espressif's recipe (`docs/esp32_p4_function_ev_board.md`, section 5.1, in the local
esp_hosted 2.12.12 copy):

```
idf.py create-project-from-example "espressif/esp_hosted=2.12.12:slave"
idf.py set-target esp32c6
idf.py menuconfig      # Bus Config -> Transport layer -> SDIO (default)
idf.py build
```

The 2.12.12 component ships `slave/sdkconfig.defaults.esp32c6` and
`slave/partitions.esp32c6.csv` (verified present locally). In esp-hosted-mcu 3.x the slave
project lives under `coprocessor/` instead of `slave/` (GitHub tree, 2026-10-02).

Wiring: the C6's UART0, boot and reset pins are on **CN5** (schematic sheet `5_ESP32-C6.png`):

| CN5 pin | Net | Use |
|---|---|---|
| 1 | `VCC3V3` | do **not** connect the adapter's 3.3 V; the board powers the C6 |
| 2 | `GND` | adapter GND |
| 3 | `C6_U0TXD` | to adapter RX |
| 4 | `C6_U0RXD` | to adapter TX |
| 5 | `C6_IO9` | boot select; pulled up by R88 5.1k; hold **low** during reset for download mode |
| 6 | `C6_CHIP_PU` | C6 EN; pulled up by R59 10k with C82 1 uF; **also driven by P4 GPIO54** |

Physical location: likely the 2x3 pin header next to the C6 module in the vendor's
"Internal Interface Display" drawing [to be confirmed with a multimeter before use]. The
pin order above is the schematic order; the silkscreen may differ.

Procedure (adapted from the 4.3" community-verified recipe, 05-troubleshooting section 7,
and Espressif's section 5.1):

```
# 1) Park the P4 in download mode so it stops resetting the C6 through GPIO54
esptool.py --chip esp32p4 -p <P4 port> --before default_reset --after no_reset run
# 2) Put the C6 in download mode: IO9 low, pulse CHIP_PU low, release IO9 (or use an
#    adapter with DTR/RTS wired to CHIP_PU and IO9)
# 3) Flash the slave app into ota_0 (slave layout: ota_0 @0x10000, ota_1 @0x190000,
#    otadata @0xd000; same layout as the vendor's own C6 images, INVENTORY 5.1)
esptool.py --chip esp32c6 -p <C6 UART> -b 460800 write_flash 0x10000 network_adapter_esp32c6.bin
esptool.py --chip esp32c6 -p <C6 UART> erase_region 0xd000 0x2000
# 4) Reset both
esptool.py --chip esp32c6 -p <C6 UART> --after hard_reset run
esptool.py --chip esp32p4 -p <P4 port> --after hard_reset run
```

If you built the whole slave project with `idf.py build`, `idf.py -p <C6 UART> flash`
writes bootloader, partition table and app together and is simpler than the app-only
write above. The warning from the 4.3" notes stands: a running P4 resets the C6 on every
WiFi-init retry and kills a flash in progress ("The chip stopped responding").

### (b) Slave OTA over SDIO from the P4 (untested)

No wires. The host API in esp_hosted 2.12.12 (`host/api/include/esp_hosted_ota.h`):

```
esp_err_t esp_hosted_slave_ota_begin(void);
esp_err_t esp_hosted_slave_ota_write(uint8_t *ota_data, uint32_t ota_data_len);
esp_err_t esp_hosted_slave_ota_end(void);
esp_err_t esp_hosted_slave_ota_activate(void);   // reboots the co-processor
esp_err_t esp_hosted_slave_ota(const char *image_url);  // deprecated convenience wrapper
```

plus `esp_hosted_get_coprocessor_fwversion()` to read the running slave version. Worked
example: `examples/host_performs_slave_ota/` in the component ("updating slave firmware
through the existing ESP-Hosted connection (SDIO/SPI/UART) without any additional hardware").
Espressif calls this the recommended update path (section 5.2 of the P4 board doc).

Production implementations to crib from: ESPHome's `update: platform: esp32_hosted` fed
by `https://esphome.github.io/esp-hosted-firmware/manifest/esp32c6.json` (EspControl
`common/device/esp32_c6_firmware_update.yaml` checks it every 24 h and installs
automatically), and EspControl's recovery build that embeds a SHA256-verified slave image
and flashes it before WiFi starts (4.3" notes, 07-espcontrol-findings).

Caveats: the transport must come up first (it does on this unit, mismatch and all), so the
OTA path is available even with the 2.3.0 slave as long as the OTA RPCs are understood by
2.3.0 [unverified; one 4.3" community report of `OTA_VALIDATE_FAILED` with an
ESPHome-mirrored image exists, fall back to (a) if that happens]. Keep the image in flash
or PSRAM, not in a tight internal-RAM buffer.

### (c) Prebuilt images from esphome/esp-hosted-firmware (untested)

`https://github.com/esphome/esp-hosted-firmware` (Apache-2.0) publishes
`network_adapter_esp32c6.bin` per version; the manifest lists 2.12.12, 2.12.13, 3.0.8 and
3.0.9 among others (fetched 2026-10-02). These are app images for the esp_hosted slave
partition layout; use them with (a) at 0x10000 or with (b). Check the SHA256 in the
manifest before flashing.

The vendor's own `JC-C6-slave_v2.3.2.bin` (full image at 0x0, SHA256 in
[../firmware/README.md](../firmware/README.md)) restores the factory state of the C6, which
is the mismatched state; it is a rollback target, not an upgrade.

## 4. How to verify

After reset, the P4 handshake block must read (for a 2.12.12 pair):

```
I transport: Identified slave [esp32c6]
I transport: capabilities: 0xd          (WLAN + HCI over SDIO, BLE only)
```

with **no** `Version mismatch` warning (a `Host [x] < Co-proc [y]` warning means the slave
is now newer than the host; WiFi tolerates it, Bluetooth does not, per the 4.3" benign-lines
table). From code: `esp_hosted_get_coprocessor_fwversion()`. Then the functional checks
from the 4.3": a WPA2 2.4 GHz association that holds, and, if BT is wanted, the three-step
recipe in `../../guition-jc4880p443c/docs/04-bluetooth.md` (host and slave versions equal,
transport up before NimBLE, controller enabled explicitly: the factory host logs `Host BT
Support: Disabled`).

## 5. Recovering a bricked C6

- The P4 is unaffected by anything done to the C6; its console and flash stay reachable.
- Symptom of a dead or empty C6 [expected]: the P4 stays in `transport: Waiting for
  esp_hosted slave to be ready` or times out in `H_SDIO_DRV` card init; the display and
  everything local keep working.
- Recovery is option (a): CN5, IO9 low, CHIP_PU pulse, `esptool.py --chip esp32c6`. If the
  bootloader itself was erased, flash a complete image at 0x0: either your own
  `idf.py flash` of the slave project or the vendor's `JC-C6-slave_v2.3.2.bin`.
- `esptool.py --chip esp32c6 chip_id` over CN5 is the first thing to try; if the chip does
  not answer in download mode, check that the P4 is parked (it holds the C6 in reset
  otherwise) and that IO9 is really low.
- Never write a C6 image to the P4 or a P4 image to the C6; esptool checks the image
  header's chip id for images carrying one and refuses with a chip-mismatch error unless
  forced [esptool behaviour; verify on your version]. Do not use `--force`.

## 7. Result on this unit, 2026-10-03

Tool: `src/tool_c6_ota` (one-shot P4 firmware, image embedded, no WiFi needed). Image:
`network_adapter_esp32c6.bin` from `https://esphome.github.io/esp-hosted-firmware/v2.12.12/`,
SHA-256 `bad97ce81e7fcf5f3365898f633b80941ae863db9c754d87a78f89c8f61f2e94` verified
before use. Full log: `../logs/c6-slave-ota-2026-10-03.txt`.

| Step | Log |
|---|---|
| Before | handshake warned `Version mismatch: Host [2.12.0] > Co-proc [2.3.0]`; `esp_hosted_get_coprocessor_fwversion` reported **2.3.2** (the mismatch warning compares major.minor only) |
| Transfer | 1,226,704 bytes in 1500-byte chunks, `ota_end -> ESP_OK after 10.9 s`; the 2.3.2 slave has no activate API and switches on `ota_end` |
| After the automatic host restart | `slave reports esp_hosted 2.12.12`, no `Version mismatch` line, `capabilities: 0xd`, `Identified slave [esp32c6]` |

Answers to the open questions below: (1) yes, a 2.3.2 slave accepts the OTA RPCs of a 2.12
host. (2) and (3) were not needed. WiFi association with the new slave is **not yet verified**
because this unit holds no WiFi credentials; enter them on the device and record the result
here.

## 6. Open questions for this unit

1. Does the 2.3.0 slave accept the OTA RPCs of a 2.12.12 host (option b), or is the UART
   route required? Unknown until tried.
2. Which physical header is CN5, and does its pin 1 match the schematic order?
3. P4 GPIO6 is wired to C6 IO2 (sheets 3 and 5). No configuration uses it; a future esp_hosted
   feature (e.g. a data-ready or power-save line) might. Leave it as input.
