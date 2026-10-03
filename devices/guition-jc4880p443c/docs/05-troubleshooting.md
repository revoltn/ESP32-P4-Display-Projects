# Troubleshooting — Every Problem Hit During Bring-up, With Fix

Chronological, with the exact error text so future searches land here. §§1-6 were hit on
this unit; §§7-8 are community-documented board gotchas kept here for the error text.

## 1. Build error: esp_lvgl_port 2.9.0 vs IDF 5.5.4

```
esp_lvgl_port_disp.c:160:16: error: 'esp_lcd_dpi_panel_event_callbacks_t' has no member named 'on_frame_buf_complete'
```

**Cause**: the registry resolved `esp_lvgl_port` to 2.9.0, which needs the
`on_frame_buf_complete` DPI callback that only landed in IDF 5.5.5.
**Fix (historical)**: pin `espressif/esp_lvgl_port: "~2.7.2"` in `main/idf_component.yml`, delete
`dependencies.lock` and `managed_components/espressif__esp_lvgl_port/`, rebuild.
**Resolved 2026-09-02**: toolchain upgraded to IDF v5.5.5 and the pin moved to `~2.9.0`;
this error can only reappear if building against an older IDF (the manifests now require
`idf: ">=5.5.5"` to fail fast).

Benign new log line since 2.9.0 (seen at boot, non-fatal):
`E (1164) lcd_panel: esp_lcd_panel_swap_xy(50): swap_xy is not supported by this panel` —
esp_lvgl_port probes for hardware rotation, the DSI/ST7701 path refuses, and it falls back
to the software (PPA) rotation we already use. Boot continues normally.

## 2. Flash refused: chip revision

```
A fatal error occurred: bootloader/bootloader.bin requires chip revision in range [v3.1 - v3.99] (this chip is revision v1.3)
```

**Cause**: IDF 5.5 builds for P4 rev ≥3.1 by default; this board is rev v1.3, and the two
silicon generations are binary-incompatible.
**Fix** in `sdkconfig.defaults` (then delete `sdkconfig` and rebuild so it regenerates):

```
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
CONFIG_ESP32P4_REV_MIN_100=y
```

Note the gate option `SELECTS_REV_LESS_V3` — setting only `REV_MIN_100` does nothing because
the choice is hidden until the gate is enabled.

Same fix in other toolchains: ESPHome `engineering_sample: true`; Arduino/PlatformIO
(pioarduino ≥ `55.03.36-1`) `chip_variant: esp32p4_es` in the board def — the symptom there
is *"Illegal instruction"* in the 2nd-stage bootloader rather than a refused flash.

## 3. Bluetooth: endless `BLE_HS_ETIMEOUT_HCI`

```
E NimBLE: HCI wait for ack returned 19
I NimBLE: ogf=0x03, ocf=0x0003 : BLE_HS_ETIMEOUT_HCI (HCI request timed out; controller unresponsive)
```

Three stacked causes; full write-up in [04-bluetooth.md](04-bluetooth.md). Short form:
1. host esp_hosted must be `~2.12.0` to match the C6 slave (2.12.12);
2. call `esp_hosted_connect_to_slave()` before `nimble_port_init()`;
3. **call `esp_hosted_bt_controller_init()` + `esp_hosted_bt_controller_enable()`** —
   the C6's controller is disabled by default since esp_hosted 2.5.2.

## 4. ninja lock / recompaction permission denied — orphaned build processes

```
ninja: error: WriteFile(.ninja_lock): Unable to create file. Permission denied
ninja: error: failed recompaction: Permission denied      (during CMake generate)
Remove-Item: ... .ninja_deps ... being used by another process
```

