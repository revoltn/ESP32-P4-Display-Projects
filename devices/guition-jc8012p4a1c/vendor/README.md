# Vendor package for the JC8012P4A1C_I_W_Y

What `vendor/package/` is, where it came from, what is inside, which licences apply, and
the paths worth opening. The file-level inventory with SHA256s and licence classification
is [INVENTORY.md](INVENTORY.md) (its body was written against the mirror described below;
where it says `mirror_devinwatson/` read `vendor/package/`).

## 1. Provenance

| Item | Value |
|---|---|
| Official source | `https://pan.jczn1688.com/directlink/1/HMI%20display/JC8012P4A1C_I_W_Y.zip` |
| What that URL really is | not a file: an 11,275-byte single-page JavaScript app (`<title>GUITION-DISK</title>`) that fetches a JSON manifest from `/api/manifest/<dir>/<file>` and downloads the file as SHA256-named shards from `https://pan.jczn1688.com/s/<sha256>` with the header `X-Gdisk-Fetch: 1`, then concatenates them in the browser (INVENTORY section 0) |
| Zip identity (from the manifest) | `public_id OtpWrcfUxr7e0sTMjQR_Fg`, `size 421,427,604` bytes, 9 shards (8 x 50,331,648 + 1 x 18,774,420), shard objects `last-modified: Mon, 31 Aug 2026` |
| Manifest saved here | [manifest.json](manifest.json) (verbatim; the nine shard SHA256s double as the shard URLs) |
| Downloader saved here | [fetch_vendor_zip.sh](fetch_vendor_zip.sh): per-shard download with `curl -H 'X-Gdisk-Fetch: 1'`, SHA256 verification, resume of verified shards, concatenation, zip self-test |
| Measured throughput | about 7 KB/s per connection, about 14 KB/s with 8 parallel range requests; more than 8 hours for 421 MB, with connection drops; the download was abandoned after 7.2 MB (INVENTORY section 0) |
| What `package/` actually contains | a `--depth 1` clone of **`github.com/DevinWatson/10.1-inch-ESP32P4-Xiaozhi-ESP32-C6-JC8012P4A1C_I_W_Y`**, commit `84e7c969d74221bfa7fe8b7d21ad7df8f2c0b39a` ("Init", 2026-04-15; `vendor/package/.git/packed-refs`), whose README links exactly the vendor URL above, i.e. a re-upload of the zip: 3,963 files, 531,287,565 bytes on disk, 8 top-level folders + `README.md` |
| Secondary copy | `community/p1ngb4ck-unofficial_guition_esp32p4_repo/` (commit `89b928ee`, 2025-12-03): schematics, spec, structure drawings byte-identical to the mirror (SHA256 cross-check in INVENTORY section 10) plus an ESPHome yaml |

Running the downloader: the script hard-codes `MAN=manifest_JC8012P4A1C_I_W_Y.json` and
writes its working list to `shards/list.txt`, while this folder stores the manifest as
`manifest.json`. Before running it: `cp manifest.json manifest_JC8012P4A1C_I_W_Y.json`
and `mkdir -p shards` (the script is kept as delivered; it is not edited here). Run it from
a fast, stable connection, or leave it running overnight; it resumes verified shards.

**Difference risk.** The vendor's shard objects are dated 2026-08-31, four and a half months
after the mirror commit, and the unit's own factory firmware was compiled 2026-07-03, which
is not in the mirror ([../firmware/README.md](../firmware/README.md)). The vendor's
`readme.docx` also describes a xiaozhi-esp32 sample folder (`jc1060pa1`, v1.5.6) that the
mirror does not contain while the shipped `..._xiaozhi2.0.4.bin` is v2.0.4. Expect the live
zip to have newer firmware and at least one extra sample folder; everything else (BSP,
drivers, schematics, spec) is most likely identical [inference]. Until the zip is fetched
and diffed, statements in this repo about "the vendor package" mean the mirror.

## 2. What is inside (folders 1-8)

Sizes from INVENTORY section 1.

