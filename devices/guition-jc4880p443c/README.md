# Guition JC4880P443C: 4.3" ESP32-P4 + ESP32-C6 smart display

Board card for the Guition **JC4880P443C_I_W** (module **JC-ESP32P4-M3**). ESPHome and EspControl
call it `jc4880p443`; this repo shortens it to JC4880P443C. All three names are the same board.
Everything marked "verified" below was measured on the author's unit; everything else names its
source. The numbered write-ups in [docs/](docs/README.md) hold the evidence.

## Identity

| Item | Value |
|---|---|
| Vendor SKU | JC4880P443C_I_W (Guition / Shenzhen Jingcai, `pan.jczn1688.com`) |
| Module | JC-ESP32P4-M3: ESP32-P4 + ESP32-C6 + 32 MB PSRAM + 16 MB flash on one module |
| ESPHome display model | `mipi_dsi` `model: JC4880P443` (present in the 2026.9.0-dev snapshot under [reference/esphome](../../reference/esphome/README.md)) |
| EspControl device slug | `guition-esp32-p4-jc4880p443` |
| Vendor package | `JC4880P443C_I_W.zip`, 295 MB; provenance in [vendor/README.md](vendor/README.md) |
| Schematic revision | `JC4880P443_V1.0` (vendor PDF in the package, dated 2025-09) |

## Verified on this unit (dated)