First seen once as a transient (retry succeeded). Root cause found 2026-08-30 during camera
bring-up: **when a shell running `idf.py build` is killed or times out, its child
`cmake`/`ninja`/python processes keep running detached**, hold handles on `.ninja_deps` /
`.ninja_log` / `build/log/*`, and every later build fails with permission errors (or worse,
two builds race in the same build dir).
**Fix**: `Get-Process | Where-Object { $_.Name -match 'ninja|cmake' }` (plus IDF-env `python`),
`Stop-Process` the orphans — or wait for them to finish — then build again. If the build dir
was half-deleted while an orphan ran, delete `build/` fully and rebuild.

## 5. Build error: `esp_cache_get_alignment` implicit declaration

```
feature_camera.c: error: implicit declaration of function 'esp_cache_get_alignment'
```

**Cause**: the function is not in `esp_cache.h` — it lives in a private header.
**Fix**: `#include "esp_private/esp_cache_private.h"` (component `esp_mm`). The vendor camera
demos use it the same way.

## 6. Camera: no `CONFIG_CAMERA_OV02C10` symbol / sensor not detected

Managed `esp_cam_sensor` 2.0.1 has **no OV02C10 driver**; the vendored 2.1.0 must be copied
in as a local component override and sdkconfig regenerated. Full recipe and the other camera
traps (bogus vendor Kconfig symbol, ISP pipeline controller required, direct-mode display
constraint): [09-camera.md](09-camera.md).

## 7. WiFi associates then drops (`ASSOC_LEAVE` loop) — C6 slave firmware too old

*(Community-documented for other units of this board; **not hit on ours**, which shipped
slave 2.12.12. Kept because Guition batches differ — boards shipping slave **2.3.0** exist.)*

```
H_SDIO_DRV: Dropping packet(s) from stream
H_SDIO_DRV: Failed to push data to rx queue
... Disconnected ... reason='Association Leave'  /  Reason: 8 - ASSOC_LEAVE
transport: Version mismatch: Host [2.12.0] > Co-proc [2.3.0]  ==> Upgrade co-proc
```

**Cause**: host/slave ESP-Hosted version mismatch — scanning works but association loops
forever. It is not the AP, signal, power, or app code. **Check the handshake block in the
boot log first**: if the slave already reports ~2.12.x (like ours), this is not your
problem — pin the *host* component instead (see §3).

**Fix A — no wires (try first)**: OTA the C6 over SDIO. EspControl/ESPHome do this in
production via `esp_hosted_slave_ota_*` with sha256-pinned images — working recipes in
[07-espcontrol-findings.md](07-espcontrol-findings.md) and
[08-esphome-findings.md](08-esphome-findings.md). (Community caveat: one report of the
ESPHome-mirrored image failing the slave's `ota_end` validation with `OTA_VALIDATE_FAILED`;
if that happens, fall back to Fix B.)

**Fix B — direct UART reflash (last resort, community-verified)**: the C6's UART0 +
boot/reset are on the **JP1 expand header** (sheet 03 in [schematics/](schematics/)). Wire a
3.3 V USB-TTL adapter: TX→`C6_U0RXD`, RX→`C6_U0TXD`, GND→GND, **leave 3V3 unconnected**
(the board powers the C6). Without DTR/RTS auto-reset, pull `C6_IO9` (BOOT) low and pulse
`C6_CHIP_PU` (reset) to enter download mode. Firmware mirror:
`https://esphome.github.io/esp-hosted-firmware/v2.12.9/network_adapter_esp32c6.bin`

> **⚠️ Halt the P4 first.** A running P4 keeps resetting the C6 over GPIO54 (its WiFi-init
> retry), which kills the flash mid-write (`A fatal error occurred: The chip stopped
> responding` — small writes succeed, large ones die).

