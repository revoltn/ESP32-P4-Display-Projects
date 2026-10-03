# Guition JC8012P4A1C_I_W_Y - 10.1" 800x1280 panel with ESP32-P4 + ESP32-C6

Board card for the second Guition panel in this repository. Status on 2026-10-03: the unit
runs `bedside_clock` from this repo since 2026-10-03 (display, touch firmware, audio codec and
camera come up; picture and touch accuracy still to be confirmed by eye, WiFi not yet
configured) and its C6 runs esp_hosted slave 2.12.12 after the OTA in
`docs/c6-firmware.md`. It was factory firmware on 2026-10-02. Every
statement below names its source. Items marked [inference] are reasoned, not measured;
items marked [unverified] come from a third party and were not reproduced here. The
machine-readable profile is [board.json](board.json).

Sources used throughout: the owner's esptool session and the captured factory boot log
([logs/factory-boot-2026-10-02.txt](logs/factory-boot-2026-10-02.txt)), the vendor
specification PDF (`JC8012P4A1C_I_W Specifications-EN-v1.1`), the vendor demo package as
mirrored on GitHub ([vendor/INVENTORY.md](vendor/INVENTORY.md)), the seven schematic PNG
sheets in that package (`5-Schematic/1_PWR ... 7_CODEC`), ESPHome core
(`reference/esphome/upstream`), EspControl (`reference/espcontrol/upstream`) and the
community items listed in [community/README.md](community/README.md).

## 1. Identity and SKU

| Field | Value | Source |
|---|---|---|
| Vendor | Guition (Shenzhen Jingcai, jczn1688.com) | spec PDF footer, package README |
| Full SKU | `JC8012P4A1C_I_W_Y` | spec PDF "Product parameters" table |
| Sibling SKU | `JC8012P4A1C_I_W_Y1` = same board "With battery" | spec PDF (the only SKU decoding the vendor gives) |
| Name used by ESPHome / EspControl / xiaozhi | `JC8012P4A1`, `jc8012p4a1` | guition.py, EspControl device slug, xiaozhi board id string in the factory bin |
| PCB silkscreen | `JC8012P4A1` | vendor "Internal Interface Display" drawing (`3-Structure_Diagram/2.jpg`) |

SKU letters, best reading: `8012` = 800x1280, `P4` = ESP32-P4, `A1` = board generation,
`C` = capacitive touch fitted (the spec says the module exists "without touch or
Capacitive touch"), `I` = IPS, `W` = wireless (the ESP32-C6), `Y` = the retail touch
variant, `Y1` = with battery pack. Only `Y` vs `Y1` is stated by the vendor; the rest is
[inference] from Guition's naming across its catalogue (the 4.3" sibling is
`JC4880P443C_I_W`, also IPS with a C6). The spec PDF's own title misspells the part as
"JC8012WP4A1C_I_W".

## 2. What was verified on this unit, and how

Owner, 2026-10-02, unit on `COM4`:

| Check | Result | How |
|---|---|---|
| Chip | ESP32-P4, **silicon revision v1.3** | esptool (`Chip is ESP32-P4 (revision v1.3)`); boot log `efuse_init: Chip rev: v1.3` |
| Flash | 16 MB, QIO, Boya die | esptool; boot log `Detected boya flash chip ... flash io: qio` |
| Crystal | 40 MHz | esptool |
| USB mode on that port | USB-Serial/JTAG | esptool (`USB mode: USB-Serial/JTAG`) |
| PSRAM | 32 MB hex, AP vendor id 0x0d, 256 Mbit die, 200 MHz, XIP for .text/.rodata | boot log `hex_psram` block, `Found 32MB PSRAM device`, `Speed: 200MHz` |
| Factory app | `esp_brookesia_demo` 1.0.1, ESP-IDF v5.5.4-dirty, compiled Jul 3 2026 19:41:38 | boot log `app_init` block |
| Panel | `jd9365: version: 1.0.2`, `LCD ID: 93 65 04`, `Display resolution 800x1280` | boot log |
| Touch | GSL3680 firmware upload ~830 ms, then touch works | boot log (`start load fw` 2552 ms to `load fw success` 3384 ms); owner |
| Audio codec | ES8311 x2 instances, slave mode, I2S 16 kHz 16-bit stereo | boot log |
| Camera | OV02C10 `PID=0x5602`, `app_video` 1920x1080 MIPI-CSI | boot log |
| microSD | mount fails: `sdmmc_init_ocr: send_op_cond (1) returned 0x107` | boot log |
| C6 link | SDIO slot 1, 4-bit, 40 MHz, `Identified slave [esp32c6]`, WLAN + BLE-only HCI, **slave 2.3.0 vs host 2.11.0 mismatch warning**, `Host BT Support: Disabled` | boot log |
| Console pins | `GPIO 38 and 37 are used as console UART I/O pins` | boot log |

