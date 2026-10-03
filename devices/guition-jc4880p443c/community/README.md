# Community resources for the JC4880P443C

Third-party work on this board. Two repositories are mirrored locally because the docs cite them;
the rest are listed so nobody has to rediscover them. Local mirrors are **gitignored**
(`devices/*/community/*/`): only this README is published.

Rule for everything here: **no licence file means all rights reserved.** Read it for ideas, pin
numbers and failure reports; do not copy code into `src/`. Repositories with a permissive licence
(Unlicense, MIT, Apache-2.0) may be copied from with attribution; GPL code may not enter `src/`.

## Mirrored locally

| Local folder | Upstream | What it is | Licence | Status |
|---|---|---|---|---|
| `seobaeksol-JC4880P443C_I_W/` | <https://github.com/seobaeksol/JC4880P443C_I_W> | Korean bring-up notes `01_hardware_overview.md` to `10_firmware_blueprint.md` plus `90_validation_and_known_issues.md` and `README.md`: pin map, LCD timing, partition and sdkconfig suggestions, a project blueprint. Original zip `JC4880P443C_I_W-main.zip` (31,207 bytes, SHA256 `9982ec28...`, downloaded 2026-08-29; files dated 2026-05-01). | none | Read. Used as the independent pin-map cross-check in [docs/01](../docs/01-hardware.md) and as a source in [docs/interface-types.md](../docs/interface-types.md). These notes used to live in the old `demo_docs_specs/` folder next to vendor files; they are not vendor documentation. |
| `bigbag-JC4880P443C-examples/` | <https://github.com/bigbag/JC4880P443C-examples> (pushed 2026-01-15) | PlatformIO (pioarduino) examples. `trimmed/` keeps the board-unique ones: `09_sleep_wakeup`, `10_battery_adc`, `12_rs485_serial`, plus `docs/architecture.md` and `docs/device-specs.md`. `archived-examples/` holds `01`-`08` and `11` (display, touch, WiFi, SD, reset, MP3 and the outdated `07_bluetooth`). Original zip `JC4880P443C-examples-main.zip` (519,063 bytes, SHA256 `3dcae93e...`, downloaded 2026-08-30). | none | Read. Its battery thresholds are in docs/01; its `07_bluetooth` predates the esp_hosted 2.5.2 controller-disabled change and would time out on current C6 firmware ([docs/04](../docs/04-bluetooth.md)). Its `device-specs.md` wrongly claims a CH340 bridge. |

## GitHub repositories found on 2026-10-02

Licence column: as read from the repository on that date; "not checked" means nobody looked.
"not reviewed" means the code was not read; the entry is a pointer only.

