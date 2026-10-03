# Components & Libraries That Work Together

The single most important decision of the bring-up: **reuse the vendor-patched BSP from the
factory demo verbatim instead of hand-rolling drivers.** The stock Espressif
`esp32_p4_function_ev_board` BSP targets a different panel (1024x600 EK79007); the copy inside
the factory demo is patched for this board's 480x800 ST7701 and correct pins.

## Local (vendored) components — copied into `src/sample_hello_world/components/`

Copied from `devices/guition-jc4880p443c/vendor/package/1-Demo/idf_examples/ESP-IDF_5.5.4/esp_brookesia_phone/components/`
(local only, gitignored; provenance and licence classes in [../vendor/README.md](../vendor/README.md)):

| Component | Version | Role |
|---|---|---|
| `espressif__esp32_p4_function_ev_board` | 5.2.3 (vendor-patched) | Display/touch/audio/SD BSP. Handles the DPHY LDO ch3 power-up, ST7701 init, GT911, ES8311 wiring. **Do not edit its manifest** — its unused deps (esp_lcd_ek79007, esp_video, …) must still resolve from the registry. |
| `espressif__esp_lcd_st7701` | 1.1.3 (vendor-patched) | Panel driver with this board's init command table. The BSP references it by relative path `../espressif__esp_lcd_st7701`, so copy both as siblings. |
| `bsp_extra` | (vendor) | Practical audio layer: `bsp_extra_codec_init/set_fs/i2s_read/i2s_write/volume_set/in_gain_set`. Also drags in `chmorgan/esp-audio-player` from the registry. |
| `esp_cam_sensor` | **2.1.0** (vendored, from `video_lcd_display/components/`) | Camera sensor drivers. **Must override** the managed 2.0.1 that esp_video pins — 2.0.1 has no OV02C10 driver. See [09-camera.md](09-camera.md). |

## Managed components — pinned in `src/sample_hello_world/main/idf_component.yml`

| Dependency | Constraint | Why this constraint |
|---|---|---|
| `espressif/esp_hosted` | **`~2.12.0`** | **Must match the C6 slave firmware version** (factory C6 runs 2.12.12). A 2.11 host produced endless HCI timeouts for Bluetooth; WiFi tolerated the mismatch with a warning. |
| `espressif/esp_wifi_remote` | `*` | WiFi API shim; version follows esp_hosted. |
| `espressif/esp_lvgl_port` | **`~2.9.0`** | Needs the `on_frame_buf_complete` DPI callback, present since **IDF 5.5.5** (on 5.5.4 it fails with `esp_lcd_dpi_panel_event_callbacks_t has no member 'on_frame_buf_complete'`). Upgraded from ~2.7.2 on 2026-09-02 together with the IDF 5.5.4→5.5.5 bump. |
| `lvgl/lvgl` | `9.5.0` | Proven by the vendor's lvgl_demo_v9. Still the registry's latest (checked 2026-09-02). |
| `esp_codec_dev` | ~1.5 (via BSP) | ES8311 codec abstraction. The BSP's `~1.5` means `<1.6.0`, so 1.6.x can't resolve without editing the BSP manifest (forbidden, see above) — stays at 1.5.11. |
| `esp_lcd_touch_gt911` | ^1 (via BSP) | Touch driver, polling mode. |

After a successful build, keep `dependencies.lock` — it freezes this known-good resolution.

## Upstream review, 2026-09-02 (what was upgraded, what was deliberately not)

Full upstream sweep against the Espressif component registry and ESP-IDF releases:

- **Upgraded**: ESP-IDF 5.5.4 → **5.5.5**, esp_lvgl_port 2.7.2 → **2.9.0** (unblocked by the
  IDF bump). Old locks kept as `dependencies.lock.pre-5.5.5.bak` in each app.
- **Already latest**: lvgl 9.5.0, esp-sr 2.5.2, esp_wifi_remote 1.6.4, esp_lcd_touch_gt911
  1.2.1, usb_host_uac 1.5.0, esp-dl 3.3.10, BSP 5.2.3, and esp_hosted 2.12.12 is the **last
  2.x release**.
