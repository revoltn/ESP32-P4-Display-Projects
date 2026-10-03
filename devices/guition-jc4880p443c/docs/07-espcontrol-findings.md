# EspControl Codebase Findings (reference/espcontrol)

[reference/espcontrol/upstream/](../../../reference/espcontrol/upstream/) (local copy, gitignored;
provenance and the licence notice in [reference/espcontrol/README.md](../../../reference/espcontrol/README.md))
is **EspControl** — a source-available Home
Assistant touchscreen-panel firmware that officially supports our JC4880P443 (plus five other
boards). Surveyed 2026-08-30. It is a goldmine of *patterns* and a second independent
confirmation of our hardware findings — but read the two warnings first.

> **Warning 1 — different framework.** EspControl is an **ESPHome** project (YAML + header-only
> C++ "external components"), not ESP-IDF/CMake. There is no `idf_component.yml`, no
> `sdkconfig`, no `main/`. ESP-IDF is reached indirectly through ESPHome (comment in the device
> YAML pins the known-good combo: IDF 5.5.4 via pioarduino 55.03.39). Most value transfers as
> architecture and config values, not drop-in code.
>
> **Warning 2 — multi-device tree.** Only some code applies to our board. The
> "does NOT apply" table at the bottom lists the traps.
>
> **License:** PolyForm **Noncommercial** 1.0.0 — source-available, not OSI open source.
> Learning patterns is fine; lifting code into anything commercial needs the owner's permission.

## Where the JC4880P443 lives in the tree

- Device config: `devices/guition-esp32-p4-jc4880p443/` — `device/device.yaml` (327 lines) is
  the hardware file; `packages.yaml` carries ~90 layout substitutions (6 card slots, 2×3,
  portrait, `compact` density).
- Build variants: `builds/guition-esp32-p4-jc4880p443.yaml` (+ `.factory`, `.recovery`).
- Device metadata source of truth: `product/v2/device_catalog.json` → generated
  `devices/manifest.json`.

## Hardware confirmations & deviations vs. our findings

EspControl independently confirms our pins (SDIO CLK18/CMD19/D0-D3 14-17/reset 54 active-high,
I2C SDA7/SCL8, backlight GPIO23, LDO ch3 @ 2.5 V for DSI PHY, GT911 polled with no RST/INT).
Differences worth knowing:

| Topic | Our firmware | EspControl's choice | Takeaway |
|---|---|---|---|
| SDIO clock | 40 MHz (vendor demo value) | **20 MHz** — comment: "lower SDIO clock for reliable ESP32-C6 communication **across production panels**" | If the C6 link ever flakes on another unit, drop to 20 MHz first |
| Backlight PWM | full-on only | LEDC **100 Hz**, `max_power: 0.8` (caps at 80%) | 100 Hz/80% are field-proven dimming values for this panel |
| I2C speed | 100 kHz | **400 kHz** | 400 kHz is safe for GT911 on this bus |
| Display rotation | native portrait | LVGL `rotation: 180` default; manifest says `displayOffset: 180` | The panel is mounted 180° relative to "natural" scan order in their enclosure assumption — if our UI ever appears upside-down relative to the logo, this is why |
| Chip revision | `CONFIG_ESP32P4_REV_MIN_100` | ESPHome `engineering_sample: true` on all six P4 devices | Same rev-v1.x issue, different knob — confirms every shipping unit of these boards is pre-v3 silicon |
| ES8311 audio | implemented (unverified, no speaker) | **absent** — no audio config at all for this board | No help there; only their `esp32-p4-86` device has an audio stack |
| Bluetooth | working BLE scan | **absent** — EspControl never uses BT | Our BLE work has no counterpart here |

Their sdkconfig overrides for this board are a useful hardening checklist
(`device.yaml:31-46`): main task stack 8192 (mbedTLS/HTTPS blows the default on P4),
`MBEDTLS_EXTERNAL_MEM_ALLOC=y`, task WDT 60 s (OTA partition erase blocks the loop),
`ESP_HOSTED_USE_MEMPOOL=n` (avoids "HS_MP: mempool create failed: no mem" on P4 display
builds), `ESP_HOSTED_DFLT_TASK_FROM_SPIRAM=y`.

Their partition table: dual-OTA 7.06 MB apps + a 2 MB custom `card_images` data partition
(`common/device/partitions_16mb_card_images.csv`) — a good template when we outgrow our
single-factory layout.

## How the on-device web server works

