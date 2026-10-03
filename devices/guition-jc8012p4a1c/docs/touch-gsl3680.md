# Touch: Silead GSL3680 on the JC8012P4A1C

The 10.1" uses a Silead GSL3680 capacitive controller, not the Goodix GT911 of the 4.3".
The practical differences: the host must upload the controller firmware over I2C at every
boot, the point-tracking algorithm runs on the host, the interrupt line is wired, and the
only driver code that exists is GPL-licensed or unlicensed. Sources: the vendor driver
`vendor/package/1-Demo/idf-examples/common_components/esp_lcd_touch_gsl3680/`, the
factory boot log, ESPHome `components/gsl3670/touchscreen.py`, EspControl
`components/gsl3680/` and `devices/guition-esp32-p4-jc8012p4a1/device/device.yaml`, the
registry page of `mangoo1/esp_lcd_touch_gsl3680`, schematic sheets 3 and 4.

## 1. Controller facts

| Item | Value | Source |
|---|---|---|
| Part | Silead GSL3680 (the vendor firmware array is named `GSLX680_FW`, the algorithm file comes from the Linux `gslX680` driver family) | vendor driver sources |
| I2C address | **0x40** | vendor header `ESP_LCD_TOUCH_IO_I2C_GSL3680_ADDRESS (0x40)`; ESPHome `i2c_device_schema(0x40)`; EspControl; mangoo1 |
| Bus | GPIO7 SDA / GPIO8 SCL, 400 kHz, shared with ES8311, RX8025T, OV02C10 | BSP; schematic sheet 7 net table; HA thread |
| Pull-ups | 5.1k R53/R54 on the touch/RTC side of the bus | schematic sheet 4 |
| RST / INT | GPIO22 / GPIO21, both active low in the vendor driver (`levels.reset = 0`, `levels.interrupt = 0`) | BSP `bsp_touch_new`; schematic sheet 3 (`TOUCH_RST`, `TOUCH_INT`) |
| Connector | FPC2, 6 pins: `TOUCH_RST, TOUCH_INT, SCL, SDA, 3V3, GND` | schematic sheet 4 |
| Firmware | Not in the chip: the GSL3680 has no flash; the host streams a firmware image after reset | vendor driver `esp_lcd_touch_gsl3680_load_fw()`; ESPHome comment "must be uploaded every time it powers up" |
| Vendor firmware image | `GSLX680_FW[]`, 4,587 `{offset, value}` entries, build stamp "Jan 8 2018 14:24:49" decoded from the tail, plus a 90-word `gsl_config_data_id[]` | INVENTORY section 3 |
| Fingers | vendor BSP copy compiled with `MAX_FINGER_NUM 3`, the loose vendor copy with 10; EspControl decodes 5 points (`TOUCH_MAX_POINTS 5`) | INVENTORY 3.1; EspControl `gsl3680.h` |
| Raw coordinate range seen by ESPHome | x 20..880, y 20..1648 (scaled to 800x1280 by the component) | ESPHome `MODELS["GUITION-JC8012P4A1"]` |

## 2. Boot sequence and the log lines it produces

From the vendor driver (INVENTORY 4.4) and the factory boot log timestamps:

1. RST and INT configured as outputs; RST low, INT low, 10 ms, INT held low (address
   select), RST high.
2. `touch_gsl3680_read_cfg()`: writes 0x12 0x34 0x56 to register 0xF0 and reads back. The
   result is never compared; the function always returns `ESP_FAIL` and the caller ignores
   it. Log: `W gsl3680: Unable to initialize the I2C address` at 2317 ms, then `read reg
   0xf0 after is 12 34 56 0` (the write did work). **Benign.**
3. `clear reg` (0x88=0x01, 0xE4=0x04, 0xE0=0x00), reset (0xE4=0x04, 0xBC=0).
4. **Firmware upload**: `start load fw` at 2552 ms, `load fw success` at 3384 ms, i.e.
   **832 ms** of 4-byte I2C writes with page selects at 0xF0. mangoo1 quotes "approximately
   800ms". EspControl feeds the task watchdog during this upload (its README: "Feed the
   watchdog during the long firmware upload"); do the same, or run it from a task that is
   not under the TWDT.