```bash
# 1) Park the P4 in download mode (stops it resetting the C6)
esptool --chip esp32p4 --port <P4_USB_PORT> --before default_reset --after no_reset flash_id

# 2) Flash the C6 (slave app partitions: ota_0 @0x10000 / ota_1 @0x190000, 1.5 MB each)
esptool --chip esp32c6 --port <C6_UART> --baud 115200 write_flash 0x10000 network_adapter_esp32c6.bin

# 3) Point otadata at ota_0, then reset both chips
esptool --chip esp32c6 --port <C6_UART> erase_region 0xd000 0x2000
esptool --chip esp32c6 --port <C6_UART> --after hard_reset flash_id
esptool --chip esp32p4 --port <P4_USB_PORT> --after hard_reset flash_id
```

The update lives in the C6's own flash, so it survives reflashing the P4.

## 8. microSD: `sdmmc_init_ocr: send_op_cond returned 0x107`

*(Vendor/community-documented; SD is **not yet exercised in this repo**.)*

**Cause**: the card has no power. `TF_VCC` comes from P4 **on-chip LDO channel 4** through an
always-on P-FET — there is no GPIO power switch (GPIO45 is NC). If firmware never acquires
LDO ch4, `send_op_cond` times out. Also: the slot is **SDMMC 4-bit, not SPI** — SPI-mode
`SD.begin()` with guessed pins just spins the retry loop and blocks boot.

**Fix**: acquire LDO ch4 @ 3300 mV before mounting, then use SDMMC mode on
CLK 43 / CMD 44 / D0-3 = 39/40/41/42. Full pin/power detail in
[01-hardware.md](01-hardware.md) (microSD bullet). Don't let the SD driver grab its own LDO
channel — it collides with the display's ch3 (`esp_ldo_acquire_channel ... already in use`).

## 9. HTTPS downloads fail mid-stream: `SSL - Verification of the message MAC failed` (-0x7180)

**Symptom** (bedside_v01, 2026-09-02): song streaming from Navidrome died every 0.15–0.6 MB.
The app log said only `Navidrome: stream read error`; with diagnostics enabled the serial log
shows:

```
E esp-tls-mbedtls: read error :-0x7180
E HTTP_CLIENT: transport_read: error - -1 | ESP_FAIL
W navidrome: ... tls_last=ESP_ERR_MBEDTLS_SSL_READ_FAILED tls_code=29056 (SSL - Verification of the message MAC failed) errno=0
```

`errno=0` and a valid TCP checksum (lwIP always verifies TCP on receive in IDF) mean the bytes
arrived intact — they were corrupted at **decryption**.

**Cause**: the ESP32-P4 hardware AES-DMA path decrypting mbedTLS's record buffer, which lives
in PSRAM because of `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`, returns wrong plaintext under
sustained load (2–3 MB/s). IDF v5.5.5 already contains Espressif's Jan-2026 cache-coherency
fixes for that file and still fails. Bisected on hardware; details in
[11-tls-stream-corruption-investigation.md](11-tls-stream-corruption-investigation.md).

**Fix**: `CONFIG_MBEDTLS_HARDWARE_AES=n` (software AES, ~0.6 MB/s TLS). Zero errors in 26 MB
afterwards. Re-test hardware AES after every IDF upgrade. Not a fix: SDIO 20 MHz (rate ~10×
lower, still fails), `CONFIG_LWIP_L2_TO_L3_COPY`, PPA off.

## 10. Reboot when a song starts: `i2s_alloc_dma_desc(541): allocate DMA buffer failed` → Load access fault

**Symptom**:

```
I bsp_extra_board: rate = 48000bits_cfg = 16
E i2s_common: i2s_alloc_dma_desc(541): allocate DMA buffer failed
E I2S_IF: Failed to reconfig STD slot, ret: 257
Guru Meditation Error: Core 0 panic'ed (Load access fault)   <- i2s_rx_channel_start <- esp_codec_dev_open
```

**Cause**: changing the codec sample rate re-allocates the I2S DMA descriptors (six ~1 KB
buffers per direction) from DMA-capable **internal** RAM. When that fails (this board idles at
80–115 KB internal free, ~40 KB while streaming before the L2→L3 copy change),
`esp_codec_dev_open()` in `espressif__esp_codec_dev` ignores the `set_fmt` error and enables
a channel whose descriptor list is NULL.