| Folder | Size | Contents |
|---|---|---|
| `1-Demo/arduino-examples/` | 1.0 MB | three Arduino-ESP32 3.1 sketches (LVGL 8.4, LVGL 9.2.2, LVGL 9 with software rotation) with a copy of the jd9365 and gsl3680 drivers and `pins_config.h` |
| `1-Demo/idf-examples/` | 273.6 MB | `common_components/` (vendor-patched esp-bsp 4.1.1, `bsp_extra`, `esp_lcd_jd9365` 1.0.2, `esp_lcd_touch_gsl3680`, `esp_cam_sensor` 0.9.0, `esp_video` 0.8.0, an unused `esp_lcd_jd9165`), projects `lvgl_demo_v8`, `lvgl_demo_v9`, `esp_draw_bit` (touch paint, no BSP), `esp_brookesia_phone` (the factory demo, 133 MB with managed components and media), `usb_extend_screen` (P4 as a USB display for Windows), `ESP-IDF_lvgl_sw_rotation` (133 MB, bundles LVGL 9.2.2; PPA/software rotation), a loose second `esp_lcd_touch_gsl3680`, `video_lcd_display.zip`, `readme.docx` (Chinese usage notes, states ESP-IDF v5.4), a Chinese `.doc` about the USB secondary-screen driver |
| `2-Specification/` | 0.6 MB | `JC8012P4A1C_I_W Specifications-EN-v1.1(2).pdf`, 7 pages |
| `3-Structure_Diagram/` | 0.6 MB | `1.jpg` (outline 242.8 x 158.7 mm), `2.jpg` ("Internal Interface Display": PCB photo with 12 numbered connectors), `JC8012P4A1.pdf` (mechanical) |
| `4-Driver_IC_Data_Sheet/` | 7.4 MB | ESP32-P4 datasheet (Chinese, v0.4.1), OV02C10 datasheet, the panel-maker JD9365 init file (`...HKC10.1_IPS(QP101BS01-1)_800x1280...(2lane).txt`), a BOE 10.1" panel drawing. No JD9365, GSL3680, ES8311 or C6 datasheets |
| `5-Schematic/` | 1.3 MB | seven PNG sheets: `1_PWR`, `2_LCD`, `3_ESP32-P4`, `4_CONN`, `5_ESP32-C6`, `6_USB`, `7_CODEC`. No PDF or CAD |
| `6-User_Manual/` | 2.1 MB | `Getting started JC8012P4A1C_I_W_Y.pdf`, 12 pages of Arduino IDE setup; text still says "JC1060P470"; no flashing offsets, nothing about the C6 |
| `7-Character&Picture_Molding_Tool/` | 3.9 MB | Windows tools as archives: a JPG compressor, the WCH CH340 driver, **WinHex 15.1 SR-8** (commercial shareware), sscom32 serial tool, a network debug assistant |
| `8-Burn operation/` | 240.8 MB | `Burn files/` (the four images listed in [../firmware/README.md](../firmware/README.md)), three screenshots of the flash tool, `烧录说明：Burning instructions.txt`, and `flash_download_tool_3.9.7/` including 191 MB of the vendor PC's `dl_temp/` cache (other products' firmware), `logs/` with device MACs and `configure/` with employee paths |

## 3. Licences inside

Classification from INVENTORY section 2 (SPDX census over `1-Demo`: Apache-2.0 311 files,
MIT 37, ESPRESSIF MIT 34, CC0 12, GPL-2.0+ 1 distinct file in 6 copies).

| Material | Licence | Consequence |
|---|---|---|
| Espressif components and examples (esp-bsp 4.1.1, esp_lcd_jd9365 1.0.2, bsp_extra, esp_cam_sensor, usb_extend_screen, lvgl_port) | Apache-2.0 with SPDX headers | reusable; keep the headers |
| `esp_video`, `human_face_detect`, `pedestrian_detect` | ESPRESSIF MIT (field-of-use restricted to Espressif chips) | reusable on this hardware; not GPL-compatible |
| esp-brookesia 0.4.2 | Apache-2.0 | reusable |
| LVGL 8.4 / 9.2.2 copies | MIT | reusable |
| xiaozhi-esp32 (the shipped binary; sources not in the mirror) | MIT upstream | reusable from upstream |
| Vendor-authored code: `esp_lcd_touch_gsl3680.c/.h`, Arduino wrappers, `esp_draw_bit`, `lvgl_sw_rotation.c`, brookesia `main.cpp` and apps, `pins_config.h`, files with stripped Espressif SPDX headers | **no licence statement** | no permission to redistribute; read for facts, do not copy |
| `gsl_point_id.c` (Silead point-tracking algorithm) | **GPL-2.0-or-later** | links GPL into every touch-enabled demo; see [../docs/touch-gsl3680.md](../docs/touch-gsl3680.md) |
| `GSLX680_FW[]` touch firmware array and `gsl_config_data_id[]` | none; Silead ownership presumed | never vendor |
| Factory and C6 firmware images | none | link with SHA256, do not host |
| Flash Download Tool 3.9.7 | Espressif freeware, closed source | do not host |
| WinHex, sscom32, JPG compressor, network assistant, CH340 driver | third-party, WinHex is commercial | do not host |
| Datasheets (Espressif, OmniVision), panel init file, schematics, drawings, spec PDF | chip-vendor / Guition copyright | link; the schematic PNGs are mirrored by p1ngb4ck "with kind allowance" from Guition, this repo still does not re-host them |
| Demo media (`BGM 1.mp3`, `Waka Waka.mp3`, `For Elise.mp3`, `Something.mp3`, images, a 7.6 MB MJPEG as a C array) | unknown; two titles are commercial recordings | never redistribute |