5. `startup_chip`: 0xE0=0x00, `gsl_DataInit(gsl_config_data_id)` (`gsl_point_id:
   gsl_DataInit` in the log), reset, startup again.
6. `read_ram_fw` reads 0xB0 and logs `startup_chip failed read 0xb0 = 5a,5a,5a,5a`
   **unconditionally before comparing**; 0x5A5A5A5A is the success value in the code
   (`if (read_buf[3] != 0x5a || ...) fail`). **Benign.**
7. Operation: reads at register 0x80 (`ESP_LCD_TOUCH_GSL3680_READ_XY_REG`), raw points
   post-processed by `gsl_alg_id_main()` from `gsl_point_id.c`.

Total time from the first touch log line to backlight-on in the factory log: about 1.2 s.
Plan the boot screen around it.

EspControl's field notes (device.yaml comments): "The controller signals a press reliably
but may not interrupt again when the finger lifts. Poll briefly to deliver LVGL's
release/click event" (`touch_timeout: 50ms`), and a rebuild marker for "noisy/multi-contact
wake taps". The INT line is wired, but do not rely on it for release events [their
observation, not reproduced here].

## 3. Coordinate transforms

Each driver defines the raw axes differently, so the published transforms are not
comparable one-to-one. All refer to the native portrait 800x1280 frame unless noted.

| Stack | Display orientation | Touch transform | Source |
|---|---|---|---|
| Vendor BSP (esp_lcd_touch) | portrait, no rotation | `swap_xy 0, mirror_x 0, mirror_y 1`, `x_max 800, y_max 1280` | `bsp_touch_new()` |
| p1ngb4ck / adarmalik ESPHome yaml (kvj `gsl3680` component) | `rotation: 180` on the display | `swap_xy false, mirror_x false, mirror_y true` | community yaml |
| EspControl (kvj `gsl3680` vendored) | portrait display, LVGL `rotation: 90` (1280x800 UI) | `swap_xy false, mirror_x true, mirror_y false`; LVGL rotates the touch with the UI | `device.yaml` (V1 and V2 identical) |
| ESPHome core `gsl3670`, model `GUITION-JC8012P4A1` | portrait | `swap_xy True, mirror_x True, mirror_y False`, min 20/20, max 880/1648 | `touchscreen.py` |

Inference: the vendor and kvj drivers already deliver panel-pixel coordinates (0..800,
0..1280) with Y inverted, whereas the ESPHome core driver exposes the controller's raw
axes (hence the 880/1648 range and the swap). Whatever driver you pick, verify with a
four-corner tap test before trusting any table above; the factory UI proves the vendor
mapping on this unit, the others are untested here.

## 4. Driver options

