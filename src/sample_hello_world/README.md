# sample_hello_world

The first firmware written for the Guition **JC4880P443C** (ESP32-P4 rev v1.3 + ESP32-C6,
4.3" 480x800 portrait panel). It is an acclimatisation demo: an LVGL 9 touch menu where each
button exercises one piece of hardware and prints what happened to a scrolling log on the
screen. It is not an application and it is kept as the smallest known-good reference for the
board; `src/bedside_clock` grew out of it. Older documents call it `hello_world`.

Licence: MIT for `main/`; the vendored components are Apache-2.0, see [NOTICE.md](NOTICE.md)
and the repository's [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).

## Features and verification status

Menu entries in the order of `MENU_ITEMS[]` in `main/ui_menu.c`. "Verified" means seen working
on the board by the author (dates from the device docs and the old top-level README).

| Menu item | Module | What it does | Status |
|---|---|---|---|
| Connect WiFi | `feature_wifi.c` | esp_hosted (SDIO) + esp_wifi_remote to the C6; tries the configured SSID, then the fallback SSID; shows the IP | Verified 2026-08-30 (primary SSID). The fallback path was never exercised. |
| BLE Scan | `feature_bt.c`, `ble_adv_fmt.c` | NimBLE host on the P4, HCI tunnelled over SDIO (vHCI) to the C6; active scan, devices listed as `name (manufacturer) -NN dBm connectable` sorted by RSSI | Verified 2026-08-30 (10 devices). Requires the controller-enable order described in `devices/guition-jc4880p443c/docs/04-bluetooth.md` and a C6 slave at esp_hosted 2.12.x. BLE only; no Classic. |
| Navidrome | `feature_navidrome.c`, `navidrome_client.c` | Lists the server's playlists, streams songs (whole file into PSRAM, `fmemopen`, `esp-audio-player`), prefetches the next track, volume slider, Prev/Next/Stop | Verified 2026-09-01 with real playlists. |
| Play Toccata | `feature_music.c`, `music_score.h` | Additive organ synthesiser playing the opening of BWV 565 from a note table, 16 kHz mono to the ES8311 | Code complete; `bsp_extra_i2s_write` returns OK. Not heard on this sample (no speaker was attached when it was tested). The same codec path is verified in `bedside_clock`. |
| Record & Playback | `feature_echo.c` | 5 s mic capture into PSRAM, 5 s countdown, playback; strictly sequential | Code complete; reads return data. Not heard on this sample for the same reason. |
| Device Specifications | `feature_specs.c` | Categorised spec sheet mixing live values (chip rev, flash, PSRAM, heap, MAC, uptime, versions) with datasheet facts, including what the board does not have | Verified 2026-08-30. |
| USB Audio | `feature_usb_audio.c` | USB host + `usb_host_uac`: finds a USB Audio Class output on the OTG port, tries every advertised 16-bit format (48 kHz first) and plays a melody; also exports `usb_audio_ready()` / `usb_audio_play_mono16()` | Verified 2026-08-30 on a Jabra Speak 510 (audible, distorted; speakerphone DSP suspected). Needed the descriptor-walk fix in `components/espressif__usb_host_uac`. |
| Camera | `feature_camera.c`, `app_video.c` | OV02C10 over MIPI-CSI, 1920x1080 RAW10 1-lane, ISP to RGB565, PPA crop/scale into an LVGL canvas, full-screen live preview, tap to exit | Verified 2026-08-30 (live 30 fps preview, correct orientation without rotation). |

Removed since the first version: a "Hello world" label feature and a Bluetooth-speaker
attempt (the C6 is BLE-only, so Classic audio is impossible; USB audio replaced it).

## Configuration

Credentials are not in the source. They are Kconfig symbols with empty defaults
(`main/Kconfig.projbuild`, menu "Sample: hello world"): `SAMPLE_WIFI_SSID`,
`SAMPLE_WIFI_SSID_ALT` (optional fallback network, same password), `SAMPLE_WIFI_PASSWORD`,
`SAMPLE_NAVIDROME_URL` (`http(s)://host[:port]`, no trailing slash), `SAMPLE_NAVIDROME_USER`
(plain ASCII, it is pasted into the query string) and `SAMPLE_NAVIDROME_PASSWORD`. With an
empty SSID the WiFi feature, and everything that needs it, reports "not configured" and does
nothing; the same for an empty Navidrome URL.

Recommended: put them in an untracked `sdkconfig.local.defaults` next to `sdkconfig.defaults`
(the repository `.gitignore` excludes `sdkconfig.local*`) and layer it on top:

```
# sdkconfig.local.defaults  (never committed)
CONFIG_SAMPLE_WIFI_SSID="my-2.4GHz-network"
CONFIG_SAMPLE_WIFI_PASSWORD="..."
CONFIG_SAMPLE_NAVIDROME_URL="https://music.example.net"
CONFIG_SAMPLE_NAVIDROME_USER="..."
CONFIG_SAMPLE_NAVIDROME_PASSWORD="..."
```

```sh
idf.py -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.local.defaults" set-target esp32p4
```

Values typed in `menuconfig` end up in the generated `sdkconfig`, which is gitignored, so that
route is also safe; it just does not survive a `set-target` / fullclean.

## Build, flash, monitor

ESP-IDF 5.5.5 with the ESP32-P4 toolchain (`idf_component.yml` requires `idf >=5.5.5`). Run
from this directory.

```sh
idf.py set-target esp32p4          # applies sdkconfig.defaults (rev v1.3, PSRAM, BSP branch, BT, camera)
idf.py build
idf.py -p COMx flash monitor       # Ctrl-] leaves the monitor
```

`sdkconfig.defaults` points at the vendor-repurposed `CONFIG_BSP_LCD_TYPE_1024_600` branch
(that is how the Guition BSP names the 480x800 ST7701), enables tear-avoidance direct mode
(so there is no software rotation here; the UI is portrait), NimBLE over vHCI, hub support for
the USB host, and the 1-lane FHD camera mode. The partition table is `nvs`, `phy_init`,
`factory` 4 MB. First-time toolchain setup for Windows is in
`devices/guition-jc4880p443c/docs/02-toolchain-and-build.md`.

## Architecture

- **Queue and worker.** LVGL callbacks never do feature work. A button posts a `feature_id_t`
  (`app_common.h`) to a queue with `app_request_feature()`; one worker task (16 KB stack, the
  Navidrome feature runs mbedTLS and cJSON on it) pops it, marks the menu busy and runs
  `feature_<x>_run()` to completion. UI updates from the worker go through `ui_log()` /
  `ui_set_status()`, which take the LVGL lock internally.
- **Audio** is 16 kHz, 16-bit, mono app-wide (`AUDIO_*` in `app_common.h`); the codec is
  initialised lazily by `app_audio_ensure_init()`. The ES8311 is a mono codec, which is why
  `components/bsp_extra` downmixes stereo in `bsp_extra_i2s_write`.
- **Crash-loop breaker** (`app_main.c`): an abnormal reset increments a counter in NVS, a
  30 s uptime timer clears it, and three in a row are flagged on screen.
- **Board support** is the Guition-patched Espressif BSP copied into `components/` together
  with `bsp_extra`, the ST7701 driver and `esp_cam_sensor` 2.1.0 (the registry's 2.0.1 has no
  OV02C10 driver). `esp_hosted` and `usb_host_uac` are local copies with fixes. Do not edit
  their manifests; `bsp_extra` depends on the BSP by sibling relative path. Details of each
  change: [NOTICE.md](NOTICE.md).
- **Adding a feature**: a row in `MENU_ITEMS[]`, a value in `feature_id_t`, a case in
  `worker_task()`, a source file in `main/CMakeLists.txt`.

## Scope and caveats

- 4.3" JC4880P443C only. Nothing here knows about the 10.1" JC8012P4A1C (different panel,
  touch controller and BSP branch).
- The speaker is external (MX1.25 2-pin). No sound without one.
- A USB audio device goes into the USB socket that does not provide the serial console; both
  sockets carry bus power.
- TLS streaming on this chip corrupts records under load when hardware AES is on; this sample
  keeps the IDF default (hardware AES) and can show "stream read error" during long Navidrome
  sessions. `bedside_clock` disables hardware AES; the investigation is
  `devices/guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md`.
- The BLE scan depends on the C6 slave version matching the host component; a factory C6 at
  esp_hosted 2.3.x (as shipped on the 10.1" board) will not sync.
- Verified-versus-assumed ledger for the whole board:
  `devices/guition-jc4880p443c/docs/06-status-and-assumptions.md`.
