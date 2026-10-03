# Hardware Findings — Guition JC4880P443C

Everything below was either read from the vendor package ([vendor/](../vendor/README.md), local
only) and the seobaeksol community notes ([community/](../community/README.md), third-party, not
vendor documentation) and then **confirmed on the actual board** during bring-up (2026-08-29/30),
or is marked as an assumption.

**Board naming**: the full vendor SKU is **JC4880P443C_I_W** (module **JC-ESP32P4-M3**);
this repo shortens it to JC4880P443C, and ESPHome/EspControl YAML uses `jc4880p443`.
All three are the same board.

**Vendor board package** (demo code, drivers, datasheets, schematic, flashing tools; 295 MB zip as
downloaded 2026-08-29): <https://pan.jczn1688.com/directlink/1/HMI%20display/JC4880P443C_I_W.zip>
(a sharded JS downloader, not a plain file; recipe in [../vendor/README.md](../vendor/README.md)).
The extracted package lives in [vendor/package/](../vendor/package/) (gitignored; inventory and
licence classes in [../vendor/README.md](../vendor/README.md)). The schematic sheets re-encoded
from it are in [schematics/](schematics/README.md) (local-only images, index at the bottom of this
file).

## SoCs

| Item | Finding | How confirmed |
|---|---|---|
| Main SoC | ESP32-P4, dual-core RISC-V, **no radio at all** | esptool + boot log |
| **Chip revision** | **v1.3** (`Chip is ESP32-P4 (revision v1.3)`) | esptool during flash |
| PSRAM | **32 MB** octal, runs at 200 MHz with XIP | boot log `Found 32MB PSRAM device` |
| Flash | 16 MB QIO, **Boya** flash chip (generic driver works; `CONFIG_SPI_FLASH_SUPPORT_BOYA_CHIP` is optional) | boot log warning |
| Wireless SoC | ESP32-C6 running **esp_hosted slave 2.12.12** (factory-installed), capabilities: WLAN + **HCI over SDIO, BLE only** | esp_hosted handshake log |

The chip revision matters: **P4 rev v1.x and rev v3.x are binary-incompatible** (v3.0 added a
new power rail on pin 54, fixed a USB PHY power issue, and changed 50+ registers/memory
architecture). IDF 5.5 defaults to building for v3.1 and esptool refuses to flash that onto a
v1.3 chip. See [02-toolchain-and-build.md](02-toolchain-and-build.md).

## USB / flashing

- Board enumerates as a plain **USB Serial Device (COM3 on this machine)** via the P4's
  built-in USB-Serial/JTAG. No driver needed, `idf.py flash` auto-resets, no boot button dance.
- Manual download mode (only if the app breaks USB): hold **BOOT (GPIO35)** while pressing RST.
- Vendor factory-restore path: `flash_download_tool_3.9.9` + merged bin at offset 0x0, using
  the **USB2 port**, ≥600 mA supply (see `vendor/package/8-Burn operation/`; image inventory,
  esptool equivalent and backup step in [../firmware/README.md](../firmware/README.md)).

## Display — 4.3" 480x800 portrait

| Item | Value |
|---|---|
| Controller | ST7701S over **MIPI-DSI** (2 lanes) → DPI, RGB565 |
| DSI/DPI link parameters | 2 lanes @ **500 Mbps**, DPI clock **34 MHz**, hsync/hbp/hfp **12/42/42**, vsync/vbp/vfp **2/8/166** (community Arduino bring-up, cross-checks the vendored BSP) |
| Reset | GPIO5 — community bring-up pulses it low 20 ms / high 120 ms before the init table |
| Backlight | GPIO23 (MP3202 boost). Boot log prints `ledc: GPIO 23 is not usable, maybe conflict` — **benign**, backlight works. Field-proven dimming values (from EspControl): LEDC PWM at **100 Hz**, brightness capped at **80%** (`max_power: 0.8`) |
| Critical prerequisite | MIPI DPHY needs **internal LDO channel 3 @ 2500 mV** enabled before panel init — the #1 cause of a black screen. The vendored BSP does this. |

The vendored BSP's Kconfig option `CONFIG_BSP_LCD_TYPE_1024_600=y` is **repurposed by the
vendor to mean this 480x800 ST7701 panel** — keep it, despite the misleading name.

## Touch — GT911

- I2C address **0x5D** on the shared bus (SDA **GPIO7**, SCL **GPIO8**). We run the bus at
  100 kHz; EspControl runs the same bus at **400 kHz** in production, so that speed is safe.