| Date | Fact | How |
|---|---|---|
| 2026-08-29 | ESP32-P4 **silicon rev v1.3**; images built for rev 3.x are refused by esptool | esptool during flash, [docs/05 §2](docs/05-troubleshooting.md) |
| 2026-08-29 | 16 MB QIO flash, **Boya** die (generic driver works) | boot log |
| 2026-08-29 | 32 MB octal PSRAM at 200 MHz with XIP | boot log `Found 32MB PSRAM device` |
| 2026-08-29 | ESP32-C6 runs **esp_hosted slave 2.12.12**; capabilities WLAN + HCI over SDIO, **BLE only** | esp_hosted handshake log |
| 2026-08-29 | Enumerates as a plain USB serial device (COM3 on the author's PC); `idf.py flash` auto-resets | repeated flashes |
| 2026-08-29 | ST7701S 480x800 over MIPI-DSI with LVGL 9; GT911 touch (ID 911, config 250, polled) | on screen |
| 2026-08-30 | WiFi STA via `esp_wifi_remote`; BLE active scan via NimBLE over vHCI (10 devices) | on screen and serial log, [docs/04](docs/04-bluetooth.md) |
| 2026-08-30 | OV02C10 camera is fitted; 1-lane RAW10 1080p30 through ISP, PPA and an LVGL canvas | live preview, [docs/09](docs/09-camera.md) |
| 2026-09-02 | Hardware AES corrupts TLS records held in PSRAM; software AES clean over 26 MB | bisection on hardware, [docs/11](docs/11-tls-stream-corruption-investigation.md) |
| 2026-09 | ES8311 + NS4150 playback and the on-board mic in daily use (external speaker fitted) | bedside_clock alarms and music, [docs/10](docs/10-bedside-v01-review.md) |
| 2026-09-27 | USB **host** works on the high-speed socket (the one that is not COM3); a UAC speaker enumerated and played | by ear, [docs/01](docs/01-hardware.md) USB bullet |

Full ledger of verified, assumed and untested items: [docs/06-status-and-assumptions.md](docs/06-status-and-assumptions.md).
The chip's own self-report as captured on 2026-08-30: [docs/device-specifications-log.md](docs/device-specifications-log.md).

## Specification

| Block | Detail |
|---|---|
| Main SoC | ESP32-P4: 2x RISC-V HP cores (360 MHz on rev v1.x silicon), 1x LP core, no radio |
| Wireless | ESP32-C6 co-processor over SDIO (esp_hosted): WiFi 6 at 2.4 GHz, BLE 5. No Bluetooth Classic, no 5 GHz, 802.15.4 not exposed |
| Memory | 32 MB octal PSRAM (200 MHz, XIP), 16 MB QIO flash, 768 KB HP SRAM |
| Display | 4.3" IPS 480x800 portrait, ST7701S, MIPI-DSI 2 lanes at 500 Mbps, DPI 34 MHz, RGB565; porches h 12/42/42, v 2/8/166 |
| Touch | Goodix GT911, I2C 0x5D, 5 points |
| Audio | ES8311 mono codec on I2S, NS4150 3 W class-D, analog mic MSM381A3729H9CP; **speaker is external** (MX1.25 2-pin) |
| Camera | OmniVision OV02C10 2 MP on MIPI-CSI (2 lanes wired, 1-lane mode proven), SCCB 0x36 |
| Storage | microSD on SDMMC 4-bit, powered from P4 LDO channel 4 (untested in this repo) |
| USB | Two USB-C sockets: native high-speed OTG (host verified) and full-speed USB-Serial/JTAG (console, flashing) |
| Serial | UART0 on CN2 (4-pin); UART1 shared between J5 TTL and J4 RS-485 (MAX485, automatic direction) |
| Expansion | JP1 2x13 header (GPIO 28-35 and 49-52, I2C, C6 UART0/boot/reset), CN3 I2C 4-pin |
| Power | 5 V in via either USB-C or CN2 VIN; TLV62569 3V3 buck; IP5306 charger/boost; 1S Li-ion on CN4 with a divider to GPIO53 |
| Mechanical | `vendor/package/3-Structure_Diagram/` (local only) |

## Pin map

Source key: **V** verified on this unit in this repo; **S(nn)** vendor schematic, sheet number as in
[docs/schematics/README.md](docs/schematics/README.md); **E** field-proven in EspControl or ESPHome
upstream config; **C** community notes (seobaeksol, bigbag); **X** vendor example code.

| Function | GPIO / net | Notes | Source |
|---|---|---|---|
| MIPI-DSI | dedicated DSI pads | 2 lanes; on-chip LDO ch3 at 2500 mV must be on before panel init | V, E |
| LCD reset | 5 | community bring-up pulses low 20 ms / high 120 ms; ESPHome model default | E, C |
| LCD backlight | 23 | MP3202 boost, LEDC PWM; `GPIO 23 is not usable` log line is benign; EspControl runs 100 Hz with an 80 % cap | V, S(03), E |
| I2C SDA / SCL | 7 / 8 | one bus for GT911 0x5D, ES8311 0x18, OV02C10 0x36, CN3 and JP1 pins 21/23; **one driver only** | V |
| GT911 INT / RST | not connected | polled mode; community maps cite 3 or 22 for RST, neither is needed | V |
| I2S MCLK | 13 | ES8311 | V, S(01), C |
| I2S BCLK | 12 | | V, S(01), C |
| I2S LRCK / WS | 10 | | V, S(01), C |
| I2S DOUT (P4 to codec) | 9 | playback | V, S(01), C |
| I2S DIN (codec to P4) | 48 | microphone | V, S(01), C |
| PA enable (NS4150) | 11 | high before playback | V, S(02) |
| Speaker | MX1.25 2-pin | external, nothing on board | S(02) |
| SDIO CLK | 18 | C6 link, slot 1, 4-bit; 40 MHz works here, EspControl ships 20 MHz | V, E |
| SDIO CMD | 19 | | V, E |
| SDIO D0..D3 | 14, 15, 16, 17 | | V, E |
| C6 reset | 54 | active high; the host resets the C6 on every boot, link up after about 1.7 s | V, E |
| C6 UART0 RX / TX, IO9, CHIP_PU | JP1 pins 18 / 20 / 22 / 24 | recovery only; untested here, community recipe in [docs/05 §7](docs/05-troubleshooting.md) | S(03), C |
| microSD CLK / CMD | 43 / 44 | SDMMC 4-bit, not SPI; needs LDO ch4 at 3300 mV; untested here | S(01), C |
| microSD D0..D3 | 39, 40, 41, 42 | 5.1 k pull-ups on board | S(01), C |
| MIPI-CSI | dedicated CSI pads | 2 lanes wired, 1-lane RAW10 1080p30 proven | V |
| Camera SCCB | 7 / 8 at 0x36 | reuse `bsp_i2c_get_handle()` | V |
| Camera reset / powerdown | none (pass -1) | | V |
| UART0 TX / RX | 37 / 38 | CN2 connector; console normally runs over USB-Serial/JTAG instead | S(02) |
| UART1 / RS-485 TX / RX | 26 / 27 | MAX485 with automatic direction; J4 and J5 share it; GPIO26 is the "LED" in some vendor code, do not blink it | S(04), X |
| Battery sense | 53 (ADC2_CH4) | 68 k / 100 k divider; vendor sample maps 2250..2500 mV to 0..100 %, unvalidated | S(02), C |
| BOOT button | 35 | strapping pin; hold while pressing RST for manual download mode | S(03), V |
| USB high-speed | `ESP_USB` pads | native OTG PHY; host role verified with a UAC speaker on the socket that is not COM3 | V |
| USB full-speed | `USB1P1` pads | USB-Serial/JTAG, the COM port; both sockets share the `USB5V_IN` rail | V, S(03) |
| JP1 expansion GPIOs | 28, 29, 30, 31, 32, 33, 34, 35, 49, 50, 51, 52 | double as RMII nets; 34/35 are strapping pins | S(03) |
| On-chip LDO ch3 / ch4 | 2.5 V DSI PHY / 3.3 V TF_VCC | acquire both from firmware; a second owner of ch3 fails with `already in use` | V, S(01) |

Pins not listed are unused or dedicated (PSRAM and flash sit inside the module). The eight external
connectors (BAT, two USB-C, JP1, CN2, J5, J4, CN3) with electrical limits, conflicts and intended
uses are specified in [docs/interface-types.md](docs/interface-types.md).

## What works in this repo

**[src/sample_hello_world](../../src/sample_hello_world/)** (ESP-IDF 5.5.5, vendor-patched BSP,
queue/worker architecture) is the bring-up app. Verified on the board: LVGL menu with touch, WiFi
through the C6 with the IP on screen, BLE scan listing names and RSSI, full-screen live camera
preview, a device-specs page, and USB audio output to a UAC speaker. Code-complete but unverified
at its last docs pass: the organ synthesizer and the 5 s mic echo (no speaker was attached then).

**[src/bedside_clock](../../src/bedside_clock/)** is the production app, in daily use on the
author's unit since 2026-09: alarm clock with three slots, weekday masks, auto-stop and a persistent
event journal; music from a Navidrome/Subsonic server over TLS (software AES); an HTTP control API on
port 80 with an API key (off by default) plus a web UI; camera-driven ambient dimming; on-device
wake word (custom microWakeWord model) and command recognition; a USB audio sink that falls back to
the codec. Review history: [docs/10](docs/10-bedside-v01-review.md). TLS case study:
[docs/11](docs/11-tls-stream-corruption-investigation.md). Partition table: 8 MB factory app,
4 MB `model` (esp-sr), 128 KB `coredump`.

**[src/sample_wakeword_bench](../../src/sample_wakeword_bench/)** is a throwaway benchmark that
proved microWakeWord runs on rev v1.3 silicon with esp-nn kernels before it went into bedside_clock.

Every firmware for this board signs off against
[docs/reliability-checklist.md](../../docs/reliability-checklist.md).

### Build and flash

```powershell
& $env:USERPROFILE\esp\esp-idf\export.ps1        # ESP-IDF v5.5.5
cd src\bedside_clock                               # or src\sample_hello_world
idf.py set-target esp32p4                          # first time only
idf.py build
idf.py -p COM3 flash monitor                       # Ctrl-] exits the monitor
```

Required sdkconfig entries (rev v1.x gate, repurposed BSP panel option, PSRAM, esp_hosted SDIO) are
listed in [docs/02](docs/02-toolchain-and-build.md); the component pins that are known to work
together are in [docs/03](docs/03-working-components.md).

## Five things that bite

1. **Silicon rev v1.3.** IDF 5.5 builds for rev 3.1 by default and esptool refuses the image
   (`requires chip revision in range [v3.1 - v3.99]`). Set `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`
   and `CONFIG_ESP32P4_REV_MIN_100=y` (ESPHome: `engineering_sample: true`; pioarduino:
   `chip_variant: esp32p4_es`). [docs/02](docs/02-toolchain-and-build.md),
   [docs/05 §2](docs/05-troubleshooting.md).
2. **Bluetooth needs three things at once**: host `esp_hosted ~2.12.0` to match the C6's 2.12.12,
   `esp_hosted_connect_to_slave()` before the host stack starts, and an explicit
   `esp_hosted_bt_controller_init()` + `_enable()` (the C6 controller is disabled by default since
   esp_hosted 2.5.2). Otherwise `BLE_HS_ETIMEOUT_HCI` forever. BLE only, no Classic.
   [docs/04](docs/04-bluetooth.md).
3. **Hardware AES corrupts TLS in PSRAM.** With `MBEDTLS_EXTERNAL_MEM_ALLOC=y` and sustained HTTPS at
   2-3 MB/s, one record in a few thousand fails its MAC (`-0x7180`). `CONFIG_MBEDTLS_HARDWARE_AES=n`
   fixed it; re-test after every IDF upgrade.
   [docs/11](docs/11-tls-stream-corruption-investigation.md).
4. **No sound and no USB host until you use the right socket.** The speaker is external (MX1.25).
   USB host works only on the high-speed socket, which is the one that does *not* give you a COM
   port, and `peripheral_map = BIT0|BIT1` silently moves the host to the full-speed controller whose
   pins reach neither socket. [docs/01](docs/01-hardware.md).
5. **One I2C driver, two LDO channels, one camera driver version.** Touch, codec and camera share
   GPIO7/8: reuse the BSP handle. DSI needs LDO ch3, the SD card LDO ch4. The managed
   `esp_cam_sensor` 2.0.1 has no OV02C10 driver; the vendored 2.1.0 must override it.
   [docs/03](docs/03-working-components.md), [docs/09](docs/09-camera.md).

Also seen on this unit: PPA hardware rotation can hang indefinitely under load on rev v1.3 (the
bedside app bounds the wait and falls back to CPU rotation); `esp_codec_dev_open()` enables an I2S
channel after a failed DMA descriptor allocation and panics (local patch, [docs/05 §10](docs/05-troubleshooting.md));
internal RAM is the scarce resource (80-115 KB free at idle with display, hosted and audio up).

## Folder map

| Path | Contents |
|---|---|
| [docs/README.md](docs/README.md) | Index and reading order for the numbered series |
| [docs/01-hardware.md](docs/01-hardware.md) | The board as measured: revision, pin map, display, touch, audio, C6 link, SD, USB, power |
| [docs/02-toolchain-and-build.md](docs/02-toolchain-and-build.md) | ESP-IDF 5.5.5 on Windows, build/flash/monitor, required sdkconfig, partition tables |
| [docs/03-working-components.md](docs/03-working-components.md) | Component versions that work together, vendored BSP, upstream review, donor code |
| [docs/04-bluetooth.md](docs/04-bluetooth.md) | BLE over the C6: three stacked root causes and the working recipe |
| [docs/05-troubleshooting.md](docs/05-troubleshooting.md) | Every error text hit during bring-up with its fix; benign log lines; diagnostic methods |
| [docs/06-status-and-assumptions.md](docs/06-status-and-assumptions.md) | Verified vs assumed vs untested |
| [docs/07-espcontrol-findings.md](docs/07-espcontrol-findings.md) | What EspControl (production ESPHome firmware) does on this board |
| [docs/08-esphome-findings.md](docs/08-esphome-findings.md) | ESPHome core support for the board, copyable fixtures, coverage verdict |
| [docs/09-camera.md](docs/09-camera.md) | OV02C10 over MIPI-CSI: the 2.1.0 override, sdkconfig, V4L2 to PPA to LVGL pipeline |
| [docs/10-bedside-v01-review.md](docs/10-bedside-v01-review.md) | bedside_clock review log 2026-09-02 to 2026-09-27, open defects, checklist scorecard |
| [docs/11-tls-stream-corruption-investigation.md](docs/11-tls-stream-corruption-investigation.md) | Hardware-AES TLS corruption case study and re-test procedure |
| [docs/device-specifications-log.md](docs/device-specifications-log.md) | The chip's self-report captured 2026-08-30 |
| [docs/interface-types.md](docs/interface-types.md) | The eight external connectors: electrical limits, conflicts, good uses |
| [docs/schematics/](docs/schematics/README.md) | Five schematic sheets as webp (local only, gitignored) and how to regenerate them |
| [firmware/](firmware/README.md) | Factory image inventory with SHA256 and layouts, unbrick procedure, C6 firmware notes, publishing our own merged images |
| [vendor/](vendor/README.md) | The 295 MB vendor package: provenance, folder-by-folder contents and licence classes (content gitignored) |
| [community/](community/README.md) | Third-party repos for this board, what is mirrored locally, licence status |

## Differences from the 10.1" sibling (JC8012P4A1C)

Same module family, same C6 link pins, same rev v1.x silicon caveat, different board support
package. Details and the 10.1" pin map: [../guition-jc8012p4a1c/README.md](../guition-jc8012p4a1c/README.md).

| Item | JC4880P443C (this board) | JC8012P4A1C (10.1") |
|---|---|---|
| Panel | 4.3" 480x800, ST7701S | 10.1" 800x1280, JD9365 |
| Touch | GT911 (I2C 0x5D) | GSL3680 |
| LCD reset | GPIO5 | GPIO27 |
| PA enable | GPIO11 | GPIO20 |
| I2S DIN (mic) | GPIO48 | GPIO11 |
| Speaker | external, MX1.25 | built in |
| Battery sense | GPIO53 | GPIO52 |
| Factory C6 slave | esp_hosted 2.12.12 on this unit | esp_hosted 2.3.0 (expect the `ASSOC_LEAVE` loop until updated, [docs/05 §7](docs/05-troubleshooting.md)) |
| ESPHome model | `JC4880P443` | `JC8012P4A1` / `JC8012P4A1-V2` |

## Links

- Vendor package: <https://pan.jczn1688.com/directlink/1/HMI%20display/JC4880P443C_I_W.zip> (sharded downloader, recipe in [vendor/README.md](vendor/README.md))
- ESPHome `mipi_dsi` models: `reference/esphome/upstream/esphome/components/mipi_dsi/models/guition.py` ([reference/esphome/README.md](../../reference/esphome/README.md))
- EspControl device config: `reference/espcontrol/upstream/devices/guition-esp32-p4-jc4880p443/` ([reference/espcontrol/README.md](../../reference/espcontrol/README.md)); project site <https://jtenniswood.github.io/espcontrol/>
- C6 slave firmware channel: <https://esphome.github.io/esp-hosted-firmware/>; C6 notes in [../esp32-c6/README.md](../esp32-c6/README.md)
- Community bring-up thread: <https://www.reddit.com/r/esp32/comments/1nml6fm/guition_jc4880p433_43_capacitive_touch_display>
- Reliability checklist for any firmware on this board: [../../docs/reliability-checklist.md](../../docs/reliability-checklist.md)