- **Deferred — esp_hosted 3.0.x** (3.0.6 as of Aug 2026): major version; requires reflashing
  the C6 slave to matching 3.x firmware (slave OTA over SDIO exists) and re-applying our local
  128-byte `hosted_malloc_align` patch. Community reports SDIO bootloops on host/slave
  mismatches. Treat as its own project.
- **Deferred — esp_video 2.4.x / esp_cam_sensor 2.4.x**: verified the **OV02C10 driver is
  vendor-custom** — it does not exist in Espressif's esp-video-components repo at the vendored
  commit (70d2ce41) nor on current master. Upgrading the camera stack drops the sensor driver
  unless the ov02c10 sources are ported forward as a local override.
- **Deferred — esp_lcd_st7701 2.x** (2.0.2~2): our 1.1.3 carries the vendor init-table patch
  for this panel; a major bump would discard it.
- **Deferred — esp_codec_dev 1.6.x / 2.0.0-beta**: blocked by the BSP's `~1.5` constraint
  (manifest must stay unedited); 1.5.11 works.
- **Deferred — ESP-IDF 6.x** (6.1 is latest stable): major migration; 6.1 defaults the P4 to
  silicon rev v3.0 (we set `SELECTS_REV_LESS_V3`, so still buildable when attempted).

## Proven donor code (worth reading before writing anything new)

All under `devices/guition-jc4880p443c/vendor/package/1-Demo/idf_examples/ESP-IDF_5.5.4/esp_brookesia_phone/`:

| File | What it demonstrates |
|---|---|
| `main/main.cpp` | The exact `bsp_display_cfg_t` that works (buffer `BSP_LCD_H_RES*80`, single buffer, PSRAM, no DMA), NVS erase-retry init |
| `main/audio.c` | Full codec init → set_fs → volume → gain → i2s read/write cycle (mic echo + PCM playback) |
| `components/apps/setting/Setting.cpp` (~line 575) | esp_wifi_remote init/scan/connect with defensive error handling |
| `sdkconfig` (lines ~2805-2870) | The working esp_hosted SDIO block |

Camera donor (separate vendor demo):
`devices/guition-jc4880p443c/vendor/package/1-Demo/idf_examples/ESP-IDF_5.5.4/video_lcd_display/` — the minimal
OV02C10 → V4L2 → PPA → LCD pipeline this board actually shipped with (`main/app_video.c` is
the wrapper our `src/sample_hello_world/main/app_video.c` was ported from). Note its display path
(direct panel draw) is incompatible with our LVGL direct-mode setup — see
[09-camera.md](09-camera.md).

Additional working references (different frameworks):
- `devices/guition-jc4880p443c/community/bigbag-JC4880P443C-examples/trimmed/examples/` -
  PlatformIO examples for the same board from
  [bigbag/JC4880P443C-examples](https://github.com/bigbag/JC4880P443C-examples) (no licence: read
  for ideas, do not copy code; see [../community/README.md](../community/README.md)), trimmed to
  the board-unique ones: sleep/wakeup, battery ADC, RS485. Its Bluedroid BLE scanner — whose
  transport-first init pattern was key to solving our NimBLE issue (see
  [04-bluetooth.md](04-bluetooth.md)) — is now outdated and lives in
  `.../bigbag-JC4880P443C-examples/archived-examples/examples/07_bluetooth`.
- `reference/espcontrol/upstream/` (local copy, see
  [reference/espcontrol/README.md](../../../reference/espcontrol/README.md)) — **ESPHome**-based
  production firmware with official JC4880P443
  support. Not drop-in code for an IDF project, but it independently confirms our pin map,
  supplies field-proven config values (SDIO 20 MHz, backlight 100 Hz/80%, I2C 400 kHz), a
  P4-specific sdkconfig hardening checklist, and an on-device web-server architecture worth
  studying. Full survey: [07-espcontrol-findings.md](07-espcontrol-findings.md).
  License caveat: PolyForm Noncommercial.

## Architecture pattern used in `src/sample_hello_world`

- LVGL callbacks never do feature work — they post a `feature_id_t` to a FreeRTOS queue.
- One worker task executes features serially (also serializes the shared I2S/codec).
- All UI updates from the worker go through `bsp_display_lock(timeout)` / `bsp_display_unlock()`.
- One app-wide audio format (16 kHz / 16-bit / mono) set via `bsp_extra_codec_set_fs`.
