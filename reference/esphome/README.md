# reference/esphome: ESPHome core, `dev` branch snapshot

| | |
|---|---|
| Upstream | <https://github.com/esphome/esphome> |
| Branch | `dev` (GitHub "Download ZIP"; branch archives carry no commit hash) |
| Archive | `esphome-dev.zip`, 11,603,106 bytes, 10,885 entries, downloaded 2026-08-30 10:51 local time |
| SHA256 | `568229b348611a431d3773ef6893434f7789c6459d0bdb2780be896ccd147706` |
| Version | `upstream/esphome/const.py`: `__version__ = "2026.9.0-dev"`; newest file inside the archive dated 2026-08-28 |
| Extracted to | `upstream/` (the archive's top folder `esphome-dev/` flattened); gitignored |
| Toolchain it targets | pioarduino 55.03.311 / ESP-IDF v5.5.5, Python >= 3.12 (per `platformio.ini` and `pyproject.toml` at the snapshot) |

## Licence

`upstream/LICENSE` ("The ESPHome License", Copyright (c) 2019 ESPHome): the C++/runtime code
(`.c`, `.cpp`, `.h`, `.hpp`, `.tcc`, `.ino`) is **GPLv3**; the Python code and everything else is
**MIT**. Check the header of any file before reusing a line of it.

**Rule for this repository: nothing GPL is copied into `src/`.** The C++ in `upstream/` is read to
understand sequences and values, which are then written independently in our ESP-IDF code with a
citation. Python and YAML may be adapted under MIT with attribution.

## Why it is kept

The board is almost natively supported by ESPHome, which makes the tree the best independent
confirmation of our hardware findings and the quickest route to a maintenance or recovery firmware.
The survey is [devices/guition-jc4880p443c/docs/08-esphome-findings.md](../../devices/guition-jc4880p443c/docs/08-esphome-findings.md).

| What | Path under `upstream/` |
|---|---|
| Native MIPI-DSI panel models for the 4.3" and the 10.1" boards | `esphome/components/mipi_dsi/models/guition.py` (`JC4880P443` from line 38: 480x800, DPI 34 MHz, 2 lanes at 500 Mbps, porches 12/42/42 and 2/8/166, reset GPIO5; `JC8012P4A1` from line 102; `JC8012P4A1-V2` from line 322) |
| The `mipi_dsi` display platform itself | `esphome/components/mipi_dsi/` |
| C6 link: `esp32_hosted` component, pins `espressif/esp_hosted` **2.12.12** (`__init__.py` line 278), SDIO pin schema, `use_psram`; C6 firmware update platform (`type: embedded` and `type: http`) | `esphome/components/esp32_hosted/`, `esphome/components/esp32_hosted/update/` |
| Hosted BLE bring-up: connect to slave, read co-processor version, controller init and enable, attach vHCI, inside a 60 s WDT raise | `esphome/components/esp32_ble/ble.cpp` (around lines 183-252) |
| P4 platform: `engineering_sample` (rev v1.x silicon), 360 vs 400 MHz, default board trap | `esphome/components/esp32/__init__.py`, `esphome/components/esp32/boards.py` |
| `gsl3670` touchscreen component (the GSL36xx family; the 10.1" board uses a GSL3680 and EspControl carries its own `gsl3680` external component) | `esphome/components/gsl3670/` |
| GT911, ES8311, I2S audio platforms used by this board | `esphome/components/gt911/`, `esphome/components/es8311/`, `esphome/components/i2s_audio/` |
| ESP32-P4 test fixtures that double as copyable configs (28 files) | `tests/components/*/test*.esp32-p4-idf.yaml` (adc, bluetooth_proxy, esp32, esp32_ble, esp32_ble_beacon, esp32_ble_client, esp32_ble_server, esp32_ble_tracker, esp32_hosted x5, esp32_touch, esp_ldo, ethernet, lvgl, mipi_dsi, mipi_rgb, psram x2, spi, tinyusb, usb_cdc_acm), `tests/test_build_components/common/{ble,i2c,spi}/esp32-p4-idf.yaml`, `tests/test_build_components/build_components_base.esp32-p4-idf.yaml` |
| The closest thing to a full app for this board | `tests/components/lvgl/test.esp32-p4-idf.yaml` (mipi_dsi + LVGL + psram + esp_ldo ch3 at 2.5 V) and `tests/test_build_components/common/ble/esp32-p4-idf.yaml` (esp32_hosted with reset 54, CMD 19, CLK 18, D0-D3 14-17, plus `esp32_ble_tracker`) |

There is no all-in-one board package upstream: `JC4880` appears only in the display model, so
display, touch, audio and hosted must be assembled in YAML from the fixtures above.

## Refreshing the snapshot

Download a new `dev` archive (or better, a tagged release so the version is exact), record its size,
SHA256 and `const.py` version here, extract over `upstream/`, and re-check the line numbers cited
above and in docs/08; they drift between releases.