Not yet done on this unit: reading the rear label (see [docs/revisions.md](docs/revisions.md)),
a full flash backup, hearing audio, connecting WiFi, any custom build.

## 3. Specification

| Item | Value | Source |
|---|---|---|
| Display | 10.1" IPS TFT, 800x1280 portrait-native, JD9365 driver IC over MIPI-DSI (2 data lanes), "24 BIT RGB 16.7M color" | spec PDF; vendor BSP |
| Active area / module | 216.58 x 135.36 mm / 242.80 x 158.70 mm | spec PDF; `3-Structure_Diagram/1.jpg` |
| Panel glass | BOE 10.1" 800xRGBx1280 per the mechanical drawing; the init file names an HKC QP101BS01-1 | `4-Driver_IC_Data_Sheet/1.png` and the `.txt` init file (two panel makers named in one package) |
| Backlight | MP3202 boost, PWM enable on GPIO23 | schematic sheet 2_LCD; BSP |
| Touch | Silead GSL3680 capacitive, I2C 0x40, firmware uploaded by the host at every boot, INT on GPIO21 | BSP driver; boot log |
| Main SoC | ESP32-P4 (QFN104 on the schematic), rev v1.3 on this unit, factory app runs the CPU at 360 MHz | schematic sheet 3; esptool; boot log `cpu freq: 360000000 Hz` |
| Memory | 32 MB hex PSRAM at 200 MHz; 16 MB QIO flash (schematic symbol W25Q128, Boya die detected) | boot log; schematic sheet 3 |
| Wireless | ESP32-C6 module, esp_hosted slave over SDIO (WiFi + BLE only), factory slave 2.3.0 | boot log; schematic sheet 5 |
| Audio | ES8311 codec, NS4150 class-D amp (PA enable GPIO20), one analog mic MSM381A3729H9CP, speaker connector CN3 | schematic sheet 7; BSP |
| Camera | OV02C10 on a 15-pin 0.3 mm MIPI-CSI FPC (FPC5), 2 lanes | boot log; schematic sheet 4 |
| Storage | microSD, SDMMC 4-bit, powered from on-chip LDO channel 4 | schematic sheets 3/4; BSP |
| RTC | Epson RX8025T-UC on the I2C bus, CR1220 holder BT1, BAT54C diode-OR with 3V3 | schematic sheet 4; HA thread scan shows 0x32 |
| Battery | IP5306 charger/boost, connector CN1, push button SW1 on the IP5306 KEY pin, sense divider 68k/100k into GPIO52 | schematic sheet 1; HA thread |
| Power | 5 V in, "About 700mA"; TLV62569 bucks for 3V3 and the P4 core (1V2 rail labelled ESP_VDD_HP) | spec PDF; schematic sheet 1 |
| Environment | operating -20..70 C, storage -30..80 C, about 550 g | spec PDF |
| Vendor frameworks | Arduino, ESP-IDF, MicroPython (claimed) | spec PDF |

## 4. Pin map

Sheet numbers refer to `vendor/package/5-Schematic/N_*.png`. "BSP" is the vendor-patched
esp-bsp `esp32_p4_function_ev_board` 4.1.1 header `include/bsp/esp32_p4_function_ev_board.h`.