**Fix**: local patch in `managed_components/espressif__esp_codec_dev/esp_codec_dev.c` (fail
the open — keep it on component refresh); `bsp_extra_codec_set_fs` now logs internal/DMA heap
headroom at every reconfigure; `CONFIG_LWIP_L2_TO_L3_COPY=y` freed ~35 KB during playback.
Watch `dma largest` in that log line — below ~12 KB the next reconfigure may fail (the song
then ends cleanly instead of rebooting).

## Benign log lines (do NOT chase these)

| Log line | Verdict |
|---|---|
| `W ledc: GPIO 23 is not usable, maybe conflict with others` | Benign — backlight on GPIO23 works fine right after it (`Setting LCD backlight: 100%`). |
| `E lcd_panel: esp_lcd_panel_swap_xy(50): swap_xy is not supported by this panel` | Benign — BSP probes swap_xy on the DPI panel; portrait orientation is native. Present in every working build. |
| `I ESP32_P4_EV: Install EK79007 LCD control panel` | Misleading BSP log text — the code path actually installs the ST7701. Cosmetic only. |
| `W spi_flash: Detected boya flash chip but using generic driver` | Benign — generic driver works. Optionally set `CONFIG_SPI_FLASH_SUPPORT_BOYA_CHIP=y`. |
| `I GT911: I2C address initialization procedure skipped - using default GT9xx setup` | Benign — touch works in polling mode with default addressing. |
| `W transport: Version mismatch: Host [x] < Co-proc [y]` | **Not benign for Bluetooth** — WiFi tolerates it, BT does not. Match the host component to the slave version. |

## Diagnostic techniques that paid off

- **Read the esp_hosted handshake block** in the boot log (`transport: capabilities: ...`) —
  it tells you the slave's chip, esp_hosted version, and whether BT-over-SDIO is compiled in,
  before you spend time on the host side.
- **`strings` on vendor firmware bins** (`devices/guition-jc4880p443c/vendor/package/8-Burn operation/Burn files/`,
  inventory with sizes, SHA256 and partition layouts in [../firmware/README.md](../firmware/README.md))
  revealed the C6 images contain vHCI/BT code — which redirected debugging from "reflash the
  C6" (unnecessary, risky) to "fix the host side" (correct).
- **The component's own docs and examples** (`managed_components/espressif__esp_hosted/docs/`,
  `examples/`) contained both the init-order pattern and the controller-disabled-by-default
  note that no external documentation mentioned.
- For scripted serial capture, use pyserial from the IDF Python env instead of the
  interactive `idf.py monitor` (see [02-toolchain-and-build.md](02-toolchain-and-build.md)).
- **Log the read-once HTTP/TLS error details at the failure site.**
  `esp_http_client_get_errno()` and `esp_http_client_get_and_clear_last_tls_error()` (with
  `mbedtls_strerror`) turned an opaque "stream read error" into "MAC failed, errno 0" — which
  by itself excluded the network and pointed at decryption. IDF's own `ESP_LOGD` lines in
  `esp_http_client_read` are compiled out at `CONFIG_LOG_MAXIMUM_LEVEL=3`, so the app must log
  them itself (`navidrome_client.c: nav_diag_log`).
- **Bisect on the device, one Kconfig per build**, with a fixed, scripted load (API-driven
  random play + `next` every 20 s, 100 s serial capture, count `0x7180`). Four builds
  separated an AES-DMA bug from three plausible-but-wrong transport theories in an evening
  (docs/11). Theories that "explained everything" (SDIO clock, hosted buffer lifetime, PPA)
  each changed the rate, not the outcome — only a zero counts.
- **Trust the layered checksums**: a TLS MAC failure with a *passing* TCP checksum localises
  the fault to the receiving side's memory/crypto, not the link.