- Detected as `TouchPad_ID: 911`, config version 250, with RST/INT left unconnected
  (`GPIO_NUM_NC`) in the working BSP path — polling mode works fine. (Community pin maps
  variously cite GPIO3 or GPIO22 for touch RST — neither is needed; don't chase them.)
- **One I2C master driver only**: touch (0x5D), ES8311 codec (0x18) and camera SCCB (0x36)
  share GPIO7/8. Creating two drivers on the same pins is a known failure mode.

## Audio — ES8311 codec

| Signal | GPIO |
|---|---|
| I2S MCLK | 13 |
| I2S BCLK/SCLK | 12 |
| I2S LRCK/WS | 10 |
| I2S DOUT (P4 → codec, playback) | 9 |
| I2S DIN (codec → P4, mic) | 48 |
| PA enable (NS4150 amp) | 11 |

- The **speaker is EXTERNAL** — MX1.25 2-pin connector, nothing on-board. No sound without it.
- Mic is a single analog mic (MSM381A3729H9CP) into the ES8311 ADC — not a PDM/I2S mic.
- 16 kHz / 16-bit mono is the rate used by `src/sample_hello_world` (also used by the vendor's
  xiaozhi port). The factory demo used 44.1 kHz stereo. Both assumed fine; **audio output is
  not yet verified end-to-end on this unit because no speaker was attached** (codec init and
  I2S writes complete without error).

## P4 ↔ C6 link (esp_hosted over SDIO)

Confirmed working configuration (WiFi and BLE both verified over it):

| Signal | GPIO |
|---|---|
| SDIO CLK | 18 |
| SDIO CMD | 19 |
| SDIO D0–D3 | 14, 15, 16, 17 |
| C6 reset (active high) | 54 |

Slot 1, 4-bit bus, 40 MHz, streaming mode both sides. The host resets the C6 on every boot
(`ESP_HOSTED_SDIO_GPIO_RESET_SLAVE=54`); the link takes ~1.7 s to come up after reset.

> Field-proven alternative: the EspControl project deliberately runs this board's SDIO at
> **20 MHz** "for reliable ESP32-C6 communication across production panels". Our unit is
> stable at 40 MHz, but if the C6 link ever misbehaves (on this or another unit), lowering
> `CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ` to 20000 is the first thing to try. More
> cross-confirmations in [07-espcontrol-findings.md](07-espcontrol-findings.md).

- **WiFi**: standard `esp_wifi_*` API via `esp_wifi_remote`. Verified: connects to a WPA2
  2.4 GHz network and gets an IP.
  - **Assumption**: the ESP32-C6 is a 2.4 GHz-only radio (WiFi 6, no 5 GHz band). A
    "_5g"-named SSID can only work if the AP broadcasts it on 2.4 GHz. Not validated on
    this board — our 5 GHz-named fallback SSID was never exercised.
- **Bluetooth**: BLE only (the C6 capability flag literally says "BLE only" — Bluetooth
  Classic is **not** available; this is confirmed, not assumed). Working setup documented
  in [04-bluetooth.md](04-bluetooth.md).
- C6 UART recovery pins are on the JP1 header (18/20/22/24 = C6 RX/TX/IO9/CHIP_PU, labeled
  `C6_U0RXD`/`C6_U0TXD`/`C6_IO9`/`C6_CHIP_PU` on schematic sheet 03) — never needed on this
  unit, **untested by us**, but a community-verified reflash recipe exists: see
  [05-troubleshooting.md §7](05-troubleshooting.md).

## Other peripherals (documented by vendor, NOT yet exercised by us)

- microSD: **SDMMC 4-bit** (not SPI) — D0-D3 = 39/40/41/42, CLK 43, CMD 44, HW 5.1 kΩ
  pull-ups on the data lines (R11/R12 on schematic sheet 01). `TF_VCC` is fed from the P4
  **on-chip LDO channel 4** through an always-on P-FET (gate held low by R13; the GPIO45
  "enable" is unpopulated/NC). Firmware must acquire LDO ch4 @ 3300 mV
  (`esp_ldo_acquire_channel`, alongside the display's ch3) or the card has no power and
  `sdmmc_init_ocr: send_op_cond returned 0x107` times out — see
  [05-troubleshooting.md §8](05-troubleshooting.md). The SDMMC bus is separate from the C6's
  SDIO, so SD + WiFi coexist. Community-verified (Arduino); **not yet exercised in this repo**.
- Camera: OV02C10 on MIPI-CSI, I2C 0x36.
- RS485: UART1, TX GPIO26, RX GPIO27 — **GPIO26 doubles as the "builtin LED" in some vendor
  code; do not blink it**, it's the RS485 driver.
- Battery ADC: ADC_UNIT_2 / CH4 / GPIO53. Working sample (curve-fitting calibration + LVGL
  gauge): `community/bigbag-JC4880P443C-examples/trimmed/examples/10_battery_adc/` (community
  PlatformIO example, no licence, see [../community/README.md](../community/README.md)). Its
  thresholds at the pin (after
  the on-board divider): **2500 mV = 100%, 2250 mV = 0%** — unvalidated by us; vendor flags
  the scaling as uncharacterized.
- UART0 console: TXD **GPIO37** / RXD **GPIO38** (broken out on the UART input header,
  sheet 02) — normally unused since flashing/monitor go over USB-Serial/JTAG.
- USB: the P4's native **High-Speed** port is the `ESP_USB` connector (the one that
  enumerates as COM3); a separate **Full-Speed** port (`USB1P1`) is on sheet 03.
- **USB host works on the HIGH-SPEED socket only - the one that does NOT give COM3.** (Corrected 2026-09-27; an earlier note here wrongly said the board cannot host.) Both sockets have 5.1 kOhm CC pulldowns and both VBUS pins sit on the shared net `USB5V_IN`, so whatever powers the board over one socket also puts 5 V on the other: bus power for a downstream device is present in practice. COM3 is USB-Serial/JTAG, which owns the PHY behind the other socket, so a device plugged in there is invisible to the host. The `src/sample_hello_world` USB-audio feature (`main/feature_usb_audio.c`) drove a Philips SPA3700 this way. Host config that works: `usb_host_config_t` with default `peripheral_map` (0 = BIT0 = the high-speed controller on the P4) and the port powered at install. Do NOT set `peripheral_map = BIT0|BIT1`: the IDF 5.5 library supports ONE root port and bit 1 wins, which moves the host onto the full-speed controller whose pins reach neither socket (serial keeps working, which is the tell). `root_port_unpowered` + manual power cycling is unnecessary and `hub_root_stop()` asserts internally.
- Power: 3V3 rail from a **TLV62569** buck; battery charge/boost via **IP5306**
  (sheet 02). Speaker amp is an **NS4150** (sheet 02), boost LED driver for the backlight
  is an **MP3202** (sheet 03).
- Reserved / hands-off GPIOs: 26/27 (RS485), 34/35 (strapping), 28-31 & 49-52 (RMII).
- Two on-chip LDO channels are spoken for: **ch3 = MIPI DSI-PHY** (2.5 V, required before
  panel init) and **ch4 = TF_VCC/microSD** (3.3 V, required before SD init).

## Schematics

The five schematic sheets (re-encoded from the PNG sheets inside the vendor `JC4880P443C_I_W.zip`)
are in [schematics/](schematics/). **The image files are local-only**: they are gitignored (Guition
copyright, no redistribution licence), so the links below resolve only on a machine that holds the
vendor package. [schematics/README.md](schematics/README.md) names the vendor source sheet for each
file and how to regenerate them.

| Sheet | Contents |
|---|---|
| [01](schematics/01-audio-es8311_sd-card_wifi-antenna.webp) | ES8311 codec · TF-card slot + TF_VCC LDO (VO4) power · C6 WiFi antenna |
| [02](schematics/02-speaker_power-3v3_uart-input_battery.webp) | NS4150 speaker amp · TLV62569 3V3 buck · UART input header · IP5306 battery |
| [03](schematics/03-expand-io_usb-fs-hs_lcd-backlight.webp) | Expand-IO header **JP1** (incl. C6 UART/boot/reset) · USB FS/HS · MP3202 backlight boost |
| [04](schematics/04-rs485-max485.webp) | MAX485 RS485 transceiver |
| [05](schematics/05-adc-mic.webp) | MSM381 analog mic (ADC/AEC) |

## External references (community bring-up of this board)

- Origin thread with schematic + vendor demo pointers:
  [r/esp32 — Guition JC4880P433 4.3" capacitive touch display](https://www.reddit.com/r/esp32/comments/1nml6fm/guition_jc4880p433_43_capacitive_touch_display)
- ST7701 manual MIPI-DSI bring-up (Arduino): [elik745i/ESP32-2432S024C-Remote](https://github.com/elik745i/ESP32-2432S024C-Remote)
- Pre-built ESP-Hosted C6 slave firmware mirror: [esphome/esp-hosted-firmware](https://github.com/esphome/esp-hosted-firmware)
- P4↔C6 RS232 + SD card reference: [buccaneer-jak/JC4880P443C-P4-to-C6-RS232-communication-and-SD-Card-connection](https://github.com/buccaneer-jak/JC4880P443C-P4-to-C6-RS232-communication-and-SD-Card-connection)
- Independent pin-map cross-check: [seobaeksol/JC4880P443C_I_W](https://github.com/seobaeksol/JC4880P443C_I_W)
- ESP-Hosted slave OTA mechanism: [espressif/esp-hosted-mcu](https://github.com/espressif/esp-hosted-mcu)
