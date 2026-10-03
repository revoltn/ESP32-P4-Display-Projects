# reference/espcontrol: EspControl, `main` branch snapshot

| | |
|---|---|
| Upstream | <https://github.com/jtenniswood/espcontrol> (project site <https://jtenniswood.github.io/espcontrol/>) |
| Branch | `main` (GitHub "Download ZIP"; branch archives carry no commit hash) |
| Archive | `espcontrol-main.zip`, 82,543,791 bytes, 1,438 entries, downloaded 2026-08-30 10:26 local time |
| SHA256 | `6651caee4db031fd232ff84384a96235963c5c3cef00d5a49303a926ba370883` |
| Version | no version file in the tree; newest file inside the archive dated 2026-08-29 |
| Extracted to | `upstream/` (the archive's top folder `espcontrol-main/` flattened); gitignored |
| What it is | A Home Assistant touchscreen-panel firmware built on ESPHome (YAML plus header-only C++ external components, no `idf_component.yml`, no `sdkconfig`), with a web installer and a browser configurator |

## Licence

**PolyForm Noncommercial License 1.0.0** (`upstream/LICENSE`,
<https://polyformproject.org/licenses/noncommercial/1.0.0>). The licence requires that anyone who
receives any part of the software also receives the licence terms and the licensor's Required
Notice. `upstream/NOTICE` reads, verbatim:

```
Required Notice: Copyright (c) 2026 jtenniswood
```

Consequences for this repository:

- **The copy is not redistributed.** `upstream/` and the zip are gitignored; what is published is
  this README and the findings written in our own words.
- **Patterns yes, code-lifting no.** Architecture, configuration values, pin confirmations and
  sequencing may be learned here and re-implemented independently in `src/` with a citation. No file,
  function or YAML block is copied into `src/`. If that rule is ever broken on purpose, the copied
  part makes the result noncommercial-only and must carry the licence text and the Required Notice.
- Noncommercial, personal and research use of the tree itself (reading it, building it for a desk
  panel) is permitted by the licence.

## Why it is kept

It is the only shipping product firmware with official support for both Guition boards in this
repository, and it runs on real panels in the field. That makes it the best source of hardening
values and the second independent confirmation of our pin map. Survey and the "does not apply"
table: [devices/guition-jc4880p443c/docs/07-espcontrol-findings.md](../../devices/guition-jc4880p443c/docs/07-espcontrol-findings.md).

| What | Path under `upstream/` |
|---|---|
| 4.3" device config: hardware file, fonts, LVGL layout, sensors, layout substitutions | `devices/guition-esp32-p4-jc4880p443/device/device.yaml`, `device/fonts.yaml`, `device/lvgl.yaml`, `device/sensors.yaml`, `packages.yaml`, `esphome.yaml`, `dev.yaml` |
| 10.1" device configs (v1 with `battery.yaml`, and v2) | `devices/guition-esp32-p4-jc8012p4a1/`, `devices/guition-esp32-p4-jc8012p4a1-v2/` |
| Build variants per device (normal, `.factory`, `.recovery`) | `builds/guition-esp32-p4-jc4880p443*.yaml`, `builds/guition-esp32-p4-jc8012p4a1*.yaml` |
| Device catalogue and generated manifest (web installer metadata, `displayOffset`, SDIO frequency) | `product/v2/device_catalog.json`, `devices/manifest.json` |
| Wire-free C6 recovery: SHA256-verified slave image embedded in the P4 build and flashed over `esp_hosted_slave_ota_*` before WiFi starts | `components/c6_recovery/__init__.py`, `c6_recovery.cpp`, `c6_recovery.h` |
| Hardening values for the 4.3" board: SDIO 20 MHz, backlight LEDC 100 Hz with `max_power: 0.8`, I2C 400 kHz, `engineering_sample: true`, main task stack 8192, `MBEDTLS_EXTERNAL_MEM_ALLOC=y`, task WDT 60 s, `ESP_HOSTED_USE_MEMPOOL=n`, `ESP_HOSTED_DFLT_TASK_FROM_SPIRAM=y`, boot-time crash-loop breaker | `devices/guition-esp32-p4-jc4880p443/device/device.yaml` (sdkconfig block around lines 31-46, crash breaker around lines 83-104) |
| Dual-OTA partition table with a 2 MB `card_images` data partition | `common/device/partitions_16mb_card_images.csv` |
| OTA UX and post-connect staging | `common/addon/firmware_update.yaml`, `common/device/core_infra.yaml` |
| Atomic two-slot NVS configuration store | `components/espcontrol/configuration_store.*` |
| Web server wrapper over `esp_http_server`, three-tier UI delivery | `components/web_server_idf/`, `dev-docs/web-configurator.md` |
| Components that do **not** apply to the 4.3" board | `components/mipi_rgb/` (S3 4848S040), `components/gsl3680/` (10.1" touch), `components/sendspin/` and all audio YAML (esp32-p4-86 only), `*_ethernet.yaml` variants |

Many of the reliability rules in [docs/reliability-checklist.md](../../docs/reliability-checklist.md)
cite this tree as "proven by". The rules were rewritten from observation, not copied.

## Refreshing the snapshot

Download a new `main` archive (or a tagged release), record size, SHA256 and the newest file date,
extract over `upstream/`, re-read `LICENSE` and `NOTICE` in case they changed, and re-check the line
numbers cited above and in docs/07.