| Option | Where | Licence | What you get | Notes |
|---|---|---|---|---|
| Vendor demo component `esp_lcd_touch_gsl3680` (`esp_lcd_touch_gsl3680.c`, `gsl_point_id.c`, `include/esp_lcd_touch_gsl3680.h`) | `vendor/package/1-Demo/idf-examples/common_components/esp_lcd_touch_gsl3680/` (three near-identical copies in the package) | `gsl_point_id.c`: **GPL-2.0-or-later** ("2010 - 2016 silead inc.", from `drivers/input/touchscreen/mediatek/gslX680/`). `esp_lcd_touch_gsl3680.c/.h`, `gsl_point_id.h`: **no licence text at all**. Firmware array: no licence or ownership statement | esp_lcd_touch 1.x interface, proven on this unit by the factory firmware | Known defects: `read_cfg` always fails, "failed" logged on success, 93 KB header with the blob |
| `mangoo1/esp_lcd_touch_gsl3680` 1.0.0 | ESP Component Registry | **GPL-2.0-or-later** (declared) | esp_lcd_touch driver, I2C 0x40, RST/INT active low, "approximately 800ms" firmware upload, "bug fixes addressing integer overflow issues in touch gesture detection that were present in the vendor's original source code", tested on "Guition JC8012P4A1C ... ESP-IDF v6.0.2" (registry page, fetched 2026-10-02) | The clean way to consume the GPL code: as a registry dependency |
| ESPHome core `gsl3670` component | `reference/esphome/upstream/esphome/components/gsl3670/` | ESPHome licence (Python MIT, C++ GPL-3.0); firmware fetched at build time from `github.com/esphome-libs/gsl3670-firmware` release v1.0.0 (repo licence Apache-2.0, no statement about the blob's origin) | Model `GUITION-JC8012P4A1`; it points at `seeed-d1001-fw.bin` with the same SHA256 as the Seeed reTerminal model, i.e. ESPHome runs the Guition panel on the Seeed blob | Whether that blob equals the vendor's `GSLX680_FW` is [unverified] |
| EspControl `components/gsl3680` | `reference/espcontrol/upstream/components/gsl3680/` | EspControl repo: **PolyForm Noncommercial 1.0.0**; component `LICENSE.md` says it carries the ESPHome licence from `kvj/esphome@dca6f3e`; embeds `gsl3680_firmware.h` (the same `GSLX680_FW` array) and `gsl_point_id.cpp` | 5-point decode, watchdog feed during upload | Not usable in an MIT project as code; usable as a reference for behaviour |
| `kvj/esphome@jd9365_gsl3680` | GitHub fork of ESPHome | ESPHome licence (fork) | Origin of the `gsl3680` external component used by the community yaml and EspControl | Branch, not a release |
| `profi-max/JC8012P4A1_BSP_ESP32P4` | GitHub | no licence file (GitHub API, 2026-10-02) | ESP-IDF BSP with a GSL3680 driver | No licence = no permission |

## 5. Licence consequence for this MIT repository

Facts: every usable driver links `gsl_point_id.c`, which is GPL-2.0-or-later, and carries a
Silead firmware image with no redistribution grant (INVENTORY sections 3 and 9). The rules
in [../../../docs/licensing.md](../../../docs/licensing.md) apply. The consequence for this
board is stronger than "the binary becomes GPL-governed":

1. **A GPL-compliant binary distribution of a 10.1" bedside_clock image is not possible.**
   The same image links esp-sr's closed libraries (WakeNet / MultiNet) and Espressif
   components under the field-of-use "ESPRESSIF MIT" licence; GPL-2.0 requires the complete
   corresponding source of the whole work, which cannot be provided for those parts.
   Therefore: **10.1" firmware is source-only; users build it themselves.** No prebuilt
   10.1" images in releases, no OTA feed of them.
2. **The 4.3" is unaffected.** Its GT911 driver is Apache-2.0 and its binaries stay
   distributable as before.
3. **Never vendor the touch driver or the firmware array into this tree.** Not even as a
   "local component"; the repo is MIT and the files are not.
4. **Consume it as a registry dependency** in `idf_component.yml`
   (`mangoo1/esp_lcd_touch_gsl3680: "^1.0.0"`), so the GPL code lives in
   `managed_components/` (gitignored) and is fetched by the builder, and keep the display
   path buildable with the touch component switched off (Kconfig) so an MIT-only artifact
   remains possible.
5. **Alternatives that would restore binary distribution** (none exists yet, 2026-10-02):
   a clean-room driver written against the Silead register interface with the firmware blob
   fetched at build time, as ESPHome's `gsl3670` platform does (the blob's own licence
   status would still need settling); or a 10.1" variant without esp-sr (no wake word,
   no on-device commands), which removes the closed libraries so the GPL linking question
   reduces to the one the registry component already answers.
6. The ESPHome route moves the question to ESPHome's licence and to a blob of unknown
   provenance; it does not remove it.