## 4. Why it is gitignored

`.gitignore` excludes `devices/*/vendor/package/` and `devices/*/vendor/*.zip`. Reasons: the
licence table above (most of the tree cannot be redistributed under this repo's MIT
licence), size (531 MB, of which 191 MB is a flash-tool cache and 133 MB a bundled LVGL),
and privacy (vendor session logs with MAC addresses, employee desktop paths). The committed
artefacts are the two provenance files, this README and the inventory; anyone can rebuild
`package/` from the mirror in one `git clone --depth 1` (on Windows set
`git config core.longpaths true` first, INVENTORY section 0).

## 5. Paths worth opening (relative to `vendor/package/`)

| What | Path |
|---|---|
| Vendor BSP fork (pins, LDO, DSI bus, panel, touch install) | `1-Demo/idf-examples/common_components/esp32_p4_function_ev_board/esp32_p4_function_ev_board.c`, `include/bsp/esp32_p4_function_ev_board.h`, `include/bsp/display.h`, `Kconfig` |
| BSP identity (esp-bsp 4.1.1, commit `0064ab93`) | `1-Demo/idf-examples/common_components/esp32_p4_function_ev_board/idf_component.yml` |
| JD9365 panel driver 1.0.2 and the DPI timing macro | `1-Demo/idf-examples/common_components/esp_lcd_jd9365/esp_lcd_jd9365.c`, `include/esp_lcd_jd9365.h` (`JD9365_800_1280_PANEL_60HZ_DPI_CONFIG`) |
| Panel-maker init sequence and porches | `4-Driver_IC_Data_Sheet/C-JD9365DA-H3_HKC10.1_IPS(QP101BS01-1)_800x1280_FP7721_ZigZag_G2.2_20221205_andy(2lane).txt` |
| GSL3680 touch driver (GPL hot-spot) | `1-Demo/idf-examples/common_components/esp_lcd_touch_gsl3680/` (`esp_lcd_touch_gsl3680.c`, `gsl_point_id.c`, `include/esp_lcd_touch_gsl3680.h`, `include/gsl_point_id.h`) |
| Audio glue (ES8311 only, PA GPIO20) | `1-Demo/idf-examples/common_components/bsp_extra/` |
| Factory demo project, its sdkconfig and partitions | `1-Demo/idf-examples/esp_brookesia_phone/` (`sdkconfig.defaults`, `sdkconfig`, `partitions.csv`, `main/`, `components/apps/`) |
| PPA / software rotation reference | `1-Demo/idf-examples/ESP-IDF_lvgl_sw_rotation/lvgl_sw_rotation/` (`sdkconfig.defaults`, `main/lvgl_port_v9.c`, `main/lvgl_sw_rotation.c`) |
| Minimal no-BSP display + touch bring-up | `1-Demo/idf-examples/esp_draw_bit/main/main.c` |
| Arduino pin table | `1-Demo/arduino-examples/esp32p4_lvgl_v8/src/pins_config.h` |
| Schematics | `5-Schematic/1_PWR.png` (IP5306, TLV62569 x2, battery divider GPIO52), `2_LCD.png` (MP3202, FPC1, LCD reset), `3_ESP32-P4.png` (every GPIO net name, LDO outputs, buttons, flash), `4_CONN.png` (expansion FPCs, TF card, WS2812, UART CN2, camera FPC5, touch FPC2, RTC), `5_ESP32-C6.png` (C6 module, CN5 header, SDIO pull-ups), `6_USB.png` (three USB-C sockets, CH340C), `7_CODEC.png` (ES8311, mic, NS4150, CN3, I2S/I2C net table) |
| Spec sheet | `2-Specification/JC8012P4A1C_I_W Specifications-EN-v1.1(2).pdf` |
| Connector drawing | `3-Structure_Diagram/2.jpg` |
| Burn files and instructions | `8-Burn operation/Burn files/`, `8-Burn operation/烧录说明：Burning instructions.txt`, `8-Burn operation/Burn operation instructions/*.png` |
| Vendor usage notes (Chinese) | `1-Demo/idf-examples/readme.docx` (translated extract in INVENTORY section 6) |
