# ESPHome Core Findings (reference/esphome)

[reference/esphome/upstream/](../../../reference/esphome/upstream/) (local copy, gitignored; snapshot
details in [reference/esphome/README.md](../../../reference/esphome/README.md)) is the **ESPHome core
source tree** (dev branch,
pioarduino 55.03.311 / ESP-IDF **v5.5.5**, Python ≥3.12). Surveyed 2026-08-30. Unlike
EspControl (an app built *on* ESPHome, see [07-espcontrol-findings.md](07-espcontrol-findings.md)),
this is the framework itself — and it turns out to have **first-class support for almost every
piece of this board**. License: ESPHome core is MIT/GPL mixed (C++ GPLv3 / Python MIT) — check
per-file headers before lifting code.

## Headline: the board is (almost) natively supported

| Board feature | ESPHome support | Where |
|---|---|---|
| ST7701 480x800 MIPI-DSI panel | ✅ **Native model `JC4880P443`** — 34 MHz DPI, 2 lanes @500 Mbps, our exact porches, reset GPIO5 default, init table sourced from esp_lcd_st7701.h | `esphome/components/mipi_dsi/models/guition.py:38-100` |
| GT911 touch | ✅ default addr 0x5D, 5-point multi-touch, up to 4 capacitive buttons | `components/gt911/` |
| ES8311 codec | ✅ audio_dac platform, I2C 0x18 default, `use_mclk`, `use_microphone`, mic gain 0-42 dB | `components/es8311/` |
| I2S audio on P4 | ✅ 3 I2S ports recognized on P4; external speaker/mic paths unrestricted | `components/i2s_audio/` |
| C6 via esp_hosted SDIO | ✅ dedicated `esp32_hosted:` component, pins **esp_hosted 2.12.12** (exactly our C6's version) | `components/esp32_hosted/` |
| **BLE over the C6** | ✅ **fully supported on P4** — Bluedroid host over hosted vHCI, incl. tracker/client/server/beacon and full **Bluetooth Proxy** | `components/esp32_ble/ble.cpp` |
| C6 firmware update | ✅ `update:` platform — `type: embedded` (sha256-pinned, baked into flash) or `type: http` (official manifest at esphome.github.io/esp-hosted-firmware) | `components/esp32_hosted/update/` |
| P4 rev v1.x silicon | ✅ `esp32: engineering_sample: true` (sets `CONFIG_ESP32P4_SELECTS_REV_LESS_V3`) | `components/esp32/__init__.py:2616` |

Notably, ESPHome's BLE bring-up (`ble.cpp` ~183-252) performs **the exact sequence we
discovered by hand** for [our firmware](04-bluetooth.md): `esp_hosted_connect_to_slave()` →
`esp_hosted_get_coprocessor_fwversion()` → `esp_hosted_bt_controller_init()/_enable()` →
attach vHCI — with a 60 s WDT raise around it and explicit "co-processor may lack BT support,
update it" error messages. Independent confirmation that our recipe is the canonical one
(ESPHome uses the Bluedroid flavor; ours uses NimBLE — both ride the same vHCI).

There is **no all-in-one board package**: repo-wide, `JC4880` appears only in the display
model. Display + touch + audio + hosted must be assembled in YAML by hand (the copyable
fixtures below make that easy). A contributed `test-jc4880p443` fixture would be a natural
upstream PR.

## Copyable YAML samples (test fixtures double as working examples)

All under `reference/esphome/upstream/tests/`:

| Fixture | What it demonstrates |
|---|---|
| `components/lvgl/test.esp32-p4-idf.yaml` | **Closest to a full app**: mipi_dsi + LVGL + psram + esp_ldo ch3 @2.5 V |
| `components/mipi_dsi/test.esp32-p4-idf.yaml` | Bare DSI display (swap in `model: JC4880P443`, drop `enable_pin`) |
| `test_build_components/common/ble/esp32-p4-idf.yaml` | **Our BLE-scan use case verbatim**: esp32_hosted (C6, reset 54, CMD19/CLK18/D0-D3 14-17) + `esp32_ble_tracker` active scan |
| `components/bluetooth_proxy/test.esp32-p4-idf.yaml` | Full HA Bluetooth Proxy on P4 (active, 9 connection slots) |
| `components/esp32/test.esp32-p4-idf.yaml` | Base P4 platform block: `engineering_sample: true`, 400 MHz (needs `enable_idf_experimental_features`), flash-saver flags |
| `components/esp32_hosted/test-{embedded,http}.esp32-p4-idf.yaml` | Both C6 firmware-update styles |
| `components/esp_ldo/test.esp32-p4-idf.yaml` | LDO channels incl. runtime voltage adjust |
| `components/gt911/common.yaml`, `components/es8311/common.yaml`, `components/i2s_audio/common.yaml` | Touch/codec/audio blocks (no P4-specific audio fixture exists, but nothing gates on P4; combine with the P4 I2C package — which already uses GPIO7/8) |

## P4-specific gotchas encoded in the framework

- **`engineering_sample: true` is mandatory for this board** — the default board for
  `variant: esp32p4` is the **rev-3** eval board; omitting the flag produces a non-booting
  image on rev 1.x silicon (`components/esp32/boards.py:1795`).
- 400 MHz CPU and 200 MHz PSRAM each require `enable_idf_experimental_features: yes`.
- `esp32_hosted: use_psram: true` — source comment names our exact scenario ("P4 + large
  LVGL UI") as the case where the SDIO mempool otherwise fails at boot.
- `esp_ldo` channel 3 @ 2.5 V is required for the DSI PHY in every P4 display config
  (matches our finding); `mipi_dsi` hard-depends on `esp32 + esp_ldo + psram`.
- DSI has no hardware rotation — rotate in LVGL (`rotation:`); with no lambda/LVGL configured
  the display auto-shows a test card (nice first-boot check).
- `esp32_ble: use_psram: true` moves Bluedroid to SPIRAM (~40 KB internal RAM back) —
  relevant when combining BLE with the DSI framebuffer.
- PA enable (GPIO11) isn't an es8311 option — model it as a `switch`/`output` toggled by
  speaker automations.
- ESPHome rejects IDF <5.3 for esp32_hosted (heap double-free in the SDIO RX path, fixed in
  esp_hosted 2.11.0).

## Higher-level features available "for free" on this board

`api:` (native encrypted HA integration) · `web_server:` v2/v3 (built-in UI + REST + SSE on
the IDF HTTP server) · `ota:` (network flashing + downgrade protection) · `captive_portal:`
(fallback AP provisioning) · `mdns:` · `http_request:` · `mqtt:` · `esp32_improv:` (BLE
provisioning — rides the C6) · `voice_assistant:` + `micro_wake_word:` (both un-gated on P4,
would use the ES8311 mic) · `media_player`/`speaker` pipelines (FLAC/MP3/WAV decode/mix).

---

# Coverage verdict: ESPHome vs the other demo trees — what to keep

ESPHome is **not** perfect coverage for this board. Verified gaps (grep-confirmed absent from
the ESPHome tree, 2026-08-30): **no MIPI-CSI camera on P4** (its `esp32_camera` is the classic
DVP driver only), **no esp-dl ML** (face/pedestrian detection), **no AVI/MJPEG video
playback**, **no esp-sr audio front-end** (AEC/advanced wake word), **no multi-app launcher
UI** (esp-brookesia). And structurally: ESPHome helps only ESPHome projects — for raw ESP-IDF
work (like `src/sample_hello_world`) the vendored BSP remains the only path.

## Keep / archive decision per tree

| Tree | Verdict | Unique value ESPHome does NOT cover |
|---|---|---|
| `vendor/package/` (was `demo_docs_specs/`) | **KEEP — irreplaceable** | Schematics, the **factory restore images** (P4 + C6 bins — the only unbrick path), flash tool, and the **vendored board-patched BSP** inside `esp_brookesia_phone/components/` that every raw-IDF project here builds on. (The `01`-`90` bring-up notes that used to sit in this folder were never vendor material: they are a copy of [seobaeksol/JC4880P443C_I_W](https://github.com/seobaeksol/JC4880P443C_I_W), no licence, now under `community/seobaeksol-JC4880P443C_I_W/`) |
| ├─ `esp_brookesia_phone` | **KEEP** | The BSP (above); **camera + face/pedestrian detection** (esp-dl on P4 — nowhere else); **AVI/MJPEG video player** (`avi_player` + simple_player); esp-brookesia multi-app launcher; proven audio donor `main/audio.c` |
| ├─ `xiaozhi-esp32-main` | **KEEP** | Full **standalone voice assistant**: esp-sr AFE (incl. echo cancellation), Opus, WebSocket/MQTT voice protocol, MCP server — ESPHome's voice path is HA-dependent and its wake-word runtime is simpler; also the board port that drives GT911 RST/INT |
| ├─ `video_lcd_display` | **KEEP** | The cleanest raw **MIPI-CSI camera → LCD** pipeline (no BSP) — the board's biggest ESPHome gap |
| ├─ `lvgl_demo_v8/v9`, `sw_rotation`, `draw_bits` | **Archived** (`vendor/package-archived-demos/1-Demo/idf_examples/ESP-IDF_5.5.4/`) | Superseded by `src/hello_world` + ESPHome LVGL fixtures; v9 has a known build bug |
| ├─ `arduino_examples/` | **Archived** (`vendor/package-archived-demos/1-Demo/arduino_examples`) | Only if the Arduino framework is ever wanted; the battery thresholds from `adc_test` are recorded in [01-hardware.md](01-hardware.md) and the kept `10_battery_adc` sample |
| `community/bigbag-JC4880P443C-examples/` (was `demos-docs-samples-2/`; [bigbag/JC4880P443C-examples](https://github.com/bigbag/JC4880P443C-examples), no licence) | **Trimmed** (`trimmed/` + `archived-examples/`) | Kept: `09_sleep_wakeup` (P4 sleep on this board), `10_battery_adc` (board-specific), `12_rs485_serial` (board pins/EN timing). Archived: display/wifi/sd/reset/mp3 examples (covered better elsewhere) and `07_bluetooth` (**outdated** — predates the controller-enable requirement) |
| `reference/espcontrol/upstream/` (was `demo-espcontrol-main/`) | **KEEP as pattern library** | Reliability patterns now distilled into [docs/reliability-checklist.md](../../../docs/reliability-checklist.md) (formerly `MUST-IMPLEMENT.md`); `configuration_store` atomic-NVS C++; three-tier web UI delivery; the field-proven config values. License: noncommercial — patterns yes, code-lifting no |
| `reference/esphome/upstream/` (was `demo-esphome-dev/`) | **KEEP** | The framework itself + the 24 P4 test fixtures; the BLE/hosted reference implementation |

Rule of thumb: **ESPHome wins on connectivity and plumbing** (WiFi, BLE, OTA, web, HA,
C6 updates); **the vendor tree wins on P4 multimedia** (camera, ML, video, advanced audio)
and on anything raw-IDF; EspControl wins on **hardening patterns**. Nothing here is fully
redundant except the small LVGL bootstraps and the outdated Bluedroid sample.

**For a Home-Assistant-style smart panel, sensor hub, or "configure it in YAML" device: yes,
and for this specific board it's unusually attractive** — the display model, the C6 link
(pinned to the exact firmware our board ships), BLE-over-C6, OTA, web UI, and the rev-1.x
silicon quirk are all already handled upstream, which took us days to establish by hand in
raw IDF. EspControl (doc 07) proves a polished product ships on this stack on this exact board.

**When it's the wrong base**: an app that is mostly custom C++ — bespoke UI logic beyond
LVGL's YAML surface, custom DSP (our Toccata synthesizer), unusual protocol work — fights
the framework. ESPHome's model is *components + YAML config*; custom behavior means writing
"external components" against ESPHome's C++ APIs and build cadence (a moving target: pinned
pioarduino/IDF combos, Python ≥3.12, ~monthly releases). Our raw-IDF `src/sample_hello_world`
remains the better base for app-style firmware where you own the main loop.

**Practical hybrid**: keep raw IDF for custom apps, and keep an ESPHome config for this board
as a maintenance/recovery tool — its `esp32_hosted` update platform is currently the easiest
way to update the C6 firmware wire-free.

## What must run OUTSIDE the device

| When | External requirement |
|---|---|
| **Building/flashing** (always) | A computer with the ESPHome CLI — `pip install esphome` (Python ≥3.12), or the ESPHome Docker image, or the Home Assistant "ESPHome Device Builder" add-on. First build downloads the pioarduino toolchain + IDF (~GBs). Flash over USB (COM3) the first time; OTA over WiFi afterwards. |
| **Day-to-day runtime** | **Nothing.** The device runs standalone: its web UI, captive portal, LVGL UI, and automations are all on-device. |
| Home Assistant integration (optional) | A running Home Assistant instance on the same network — this is what the native `api:`, voice assistant, and Bluetooth Proxy talk to. HA discovers the device via mDNS. |
| MQTT (optional alternative) | An MQTT broker (e.g. Mosquitto). |
| C6 firmware auto-update via `type: http` (optional) | Internet access to `esphome.github.io/esp-hosted-firmware` (or self-host the manifest); `type: embedded` needs nothing external. |
| Remote web-UI bundles, EspControl-style (optional) | A static file host (GitHub Pages works) — only if you adopt their three-tier UI delivery. |

Minimal viable setup: one laptop with `pip install esphome`, a YAML file assembled from the
fixtures above, USB cable once. Add Home Assistant only when you want the panel to control
things.
