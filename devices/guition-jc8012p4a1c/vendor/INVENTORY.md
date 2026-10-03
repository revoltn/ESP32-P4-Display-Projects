<!-- Provenance header added 2026-10-02 by the board documentation pass. The body below is the
unmodified inventory written by an automated pass over the DevinWatson GitHub mirror of the vendor
package (commit 84e7c969d74221bfa7fe8b7d21ad7df8f2c0b39a, 2026-04-15), which is what vendor/package/
now contains. Read `mirror_devinwatson/` as `vendor/package/` and `p1ngb4ck/` as
`community/p1ngb4ck-unofficial_guition_esp32p4_repo/`. The live vendor zip (421,427,604 bytes, shards
dated 2026-08-31) was NOT obtained, so this describes the mirror, not the zip; see README.md in this
folder for the download procedure and the difference risk. -->

# JC8012P4A1C_I_W_Y (Guition 10.1" ESP32-P4 + ESP32-C6) — vendor package provenance & license inventory

Generated 2026-10-02 from the scratchpad `vendor_fetch/` working set. Nothing under `U:\Dev\ESP32-P4` was touched.

## 0. Where the package came from

| Source | Result |
|---|---|
| **Official vendor mirror** `https://pan.jczn1688.com/directlink/1/HMI%20display/JC8012P4A1C_I_W_Y.zip` | The URL is NOT a file. Every `/directlink/1/...` path returns the same 11 275-byte HTML single-page app (`<title>GUITION-DISK</title>`, saved as `landing_full.html`). Its JS fetches a JSON manifest from `/api/manifest/<dir>/<file>` and then downloads the file as SHA256-named shards from `https://pan.jczn1688.com/s/<sha256>` (6 in parallel, header `X-Gdisk-Fetch: 1`) and concatenates them in `seq` order with StreamSaver. |
| `JC8012P4A1C_I_W_Y.zip` manifest | **EXISTS**: `public_id OtpWrcfUxr7e0sTMjQR_Fg`, `size 421 427 604` bytes, `mime application/x-zip-compressed`, 9 shards (8 x 50 331 648 + 1 x 18 774 420). Saved verbatim as `manifest_JC8012P4A1C_I_W_Y.json`; shard list in `shards/list.txt`. Shard objects carry `last-modified: Mon, 31 Aug 2026`. |
| `JC8012P4A1C_I_W_Y1.zip`, `JC8012P4A1C_I_W.zip` (+ `.rar`, `JC8012P4A1C.zip`, `_V1`, `_I_Y`, …) | `404 {"detail":"文件不存在: …"}` — do not exist. |
| `JC4880P443C_I_W.zip` (4.3" sibling, control) | exists, 477 630 572 bytes, 10 shards. |
| **Download attempt** | Started `fetch_shards.sh` (per-shard download + SHA256 verify + concat). The CDN throttles to ~7 KB/s per connection and ~14 KB/s with 8 parallel range requests (measured; `Accept-Ranges: bytes` is supported), i.e. >8 h for 421 MB, with connection drops (`curl exit 18`). **Aborted** after 7.2 MB of shard 0. The zip was therefore NOT obtained; the step-4 zip-vs-mirror diff could not be done. Re-run `./fetch_shards.sh` from a faster network to fetch it (it resumes verified shards). |
| **Community mirror** `github.com/DevinWatson/10.1-inch-ESP32P4-Xiaozhi-ESP32-C6-JC8012P4A1C_I_W_Y` | Cloned `--depth 1` into `mirror_devinwatson/` (needed `git config core.longpaths true` + `git restore --source=HEAD :/` on Windows). Commit `84e7c969d74221bfa7fe8b7d21ad7df8f2c0b39a` "Init", Devin Watson, 2026-04-15. Its README links exactly the vendor zip URL above, so it is a re-upload of that zip. 3 963 files, **531 287 565 bytes (506.7 MiB)** on disk; git pack 128.8 MiB. 8 top-level folders + README.md. |
| **p1ngb4ck/unofficial_guition_esp32p4_repo** | Cloned into `p1ngb4ck/` (38 MB incl. .git). Commit `89b928eec1dcbc5600d3e84fb48edff8e4f762c4`, Oliver Kleinecke, 2025-12-03. README: "contains files provided by Guition (from their .zip-files) with kind allowance to be put on git for easier access … Special thanks to @adarmalik for providing JC8012P4A1C_I_W_Y data/info & esphome-yaml-example". Three boards: `JC-ESP32P4-M3-Dev`, `JC1060P470_I_W_Y`, `JC8012P4A1C_I_W_Y`. |

**Everything below is inventoried from `mirror_devinwatson/`** (the vendor zip content as re-uploaded by DevinWatson). Caveat: the vendor's shard objects are dated 2026-08-31, i.e. after the mirror commit; the live zip may differ (newer firmware, the xiaozhi sample folder that `readme.docx` mentions but the mirror lacks — see §9).

### 0.1 Zip manifest (for later retrieval)

```
seq size      sha256 (= shard URL https://pan.jczn1688.com/s/<sha256>, send header "X-Gdisk-Fetch: 1")
0   50331648  48f25fb82a5dfe727cbeaeb29f629c8f5a7a443853acee34c6ebadbc43c9d4e0
1   50331648  00af30f434b51d50841c2e81c2159cd3eead0df91fe8de7b525ebb2eb6cac45d
2   50331648  7881f43ae84efbe2e90768f47f1bcda195a09741fb6a9da08366d4a75a30d6aa
3   50331648  249fece1bd926f8aff28e2f7ccd953dab643cf63cac16e518f7b92576310778f
4   50331648  4ca0bd8bd117bbf0cfd0845ca20f3e4cc3ffa15efacae4b9bea80b57aff87ade
5   50331648  497d0d87115ea8a44d0ac915df0c04c874540e57007bfb281f8a65fba32ac961
6   50331648  69a678ca0c756591a817e761b1a079e5263ec18d9b56b414cd26a2aedb230870
7   50331648  9da94a8a9e2b4747b573502fd4029abb2761dffb33fd2bac64331b34c0e7452e
8   18774420  eeb5a16d29927c76b3b734f31cc3c62acb9b49414f16e3949a508cfda9acf7e1
total 421427604
```

## 1. Tree to depth 3 (bytes, file counts) — `mirror_devinwatson/`

```
   531287565   3963 files  .   (README.md 804 B at root)
   274633261   3867 files  1-Demo
     1003695     53 files  1-Demo/arduino-examples
      331030     17 files  1-Demo/arduino-examples/Arduino_lvgl_sw_rotation
      289560     16 files  1-Demo/arduino-examples/esp32p4_lvgl _v9        (sic: space in folder name)
      277275     16 files  1-Demo/arduino-examples/esp32p4_lvgl_v8
       30508              1-Demo/arduino-examples/esp32_v3.1.0.png        (screenshots of required versions)
       52623              1-Demo/arduino-examples/esp32p4.png
       12510              1-Demo/arduino-examples/lvgl_v8.4.png
       10189              1-Demo/arduino-examples/lvgl_v9.2.2.png
   273629566   3814 files  1-Demo/idf-examples
   133566051   2784 files  1-Demo/idf-examples/ESP-IDF_lvgl_sw_rotation   (contains a full LVGL 9.2.2 copy)
     1844785    245 files  1-Demo/idf-examples/common_components
   133853766    636 files  1-Demo/idf-examples/esp_brookesia_phone        (managed_components + spiffs media)
      203037     13 files  1-Demo/idf-examples/esp_draw_bit
      200150      6 files  1-Demo/idf-examples/esp_lcd_touch_gsl3680      (loose component, not a project)
      195700     13 files  1-Demo/idf-examples/lvgl_demo_v8
      201671     13 files  1-Demo/idf-examples/lvgl_demo_v9
      940501    101 files  1-Demo/idf-examples/usb_extend_screen
      624462              1-Demo/idf-examples/readme.docx                 (Chinese usage notes, text in §6)
      270419              1-Demo/idf-examples/video_lcd_display.zip       (202 entries, Espressif esp_video example + esp_cam_sensor)
     1729024              1-Demo/idf-examples/副屏.doc                    ("secondary screen" Windows-driver note, links to esp-iot-solution)
      559802      1 files  2-Specification
                           JC8012P4A1C_I_W Specifications-EN-v1.1(2).pdf (7 pages)
      649938      3 files  3-Structure_Diagram
                           1.jpg 107029, 2.jpg 517009, JC8012P4A1.pdf 25900
     7428790      4 files  4-Driver_IC_Data_Sheet
                           1.png 427479  (mechanical drawing "BOE TFT 10.1 inch 800*RGB*1280 DOTS", 228.6x143 mm outline)
                           C-JD9365DA-H3_HKC10.1_IPS(QP101BS01-1)_800x1280_FP7721_ZigZag_G2.2_20221205_andy(2lane).txt 4028  (panel init sequence)
                           ESP32_P4_Chip_Datasheet_CN_v0.4.1_0320 .pdf 2049946
                           OV02C10_CSP_DS_2.01_Waching.pdf 4947337
     1272801      7 files  5-Schematic   (PNG only, no PDF/DWG)
                           1_PWR.png 2_LCD.png 3_ESP32-P4.png 4_CONN.png 5_ESP32-C6.png 6_USB.png 7_CODEC.png
     2077820      1 files  6-User_Manual
                           Getting started JC8012P4A1C_I_W_Y.pdf (12 pages, Arduino IDE setup; text still says "JC1060P470 module")
     3852200      5 files  7-Character&Picture_Molding_Tool
                           JPGCompact_V5.0_XiTongZhiJia.zip 318115, USB-SERIAL CH340.rar 250569, WinHex15.1SR-8.rar 2476677,
                           串口工具-sscom32.rar 419058, 网络调试助手.rar 387781
   240812149     74 files  8-Burn operation
    27280557      4 files  8-Burn operation/Burn files
       55911      3 files  8-Burn operation/Burn operation instructions   (3 PNG screenshots)
   213475395     66 files  8-Burn operation/flash_download_tool_3.9.7
       10674     10 files  8-Burn operation/flash_download_tool_3.9.7/configure
   191102352     38 files  8-Burn operation/flash_download_tool_3.9.7/dl_temp   (tool cache: other products' firmware!)
     1648228      3 files  8-Burn operation/flash_download_tool_3.9.7/doc
       21178     14 files  8-Burn operation/flash_download_tool_3.9.7/logs      (vendor flashing-session logs with MACs)
         286              8-Burn operation/烧录说明：Burning instructions.txt
```

File-type census (whole mirror): 1073 .c, 1067 .png, 757 .h, 223 .rst, 109 .txt, 97 .cpp, 77 .md, 58 .yml, 46 .py, 38 .bin_rep, 34 .hpp, 31 .jpg, 14 .fnt, 13 .ttf, 12 .json, 9 .mp3, 9 .gif, 8 .pdf, 4 .bin (+ 1 .exe, 3 .rar, 2 .zip, 1 .doc, 1 .docx).

## 2. Per top-level folder: contents and license classification

Legend: (i) Espressif open source · (ii) third-party open source · (iii) vendor-authored, NO license statement · (iv) binaries/firmware · (v) proprietary/freeware tools · (vi) datasheets/PDF (chip-vendor copyright) · (vii) CAD · (viii) fonts/images/media.

SPDX census over `1-Demo` (all `.c/.h/.cpp/.hpp/.py/.ino/CMake`): `Apache-2.0` 311, `MIT` 37, `ESPRESSIF MIT` 34, `CC0-1.0` 12, `Unlicense OR CC0-1.0` 4, `((GPL-2.0+ WITH Linux-syscall-note) OR BSD-3-Clause)` 3 (the latter are `esp_video` V4L2 headers). `gsl_point_id.c` carries a GPL-2.0-or-later notice but no SPDX tag (see §3).

### 2.1 `1-Demo/arduino-examples` (3 sketches, 1.0 MB)
* `esp32p4_lvgl_v8/`, `esp32p4_lvgl _v9/esp32p4_lvgl_v9/`, `Arduino_lvgl_sw_rotation/esp32p4_lvgl_v9/` — Arduino-ESP32 **v3.1** (README: "请使用arduino_esp32_v3.1版本 / 请将lvgl文件夹中的demo文件夹移动至src文件夹" = use arduino-esp32 v3.1; move LVGL's `demos` folder into `src`). PNGs show arduino-esp32 3.1.0, LVGL 8.4 and 9.2.2.
* (i) Apache-2.0 with Espressif SPDX headers: `src/lcd/esp_lcd_jd9365.c/.h` (esp-iot-solution driver copy, 198-entry init table), `src/touch/esp_lcd_touch.c/.h` (esp_lcd_touch 1.x, "2015-2022 Espressif"), `lvgl_port_v9.c/.h` ("2023-2024 Espressif", esp_lvgl_port derived).
* (iii) NO license header: `*.ino`, `lv_conf.h` (LVGL template, MIT upstream but header stripped), `lvgl_sw_rotation.c`, `pins_config.h`, `src/lcd/jd9365_lcd.cpp/.h` (Arduino wrapper class, Chinese comments), `src/touch/gsl3680_touch.cpp/.h` (wrapper; header guard still says `_GT911_TOUCH_H`), `src/touch/esp_lcd_gsl3680.c/.h` (**contains the Silead firmware blob**), `src/touch/gsl_point_id.h`.
* (ii)/(iii) `src/touch/gsl_point_id.c` — Silead, **GPL-2.0-or-later** notice (see §3).
* `pins_config.h` (verbatim): `LCD_H_RES 800`, `LCD_V_RES 1280`, `LCD_RST 27`, `LCD_LED 23`, `TP_I2C_SDA 7`, `TP_I2C_SCL 8`, `TP_RST 22`, `TP_INT 21`.

### 2.2 `1-Demo/idf-examples` (273.6 MB) — details in §4–§6
* (i) Espressif Apache-2.0: `common_components/esp32_p4_function_ev_board` (esp-bsp 4.1.1, vendor-patched), `common_components/esp_lcd_jd9365` (1.0.2, esp-iot-solution; vendor added one macro), `common_components/espressif__esp_lcd_jd9165` (1.0.2, unused for this panel), `common_components/bsp_extra` (0.0.2, "2015-2024 Espressif"), `common_components/esp_cam_sensor` (0.9.0), `usb_extend_screen/*` (esp-iot-solution example + its own BSP 0.0.1 + esp_lcd_touch/gt911/ft5x06 copies), `lvgl_demo_v9/main/main.c` (SPDX 2025), `ESP-IDF_lvgl_sw_rotation/.../managed_components/espressif__esp_lcd_touch_cst816s` (1.1.0), `esp_brookesia_phone/managed_components/espressif__esp_lcd_touch_gt911` (1.1.3), `esp_brookesia_phone/components/apps/camera/app_*` files.
* (i) **ESPRESSIF MIT**: `common_components/esp_video` (0.8.0~3, LICENSE "ESPRESSIF MIT License, Copyright (c) 2024 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>"), `esp_brookesia_phone/components/human_face_detect` and `pedestrian_detect` (LICENSE = "MIT License / Copyright (c) 2021 Espressif Systems (Shanghai) Co., Ltd.").
* (ii) third-party: `esp_brookesia_phone/managed_components/espressif__esp-brookesia` 0.4.2 (Apache-2.0 `license.txt`); `ESP-IDF_lvgl_sw_rotation/lvgl_sw_rotation/components/lvgl__lvgl` = LVGL 9.2.2 (`LICENCE.txt` "MIT licence / Copyright (c) 2021 LVGL Kft", plus `src/libs/lz4/LICENSE` and a test-fixture LICENSE); LVGL music-demo image assets copied into `esp_brookesia_phone/components/apps/music_player/gui_music/assets/` (LVGL MIT, headers stripped).
* (iii) NO license statement (vendor or stripped): `common_components/esp_lcd_touch_gsl3680/{esp_lcd_touch_gsl3680.c, include/esp_lcd_touch_gsl3680.h, include/gsl_point_id.h}` and the two further copies (`idf-examples/esp_lcd_touch_gsl3680/`, `usb_extend_screen/components/esp_lcd_touch_gsl3680/`); `esp_draw_bit/main/main.c`; `lvgl_demo_v8/main/main.c`; `esp_brookesia_phone/main/main.cpp`; `ESP-IDF_lvgl_sw_rotation/lvgl_sw_rotation/main/lvgl_sw_rotation.c`; most of `esp_brookesia_phone/components/apps/**` (Calculator.cpp, Game_2048.cpp, ImageDisplay.cpp, setting/ui/*.c (SquareLine export), video_player, image `.c` arrays). The app set (calculator, camera, game_2048, image_display, music_player, setting, video_player) is the same as Espressif's esp-brookesia P4 example and the camera sub-files carry Espressif SPDX headers, so these are most likely Espressif-origin with headers absent upstream — unverified offline.
* (ii) GPL: `gsl_point_id.c` (3 identical copies here, md5 `ac21371454f16ba1c7ebf33655f91eab`).
* (viii) media: `esp_brookesia_phone/spiffs/music/{BGM 1.mp3, BGM 2.mp3, For Elise.mp3, Something.mp3, Waka Waka.mp3}`, `spiffs/2048/*.mp3`, `spiffs/image/114335*.jpg`, `components/apps/video_player/assets/breaking_news.c` (7.6 MB MJPEG as C array), LVGL demo `img_lv_demo_music_*` arrays, `lv_font_montserrat` references. **"Waka Waka" / "Something" are titles of commercial recordings** — redistribution right unknown; verify before publishing.
* (iv) none here except `video_lcd_display.zip` (source).
* Docs: `readme.docx` (vendor, Chinese), `副屏.doc` (vendor, Chinese; WPS Office; links `esp-iot-solution/.../usb_extend_screen/windows_driver/README.md`).

### 2.3 `2-Specification` — (vi) vendor PDF
`JC8012P4A1C_I_W Specifications-EN-v1.1(2).pdf`, 7 pp. Key text: "10.1 inch ESP32P4 module JC8012WP4A1C_I_W … 32M PSRAM, Flash size is 16MB, display resolution 800*1280 … Driver chip JD9365 … Effective display area 216.58*135.36 mm, Module size 242.80*158.70 mm, IPS, 5V, ~700 mA, ~550 g. SKU JC8012P4A1C_I_W_Y (capacitive touch), JC8012P4A1C_I_W_Y1 (with battery)". Byte-identical to p1ngb4ck's copy.

### 2.4 `3-Structure_Diagram` — (vii) mechanical: `1.jpg`, `2.jpg`, `JC8012P4A1.pdf` (identical to p1ngb4ck). No `.dwg` for this board (p1ngb4ck has a `.dwg` only for JC1060P470).

### 2.5 `4-Driver_IC_Data_Sheet` — (vi)
* `ESP32_P4_Chip_Datasheet_CN_v0.4.1_0320 .pdf` — Espressif (© Espressif, public datasheet).
* `OV02C10_CSP_DS_2.01_Waching.pdf` — OmniVision datasheet (© OmniVision).
* `C-JD9365DA-H3_HKC10.1_IPS(QP101BS01-1)_800x1280_FP7721_ZigZag_G2.2_20221205_andy(2lane).txt` — panel-maker init code (no license; header quoted in §4.5).
* `1.png` — panel mechanical drawing (BOE TFT 10.1").
* NOTE: there are **no** JD9365, GSL3680, ES8311 or ESP32-C6 datasheets in the package.

### 2.6 `5-Schematic` — (vii) 7 PNG sheets, identical bytes to p1ngb4ck (see §8/§10). Vendor copyright.

### 2.7 `6-User_Manual` — (vi) `Getting started JC8012P4A1C_I_W_Y.pdf` (vendor; Arduino IDE 2.3.4 install, "ESP32P4 Dev Module", LVGL >= 8.3.0 library install). Contains no flash-offset information.

### 2.8 `7-Character&Picture_Molding_Tool` — (v) third-party Windows tools, all redistributed as archives:
`JPGCompact_V5.0_XiTongZhiJia.zip` (= 使用说明.txt, 更多系统软件下载.html, `JPG图片压缩器.exe` — freeware from a Chinese download site), `USB-SERIAL CH340.rar` (WCH driver), **`WinHex15.1SR-8.rar` (X-Ways WinHex — commercial shareware; redistribution not permitted by its license)**, `串口工具-sscom32.rar` (sscom serial tool, freeware), `网络调试助手.rar` (network debug assistant, freeware). None are needed for the open-source project; do not republish.

### 2.9 `8-Burn operation` — (iv)+(v), details in §5
* `Burn files/` 4 firmware images (§5).
* `flash_download_tool_3.9.7/` — Espressif Flash Download Tool 3.9.7 (freeware, closed source; `doc/release_note.txt`: "3.9.7: support ESP32-P4"), plus **vendor leftovers**: `dl_temp/` (191 MB of `.bin_rep` cache copies of other Guition products' firmware: JC1060P470_P4/C6, JC8048W550C_I_NEW, JC-ESP32P4-M3-DEV-TEST-V1.3, 1.85_demo, "5inch LVGL CTP", firware.bin, and `JC8012P4A1_V1.5..bin_rep` 13 697 024 B), `logs/` (14 session logs 2024-12 with device MAC addresses), `configure/*/spi_download.conf` with the vendor employee's desktop path (`C:\Users\zhang'pei\Desktop\...`, `E:\晶彩\资料下载\ESP32\P4\JC1060P470\...`). Strip all of this before publication.
* `Burn operation instructions/*.png` and `烧录说明：Burning instructions.txt` (§5.2).

## 3. GSL3680 touch driver sources — the licensing hot-spot

### 3.1 All copies (size, md5)

| path (under mirror_devinwatson/1-Demo) | size | md5 | notes |
|---|---|---|---|
| `idf-examples/common_components/esp_lcd_touch_gsl3680/esp_lcd_touch_gsl3680.c` | 22181 | 6b6cc451… | used by BSP |
| `idf-examples/esp_lcd_touch_gsl3680/esp_lcd_touch_gsl3680.c` | 24545 | fa8cf796… | loose copy |
| `idf-examples/usb_extend_screen/components/esp_lcd_touch_gsl3680/esp_lcd_touch_gsl3680.c` | 23667 | e89f7301… | |
| `arduino-examples/{esp32p4_lvgl_v8, esp32p4_lvgl _v9/esp32p4_lvgl_v9, Arduino_lvgl_sw_rotation/esp32p4_lvgl_v9}/src/touch/esp_lcd_gsl3680.c` | 23518 | 60702292… (3 identical) | |
| `idf-examples/common_components/esp_lcd_touch_gsl3680/include/esp_lcd_touch_gsl3680.h` | 93097 | 4e8b2f57… | **firmware blob**; `MAX_FINGER_NUM 3` |
| `idf-examples/usb_extend_screen/components/esp_lcd_touch_gsl3680/include/esp_lcd_touch_gsl3680.h` | 93097 | 4e8b2f57… (identical) | |
| `idf-examples/esp_lcd_touch_gsl3680/include/esp_lcd_touch_gsl3680.h` | 93098 | 1f3e20a4… | `MAX_FINGER_NUM 10` |
| `arduino-examples/*/src/touch/esp_lcd_gsl3680.h` | 93145 | 4fc1995a… (3 identical) | adds `extern "C"` |
| `gsl_point_id.c` (all 6 locations) | 81985 | ac213714… (all identical) | **Silead GPL-2.0+** |
| `gsl_point_id.h` (all 6 locations) | 337 | 945334e9… (all identical) | no license |
| `arduino-examples/esp32p4_lvgl_v8/src/touch/gsl3680_touch.cpp` / `.h` | 2908 / 361 | 7ca45448… / 444c9c74… | vendor wrapper, no license |

### 3.2 First 30 lines, verbatim

`idf-examples/common_components/esp_lcd_touch_gsl3680/esp_lcd_touch_gsl3680.c` (22 181 B) — the other two IDF copies and the Arduino `esp_lcd_gsl3680.c` start identically (Arduino copy includes `"esp_lcd_gsl3680.h"` instead):
```c
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gsl3680.h"
#include "gsl_point_id.h"

#define TAG "gsl3680"

/* gsl3680 registers */
#define ESP_LCD_TOUCH_GSL3680_READ_XY_REG     (0x80)

/* gsl3680 support key num */
#define ESP_gsl3680_TOUCH_MAX_BUTTONS         (9)


unsigned int gsl_config_data_id[] =
{
	0xccb69a,  
	0x200,
	0,0,
	0,
```
→ **No copyright, no license, no SPDX.** Lines 25–118 are `gsl_config_data_id[]` (94 lines of Silead configuration words, e.g. `0x40000d00,0xa,0xe001a,...`), passed to `gsl_DataInit()`.

`idf-examples/common_components/esp_lcd_touch_gsl3680/gsl_point_id.c` (81 985 B, identical in all 6 locations):
```c
/* drivers/input/touchscreen/mediatek/gslX680/
 *
 * 2010 - 2016 silead inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the sileadinc's CTP IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */
// #include "bsp/lcd_gsl3680.h"
#include "gsl_point_id.h"
#include "esp_log.h"
#include "stdio.h"

#define GSL_VERSION                                                            \
	0x20160901 /* NO GESTURE VERSION COME FROM VERSION 20150706 */

#ifndef NULL
#define NULL ((void *)0)
#endif
#ifndef UINT
#define UINT unsigned int
#endif
```
→ **GPL-2.0-or-later, "2010 - 2016 silead inc."**, originating from the MediaTek Android kernel tree path `drivers/input/touchscreen/mediatek/gslX680/`. It is the Silead point-ID/filter algorithm (obfuscated-style C: 12 static data tables, functions `gsl_DataInit`, `gsl_alg_id_main`, `gsl_version_id`, `gsl_mask_tiaoping`, `gsl_ReportPressure`, …, ~3 200 lines). It is compiled into every demo (`CMakeLists.txt`: `idf_component_register(SRCS "esp_lcd_touch_gsl3680.c" "gsl_point_id.c" INCLUDE_DIRS "include" REQUIRES "esp_lcd")`), i.e. linked into the same firmware image as Apache-2.0 / MIT / proprietary code. **This is a GPL-2.0 incompatibility problem for any closed or Apache-only distribution of a firmware that links it.**

`idf-examples/common_components/esp_lcd_touch_gsl3680/include/esp_lcd_touch_gsl3680.h` (93 097 B, 4 692 lines):
```c
#ifndef _LCD_GSL3680_H
#define _LCD_GSL3680_H

#include "esp_lcd_touch.h"

#define MAX_FINGER_NUM      3
#define TP_MULTI_SUCCESS    0

#define TP_PEN_NONE         0
#define TP_PEN_MOVE         1
#define TP_PEN_UP           2
#define TP_PEN_DOWN         3

#define TG_NO_DETECT        0
#define TG_ZOOM_IN          1
#define TG_ZOOM_OUT         2
#define TG_UNKNOWN_STATE    3

#define TP_UP_MSG           0
#define TP_DOWN_MSG         1
#define TP_MOVE_MSG         1
#define TP_MAX_MSG          0

typedef unsigned int DAWORD;
typedef unsigned short WORD;
typedef unsigned char BYTE;

typedef int s32;
typedef short s16;
typedef char s8;
```
→ **No copyright/license.** Lines 59–63: `esp_err_t esp_lcd_touch_new_i2c_gsl3680(...)`, `#define ESP_LCD_TOUCH_IO_I2C_GSL3680_ADDRESS (0x40)`. Lines 64–68 `struct fw_data { char offset; uint32_t val; };` and line 88 `static const struct fw_data GSLX680_FW[] = {` followed by **4 587 `{offset, value}` entries** (first entry `{0xf0,0x2}`, then `{0x00,0x00000000}` …; the tail decodes to the ASCII build stamp "Jan  8 2018 14:24:49"). **Yes — a Silead GSLX680 touch-controller firmware image is embedded in the header**, in every copy (IDF and Arduino). It is written to the chip at init by `esp_lcd_touch_gsl3680_load_fw()` (4-byte I2C writes per entry, 1-byte page-select writes at reg 0xf0). No license or ownership statement accompanies the blob.

`include/gsl_point_id.h` (337 B, whole file, identical everywhere):
```c
#ifndef _GSL_POINT_ID_H
#define _GSL_POINT_ID_H

struct gsl_touch_info
{
    int x[10];
    int y[10];
    int id[10];
    int finger_num;
};

unsigned int gsl_mask_tiaoping(void);
unsigned int gsl_version_id(void);
void gsl_alg_id_main(struct gsl_touch_info *cinfo);
void gsl_DataInit(unsigned int *conf_in);

#endif
```

`arduino-examples/esp32p4_lvgl_v8/src/touch/gsl3680_touch.cpp` (2 908 B), first 30 lines:
```cpp
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_gsl3680.h"
#include "gsl3680_touch.h"

#define CONFIG_LCD_HRES 800
#define CONFIG_LCD_VRES 1280

static const char *TAG = "example";

esp_lcd_touch_handle_t tp;
esp_lcd_panel_io_handle_t tp_io_handle;

uint16_t touch_strength[1];
uint8_t touch_cnt = 0;

gsl3680_touch::gsl3680_touch(int8_t sda_pin, int8_t scl_pin, int8_t rst_pin, int8_t int_pin)
{
    _sda = sda_pin;
    _scl = scl_pin;
    _rst = rst_pin;
    _int = int_pin;
}

void gsl3680_touch::begin()
```
`gsl3680_touch.h` (361 B, whole): `#ifndef _GT911_TOUCH_H / #define _GT911_TOUCH_H / #include <stdio.h> / class gsl3680_touch { public: gsl3680_touch(int8_t sda_pin, int8_t scl_pin, int8_t rst_pin = -1, int8_t int_pin = -1); void begin(); bool getTouch(uint16_t *x, uint16_t *y); void set_rotation(uint8_t r); private: int8_t _sda, _scl, _rst, _int; }; #endif` — no license.

### 3.3 Summary of the finding
* `gsl_point_id.c`: **GPL-2.0-or-later (Silead, 2010–2016)** — explicit, verbatim.
* `esp_lcd_touch_gsl3680.c` / `.h`, `gsl_point_id.h`, Arduino wrappers: **no license text at all** (vendor/Silead-derived; the `.c` is modelled on Espressif's `esp_lcd_touch_gt911.c` structure but the Espressif SPDX header is absent).
* Silead firmware blob `GSLX680_FW[]` (4 587 words) and config table `gsl_config_data_id[]` (≈90 words) are embedded in source form, no license.
* Nothing in the package claims the right to redistribute Silead firmware; the only licence that is present (GPL-2.0+) is on the algorithm file.

## 4. Vendor-patched BSP and JD9365 panel driver

### 4.1 BSP identity
`1-Demo/idf-examples/common_components/esp32_p4_function_ev_board/idf_component.yml` (verbatim):
```yaml
dependencies:
  esp_codec_dev:
    public: true
    version: 1.3.2
  esp_lcd_ek79007: 1.*
  esp_lcd_ili9881c: 1.*
  esp_lcd_touch_gt911: ^1
  esp_lcd_jd9365: 
    path: ../esp_lcd_jd9365
  esp_lcd_touch_gsl3680:
    path: ../esp_lcd_touch_gsl3680
  espressif/esp_lvgl_port:
    public: true
    version: ^2
  idf: '>=5.3'
  lvgl/lvgl: '>=8,<10'
description: Board Support Package (BSP) for ESP32-P4 Function EV Board (preview)
repository: git://github.com/espressif/esp-bsp.git
repository_info:
  commit_sha: 0064ab931c38699fc993ee161713ac1d1c0943a5
  path: bsp/esp32_p4_function_ev_board
tags:
- bsp
targets:
- esp32p4
url: https://github.com/espressif/esp-bsp/tree/master/bsp/esp32_p4_function_ev_board
version: 4.1.1
```
→ **esp-bsp `esp32_p4_function_ev_board` v4.1.1** (esp-bsp commit `0064ab93`), Apache-2.0 (`LICENSE` = Apache 2.0 text; `esp32_p4_function_ev_board.c` header `SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD / SPDX-License-Identifier: Apache-2.0`). Vendor patches: two path dependencies (`esp_lcd_jd9365`, `esp_lcd_touch_gsl3680`), `#include "esp_lcd_jd9365.h"` / `"esp_lcd_touch_gsl3680.h"`, replaced the ILI9881C branch with JD9365 and GT911 with GSL3680, changed pins. Kconfig still offers `BSP_LCD_TYPE_1024_600 "LCD 7-inch 1024x600 - ek79007"` vs default `BSP_LCD_TYPE_1280_800 "LCD 1280x800 - ili9881c"`; log strings still say "ILI9881C". A second, older BSP copy (`usb_extend_screen/components/esp32_p4_function_ev_board`, version 0.0.1, from the esp-iot-solution example) also depends on the same two path components.

### 4.2 Display configuration — verbatim

`include/bsp/display.h` lines 50–67 (the `#else` branch = this board; the `#if CONFIG_BSP_LCD_TYPE_1024_600` branch is the Espressif 7" kit):
```c
/* LCD display definition 1280x800 */
#define BSP_LCD_H_RES              (800)
#define BSP_LCD_V_RES              (1280)

#define BSP_LCD_MIPI_DSI_LCD_HSYNC    (40)
#define BSP_LCD_MIPI_DSI_LCD_HBP      (140)
#define BSP_LCD_MIPI_DSI_LCD_HFP      (40)
#define BSP_LCD_MIPI_DSI_LCD_VSYNC    (4)
#define BSP_LCD_MIPI_DSI_LCD_VBP      (16)
#define BSP_LCD_MIPI_DSI_LCD_VFP      (16)
#endif

#define BSP_LCD_MIPI_DSI_LANE_NUM          (2)    // 2 data lanes
#define BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS (1000) // 1Gbps

#define BSP_MIPI_DSI_PHY_PWR_LDO_CHAN       (3)  // LDO_VO3 is connected to VDD_MIPI_DPHY
#define BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV (2500)
```
**Caution:** the `BSP_LCD_MIPI_DSI_LCD_H*/V*` porch macros and `BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS (1000)` are NOT referenced anywhere in `esp32_p4_function_ev_board.c` (leftovers from the ILI9881C BSP). The values actually programmed are below.

`include/bsp/display.h` lines 22–37 (pixel format):
```c
/* LCD color formats */
#define ESP_LCD_COLOR_FORMAT_RGB565    (1)
#define ESP_LCD_COLOR_FORMAT_RGB888    (2)

/* LCD display color format */
#if CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
#define BSP_LCD_COLOR_FORMAT        (ESP_LCD_COLOR_FORMAT_RGB888)
#else
#define BSP_LCD_COLOR_FORMAT        (ESP_LCD_COLOR_FORMAT_RGB565)
#endif
/* LCD display color bytes endianess */
#define BSP_LCD_BIGENDIAN           (0)
/* LCD display color bits */
#define BSP_LCD_BITS_PER_PIXEL      (16)
/* LCD display color space */
#define BSP_LCD_COLOR_SPACE         (ESP_LCD_COLOR_SPACE_RGB)
```
Kconfig defaults: `BSP_LCD_COLOR_FORMAT_RGB565` (RGB888 selectable), `BSP_LCD_DPI_BUFFER_NUMS` 1 (1–3), `BSP_DISPLAY_BRIGHTNESS_LEDC_CH` 1, `BSP_I2C_NUM` 1, `BSP_I2C_CLK_SPEED_HZ` 400000, `BSP_I2S_NUM` 1. `include/bsp/esp32_p4_function_ev_board.h` line 261: `#define BSP_LCD_PIXEL_CLOCK_MHZ     (80)` (also unused by the DSI path).

`esp32_p4_function_ev_board.c` lines 389–400 (DSI PHY LDO):
```c
static esp_err_t bsp_enable_dsi_phy_power(void)
{
#if BSP_MIPI_DSI_PHY_PWR_LDO_CHAN > 0
    // Turn on the power for MIPI DSI PHY, so it can go from "No Power" state to "Shutdown" state
    static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = BSP_MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan), TAG, "Acquire LDO channel for DPHY failed");
    ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
#endif // BSP_MIPI_DSI_PHY_PWR_LDO_CHAN > 0
```
`esp32_p4_function_ev_board.c` lines 424–442 (DSI bus — **lane bit rate 1500 Mbps hard-coded**):
```c
    /* create MIPI DSI bus first, it will initialize the DSI PHY as well */
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = BSP_LCD_MIPI_DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 1500,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus), TAG, "New DSI bus init failed");

    ESP_LOGI(TAG, "Install MIPI DSI LCD control panel");
    // we use DBI interface to send LCD commands and parameters
    esp_lcd_panel_io_handle_t io;
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,   // according to the LCD ILI9881C spec
        .lcd_param_bits = 8, // according to the LCD ILI9881C spec
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io), err, TAG, "New panel IO failed");
```
`esp32_p4_function_ev_board.c` lines 471–497 (panel):
```c
#else
    // create ILI9881C control panel
    ESP_LOGI(TAG, "Install ILI9881C LCD control panel");
#if CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
    esp_lcd_dpi_panel_config_t dpi_config = JD9365_800_1280_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB888);
#else
    esp_lcd_dpi_panel_config_t dpi_config = JD9365_800_1280_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
#endif
    dpi_config.num_fbs = CONFIG_BSP_LCD_DPI_BUFFER_NUMS;

    jd9365_vendor_config_t vendor_config = {
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = BSP_LCD_MIPI_DSI_LANE_NUM,
        },
    };
    const esp_lcd_panel_dev_config_t lcd_dev_config = {
        .reset_gpio_num = BSP_LCD_RST,
        .rgb_ele_order = BSP_LCD_COLOR_SPACE,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_jd9365(io, &lcd_dev_config, &disp_panel), err, TAG, "New LCD panel ILI9881C failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(disp_panel), err, TAG, "LCD panel reset failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(disp_panel), err, TAG, "LCD panel init failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_disp_on_off(disp_panel, true), err, TAG, "LCD panel ON failed");
#endif
```
The DPI timing macro used, `common_components/esp_lcd_jd9365/include/esp_lcd_jd9365.h` lines 69–118 (the `_XIAOZHI` variant is a vendor addition absent from upstream esp-iot-solution; it only reorders fields):
```c
#define JD9365_PANEL_BUS_DSI_2CH_CONFIG()                \
    {                                                    \
        .bus_id = 0,                                     \
        .num_data_lanes = 2,                             \
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,     \
        .lane_bit_rate_mbps = 1500,                      \
    }
...
#define JD9365_800_1280_PANEL_60HZ_DPI_CONFIG(px_format) \
    {                                                    \
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,     \
        .dpi_clock_freq_mhz = 60,                        \
        .virtual_channel = 0,                            \
        .pixel_format = px_format,                       \
        .num_fbs = 1,                                    \
        .video_timing = {                                \
            .h_size = 800,                               \
            .v_size = 1280,                              \
            .hsync_back_porch = 20,                      \
            .hsync_pulse_width = 20,                     \
            .hsync_front_porch = 40,                     \
            .vsync_back_porch = 8,                      \
            .vsync_pulse_width = 4,                      \
            .vsync_front_porch = 20,                     \
        },                                               \
        .flags.use_dma2d = true,                         \
    }

#define JD9365_800_1280_PANEL_60HZ_DPI_CONFIG_XIAOZHI(px_format) \
     {                                                    \
        .virtual_channel = 0,                            \
         .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,     \
         .dpi_clock_freq_mhz = 60,                        \
         .pixel_format = px_format,                       \
         .num_fbs = 1,                                    \
         .video_timing = {                                \
             .h_size = 800,                               \
             .v_size = 1280,                              \
             .hsync_pulse_width = 20,                     \
             .hsync_back_porch = 20,                      \
             .hsync_front_porch = 40,                     \
             .vsync_pulse_width = 4,                      \
             .vsync_back_porch = 8,                      \
             .vsync_front_porch = 20,                     \
         },                                               \
         .flags={                                          \
            .use_dma2d = true,                             \
        },                        \
     }
```
**Effective display block for this board:** 800 x 1280, MIPI-DSI 2 data lanes, **1500 Mbps/lane** (BSP, Arduino and `esp_draw_bit` all use 1500; the unused header macro says 1000), **DPI clock 60 MHz** (the `ESP-IDF_lvgl_sw_rotation` demo uses 63 MHz), HSYNC 20 / HBP 20 / HFP 40, VSYNC 4 / VBP 8 / VFP 20 → 60e6 / (880 × 1312) = **52.0 Hz actual refresh** despite the "60HZ" macro name; RGB565 16 bpp by default (RGB888 via Kconfig), `use_dma2d = true`, DBI cmd/param 8 bit, virtual channel 0; `rgb_ele_order = ESP_LCD_COLOR_SPACE_RGB`; the JD9365 driver writes lane command `0x80 = JD9365_DSI_2_LANE (0x01)` according to `lane_num`. The panel-maker init file (§4.5) specifies the same porches (`vertical_sync_active=4, vertical_backporch=8, vertical_frontporch=20, horizontal_sync_active=20, horizontal_backporch=20, horizontal_frontporch=40, PLL_CLOCK=416`).

### 4.3 Pins — `include/bsp/esp32_p4_function_ev_board.h` lines 45–76 (verbatim)
```c
/* I2C */
#define BSP_I2C_SCL           (GPIO_NUM_8)
#define BSP_I2C_SDA           (GPIO_NUM_7)

/* Audio */
#define BSP_I2S_SCLK          (GPIO_NUM_12)
#define BSP_I2S_MCLK          (GPIO_NUM_13)
#define BSP_I2S_LCLK          (GPIO_NUM_10)
#define BSP_I2S_DOUT          (GPIO_NUM_9)
#define BSP_I2S_DSIN          (GPIO_NUM_11)
#define BSP_POWER_AMP_IO      (GPIO_NUM_20)

/* Display */
#if CONFIG_BSP_LCD_TYPE_1024_600
#define BSP_LCD_BACKLIGHT     (GPIO_NUM_26)
#define BSP_LCD_RST           (GPIO_NUM_27)
#define BSP_LCD_TOUCH_RST     (GPIO_NUM_NC)
#define BSP_LCD_TOUCH_INT     (GPIO_NUM_NC)
#else
#define BSP_LCD_BACKLIGHT     (GPIO_NUM_23)
#define BSP_LCD_RST           (GPIO_NUM_27)
#define BSP_LCD_TOUCH_RST     (GPIO_NUM_22)
#define BSP_LCD_TOUCH_INT     (GPIO_NUM_21)
#endif

/* uSD card */
#define BSP_SD_D0             (GPIO_NUM_39)
#define BSP_SD_D1             (GPIO_NUM_40)
#define BSP_SD_D2             (GPIO_NUM_41)
#define BSP_SD_D3             (GPIO_NUM_42)
#define BSP_SD_CMD            (GPIO_NUM_44)
#define BSP_SD_CLK            (GPIO_NUM_43)
```
Summary: LCD reset GPIO27; backlight GPIO23 (LEDC low-speed, timer 1, 10-bit, **20 kHz**, channel `CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH`=1); touch RST GPIO22 / INT GPIO21; I2C SDA 7 / SCL 8 (port 1, 400 kHz); I2S SCLK 12 / MCLK 13 / LCLK 10 / DOUT 9 / DSIN 11; PA enable GPIO20 (`.pa_pin = BSP_POWER_AMP_IO, .pa_reverted = false, .pa_voltage = 5.0`); codec **ES8311** at `ES8311_CODEC_DEFAULT_ADDR` for both speaker and mic (`es8311_codec_new`; no ES7210); SD = SDMMC slot 0, 4-bit, IO-MUX pins D0 39 D1 40 D2 41 D3 42 CMD 44 CLK 43, `SDMMC_FREQ_HIGHSPEED`, powered by on-chip **LDO channel 4** (`sd_pwr_ctrl_ldo_config_t ldo_config = { .ldo_chan_id = 4 }`, lines 137–138, `sd_pwr_ctrl_new_on_chip_ldo`); DSI PHY = **LDO channel 3 @ 2500 mV**. The C6 (SDIO) pins are not in this BSP; the `usb_extend_screen` BSP copy and the ESPHome yaml give `BSP_WIRELESS_EN GPIO54`, `D0 14, D1 15, D2 16, D3 17` (+ CLK 18, CMD 19 per ESPHome).

### 4.4 Touch installation — `esp32_p4_function_ev_board.c` lines 523–554 (verbatim)
```c
esp_err_t bsp_touch_new(const bsp_touch_config_t *config, esp_lcd_touch_handle_t *ret_touch)
{
    /* Initilize I2C */
    BSP_ERROR_CHECK_RETURN_ERR(bsp_i2c_init());

    /* Initialize touch */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = BSP_LCD_TOUCH_RST, // Shared with LCD reset
        .int_gpio_num = BSP_LCD_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
#if CONFIG_BSP_LCD_TYPE_1024_600
            .mirror_x = 1,
            .mirror_y = 1,
#else
            .mirror_x = 0,
            .mirror_y = 1,
#endif
        },
    };
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GSL3680_CONFIG();
    tp_io_config.scl_speed_hz = CONFIG_BSP_I2C_CLK_SPEED_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_handle, &tp_io_config, &tp_io_handle), TAG, "");
    return esp_lcd_touch_new_i2c_gsl3680(tp_io_handle, &tp_cfg, ret_touch);
}
```
Mechanism inside `esp_lcd_touch_new_i2c_gsl3680()` (`esp_lcd_touch_gsl3680.c` 161–260): I2C address **0x40** (`ESP_LCD_TOUCH_IO_I2C_GSL3680_ADDRESS`); configures RST and INT as outputs, drives RST=0, INT=0, waits 10 ms, INT=0 (address select), RST=1; then `touch_gsl3680_read_cfg()` (writes 0x12 0x34 0x56 to reg 0xf0 and reads back — the result is never actually compared; `ret` depends on two locals so it always returns `ESP_FAIL`, which the caller ignores), `esp_lcd_touch_gsl3680_init()` = `clear_reg` (RST pulse, reg 0x88←0x01, 0xe4←0x04, 0xe0←0x00) → `touch_gsl3680_reset` (RST pulse, 0xe4←0x04, 0xbc←0) → **`esp_lcd_touch_gsl3680_load_fw()` streams all 4 587 `GSLX680_FW[]` words over I2C** → `startup_chip` (0xe0←0x00, `gsl_DataInit(gsl_config_data_id)`) → reset → startup again; then `read_ram_fw()` checks reg 0xb0 == 0x5a5a5a5a. Reads use reg 0x80 (`ESP_LCD_TOUCH_GSL3680_READ_XY_REG`) and are post-processed by the GPL `gsl_alg_id_main()`. The firmware is therefore uploaded from the host on every boot (GSL3680 has no flash).

### 4.5 JD9365 panel driver
`common_components/esp_lcd_jd9365/idf_component.yml`: `version: 1.0.2`, `repository: git://github.com/espressif/esp-iot-solution.git`, `commit_sha: 6a112f4ddfeaf30ec360567ea9260a39e195c385`, `path: components/display/lcd/esp_lcd_jd9365`, deps `cmake_utilities 0.*`, `idf >=5.3`; `license.txt` Apache-2.0; `esp_lcd_jd9365.c` header `SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD / SPDX-License-Identifier: Apache-2.0`; CHANGELOG up to v1.0.2 (2025-01-13). Vendor modifications vs the copy in `usb_extend_screen/components/espressif__esp_lcd_jd9365` (same version): one added `ESP_LOGI(TAG,"mirrot panel_jd9365")` in `panel_jd9365_mirror()`, and the `_XIAOZHI` macro in the header (`include/esp_lcd_jd9365.h` 6067 B vs 4853 B). The 198-entry `vendor_specific_init_default[]` (starts `{0xE0,{0x00}}, {0xE1,{0x93}}, {0xE2,{0x65}}, {0xE3,{0xF8}}, {0x80,{0x01}}` …) is the panel maker's sequence from `4-Driver_IC_Data_Sheet/...txt` (196 `{0x..}` lines + 2 delay rows); the Arduino copy and `ESP-IDF_lvgl_sw_rotation/main/lvgl_sw_rotation.c` (`lcd_cmd[]`, 198 entries) carry the same table. The driver selects the lane count by writing `0x80` (`JD9365_CMD_DSI_INT0`) with `JD9365_DSI_2_LANE (0x01)` (init file comment: `//03=4lane ,01=2lane`).

Panel-maker init file header (verbatim first lines of `4-Driver_IC_Data_Sheet/C-JD9365DA-H3_HKC10.1_IPS(QP101BS01-1)_800x1280_FP7721_ZigZag_G2.2_20221205_andy(2lane).txt`):
```
params->dsi.vertical_sync_active=4
params->dsi.vertical_backporch=8
params->dsi.vertical_frontporch=20
params->dsi.horizontal_sync_active=20
params->dsi.horizontal_backporch=20
params->dsi.horizontal_frontporch=40
params->dsi.PLL_CLOCK=416
 
LCD_nReset=1;
Delayms(5);
LCD_nReset=0;
Delayms(10);
LCD_nReset=1;
Delayms(120);
```
(then 196 `{reg,1,{val}}` rows ending `{0x11,...},{REGFLAG_DELAY,120},{0x29,...},{REGFLAG_DELAY,5},{0x35,1,{0x00}},{REGFLAG_END_OF_TABLE}`.)

### 4.6 Other board-config variants in the package (all consistent with §4.3)
* `esp_draw_bit/main/main.c` (no BSP): I2C 7/8, BL 23, RST 27, TP RST 22 / INT 21, 800x1280, 2 lanes, `BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS (1500)`, LDO 3 @ 2500 mV.
* Arduino `jd9365_lcd.cpp`: `JD9365_PANEL_BUS_DSI_2CH_CONFIG()` (1500 Mbps), `JD9365_800_1280_PANEL_60HZ_DPI_CONFIG(RGB565)`, LDO chan 3 @ 2500 mV, backlight GPIO23.
* `ESP-IDF_lvgl_sw_rotation/main/lvgl_sw_rotation.c`: DPI **63 MHz**, same porches, `reset_gpio_num = GPIO_NUM_27` (a stray `#define BSP_LCD_RST (GPIO_NUM_9)` is unused), I2C port 1 SDA 7 SCL 8; **touch creation is commented out** (only gt911/cst816s headers included, no gsl3680), log string still says "Install LCD driver of st7701"; README is Espressif's "MIPI-DSI Avoid Tearing Example" text (EK79007/GT911).

## 5. `8-Burn operation` firmware images

### 5.1 `Burn files/` — all four are full merged flash images to be written at offset **0x0** (bootloader + partition table + app)

| file | bytes | SHA256 | chip | partition table @0x8000 | app descriptor (`esp_app_desc_t`) |
|---|---|---|---|---|---|
| `JC-C6-slave_v2.3.2.bin` | 1 196 480 | `f9ec1205c5fa1e497032dedcee33d2ab9560bd2768fc1ea06acb6d8e2773f80c` | **ESP32-C6** (image chip-id 0x000d; bootloader @0x0, app @0x10000) | nvs 0x9000/0x4000, otadata 0xd000/0x2000, phy_init 0xf000/0x1000, ota_0 0x10000/0x180000, ota_1 0x190000/0x180000 | project **`network_adapter`**, version `f0a63f7-dirty`, idf_ver `v5.5`, built `Aug 26 2025 11:55:53`. Contains `esp_hosted_coprocessor.c`, `esp_hosted_rpc.pb-c.c`, `"ESP-Hosted-MCU Slave FW version :: %d.%d.%d"`, SDIO slave + BT controller (NimBLE HCI) code. ⇒ **ESP-Hosted-MCU slave (co-processor) firmware; filename says v2.3.2** (the numeric version is a compiled constant and could not be read from strings). |
| `JC8012P4A1_C6.bin` | 1 146 768 | `8b16a042713bca67d64a1d7af6a296432c1c5f8116fe015ae24acfca3a66fec0` | **ESP32-C6** (same layout) | identical layout | project **`network_adapter`**, version `54aafd0-dirty`, idf_ver `v5.4-dev-3951-g9106c43acc-dirty`, built `Dec 26 2024 18:41:41`. Same ESP-Hosted-MCU slave strings. ⇒ older C6 slave build (esp_hosted version unknown; `54aafd0` / `f0a63f7` are not commits of `espressif/esp-hosted-mcu` per GitHub API — vendor-local trees). |
| `JC8012P4A1_P4-V2.8.bin` | 13 697 024 | `ef972908b06889aa1cf43194c613fa848804f17a444b847b954615f05a59c4b8` | **ESP32-P4** (bootloader @0x2000, app @0x10000) | nvs 0x9000/0x6000, phy_init 0xf000/0x1000, factory 0x10000/0x900000, storage(spiffs) 0x910000/0x400000 — matches `esp_brookesia_phone/partitions.csv` | project **`esp_brookesia_demo`**, version `1.0.1`, idf_ver `v5.5-dev-3618-g7f6e7f4506-dirty`, built `Jun 20 2025 18:02:02`. Contains esp_hosted **host** strings (`esp_hosted/host/...`, SDIO transport). ⇒ the factory demo (esp_brookesia_phone) incl. spiffs media. |
| `JC8012P4A1C_I_W_Y_xiaozhi2.0.4.bin` | 11 240 285 | `7bc321ff9c27aaa8b678e968ec12e7ec681242352caa7431fdb1d9651b25ae2e` | **ESP32-P4** (bootloader @0x2000, app @0x20000) | nvs 0x9000/0x4000, otadata 0xd000/0x2000, phy_init 0xf000/0x1000, ota_0 0x20000/0x3f0000, ota_1 0x410000/0x3f0000, assets 0x800000/0x800000 | project **`xiaozhi`**, version `2.0.4`, idf_ver `v5.5.1-dirty`, built `Dec 25 2025 17:25:06`; board id string `guition-jc8012p4a1` (`boards/guition-jc8012p4a1/jc8012p4a1.cc`), esp_hosted host strings. ⇒ xiaozhi-esp32 (github.com/78/xiaozhi-esp32, MIT) v2.0.4 build for this board. |

### 5.2 Flash-offset documentation present
* `8-Burn operation/烧录说明：Burning instructions.txt` (verbatim):
  ```
  1、电脑需要安装CH340驱动程序：The computer needs to install the CH340 driver program
  2、保证供电充足，开发板电流600ma以上：Ensure sufficient power supply, with a development board current of over 600mA
  3、烧录选择USB1口：Choose USB1 port for burning
  ```
* `Burn operation instructions/Burn operation-1.png`: tool mode dialog ChipType **ESP32-P4**, WorkMode Develop, LoadMode UART. `Burn operation-2.png`: SPIDownload tab, one file row (a JC1060P470 path) at **`@ 0x0000`**, SPI SPEED **80MHz**, SPI MODE **QIO**, DoNotChgBin checked, COM6, BAUD 1152000, START. `Burn operation-3.png`: result.
* `flash_download_tool_3.9.7/configure/esp32p4/spi_download.conf`: `file_path0 = ...\JC-ESP32P4-M3-DEV-TEST-V1.3.bin`, `file_offset0 = 0x0000`, `spispeed = 3`, `spimode = 0`, COM4. `configure/esp32c6/spi_download.conf`: `file_path0 = ...\JC1060P470\8-Burn operation\Burn files\JC1060P470_C6.bin`, `file_offset0 = 0x0`, `spimode = 2`, COM6.
* `logs/*.txt` confirm `Wrote 13697024 bytes (4639600 compressed) at 0x00000000` (= the P4 V2.8 image) and one `Wrote 16777216 bytes at 0x00000000` (full 16 MB dump).
* No README describes which USB port talks to the C6; `readme.docx` and the user manual do not cover the C6 at all.

## 6. ESP-IDF demo projects (`1-Demo/idf-examples`)

`readme.docx` (vendor, Chinese; extracted): "使用的版本为esp-idf v5.4" (**ESP-IDF v5.4 is the stated version**); lvgl_demo_v8 / lvgl_demo_v9 / esp_draw_bit / esp_brookesia_phone: `idf.py set-target esp32p4; idf.py flash monitor`; usb_extend_screen needs menuconfig, then works as a Windows secondary screen over the high-speed USB port ("driver does not support portrait, will not fill the screen"); xiaozhi-esp32: source at github.com/78/xiaozhi-esp32, "the provided xiaozhi-esp32 sample is v1.5.6", copy the `jc1060pa1` folder into xiaozhi and add `common_components` via `EXTRA_COMPONENT_DIRS` (edits to `system_info.cc`, `Kconfig.projbuild`, `CMakeLists.txt`, `lcd_display.cc`). **No xiaozhi sample folder exists in the mirror** (nothing named xiaozhi/jc1060pa1 anywhere; the git pack has no LFS). There is no `ESP-IDF_5.5.4`-style folder; `sdkconfig` files record `CONFIG_IDF_INIT_VERSION="5.4.0"` for all projects except `ESP-IDF_lvgl_sw_rotation` (`"6.0.0"`, target esp32p4); the BSP requires `idf >=5.3`; the shipped binaries were built with IDF v5.5-dev / v5.5.1.

| project | purpose | key deps / notes |
|---|---|---|
| `lvgl_demo_v8` | LVGL 8.4 `lv_demo_music()` on the BSP (`main/main.c`, no license header) | `lvgl/lvgl 8.4.*`; README is Espressif's generic P4 text |
| `lvgl_demo_v9` | LVGL 9.2 demo via BSP (`main/main.c` has Espressif SPDX 2025) | `lvgl/lvgl 9.2.*` |
| `esp_draw_bit` | touch-paint test: draws dots where the GSL3680 reports touches, raw esp_lcd + DSI, no BSP (vendor code, no license) | `lvgl/lvgl 8.4.*` (README is a copy of lvgl_demo_v8's) |
| `esp_brookesia_phone` | Android-like phone UI (esp-brookesia 0.4.2) with calculator / camera (esp_video + human/pedestrian detect) / 2048 / image viewer / music player / settings (Wi-Fi via esp_hosted + esp_wifi_remote) / video player; vendor added `main/Kconfig.projbuild` (`EXAMPLE_ENABLE_SD_CARD`), `partitions.csv` (factory 9M + spiffs 4M), `spiffs/` media, `components/apps`, and modified `main.cpp` (diffed against the upstream example bundled in `managed_components/espressif__esp-brookesia/examples/esp_idf/esp_brookesia_phone_p4_function_ev_board`) | `espressif/esp_wifi_remote 0.*`, `espressif/esp_hosted 0.*`, `espressif/esp-brookesia 0.4.*`, `esp_video` (path) — esp_hosted/wifi_remote are NOT vendored (fetched at build) |
| `usb_extend_screen` | esp-iot-solution "USB Extension Screen": P4 as a USB display + 5-point touch + audio for Windows; own BSP copy 0.0.1 patched with jd9365/gsl3680; `windows_driver/` holds only READMEs | `leeebo/tinyusb_src`, `espressif/esp_lcd_ek79007`, `esp_codec_dev 1.3.0`, `lvgl 9.2.0`; README still says 1024x600 |
| `ESP-IDF_lvgl_sw_rotation/lvgl_sw_rotation` | Espressif "MIPI-DSI avoid tearing" example adapted to JD9365 with LVGL 9.2.2 software rotation (lvgl vendored in `components/lvgl__lvgl`); touch disabled | `esp_lcd_touch_gt911 ^1`, `lvgl ^9.2.2`, `esp_lcd_st7701 ^1`, `esp_lcd_touch_cst816s ^1.1.0`, `espressif/esp_lcd_jd9365 ^1.0.2` (registry); sdkconfig IDF 6.0.0 |
| `esp_lcd_touch_gsl3680` (folder) | not a project: a loose copy of the touch component (`MAX_FINGER_NUM 10` variant) | `esp_lcd_touch ^1.1.0` |
| `video_lcd_display.zip` | Espressif esp_video "video_lcd_display" example (camera → LCD) with `espressif__esp_cam_sensor` (Apache-2.0) | not unpacked |
| `common_components/` | `esp32_p4_function_ev_board` 4.1.1 (patched), `bsp_extra` 0.0.2, `esp_lcd_jd9365` 1.0.2, `esp_lcd_touch_gsl3680`, `esp_cam_sensor` 0.9.0, `esp_video` 0.8.0~3, `espressif__esp_lcd_jd9165` 1.0.2 | |

## 7. ESPHome yaml from p1ngb4ck — `p1ngb4ck/JC8012P4A1C_I_W_Y/esphome_yaml_example/JC8012P4A1C_I_W_Y.yaml` (2 580 B, verbatim)

```yaml
external_components:
  - source: github://willumpie82/esphome@dev
    components: [mipi_dsi]
  - source: github://kvj/esphome@jd9365_gsl3680
    components: [gsl3680]

esphome:
  name: esp32-p4-tab10
  friendly_name: 10.1inch-tab
  platformio_options:
    board_build.flash_mode: dio

esp32:
  variant: ESP32P4
  flash_size: 16MB
  cpu_frequency: 400MHz
  framework:
    type: esp-idf
    advanced:
      enable_idf_experimental_features: yes

logger:
  level: DEBUG

psram:
  mode: hex
  speed: 200MHz

esp32_hosted:
  variant: ESP32C6
  reset_pin: GPIO54
  cmd_pin: GPIO19
  clk_pin: GPIO18
  d0_pin: GPIO14
  d1_pin: GPIO15
  d2_pin: GPIO16
  d3_pin: GPIO17
  active_high: true

output:
  - id: backlight_output
    platform: ledc
    pin: GPIO23

light:
  - platform: monochromatic
    output: backlight_output
    name: LCD Backlight
    id: display_backlight
    restore_mode: ALWAYS_ON
    default_transition_length: 0s
    gamma_correct: 1.0

esp_ldo:
  - channel: 3
    id: dsi_phy_enable
    voltage: 2.5V
    adjustable: True

display:
  - platform: mipi_dsi
    model: JC8012P4A1
    reset_pin: GPIO27
    update_interval: 17ms
    id: my_display
    rotation: 180
    auto_clear_enabled: false
    color_order: RGB
    dimensions:
      width: 800
      height: 1280

i2c:
  - id: i2c_bus_a
    sda: GPIO7
    scl: GPIO8
    scan: true
    frequency: 400kHz
    sda_pullup_enabled: False
    scl_pullup_enabled: False

touchscreen:
  - platform: gsl3680
    id: my_touchscreen
    reset_pin: GPIO22
    interrupt_pin: GPIO21
    transform:
      swap_xy: false
      mirror_x: false
      mirror_y: true
    on_touch:
      then:
        - lvgl.label.update:
            id: txtlbl
            text:
              format: "Touch at %d/%d"
              args: [touch.x, touch.y]
    on_release:
      then:
        - if:
            condition: lvgl.is_paused
            then:
              - light.turn_on: display_backlight
              - lvgl.resume:
              - lvgl.widget.redraw:

lvgl:
  buffer_size: 100%
  byte_order: little_endian
  displays:
    - my_display
  touchscreens:
    - my_touchscreen
  on_idle:
    timeout: 300s
    then:
      - logger.log: "LVGL is idle"
      - lvgl.pause:
      - light.turn_off:
          id: display_backlight
          transition_length: 2s
  widgets:
    - label:
        id: txtlbl
        text_color: black
        text: "ESPHOME"
        align: CENTER
        text_font: montserrat_48
```
Pins defined: ESP32-C6 (esp_hosted SDIO) reset **GPIO54**, CMD **GPIO19**, CLK **GPIO18**, D0 **GPIO14**, D1 **GPIO15**, D2 **GPIO16**, D3 **GPIO17** (active_high); backlight **GPIO23** (LEDC); DSI PHY **LDO channel 3 @ 2.5 V**; LCD reset **GPIO27** (model `JC8012P4A1`, 800x1280, rotation 180, RGB); I2C SDA **GPIO7** / SCL **GPIO8** @ 400 kHz, no internal pull-ups; GSL3680 reset **GPIO22**, interrupt **GPIO21**, `mirror_y: true` (same as the BSP's `.mirror_y = 1`). PSRAM hex 200 MHz, flash 16 MB DIO, CPU 400 MHz. The ESPHome gsl3680 component (`kvj/esphome@jd9365_gsl3680`) necessarily carries its own copy of the Silead firmware — not part of this package. The folder's `.gitignore` is the ESPHome default (`/.esphome/`, `/secrets.yaml`).

## 8. Schematic sheets (p1ngb4ck `JC8012P4A1C_I_W_Y/schematics/`, PNG, not OCR'd)
`1_PWR.png` (127 803), `2_LCD.png` (122 823), `3_ESP32-P4.png` (170 922), `4_CONN.png` (278 706), `5_ESP32-C6.png` (175 073), `6_USB.png` (191 639), `7_CODEC.png` (205 835). Same seven files, same sizes, in the mirror's `5-Schematic/`; SHA256 cross-check in §10. p1ngb4ck also holds `specification/JC8012P4A1C_I_W Specifications-EN-v1.1(2).pdf` and `structure_diagram/{1.jpg, 2.jpg, JC8012P4A1.pdf}` — identical to the mirror's `2-Specification` / `3-Structure_Diagram`. (For reference the sibling boards in p1ngb4ck: `JC-ESP32P4-M3-Dev` has 8 sheets + Altium `.PcbLib/.SCHLIB`; `JC1060P470_I_W_Y` has a schematic PDF `JC1060P470C_I_W_Y-V1.0.pdf` and a `.dwg`.)

## 9. Surprises / things to act on before publishing
1. **GPL-2.0+ `gsl_point_id.c` (Silead) is linked into every touch-enabled demo**, alongside an embedded Silead firmware blob (`GSLX680_FW[]`, 4 587 words) and config table with no license at all. Any redistributed firmware containing the GSL3680 path must be treated as GPL-2.0 (or the touch stack replaced/isolated).
2. The vendor "zip" URL is a JS downloader; the real file is sharded (manifest saved). Download was abandoned at ~14 KB/s; the mirror is a faithful re-upload per its README, but the live zip (objects dated 2026-08-31) may be newer. `readme.docx` describes a xiaozhi-esp32 v1.5.6 sample folder (`jc1060pa1`) that is **absent** from the mirror, while the shipped `..._xiaozhi2.0.4.bin` is xiaozhi **v2.0.4** for board `guition-jc8012p4a1` — the live zip probably differs from the mirror here.
3. The BSP header's porch macros (40/140/40, 4/16/16) and `LANE_BITRATE_MBPS (1000)` are dead code; real timing = DPI 60 MHz, 20/20/40, 4/8/20, 1500 Mbps/lane → ~52 Hz.
4. Two different C6 slave images (Dec 2024 IDF v5.4-dev vs Aug 2025 IDF v5.5 "v2.3.2"); neither commit hash is in `espressif/esp-hosted-mcu`.
5. `8-Burn operation/flash_download_tool_3.9.7/dl_temp` (191 MB) is the vendor PC's tool cache with other products' firmware (`JC8012P4A1_V1.5..bin_rep`, JC1060P470, JC8048W550C …), and `logs/` + `configure/` contain device MACs and the employee's local paths. `7-Character&Picture_Molding_Tool` ships **WinHex 15.1 SR-8** (commercial) and other Windows freeware. Exclude all of this.
6. `esp_brookesia_phone/spiffs/music/` contains MP3s named after commercial songs ("Waka Waka", "Something", "For Elise"); provenance/rights unknown.
7. Several Espressif-derived files have had their SPDX headers stripped (e.g. `lvgl_demo_v8/main/main.c`, `esp_brookesia_phone/main/main.cpp`, Arduino `lv_conf.h`), and the vendor's own code (`esp_draw_bit`, Arduino wrappers, touch driver) has no license statement anywhere — Guition would need to grant one explicitly.
8. Minor: folder `1-Demo/arduino-examples/esp32p4_lvgl _v9` has a space in its name; the user manual still refers to "JC1060P470"; the spec PDF title says "JC8012WP4A1C_I_W".

## 10. Cross-checks (computed)
### p1ngb4ck vs mirror SHA256 (JC8012P4A1C_I_W_Y files)
```
IDENTICAL  schematics/1_PWR.png                                       78401974e10c4ade07db960ce313ee0b3c3c98951a0ae998e582e7bfd4ef461b
IDENTICAL  schematics/2_LCD.png                                       73fb21d7b370a93946bee867d725844988089296e1d819237f5d2cd1b119737b
IDENTICAL  schematics/3_ESP32-P4.png                                  cf5a0c0ec0574365171dccc1e245152476c0b640c5f2db5d5a60cf5128c412fb
IDENTICAL  schematics/4_CONN.png                                      3bfcb4b35e15162ae60ca0a4e88b5ba6d539fe0d4066d78227cc75d0718dda9e
IDENTICAL  schematics/5_ESP32-C6.png                                  fb91c52113504db60abf41c678f65d0dd6add42cfd22d38498f140a8b72797e3
IDENTICAL  schematics/6_USB.png                                       3b391469f727e12220e31acce37b38cbd70e39e176c87bbb7552dbfa8918241d
IDENTICAL  schematics/7_CODEC.png                                     a557c1b77dd2cd118b46cc0a4d97b0edcb2279169c17cd9af60e4ff8f62c8885
IDENTICAL  specification/JC8012P4A1C_I_W Specifications-EN-v1.1(2).pdf 92541f1424fbb7597eeee9ecb22373c4bf9def163565fcdaff85ae234c1f1c23
IDENTICAL  structure_diagram/1.jpg                                    541f13ad87f3920fb1e88c0e8d2a2a14867d540f737aff85ca6e8a202601569e
IDENTICAL  structure_diagram/2.jpg                                    3e4dc75c36d408ce2490f4eb5e8ad55bef98560af91ce7eb2962b1a74914327c
IDENTICAL  structure_diagram/JC8012P4A1.pdf                           7790ddddea01fc878c2bcd7a30f68ac3ec21d4ea3f8857add3bb33f268892e96
```
### Tool-cache `JC8012P4A1_V1.5..bin_rep` vs shipped `JC8012P4A1_P4-V2.8.bin` (same size 13 697 024)
```
c13b15b384bb748a3e98578437deaba070ad84a588e0dd60e5ef5a96471e5cdc *8-Burn operation/flash_download_tool_3.9.7/dl_temp/_temp_by_dltool/downloadPanel1/JC8012P4A1_V1.5..bin_rep
ef972908b06889aa1cf43194c613fa848804f17a444b847b954615f05a59c4b8 *8-Burn operation/Burn files/JC8012P4A1_P4-V2.8.bin
```