- **Server**: ESP-IDF `esp_http_server`, wrapped by `components/web_server_idf/` (a vendored
  fork of ESPHome's wrapper that reimplements the AsyncWebServer API on top of it), with
  ESPHome's web_server v3 on port 80. Header limit raised to 4096
  (`CONFIG_HTTPD_MAX_REQ_HDR_LEN`).
- **UI delivery — the most stealable idea**: a three-tier fallback.
  1. A minified offline configurator (`www.js`, esbuild IIFE bundle) is **baked into the
     firmware binary** as a C array via `web_server.js_include` — no SPIFFS needed.
  2. `js_url` points at a hosted bridge on GitHub Pages that picks an immutable,
     content-addressed bundle (`bundles/<sha256>/www.js`) so the web UI can improve after
     the firmware ships.
  3. If the remote fails, the embedded copy runs. (`dev-docs/web-configurator.md`)
- **API**: for this board, plain ESPHome REST over entities (`GET/POST /text/...`,
  `/select/...`, `/switch/...`), with card settings serialized into a compact string stored
  in a text entity. Live updates via **Server-Sent Events** (`/events`), not WebSocket. A
  newer opt-in binary config API (`/api/v1/config` with ETag + generation-counter conflict
  detection) exists but is **not enabled** for the JC4880P443.
- **Persistence**: ESPHome NVS preferences on this board — with a discipline worth copying:
  always `global_preferences->sync()` before any reboot, because writes are batched.
  (The newer devices use a two-slot atomic store: payload written first, checksummed header
  published last, monotonic generation counter — `components/espcontrol/configuration_store.*`,
  portable C++ with tests.)
- **Provisioning**: ESPHome `captive_portal` (AP named `ESP_<MAC suffix>`, 90 s AP timeout so
  brief WiFi outages don't pop the hotspot). No Improv.
- **mDNS**: used both to advertise the device and *as a client* — async
  `_home-assistant._tcp` query to auto-discover the Home Assistant URL
  (`home_assistant_endpoint_resolver.cpp`).
- **HA integration**: ESPHome's native encrypted API (not MQTT); HTTP only for artwork/camera.

## C6 co-processor firmware management (relevant beyond EspControl)

Two mechanisms we should remember for our own C6 lifecycle:

1. **C6 firmware has an official distribution channel**:
   `https://esphome.github.io/esp-hosted-firmware/` — EspControl pins slave **2.12.12** by
   sha256 (the exact version our board's C6 runs; see [04-bluetooth.md](04-bluetooth.md)).
   An `update:` platform polls its manifest for newer C6 firmware.
2. **C6 recovery without wires**: their recovery build embeds the sha256-verified C6 binary
   in the P4 image and flashes it over `esp_hosted_slave_ota_*` **before WiFi starts**
   (`components/c6_recovery/`, `scripts/prepare_c6_firmware.py`). This is the practical
   answer to a bricked/mismatched C6 — no JP1 UART soldering needed. **Untested by us.**

## Other patterns worth stealing (host-side)

- **OTA UX**: backlight blanked during flash writes (power + flash-contention), LVGL progress
  page, and a "update complete" flag persisted across the reboot so the success screen shows
  on the *next* boot (`common/addon/firmware_update.yaml`).
- **Boot-time crash-loop breaker**: on boot, if a crash report exists — clear it, mark safe
  mode successful, sync preferences, reboot once — so the saved crash isn't replayed and
  re-triggered (`device.yaml:83-104`).
- **HA reconnect staging**: on API connect, yield 1 s before doing panel work (don't starve
  the handshake), then staged re-fetches at 2/8/20/25 s (`common/device/core_infra.yaml`).
- **Web installer**: ESP Web Tools v10 manifests per device; recovery manifests force
  full-erase (`new_install_prompt_erase: true`).
- **Release hygiene**: string-scan the final .bin to assert the real version string got in and
  no `0.0.0`/`dev` placeholder survived (`scripts/firmware_release.py`); frozen compatibility
  fixtures + mutation tests that deliberately break invariants to prove the checks catch them.

## Does NOT apply to the JC4880P443 — do not copy from these

| Item | Why not |
|---|---|
| `components/mipi_rgb/` | Parallel-RGB ST7701**S** driver for the ESP32-**S3** 4848S040 (480×480). Our panel is MIPI-DSI (`mipi_dsi` platform, `model: JC4880P443`, driver lives upstream in ESPHome) |
| `components/gsl3680/` | Touch controller for the 10.1" jc8012p4a1 only; ours is GT911 |
| `components/sendspin/`, all audio YAML, `voice_assistant` | Only the `esp32-p4-86` device has an audio stack |
| Ethernet YAML variants (`*_ethernet.yaml`) | jc1060p470 / p4-86 only; the 4880 has no ethernet |
| `sdio_frequency: 40MHz` (jc8012p4a1) | The 4880 deliberately runs 20 MHz for reliability |
| GT911 `reset_pin: GPIO22 / interrupt_pin: GPIO21` (jc1060p470) | Not used in the 4880 config (polled mode) |
| `battery.yaml` (jc8012p4a1) | Battery-equipped panel only |
| Native `panel_config:` binary storage | Enabled for jc1060p470/jc8012p4a1 only; the 4880 uses legacy text-entity storage |
| `docs/public/webserver/bundles/**` | ~50 dirs of minified build output — not source |
