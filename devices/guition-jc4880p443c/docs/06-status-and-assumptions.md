# Status: Verified vs Assumed vs Untested

Last updated **2026-08-30** after bring-up of `src/sample_hello_world` (then `src/hello_world`).

## ✅ Verified on this actual board

| Item | Evidence |
|---|---|
| Display: ST7701 480x800 over MIPI-DSI, LVGL 9 menu renders | Boot log + user confirmed menu on screen |
| Touch: GT911, buttons respond | User exercised the menu |
| WiFi: STA connect to WPA2 network ("<home-ssid>"), IP acquired | User confirmed on device |
| BLE: NimBLE host sync over C6 vHCI, active scan, 10 devices found with names/RSSI | Serial log 2026-08-30 |
| Flash/boot cycle on COM3, rev v1.3 build config | Repeated flashes |
| PSRAM 32 MB @200 MHz, 16 MB flash QIO | Boot log |
| esp_hosted SDIO link incl. C6 auto-reset on boot | Handshake in every boot log |
| ES8311 codec init + I2S read/write complete without error | Serial log (but see below) |
| **Camera: OV02C10 over MIPI-CSI, live 30 fps preview on screen** (1-lane FHD RAW10 → ISP RGB565 → PPA → LVGL canvas), tap-to-exit, re-entry, correct orientation with **no rotation** | User confirmed live preview 2026-08-30; details in [09-camera.md](09-camera.md) |
| A camera module IS physically fitted on the board | Preview shows live scene (an earlier "no camera fitted" note was wrong) |

## ⚠️ Assumed working — NOT yet fully validated

State these as assumptions; validate when possible.

| Assumption | Basis | How to validate |
|---|---|---|
| **Music synth is audible and sounds right** | Codec init + `bsp_extra_i2s_write` return OK; no speaker was attached during testing | Plug a speaker into the MX1.25 2-pin connector, tap "Play Toccata" |
| **Mic record → playback works end-to-end** | `bsp_extra_i2s_read` returns data without error; playback never heard (no speaker) | Same — also check recorded audio isn't silence/noise (mic gain 24 dB is the vendor value) |
| **Mono 16 kHz slot mode is correct for ES8311 on this board** | xiaozhi vendor port uses it; our writes don't error | If playback is wrong-speed/garbled, switch `AUDIO_SLOT_MODE` to stereo + duplicate samples (see `app_common.h`) |
| **"<home-ssid>_5g" fallback SSID connects** | Code path exists and mirrors the verified SSID path; never exercised (primary SSID succeeded first) | Take "<home-ssid>" down and retry. Note: the C6 radio is 2.4 GHz-only — a 5 GHz-band SSID cannot work; only a 2.4 GHz AP with that name would |
| **Bluedroid (instead of NimBLE) also works over vHCI** | Community sample uses it; esp_hosted supports it | Would need the controller-enable calls added; untested |
| **BLE roles beyond scanning** (advertise, connect, GATT) | Stack syncs and scans; nothing beyond `ble_gap_disc` was exercised | Extend feature_bt.c |
| **ninja_lock build failure was a transient race** | Happened once, identical retry succeeded | N/A unless it recurs |

## ❌ Confirmed NOT available (validated roadblocks)

| Item | Evidence |
|---|---|
| Bluetooth Classic | C6 capability handshake reports "**BLE only**" — hard limit of the installed controller/firmware |
| P4-native WiFi/BT radio | ESP32-P4 has no radio; everything wireless goes through the C6 |
| Firmware built for chip rev ≥3.0 on this board | esptool hard-refuses; silicon is rev v1.3 |

## ⬜ Untested / unexplored on this board (no claim either way)

- microSD card slot (BSP support exists: `bsp_sdcard_mount()`)
- RS485 (UART1 on GPIO26/27; vendor examples exist)
- Battery ADC (GPIO53; vendor flags the scaling as uncharacterized)
- Backlight PWM dimming curve (only full-on used; vendor flags duty→luminance as unmeasured)
- C6 firmware update paths — not needed so far. Two exist: esp_hosted's OTA-over-SDIO API
  (working reference implementation in `reference/espcontrol/upstream/components/c6_recovery/`, with
  official C6 firmware published at esphome.github.io/esp-hosted-firmware) and the JP1 UART
  recovery header
- WiFi throughput, AP mode, and WiFi+BLE coexistence under load
- Sleep/wake power modes

## Known-good baseline

`src/sample_hello_world` as of 2026-09-02: ESP-IDF v5.5.5, esp_hosted ~2.12.0 (host) ↔ 2.12.12
(C6 slave), esp_lvgl_port 2.9.0, lvgl 9.5.0, vendored BSP 5.2.3. `dependencies.lock` in the
project freezes the resolution (previous baseline kept as `dependencies.lock.pre-5.5.5.bak`).
Upstream review + upgrade rationale: [03-working-components.md](03-working-components.md).