| Function | GPIO | Sources | Notes |
|---|---|---|---|
| I2C SDA / SCL | 7 / 8 | BSP; sheet 7 net table (`ES_I2C_SDA = GPIO7`, `ES_I2C_SCL = GPIO8`); ESPHome yaml; EspControl | One bus for ES8311 (0x18), RX8025T (0x32), OV02C10 (0x36), GSL3680 (0x40) [addresses: HA thread config + BSP]. Hardware pull-ups R53/R54 5.1k and R43/R44 2.2k (sheet 4): run 400 kHz, internal pull-ups off |
| LCD reset | 27 | BSP; sheet 2 (`GPIO27 LCD_RST`); ESPHome; EspControl | FPC1 pin 27 |
| Backlight PWM | 23 | BSP (LEDC 20 kHz, 10-bit); sheet 3 `LCD_PWM`; sheet 2 MP3202 EN; boot log | `ledc: GPIO 23 is not usable` warning is benign (same on the 4.3") |
| Touch INT / RST | 21 / 22 | BSP; sheet 3; ESPHome; EspControl | INT is wired (unlike the 4.3" GT911) |
| I2S MCLK / SCLK / LRCK | 13 / 12 / 10 | BSP; sheet 7 net table | |
| I2S DOUT (P4 to codec) / DIN (codec to P4) | 9 / 11 | BSP (`BSP_I2S_DOUT`, `BSP_I2S_DSIN`); sheet 7 (`CODEC_I2S0_DSDIN = GPIO9`, `ES7210_SDOUT = GPIO11`) | Net names mention an ES7210 but none is fitted; only ES8311 U10 |
| Power amp enable | 20 | BSP `BSP_POWER_AMP_IO`; sheet 3 `PA_CTRL`; sheet 7 NS4150 | |
| microSD D0-D3 / CMD / CLK | 39,40,41,42 / 44 / 43 | BSP (slot 0, 4-bit, `SDMMC_FREQ_HIGHSPEED`); sheets 3 and 4 | 5.1k pull-ups R47-R52; card VDD = `ESP_LDO_VO4` |
| C6 SDIO CLK / CMD / D0-D3 | 18 / 19 / 14,15,16,17 | boot log `sdio_wrapper`; sheet 3 `SD2_*`; sheet 5 (C6 IO19 CLK, IO18 CMD, IO20-IO23 D0-D3); ESPHome; EspControl | 51k pull-ups on the C6 side |
| C6 reset | 54 | boot log `Reset slave using GPIO[54]`; sheets 3/5 (`C6_CHIP_PU` = C6 EN, 10k pull-up, 1 uF) | ESPHome/EspControl: `active_high: true` |
| Spare P4-C6 line | 6 | sheet 3 (`C6_IO2`) and sheet 5 | Purpose unknown, unused by every config seen [unverified] |
| UART0 TX / RX | 37 / 38 | boot log; sheet 3; sheet 6 (CH340C); sheet 4 (CN2) | Strapping pins; reach both the CH340 socket and CN2 |
| Battery sense | 52 | sheet 1 (R2 68k from BAT+, R6 100k to GND, R4 0R); EspControl `battery.yaml` (x1.68); profi-max README | ADC; factory firmware does not log it |
| WS2812 LED data | 26 | sheet 3 `WS2812_DAT`; sheet 4 LED1 | Whether LED1 is fitted: [unverified] |
| BOOT strapping | 35 | sheet 3 `BOOTMODE`, SW3, 10k pull-up | RST = `CHIP_PU` via SW2; the CH340's DTR/RTS also drive both (sheet 6) |
| GPIO36 | - | sheet 3 (10k pull-up R34) | Strapping pin, no function found |
| Expansion FPC3 | 2, 3, 4, 5, 45, 46, 47, 48 + I2C | sheet 4 | 16-pin FPC; fitted on the retail board: [unverified] |
| Expansion FPC4 | 28, 29, 30, 31, 32, 33, 34 | sheet 4 | 15-pin FPC; same caveat |
| Expansion CN4 | I2C + 3V3 + GND | sheet 4 | 4-pin |
| USB full-speed PHY | 24 / 25 (`USB1P1_N/P`) | sheets 3 and 6 | Goes to the socket drawn as USB2 |
| USB high-speed PHY (USB-Serial/JTAG) | dedicated DM/DP (`ESP_USB_N/P`) | sheets 3 and 6 | Goes to the socket drawn as USB3; the port that appeared as COM4 |
| Camera control lines | `CSI_IO0` (10k pull-up), `CSI_IO1` | sheet 4 | P4 GPIO numbers not legible on sheet 3 [to be confirmed] |
| On-chip LDO VO1 / VO2 / VO3 / VO4 | - | sheet 3 (VO1 -> VDD_FLASH via R38, VO2 -> VDDPST, VO3 -> VDD_MIPI_DPHY, VO4 -> TF card and camera FPC); BSP (ch3 2500 mV; SD ch4) | Firmware must acquire ch3 before DSI init and ch4 before SD init |

## 5. Hardware revisions (short)

Three revisions are known (EspControl, live docs page, 2026-10-02): **V1** rear case date
code `2627` or lower, **V2** `2628` or higher ("some units of this model have a different
LCD panel but still use the same JD9365 driver chip", ESPHome `guition.py`), and **V3** =
units with ESP32-P4 **v3.x production silicon** (EspControl issue #1939: `Chip is ESP32-P4
(revision v3.2)`). V1 vs V2 changes the DSI timing and the panel init table; V3 changes the
build target entirely (v1.x and v3.x P4 silicon are binary-incompatible, see the 4.3" notes).

This unit: silicon v1.3, so not V3. The rear label has **not been read yet**; the factory
image was compiled on 2026-07-03, ISO week 27 of 2026, right at the V1/V2 boundary if the
`26xx` codes are year-week date codes (EspControl calls them "case dates") [inference].
Full matrix and how to tell: [docs/revisions.md](docs/revisions.md).

## 6. Interfaces and connectors

Numbers in parentheses are the callouts in the vendor's "Internal Interface Display" drawing.

- **USB-C sockets.** Schematic sheet 6 and the vendor drawing (5) show **three** Type-C
  sockets, all with 5.1k CC pull-downs and VBUS on the shared `USB5V_IN` net:
  - `USB1`: CH340C USB-UART bridge to P4 UART0 (GPIO37/38) with DTR/RTS driving
    BOOTMODE and CHIP_PU. This is the port the vendor's flashing note means ("install the
    CH340 driver", "Choose USB1 port for burning"). Shows up as a "USB-SERIAL CH340" port.
  - `USB2`: the P4's full-speed USB PHY (`USB1P1`). Not a console.
  - `USB3`: the P4's high-speed USB PHY, i.e. the built-in USB-Serial/JTAG. This is the one
    that enumerated as COM4 in USB-Serial/JTAG mode. Also the only native USB host port.
  Which silkscreen label sits on which physical socket is [to be confirmed] on the unit.
- **UART / power header CN2** (10): MX1.25 4-pin `VIN, TX0, RX0, GND`; TX0/RX0 are UART0
  through 0R links, VIN feeds `USB5V_IN` through a P-FET (sheet 4), so it is also a 5 V input.
- **C6 programming header CN5** (sheet 5): 6 pins `3V3, GND, C6_U0TXD, C6_U0RXD, C6_IO9,
  C6_CHIP_PU`. Likely the 2x3 header beside the C6 module in the drawing [to be confirmed].
  See [docs/c6-firmware.md](docs/c6-firmware.md).
- **TF card holder** (6): SDMMC 4-bit on GPIO39-44, powered by LDO channel 4.
- **Battery connector CN1** (4) + IP5306 + SW1 power key; the Y1 SKU ships with a cell.
- **RTC battery holder BT1** (3): CR1220 for the RX8025T.
- **LCD FPC1** 31-pin 0.3 mm (7), **touch FPC2** 6-pin (8), **camera FPC5** 15-pin (9).
- **Speaker connector CN3** (11) and the analog mic (12). The retail unit is reported to ship
  with a speaker fitted (owner's brief); the schematic shows a connector, so it is a cabled
  part inside the housing [inference]. No sound has been heard in this repo yet.
- **Buttons**: SW2 reset (`CHIP_PU`), SW3 BOOT (GPIO35), SW1 IP5306 KEY (sheet 1/3; HA thread).

## 7. Differences from the 4.3" JC4880P443C

Reference: [../guition-jc4880p443c/docs/01-hardware.md](../guition-jc4880p443c/docs/01-hardware.md).

| Item | JC8012P4A1C (this board) | JC4880P443C (4.3") |
|---|---|---|
| Panel | JD9365, 800x1280, DPI 60 MHz, 1.5 Gbps/lane (vendor BSP) | ST7701S, 480x800, DPI 34 MHz, 500 Mbps/lane |
| LCD reset | GPIO27 | GPIO5 |
| Touch | GSL3680 at 0x40, host uploads firmware, INT wired (GPIO21), RST GPIO22 | GT911 at 0x5D, polled, RST/INT unconnected |
| PA enable | GPIO20 | GPIO11 |
| I2S DIN | GPIO11 | GPIO48 |
| Speaker | connector CN3, speaker reported fitted | external MX1.25, nothing on board |
| Battery ADC | GPIO52 (68k/100k) | GPIO53 |
| RTC | RX8025T + CR1220 holder | none |
| Console USB | CH340C socket in addition to the native USB-Serial/JTAG socket | native USB-Serial/JTAG only |
| C6 slave firmware | 2.3.0 (mismatch warning) | 2.12.12 |
| Touch driver licence | GPL-2.0-or-later plus an unlicensed firmware blob: 10.1" firmware is source-only (see [docs/touch-gsl3680.md](docs/touch-gsl3680.md)) | Apache-2.0 GT911 driver, binaries distributable |
| Same | I2C GPIO7/8; SDIO 14-19 + reset 54; backlight 23; UART0 37/38; LDO ch3 DPHY, ch4 SD; OV02C10; ES8311; P4 rev v1.3; 16 MB + 32 MB | |

## 8. Status

| Area | Factory firmware (observed in the boot log) | This repo |
|---|---|---|
| Display | initialised, 800x1280, backlight 95 % | no custom build yet |
| Touch | firmware loaded, works (owner) | driver choice and licence: [docs/touch-gsl3680.md](docs/touch-gsl3680.md) |
| Audio | ES8311 initialised, MP3s listed from SPIFFS | not heard yet |
| Camera | OV02C10 detected, 1920x1080 pipeline up | untested |
| microSD | mount failed (0x107) | expected fix = LDO ch4 (4.3" precedent) [untested here] |
| WiFi | station started, no connection attempted | untested |
| Bluetooth | host BT disabled in the factory build | untested |
| C6 firmware | 2.3.0, too old for a 2.12.x host | upgrade plan: [docs/c6-firmware.md](docs/c6-firmware.md), nothing done yet |
| Backup of the factory image | - | **not yet taken**; do it first ([firmware/README.md](firmware/README.md)) |
| bedside_clock port | first boot 2026-10-03, see `docs/bring-up-notes.md` section 9 | plan and re-sequencing: [../../docs/architecture/board-profiles-plan.md](../../docs/architecture/board-profiles-plan.md); the 10.1" build will be source-only because of the touch driver licence |

## 9. Folder map

```
devices/guition-jc8012p4a1c/
  README.md                       this card
  board.json                      machine-readable board profile (maintained by the owner)
  docs/bring-up-notes.md          ports, boot-log anatomy, DSI timing, LDO, PSRAM, rotation, gotchas
  docs/touch-gsl3680.md           controller facts, boot sequence, transforms, driver and licence options
  docs/c6-firmware.md             the 2.3.0 slave problem and the three upgrade paths
  docs/revisions.md               V1 / V2 / V3 matrix and how to identify a unit
  firmware/README.md              factory and C6 image inventory, flashing and backup recipes (no binaries hosted)
  vendor/README.md                where the vendor package comes from and what is inside
  vendor/INVENTORY.md             file-level inventory with licence classification and SHA256s
  vendor/manifest.json            shard manifest of the live vendor zip
  vendor/fetch_vendor_zip.sh      sharded downloader (see vendor/README.md for the manifest name it expects)
  vendor/package/                 the mirrored vendor package (gitignored)
  community/README.md             third-party repos and threads for this board
  community/p1ngb4ck-*/           schematics + ESPHome yaml mirror (gitignored)
  logs/factory-boot-2026-10-02.txt  factory boot log, MAC redacted
```

## 10. Links

- Vendor download page: <https://www.guition.com/-download> (spec PDF) and the package
  downloader <https://pan.jczn1688.com/directlink/1/HMI%20display/JC8012P4A1C_I_W_Y.zip>
  (a JavaScript downloader, not a file; see [vendor/README.md](vendor/README.md))
- GitHub mirror of the package: <https://github.com/DevinWatson/10.1-inch-ESP32P4-Xiaozhi-ESP32-C6-JC8012P4A1C_I_W_Y>
- Schematics and ESPHome example: <https://github.com/p1ngb4ck/unofficial_guition_esp32p4_repo>
- EspControl device page: <https://jtenniswood.github.io/espcontrol/screens/jc8012p4a1>
- ESPHome panel models: `esphome/components/mipi_dsi/models/guition.py`; touch: `esphome/components/gsl3670/`
- Registry touch driver: <https://components.espressif.com/components/mangoo1/esp_lcd_touch_gsl3680>
- HA community thread: <https://community.home-assistant.io/t/jc8012p4a1-guition-esp32-p4-esp32-c6/939971>
- Licensing rules for this repo: [../../docs/licensing.md](../../docs/licensing.md)
- Repo-wide reliability checklist: [../../docs/reliability-checklist.md](../../docs/reliability-checklist.md)