| Repository | What it says it is | Licence | Status |
|---|---|---|---|
| [buccaneer-jak/JC4880P443C-P4-to-C6-RS232-communication-and-SD-Card-connection](https://github.com/buccaneer-jak/JC4880P443C-P4-to-C6-RS232-communication-and-SD-Card-connection) | P4 to C6 serial link and SD card wiring | Unlicense | cited in docs/01 as the SD and RS232 reference; code not reviewed |
| [ultramcu/guition-jc4880p4-bsp](https://github.com/ultramcu/guition-jc4880p4-bsp) | A board support package | MIT | not reviewed |
| [ultramcu/guition-jc4880p443c-i-w](https://github.com/ultramcu/guition-jc4880p443c-i-w) | Field notes | not checked | not reviewed |
| [ronger-x/jc4880p443c](https://github.com/ronger-x/jc4880p443c) | Board project | Apache-2.0 | not reviewed |
| [ZyoungInc/JC4880P443C_BSP](https://github.com/ZyoungInc/JC4880P443C_BSP) | BSP | not checked | not reviewed |
| [lalith-ais/JC4880P443C_Demo](https://github.com/lalith-ais/JC4880P443C_Demo) | Demo | not checked | not reviewed |
| [lalith-ais/gt911_touch](https://github.com/lalith-ais/gt911_touch) | GT911 touch driver | not checked | not reviewed |
| [elik745i/JC4880P443C_I_W_Remote](https://github.com/elik745i/JC4880P443C_I_W_Remote) | Remote-control firmware | not checked | not reviewed; the same author's [ESP32-2432S024C-Remote](https://github.com/elik745i/ESP32-2432S024C-Remote) is the Arduino ST7701 DSI bring-up cited in docs/01 |
| [csvke/JC4880P443C_I_W](https://github.com/csvke/JC4880P443C_I_W) | Board project | not checked | not reviewed |
| [ageiron/jc4880p443c-template](https://github.com/ageiron/jc4880p443c-template) | Project template | not checked | not reviewed |
| [h3ct0r/guition_JC4880P443C_face_detection_platformio](https://github.com/h3ct0r/guition_JC4880P443C_face_detection_platformio) | Face detection under PlatformIO | not checked | not reviewed |
| [kaber420/cbdos](https://github.com/kaber420/cbdos) | Application targeting the board | GPL-3.0 | not reviewed; GPL, nothing may be copied into `src/` |
| [bratland/smart-home-panel](https://github.com/bratland/smart-home-panel) | Smart home panel | not checked | not reviewed |
| [Miramac/Home-Assistant-JC4880P443C_I_W](https://github.com/Miramac/Home-Assistant-JC4880P443C_I_W) | Home Assistant panel (ESPHome) | not checked | not reviewed |
| [b42h/eva_wthr](https://github.com/b42h/eva_wthr) | Weather display | not checked | not reviewed |
| [wegi1/ESP32P4-JC4880P443C-I-W](https://github.com/wegi1/ESP32P4-JC4880P443C-I-W) | Board project | not checked | not reviewed |
| [DRubioG/JC4880P443C-I-W](https://github.com/DRubioG/JC4880P443C-I-W) | Board project | not checked | not reviewed |
| [commanderk33n/esp_JC4880P443C_helloworld](https://github.com/commanderk33n/esp_JC4880P443C_helloworld) | Hello world | not checked | not reviewed |
| [cubcrafts/Guition-JC4880P443C_I_W---Hello-World-Example](https://github.com/cubcrafts/Guition-JC4880P443C_I_W---Hello-World-Example) | Hello world | not checked | not reviewed |
| [microrobotics/JC4880P443C_I_W_Y](https://github.com/microrobotics/JC4880P443C_I_W_Y) | Board project (`_Y` variant name) | not checked | not reviewed |
| [CBI-MECATRONIQUE/JC4880P443C-I-W-Y](https://github.com/CBI-MECATRONIQUE/JC4880P443C-I-W-Y) | Board project (`_Y` variant name) | not checked | not reviewed |
| [muratgurler/JC4880P443C_I_W](https://github.com/muratgurler/JC4880P443C_I_W) | Board project | not checked | not reviewed |
| [sergw3x/JC4880P443C_I_W-DEMOS](https://github.com/sergw3x/JC4880P443C_I_W-DEMOS) | Demos | not checked | not reviewed |
| [youkorr/GUITION-JC4880P443C](https://github.com/youkorr/GUITION-JC4880P443C) | Board project (ESPHome oriented author) | not checked | not reviewed |
| [gmtii/esp_JC4880P443C_SDR](https://github.com/gmtii/esp_JC4880P443C_SDR) | Software-defined radio front end | not checked | not reviewed |

## Other places

| Resource | Why it matters |
|---|---|
| r/esp32 thread [Guition JC4880P433 4.3" capacitive touch display](https://www.reddit.com/r/esp32/comments/1nml6fm/guition_jc4880p433_43_capacitive_touch_display) | Origin thread with schematic and vendor-demo pointers; the first place the rev v1.x silicon and C6 firmware issues were discussed |
| EspControl, <https://jtenniswood.github.io/espcontrol/> | Shipping ESPHome firmware with official support for this board (`devices/guition-esp32-p4-jc4880p443`). Source-available, PolyForm Noncommercial. Local copy and licence notice: [reference/espcontrol/README.md](../../../reference/espcontrol/README.md); findings in [docs/07](../docs/07-espcontrol-findings.md) |
| ESPHome `mipi_dsi` display component, model `JC4880P443` | Native panel support upstream (`esphome/components/mipi_dsi/models/guition.py`), plus `esp32_hosted`, `gt911`, `es8311`. Local snapshot: [reference/esphome/README.md](../../../reference/esphome/README.md); findings in [docs/08](../docs/08-esphome-findings.md) |
| [esphome/esp-hosted-firmware](https://github.com/esphome/esp-hosted-firmware) | Pre-built ESP-Hosted C6 slave images and manifest, the channel EspControl and ESPHome pin by SHA256 |
| [espressif/esp-hosted-mcu](https://github.com/espressif/esp-hosted-mcu) | The esp_hosted source, including the slave OTA mechanism and `docs/bluetooth_design.md` (controller disabled by default since 2.5.2) |

To add an entry: name, URL, one line of what it is, the licence as read from the repo, and whether
anyone here has read the code. Mirror locally only when a doc needs to cite a specific file, and
record the zip hash and download date as above.
