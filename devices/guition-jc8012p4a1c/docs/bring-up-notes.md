# Bring-up notes - JC8012P4A1C_I_W_Y

What to expect when you first plug the board in, how to read its factory boot log, and the
numbers a first custom build needs. Nothing in this file has been built or flashed yet on
this unit (2026-10-02); the "expected" items are marked as such and carry their source.

## 1. Talking to it

Three USB-C sockets exist on the schematic (sheet `6_USB.png`) and in the vendor's
"Internal Interface Display" drawing:

| Socket (schematic name) | What it is | How it shows up |
|---|---|---|
| `USB1` | CH340C USB-UART on P4 UART0 (GPIO37 TX / GPIO38 RX), DTR/RTS wired to BOOTMODE and CHIP_PU | "USB-SERIAL CH340 (COMx)"; needs the WCH driver (the package ships `USB-SERIAL CH340.rar`); esptool reports `USB mode: UART` [expected] |
| `USB2` | P4 full-speed USB PHY (`USB1P1`) | nothing, unless your firmware runs a USB device stack on it |
| `USB3` | P4 high-speed USB PHY = built-in USB-Serial/JTAG | "USB Serial Device (COMx)", no driver; esptool reports `USB mode: USB-Serial/JTAG` |

The owner's COM4 reported USB-Serial/JTAG, so it was `USB3`. The vendor's burning note says
to use `USB1`; both can flash. The CH340 path auto-enters download mode via DTR/RTS; the
native path auto-resets through the USB-Serial/JTAG peripheral. Manual download mode: hold
BOOT (SW3, GPIO35) while pressing RST (SW2), as on the 4.3".

