# Vendor package: JC4880P443C_I_W.zip

Everything Guition ships for the board, extracted under `package/`. The content is **gitignored**
(`devices/*/vendor/package/`, `devices/*/vendor/package-*/`, `devices/*/vendor/*.zip`); only this
README is published. The reasons are in "Why gitignored" below.

## Provenance

| | |
|---|---|
| Vendor | Guition (Shenzhen Jingcai Intelligent Technology; file host `pan.jczn1688.com`) |
| Download URL | <https://pan.jczn1688.com/directlink/1/HMI%20display/JC4880P443C_I_W.zip> |
| Local zip | `JC4880P443C_I_W.zip`, 309,232,242 bytes (295 MiB), downloaded 2026-08-29, 5,665 entries under one top folder `JC4880P443C_I_W/` |
| SHA256 of the zip | `7cea2154667033a639b62a42d1952066ca55c78e187846351f5facb0c3f5232f` |
| Content dates | newest entry inside the zip is 2026-07-13; the factory images are dated 2025-08 to 2026-07 |
| Upstream state on 2026-10-02 | The vendor manifest reported 477,630,572 bytes in 10 shards for the same URL (checked while fetching the 10.1" package; shard objects dated 2026-08-31). The vendor has updated the package since our copy. Re-download before relying on image versions or demo trees. |
| Download mechanism | The "directlink" URL returns an HTML single-page app (`GUITION-DISK`), not the file. Its script fetches `/api/manifest/<dir>/<file>` and then SHA256-named shards from `https://pan.jczn1688.com/s/<sha256>` with the header `X-Gdisk-Fetch: 1`, concatenated in `seq` order. The 10.1" board's [vendor/README.md](../../guition-jc8012p4a1c/vendor/README.md) has the recipe and `vendor/fetch_vendor_zip.sh` is a working, resumable script (the CDN throttles to tens of KB/s per connection; expect hours). |
| Extracted to | `package/` (the zip's top folder flattened). `package-archived-demos/` holds demos moved out on 2026-08-30, see the last section. |
| Files not from the zip | `package/JC4880P443C_I_W Specifications.pdf` and `package/Sc7fee4e9001d4cd6a524e35121673519L.pdf` (5.2 MB, opaque CDN name) were saved from the vendor product page on 2026-08-29. `package/Getting started JC4880P443C_I_W.pdf` duplicates `6-User_Manual/`. |

## Why gitignored

- Guition grants no licence for the package. Schematics, datasheet re-hosts, mechanical drawings and
  factory firmware are all "all rights reserved" by default.
- Parts are third-party with their own terms: Espressif datasheets, an OmniVision datasheet marked
  confidential, a copy of WinHex (commercial software), the Espressif Flash Download Tool.
- 185 MB of sample video of unknown provenance and 102 MB of flashing tool and images.
- The flash tool's cache and logs contain other products' firmware and the vendor's device MACs.

What is safe to publish is published: this inventory, SHA256 hashes, and the knowledge extracted
into `docs/`. The Apache-2.0 components we build on are copied into `src/*/components/` with their
licence headers intact; that copy is the only vendor-derived code in the tree.

## Contents by folder, with licence class

Sizes are on-disk after extraction.

| Folder | Size | Contents | Licence class and what you may do |
|---|---|---|---|
| `1-Demo/idf_examples/README.md` | | Vendor note (Chinese): built with ESP-IDF 5.5.4; when using the camera replace the managed `esp_cam_sensor` with the provided v2.1.0 | n/a |
| `1-Demo/idf_examples/ESP-IDF_5.5.4/esp_brookesia_phone/` | part of 405 MB | Espressif esp-brookesia "phone" launcher adapted by the vendor: launcher, camera app, music and video players, 2048, settings with WiFi | Apache-2.0 throughout: `espressif__esp-brookesia`, the BSP `espressif__esp32_p4_function_ev_board` 5.2.3, `espressif__esp_lcd_st7701` 1.1.3, `bsp_extra` (SPDX Apache-2.0, Espressif), `esp_cam_sensor` 2.1.0. `human_face_detect` and `pedestrian_detect` ship their own LICENSE (esp-dl). The vendor's patches to the BSP and the ST7701 init table are unlicensed modifications of Apache-2.0 code; we copy the patched components into `src/*/components/` and keep the headers and NOTICE obligations. |
| `1-Demo/idf_examples/ESP-IDF_5.5.4/video_lcd_display/` | | Minimal OV02C10 to V4L2 to PPA to LCD pipeline; `components/esp_cam_sensor/` 2.1.0 with the vendor-custom `ov02c10` driver | Example sources carry `SPDX-License-Identifier: CC0-1.0` (Espressif); `esp_cam_sensor` Apache-2.0. Free to reuse. |
| `1-Demo/idf_examples/ESP-IDF_5.5.4/uart_echo_rs485/`, `uart_send_rs485/` | | IDF UART examples set up for the MAX485 on GPIO26/27 | "Public Domain (or CC0 licensed, at your option)" per the headers. |
| `1-Demo/idf_examples/ESP-IDF_5.5.4/xiaozhi-esp32-main/` | | xiaozhi-esp32 voice assistant with the board port `main/boards/guition-jc4880p443/` (also ports for jc1060p470 and jc8012p4a1) | MIT (Shenzhen Xinzhi Future Technology and contributors). Reusable with attribution. Note it defaults to the tenclass.net cloud. |
| `1-Demo/idf_examples/ESP-IDF_5.5.4/common_components/` | | The vendor's shared copy of `bsp_extra`, `esp_cam_sensor`, the BSP and the ST7701 driver | Apache-2.0, same as above. |
| `1-Demo/idf_examples/components/` | | `esp_cam_sensor` (Apache-2.0), `espressif__esp-brookesia` (Apache-2.0) | Apache-2.0. |
| `1-Demo/4.3 TF card video file/MJPEG/` | 185 MB | Sample videos for the demo's video player; `Instructions.txt`: copy the folder to a FAT32 card under 32 GB, speaker required | Unknown copyright. Never redistribute. |
| `2-Specification/` | 0.6 MB | `JC4880P443C_I_W Specifications-EN-V1.0.pdf` | Vendor document, all rights reserved. Quote facts, do not re-host. |
| `3-Structure_Diagram/` | 12 MB | `JC4880P443C_I_W_Y.dwg`, `.pdf`, `.jpg` mechanical drawings | Vendor, all rights reserved. |
| `4-Driver_IC_Data_Sheet/` | 6.7 MB | `ESP32_P4_Chip_Datasheet_CN_v0.4.1_0320 .pdf` (sic, trailing space), `OV02C10_CSP_DS_2.01_Waching.pdf` | Espressif and OmniVision, all rights reserved; the OmniVision file is a confidential-class datasheet. Do not re-host. |
| `5-Schematic/` | 1.8 MB | `1_PWR.png` to `7_OTHER.png` (2025-07-24) and `JC4880P443_V1.0.pdf` (one vector page, 2025-09-27) | Vendor, all rights reserved. Our local webp crops are described in [../docs/schematics/README.md](../docs/schematics/README.md). |
| `6-User_Manual/` | 2.5 MB | `Getting started JC4880P443C_I_W.pdf` (Arduino IDE oriented) | Vendor, all rights reserved. |
| `7-Character&Picture_Molding_Tool/` | 3.7 MB | Windows utilities: `WinHex15.1SR-8.rar` (X-Ways, **commercial**), `USB-SERIAL CH340.rar` (WCH driver; this board has no CH340), `串口工具-sscom32.rar` (serial terminal), `网络调试助手.rar` (network debug assistant), `JPGCompact_V5.0_XiTongZhiJia.zip` | Third-party binaries with no grant from the vendor. **Never redistribute**; nothing here is needed for this board. |
| `8-Burn operation/` | 102 MB | `Burn files/` (five factory images, see [../firmware/README.md](../firmware/README.md)), `Burn operation instructions/` (three screenshots), `flash_download_tool_3.9.9/` (Espressif Flash Download Tool 3.9.9_R2 with its `dl_temp/` cache of other products' images and `logs/` with vendor MACs), `烧录说明：Burning instructions.txt` (600 mA supply, USB2 port) | Images: vendor plus embedded Espressif/xiaozhi code, no licence. Tool: Espressif freeware under Espressif's terms. Do not re-host either. |
| root PDFs | 8 MB | Specifications, Getting started (duplicate), product-page PDF | Vendor, all rights reserved. |

## The paths that matter

| Purpose | Path under `package/1-Demo/idf_examples/ESP-IDF_5.5.4/` |
|---|---|
| Vendor-patched BSP (ST7701 480x800 behind the misleading `BSP_LCD_TYPE_1024_600` option, DPHY LDO ch3, GT911, ES8311) | `esp_brookesia_phone/components/espressif__esp32_p4_function_ev_board/` (5.2.3) |
| Panel driver with this board's init table | `esp_brookesia_phone/components/espressif__esp_lcd_st7701/` (1.1.3; referenced by the BSP by relative path, copy both as siblings) |
| Practical audio layer (`bsp_extra_codec_init/set_fs/i2s_read/i2s_write/volume_set`) | `esp_brookesia_phone/components/bsp_extra/` |
| Camera sensor drivers with OV02C10 | `video_lcd_display/components/esp_cam_sensor/` (2.1.0; also under `common_components/` and `../components/`) |
| The `bsp_display_cfg_t` that works, NVS erase-retry | `esp_brookesia_phone/main/main.cpp` |
| Codec init to set_fs to volume to gain to I2S read/write | `esp_brookesia_phone/main/audio.c` |
| `esp_wifi_remote` init/scan/connect with error handling | `esp_brookesia_phone/components/apps/setting/Setting.cpp` (around line 575) |
| The working esp_hosted SDIO sdkconfig block | `esp_brookesia_phone/sdkconfig` (around lines 2805-2870) |
| On-device ML on the P4 (esp-dl) | `esp_brookesia_phone/components/human_face_detect/`, `pedestrian_detect/` |
| AVI/MJPEG playback | `esp_brookesia_phone/components/apps/video_player/` |
| Camera to LCD without LVGL (donor for `app_video.c`) | `video_lcd_display/main/app_video.c` |
| Standalone voice assistant with esp-sr AFE and echo cancellation, drives GT911 RST/INT | `xiaozhi-esp32-main/` (board port `main/boards/guition-jc4880p443/`, `sdkconfig.defaults.esp32p4`) |
| RS-485 on UART1 | `uart_echo_rs485/`, `uart_send_rs485/` |

Outside the demo tree: `package/8-Burn operation/Burn files/` (factory images),
`package/5-Schematic/JC4880P443_V1.0.pdf` (searchable schematic), `package/2-Specification/`.

How these are used in `docs/`: [03-working-components.md](../docs/03-working-components.md)
(which components are copied and why), [09-camera.md](../docs/09-camera.md) (the esp_cam_sensor
override), [08-esphome-findings.md](../docs/08-esphome-findings.md) (what the vendor tree covers
that ESPHome does not).

## package-archived-demos/

Moved out of `package/1-Demo/` on 2026-08-30 after the coverage review in
[08-esphome-findings.md](../docs/08-esphome-findings.md); 11 MB, original relative paths kept:

- `1-Demo/idf_examples/ESP-IDF_5.5.4/lvgl_demo_v8`, `lvgl_demo_v9`, `lvgl_sw_rotation`,
  `esp_draw_bits`: minimal LVGL bootstraps, superseded by `src/sample_hello_world`;
  `lvgl_demo_v9` has a known build bug (missing semicolon).
- `1-Demo/arduino_examples/Wifi_scan`, `adc_test`, `lvgl_demo_v8`, `lvgl_v9_sw_rotation`,
  `mp3_player`: Arduino-framework variants; the battery thresholds from `adc_test` are recorded in
  [01-hardware.md](../docs/01-hardware.md).

Nothing in there is needed to build anything in `src/`. The move itself is logged in
[reference/archive-inventory-2026-08-30.md](../../../reference/archive-inventory-2026-08-30.md).