Expected esptool output (owner's session, 2026-10-02, values confirmed by the boot log):

```
esptool.py -p COM4 chip_id
  Chip is ESP32-P4 (revision v1.3)
  Crystal is 40MHz
  USB mode: USB-Serial/JTAG
esptool.py -p COM4 flash_id
  Detected flash size: 16MB
```

The vendor's flash tool uses 1152000 baud; esptool at `-b 1152000` works on the native port
of the 4.3" sibling, so expect the same here [inference]. Serial monitor: 115200 baud.

Finding the port on Windows without guessing: `Get-PnpDevice -Class Ports | Select
FriendlyName, InstanceId`; the native port's InstanceId contains `VID_303A&PID_1001`
(Espressif USB-Serial/JTAG), the CH340's contains `VID_1A86`.

## 2. Reading the factory boot log

[`../logs/factory-boot-2026-10-02.txt`](../logs/factory-boot-2026-10-02.txt), block by block:

1. `esp_image: segment ...` then `Loaded app from partition at offset 0x10000`: a single
   factory app at 0x10000, 1.2 MB of mapped code. Matches the `partitions.csv` of the
   `esp_brookesia_phone` demo (nvs, phy_init, factory 9M, storage 4M).
2. `hex_psram` block: AP vendor, 256 Mbit, X16, then `Found 32MB PSRAM device`, `Speed:
   200MHz`, `.rodata xip on psram`, `.text xip on psram`. The build runs code from PSRAM
   (`CONFIG_SPIRAM_XIP_FROM_PSRAM=y` in the demo's `sdkconfig.defaults`).
3. `cpu_start`: `GPIO 38 and 37 are used as console UART I/O pins`, `cpu freq: 360000000 Hz`.
   The factory build runs at 360 MHz, not 400.
4. `app_init`: project `esp_brookesia_demo`, version `1.0.1`, compiled `Jul 3 2026`,
   `ESP-IDF: v5.5.4-dirty`. This is **not** one of the four binaries in the vendor package
   (the packaged P4 image was built Jun 20 2025 with IDF v5.5-dev), so the package cannot
   restore this unit byte-for-byte. Back the flash up first.
5. `efuse_init: Min chip rev v1.0, Max chip rev v1.99, Chip rev v1.3`: the image was built
   for rev 1.x silicon (`CONFIG_ESP32P4_REV_MIN_1=y` in the demo's sdkconfig). A build for
   v3.x silicon would refuse to flash here and vice versa.
6. `heap_init`: internal pools 79 KiB RETENT_RAM + 18 KiB + 256 KiB + 31 KiB RTCRAM + 7 KiB
   TCM; then `Adding pool of 27520K of PSRAM memory`: **27.5 MB of PSRAM heap** remains
   after XIP in this build.
7. `spi_flash: Detected boya flash chip but using generic driver`: benign; optional
   `CONFIG_SPI_FLASH_SUPPORT_BOYA_CHIP=y`.
8. `ESP32_P4_EV: Partition size: total: 3848081, used: 3630464`, `SPIFFS mount
   successfully`: the 4 MB storage partition holds the demo's music and images.
9. `W ldo: The voltage value 0 is out of the recommended range [500, 2700]` then
   `sdmmc_init_ocr: send_op_cond (1) returned 0x107` and `SD card mount failed`: the card
   never powers up. Same error, same LDO-channel-4 cause as on the 4.3" (section 7).
10. `W i2c.master: Please check pull-up resistances`: benign; the bus has 5.1k and 2.2k
    hardware pull-ups (schematic sheet 4). `ES8311: Work in Slave mode` twice (speaker and
    mic instances), `I2S_IF ... sample_rate:16000` stereo 16-bit.
11. `LVGL: Starting LVGL task`, `W ledc: GPIO 23 is not usable` (benign, backlight works),
    `MIPI DSI PHY Powered on` (LDO ch3), `Install ILI9881C LCD control panel` (stale BSP
    string; the code installs the JD9365), `jd9365: version: 1.0.2`, `LCD ID: 93 65 04`,
    `Display resolution 800x1280`, `E lcd_panel: esp_lcd_panel_swap_xy(50): swap_xy is not
    supported by this panel` (benign: DPI panels have no hardware rotation).
12. `gsl3680` block: `W Unable to initialize the I2C address` (benign, see
    [touch-gsl3680.md](touch-gsl3680.md)), `start load fw` at 2552 ms, `load fw success`
    at 3384 ms, `startup_chip failed read 0xb0 = 5a,5a,5a,5a` (benign: 0x5a5a5a5a is the
    success value, the driver logs "failed" unconditionally). Then `Setting LCD backlight:
    100%`.
13. esp-brookesia 0.4.1 UI, `file_iterator` lists the SPIFFS media, `EUI_Setting` restores
    `ble_en: 0`, `brightness: 95`, `volume: 60`, `wifi_en: 1`.
14. `ov02c10: Detected Camera sensor PID=0x5602`, `app_video: width=1920 height=1080`,
    `driver: MIPI-CSI`. The camera is alive in the factory build.
15. esp_hosted handshake: `sdio_wrapper: Slot 1, 4-bit, 40000 KHz, CLK[18] CMD[19] D0[14]
    D1[15] D2[16] D3[17] Slave_Reset[54]`, `Identified slave [esp32c6]`, `capabilities:
    0xd` = WLAN + HCI over SDIO, BLE only, **`Version mismatch: Host [2.11.0] > Co-proc
    [2.3.0] ==> Upgrade co-proc to avoid RPC timeouts`**, `Host BT Support: Disabled`,
    `wifi station started`, and a final `W rpc_rsp: Hosted RPC_Resp [0x21a] ... resp code
    [12298]` (a failed RPC, most likely a WiFi call with no stored credentials [inference]).
    What to do about the 2.3.0 slave: [c6-firmware.md](c6-firmware.md).

## 3. DSI / DPI timing

All known timing sets for this panel. "Frame rate" is arithmetic: DPI clock / ((800 + HSYNC
+ HBP + HFP) x (1280 + VSYNC + VBP + VFP)).

| Source | Lanes | Lane rate | DPI clock | HSYNC pw / bp / fp | VSYNC pw / bp / fp | Frame rate | Init table |
|---|---|---|---|---|---|---|---|
| Vendor BSP 4.1.1, `esp32_p4_function_ev_board.c` + `JD9365_800_1280_PANEL_60HZ_DPI_CONFIG` | 2 | **1500 Mbps** (hard-coded in `esp_lcd_dsi_bus_config_t`) | **60 MHz** | 20 / 20 / 40 | 4 / 8 / 20 | 52.0 Hz | esp_lcd_jd9365 1.0.2 default, 198 entries |
| Vendor `ESP-IDF_lvgl_sw_rotation` demo | 2 | same macro family | 63 MHz | 20 / 20 / 40 | 4 / 8 / 20 | 54.6 Hz | same 198-entry `lcd_cmd[]` |
| Panel-maker init file `4-Driver_IC_Data_Sheet/C-JD9365DA-H3_HKC10.1_...(2lane).txt` | 2 | `PLL_CLOCK=416` (MediaTek LCM notation; roughly 832 Mbps/lane [inference]) | - | 20 / 20 / 40 | 4 / 8 / 20 | - | origin of the 198-entry table |
| ESPHome `JC8012P4A1` (`mipi_dsi/models/guition.py`) | 2 | 1 Gbps | 60 MHz | 20 / 20 / 40 | 4 / 8 / 20 | 52.0 Hz | 193 lines, reset GPIO27 |
| ESPHome `JC8012P4A1-V2` | 2 | 1500 Mbps | 70 MHz | 20 / 20 / 40 | 4 / 10 / 20 | 60.5 Hz | a different 200-line table |
| EspControl V1 `device.yaml` | = ESPHome `JC8012P4A1` | | | | | | |
| EspControl V2 `device.yaml` (`model: CUSTOM`) | 2 | 840 Mbps | 70 MHz | 20 / 20 / 40 | 4 / 10 / 30 | 60.1 Hz | its own `init_sequence` |
| Unused macros in the BSP header (`display.h`: HBP 140, VBP/VFP 16, 1000 Mbps) | dead code, not programmed (INVENTORY 4.2) | | | | | | |

Bandwidth sanity (arithmetic): RGB565 at 60 MHz DPI = 960 Mbps of pixel data, 480 Mbps per
lane before DSI overhead, so 1 Gbps/lane has about 2x headroom and 1.5 Gbps about 3x. At
70 MHz the figure is 560 Mbps/lane; EspControl ships 840 Mbps/lane for V2 panels.

The vendor's "60HZ" macro name does not describe the result: 60 MHz with these porches is
52 Hz. A 60 Hz portrait refresh would need about 69 MHz with the V1 porches [arithmetic].

Recommendation for the first build on this unit: **start from the vendor BSP values**
(2 lanes, 1500 Mbps, 60 MHz, 20/20/40, 4/8/20, RGB565, 2 frame buffers, `use_dma2d`),
because that is what the factory image running on this very panel is built from (same BSP,
same `jd9365 1.0.2`), and keep the ESPHome `JC8012P4A1` set (1 Gbps) as the first thing to
try if the link is unstable. Only if the picture stays black or scrolls with those two sets
is a V2 panel plausible; then try the `JC8012P4A1-V2` table at 70 MHz (see
[revisions.md](revisions.md)).

Related failure signature: esp-brookesia issue #72 (`AEGHB-1341`) reports this exact
panel (`JC8012P4A1C_I_W_Y1`) staying black under stock Espressif examples with
`lcd.dsi.dpi: can't fetch data fast enough, underrun happens` while the factory image
works. The issue is still open without a posted fix (fetched 2026-10-02). The usual causes
of a DPI underrun are frame buffers the DMA cannot read fast enough (PSRAM bandwidth shared
with XIP, cache settings) or a lane rate / DPI clock pair the panel rejects [inference];
the factory build's answer is `CONFIG_CACHE_L2_CACHE_256KB`, 200 MHz PSRAM,
`CONFIG_BSP_LCD_DPI_BUFFER_NUMS=2`, `CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR=y` and
`CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE=y` (demo `sdkconfig.defaults`).

## 4. LDO channel prerequisites

The P4's four on-chip LDO outputs are all used (schematic sheet 3; BSP):

| Channel | Powers | Who sets it | Symptom if missing |
|---|---|---|---|
| VO1 | `VDD_FLASH` (via R38 0R) | ROM / `CONFIG_ESP_LDO_RESERVE_SPI_NOR_FLASH=y` (set in the demo sdkconfig) | board does not boot; never touch it |
| VO2 | `VDDPST` (an I/O power domain pin) | hardware default | do not acquire it in firmware |
| VO3 | `VDD_MIPI_DPHY` | firmware, **2500 mV before DSI bus creation** (`bsp_enable_dsi_phy_power`, `esp_ldo_acquire_channel` ch3) | black screen with backlight on, the number one bring-up failure on the 4.3" |
| VO4 | TF card VDD and the camera FPC5 supply | firmware, **3300 mV before `esp_vfs_fat_sdmmc_mount`** (BSP uses `sd_pwr_ctrl_new_on_chip_ldo` with `ldo_chan_id = 4`) | `sdmmc_init_ocr: send_op_cond returned 0x107`, exactly what the factory log shows |

Only one owner per channel: the 4.3" notes record `esp_ldo_acquire_channel ... already in
use` when the SD driver and the display both tried to own a channel. VO4 also feeds the
camera connector on this board (sheet 4), so a build that powers the camera has already
enabled the card's supply [inference from the schematic; untested].

## 5. PSRAM budget at 800x1280

Arithmetic, with the factory build's measured 27,520 KB PSRAM heap pool as the ceiling:

| Buffer | Bytes | Note |
|---|---|---|
| One RGB565 frame buffer | 800 x 1280 x 2 = 2,048,000 (1.95 MiB) | `num_fbs = 1` default in the JD9365 macro |
| Two frame buffers (factory build, `CONFIG_BSP_LCD_DPI_BUFFER_NUMS=2`) | 4,096,000 (3.9 MiB) | needed for avoid-tear / direct mode |
| Three (max selectable) | 6,144,000 | |
| One RGB888 frame buffer (Kconfig option) | 3,072,000 | triples the DSI payload too |
| BSP default LVGL draw buffer `BSP_LCD_DRAW_BUFF_SIZE` = 800 x 80 px | 128,000 | single buffer by default |
| Two full-frame LVGL draw buffers | 4,096,000 | only if you choose full-refresh software rotation |
| Camera 1920 x 1080 at 16 bpp (factory `app_video` size) | 4,147,200 per buffer | esp_video wants at least 2 |
| Camera at the 4.3" bedside capture size 1288 x 728, 16 bpp | 1,875,328 per buffer | |

So display (2 FBs) + a 2-buffer full-HD camera pipeline is about 12.4 MB before LVGL,
fonts, images, audio and TLS buffers; the 27.5 MB pool covers it, the 32 MB label does
not mean 32 MB is free. Internal RAM is the scarcer resource: 256 KiB plus small pools,
and DMA descriptors (I2S, SDIO, DSI) must live there (4.3" troubleshooting section 10).

## 6. Rotation to landscape

The panel is portrait-native and the DPI path cannot rotate: the factory log prints
`swap_xy is not supported by this panel`, and the HA thread quotes the ESPHome maintainer:
"No DSI or RGB controllers support hardware rotation (since the driver chip does not host
the display buffer.)" Options, each with its source:

1. **LVGL software rotation** (`lv_display_set_rotation`, or `lvgl: rotation: 90` in
   ESPHome). EspControl ships the 10.1" this way (1280x800 layout, `rotation: 90`) and a
   HA-thread user calls software rotation laggy; both can be true depending on the redraw
   area [inference]. Cheapest to try.
2. **PPA (2D hardware) rotation of the dirty area or the whole frame**. The vendor's
   `ESP-IDF_lvgl_sw_rotation` demo does this (`CONFIG_EXAMPLE_LVGL_PORT_PPA_ROTATION_ENABLE=y`,
   `ROTATION_90`, avoid-tear) with a CPU transpose fallback (`rotate_image()`), and
   profi-max's BSP advertises "LVGL9.x integration with PPA hardware acceleration".
   Caveat from this repo's 4.3" work: on rev v1.3 silicon a PPA scale-rotate call was
   observed to block indefinitely (owner's bring-up notes, 2026-09-06, confirmed over
   JTAG), which is why bedside_clock's display port uses a bounded PPA wait with a CPU
   fallback. Treat every PPA call as something that can hang and time it out.
3. **Rotate in the application only** (draw a landscape UI into the portrait buffer).
   Fine for a fixed layout such as a clock face; not what LVGL widgets expect.

Touch has to be rotated together with the image; the transforms differ per driver, see
[touch-gsl3680.md](touch-gsl3680.md).

## 7. Gotchas carried over from the 4.3" (expected to apply, not yet reproduced here)

Source for each: [../../guition-jc4880p443c/docs/05-troubleshooting.md](../../guition-jc4880p443c/docs/05-troubleshooting.md)
unless stated.

| Symptom | Cause | Fix |
|---|---|---|
| esptool refuses to flash: "requires chip revision in range v3.1-v3.99" | IDF 5.5 defaults to rev 3.x silicon; this unit is v1.3 | `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` and `CONFIG_ESP32P4_REV_MIN_100=y` (bedside_clock `sdkconfig.defaults`); ESPHome: `engineering_sample: true` (EspControl does this for the 10.1") |
| Backlight on, picture black | LDO ch3 not at 2.5 V before DSI init | section 4 |
| `sdmmc_init_ocr: send_op_cond returned 0x107` | LDO ch4 not at 3.3 V; or SPI-mode SD with guessed pins | section 4; use SDMMC 4-bit on 39-44 |
| HTTPS streams die with `SSL - Verification of the message MAC failed (-0x7180)` | hardware AES corrupts TLS records held in PSRAM on this silicon (4.3" doc 11) | `CONFIG_MBEDTLS_HARDWARE_AES=n` |
| WiFi associates then drops in an `ASSOC_LEAVE` loop; BT HCI timeouts | esp_hosted host/slave version mismatch; this unit ships slave 2.3.0 | [c6-firmware.md](c6-firmware.md) |
| `i2s_alloc_dma_desc ... allocate DMA buffer failed` then a load access fault | internal RAM exhausted | keep large buffers in PSRAM (`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`, hosted task stacks in SPIRAM) |
| C6 link flaky | SDIO clock | the factory build runs 40 MHz; bedside_clock on the 4.3" runs 20 MHz (`CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ=20000`), EspControl runs 40 MHz on this board |
| Benign log lines | `ledc: GPIO 23 is not usable`, `swap_xy is not supported`, `Install ILI9881C LCD control panel`, `boya flash chip`, `i2c.master: Please check pull-up`, `gsl3680: Unable to initialize the I2C address`, `startup_chip failed read 0xb0 = 5a,5a,5a,5a` | do not chase |

New on this board and not covered by the 4.3" notes: the HA thread claim that the SD slot
is "unusable due to GPIO conflicts" [unverified, reported second-hand]. The schematic
shows dedicated pins 39-44 separate from the C6's 14-19, and the factory failure is a
power error, not a pin clash; treat the claim as unproven until a card mounts with LDO
ch4 enabled.

## 8. First-build checklist (proposed)

1. `esptool.py -p COMx read_flash 0 0x1000000 backup.bin` (see
   [../firmware/README.md](../firmware/README.md)).
2. Build the vendor `lvgl_demo_v9` or the repo's hello-world against the vendor BSP with the
   rev-1.x options from section 7, LDO ch3 and ch4, 2 frame buffers, software AES.
3. Confirm `jd9365: LCD ID: 93 65 04` and a picture, then touch, then the C6 handshake.
4. Read and record the rear label ([revisions.md](revisions.md)) before concluding anything
   about panel timing.

## 9. First bedside_clock build on this unit (2026-10-03)

Build: `docs/howto/build-and-flash.md` ("10.1" Guition JC8012P4A1C"). Panel revision V2
(rear label 2628 or higher). Logs: `../logs/bedside_clock-first-boot-2026-10-03.txt`,
`../logs/bedside_clock-boot-after-c6-ota-2026-10-03.txt`.

What the serial log proves (boot in about 5.5 s to "bedside_clock ready"):

| Check | Result |
|---|---|
| DSI PHY and panel | `MIPI DSI PHY Powered on`, `Install JD9365 LCD control panel (V2 table, DPI 70 MHz)`, `jd9365: version: 1.0.4` (registry resolved 1.0.4, not 1.0.2), `LCD ID: 93 65 04`, `Display initialized`. No underrun lines. The V2 table was accepted by the controller on the first try; V1 was not tried. |
| Touch | GSL3680 firmware uploaded in 0.83 s (`start load fw` at 2.15 s, `load fw success` at 2.97 s), then the expected benign `startup_chip failed read 0xb0 = 5a,5a,5a,5a`. Touch accuracy: see section 9.1. |
| Audio | ES8311 opened twice, codec devices OK, PA on GPIO20 through the shared BSP macro. Voice self-test: first boot `+2.8 dB, mic rms 58 [SUSPECT]`, second boot `+39.2 dB, mic rms 59 [SUSPECT]`. Both fail the app's `rms > 100` rule while the in-use microphone level is healthy (`mic alive: rms` between 740 and 2460). The 4.3" threshold and the 24 dB analog gain need re-tuning for this speaker and mic placement (phase 4 of the plan). |
| Camera | `ov02c10: Detected Camera sensor PID=0x5602`, `ambient light sensing running`, the backlight follows the room (14 % to 80 % within a minute). |
| Voice | microWakeWord "Hi ESP" fallback model loaded (the `hey_mr_clock` model is selected by NVS, which is fresh here); `internal heap 232755 -> 193239` after voice start. |
| Memory | Image 6,088,004 bytes against 6,042,052 for the 4.3" (about 46 KB more: JD9365 and GSL3680 drivers plus the V2 table); static D/IRAM 179,652 bytes against 172,316. |
| Faults | None: no abort, no watchdog. A stale core dump from the factory image (`Incorrect size of core dump image: 327685`) was erased with `esptool erase_region 0xc20000 0x20000`. |
| Warnings seen | `ledc: GPIO 23 is not usable, maybe conflict with others` (backlight still works), `i2c.master: Please check pull-up resistances` (the BSP enables the internal pull-ups, the vendor README recommends off), `i2s_channel_disable ... has not been enabled yet` x3 during codec bring-up. |
| Not verified | The picture itself, touch accuracy, speaker loudness, WiFi association (no credentials stored on this unit), 24 h soak, PPA fallback counter under scrolling. These need eyes and fingers on the board. |

Mic count: the AFE runs one channel (`AFE Version: (1MIC_V251128)`), and only one ES8311 ADC
path exists. The spec's "2 mics" is not usable by this firmware; `board.json`
`audio.mic_count` stays 2 as the vendor claim, the README's single `MSM381A3729H9CP` is what
the firmware sees.

### 9.1 Touch calibration (2026-10-03, same day)

Symptom on first boot: touch was "offset". Four-corner + centre tap test (top-left, top-right,
bottom-right, bottom-left, centre, landscape), raw readings logged from `process_coordinates`
(`../logs/touch-corner-test-2026-10-03.txt`, 46 taps):

- The registry driver returns **native portrait pixels** (about x 8..772, y 28..1265), not the
  controller's 880x1648 raw range that ESPHome documents; no scaling is needed.
- With the vendor BSP flags (`mirror_y=1`) every landscape tap landed **point-reflected**: top-left
  registered at bottom-right. The vendor flags belong to its own display orientation. Native
  portrait truth for this firmware is `px = 800 - x`, `py = y` before LVGL's 90 degree rotation.
- The span is slightly shorter than the glass, so a least-squares fit was applied in
  `gsl3680_calibrate_cb` (BSP `.c`): `px = 809.64 - 1.0585*x`, `py = -29.81 + 1.0434*y`, clamped,
  with esp_lcd_touch's own swap/mirror left off. Constants are for this V2 unit; other units
  may differ by a few pixels.

Verification (`../logs/touch-verify-2026-10-03.txt`, 35 taps, medians in landscape pixels):

| Target | Landed |
|---|---|
| top-left (0,0) | (0,0) |
| top-right (1279,0) | (1273,0) |
| bottom-right (1279,799) | (1244,786) |
| bottom-left (0,799) | (9,790) |
| centre (640,400) | (671,413) |

Corners are correct; the centre is about 31 px right and 13 px low. It may be finger bias or
a mild non-linearity near the middle. A proper nine-point on-screen calibration screen is the
next step if buttons feel off.
