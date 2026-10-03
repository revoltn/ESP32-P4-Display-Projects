# ESP32-P4 Guition smart displays

Firmware, board knowledge and hard-won bring-up notes for the **Guition ESP32-P4 + ESP32-C6
touch panels**: the 4.3" **JC4880P443C** and the 10.1" **JC8012P4A1C**. The main product is
`bedside_clock`, an alarm clock with music streaming, voice control and an HTTP API that has
been running on the 4.3" panel since September 2026; the 10.1" panel was added to the repo on
2026-10-02 and its port is planned, not done.

Everything here was verified on real boards unless a document says otherwise. Both panels use
the early **ESP32-P4 rev v1.3** silicon, which bites in several documented ways.

> Folder name note: this working folder is still called `ESP32-P4-Function-EV-Board` after
> the Espressif dev kit whose BSP the vendor forked. The GitHub repository name should be
> something like `esp32-p4-guition-displays`; nothing inside depends on the folder name.

## Boards

| Board | Panel | Touch | Audio | Extras | Status |
|---|---|---|---|---|---|
| [Guition JC4880P443C 4.3"](devices/guition-jc4880p443c/) | 480x800 ST7701S, MIPI-DSI | GT911 | ES8311 + external speaker | OV02C10 camera, microSD, RS-485, battery ADC | `bedside_clock` daily use |
| [Guition JC8012P4A1C 10.1"](devices/guition-jc8012p4a1c/) | 800x1280 JD9365, MIPI-DSI | GSL3680 | ES8311 + built-in speaker, 2 mics | OV02C10 camera, microSD, RTC, battery ADC | identified, documented, factory firmware |

Shared platform: ESP32-P4 rev v1.3 (16 MB flash, 32 MB PSRAM) with an ESP32-C6 for WiFi 6 /
BLE over SDIO via esp_hosted. Chip-level notes: [devices/esp32-p4](devices/esp32-p4/),
[devices/esp32-c6](devices/esp32-c6/).

## What is in the repo

```
src/
  bedside_clock/          the product (ESP-IDF 5.5.5, LVGL 9.5, esp-sr, microWakeWord)
  sample_hello_world/     first bring-up sample: menu of every hardware feature
  sample_wakeword_bench/  microWakeWord on the P4: numerics and cost measurements
devices/
  esp32-p4/  esp32-c6/    chip-level facts that apply to every board
  guition-jc4880p443c/    4.3" board card, board.json, bring-up series, firmware inventory
  guition-jc8012p4a1c/    10.1" board card, board.json, bring-up notes, factory boot log
docs/                     getting started, how-tos, troubleshooting, learnings, licensing,
                          reliability checklist, architecture (board-profiles plan)
reference/                pointers to ESPHome and EspControl snapshots used for study
THIRD_PARTY_NOTICES.md    every third-party component, font, model and reference, with licence
```

Third-party trees (vendor packages, community repos, ESPHome, EspControl) stay on disk under
`devices/*/vendor`, `devices/*/community` and `reference/*/upstream` but are **gitignored**;
each has a tracked `README.md` with the upstream URL, version, licence and SHA256. See
[docs/licensing.md](docs/licensing.md) for the rules and the audit result.

## Quick start

```powershell
& $env:USERPROFILE\esp\esp-idf\export.ps1          # ESP-IDF 5.5.5
cd src\bedside_clock
idf.py -p COM3 build flash monitor                  # 4.3" board; Ctrl-] exits monitor
```

First time here: read [docs/getting-started.md](docs/getting-started.md). It covers the
toolchain, telling the two boards apart (`esptool.py chip_id`), the mandatory chip-revision
sdkconfig lines, the `build_mww` wake-word variant, WiFi and API configuration, and what to
back up before flashing.

## bedside_clock in one paragraph

Giant-digit clock with three weekday-masked alarm slots, songs and playlists streamed from a
Navidrome/Subsonic server (with on-device cache and self-healing playlist ids), wake-word voice
control ("Hey Mr Clock" via microWakeWord, or Espressif WakeNet) with MultiNet commands and an
optional LocalAI backend, camera-based ambient dimming, USB audio output when a USB speaker is
plugged in, a web UI and an HTTP API on port 80 behind an `X-Api-Key` header, a persistent
event journal and core dumps for post-mortems. Details: [src/bedside_clock/README.md](src/bedside_clock/README.md).

## Documentation map

| Start here | Then |
|---|---|
| [docs/README.md](docs/README.md) index | [docs/getting-started.md](docs/getting-started.md) |
| [docs/troubleshooting.md](docs/troubleshooting.md) symptom -> fix | [docs/learnings.md](docs/learnings.md) the lessons behind the fixes |
| [docs/reliability-checklist.md](docs/reliability-checklist.md) rules every firmware for these boards must satisfy | [docs/architecture/board-profiles-plan.md](docs/architecture/board-profiles-plan.md) how one codebase will serve both boards |
| [devices/guition-jc4880p443c/docs/](devices/guition-jc4880p443c/docs/) the numbered 4.3" bring-up series (hardware, toolchain, components, Bluetooth, troubleshooting, camera, TLS corruption investigation) | [devices/guition-jc8012p4a1c/docs/](devices/guition-jc8012p4a1c/docs/) 10.1" bring-up notes, GSL3680 touch, C6 firmware, revisions |
| [docs/reporting-issues.md](docs/reporting-issues.md) | [CONTRIBUTING.md](CONTRIBUTING.md) |

## Five things that will bite you

1. **Chip revision.** Both boards are P4 rev v1.3; ESP-IDF 5.5 builds for v3.1 by default and
   esptool refuses the image. `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` and
   `CONFIG_ESP32P4_REV_MIN_100=y` are mandatory.
2. **Two LDO channels.** Channel 3 at 2.5 V powers the MIPI DSI PHY (black screen without it);
   channel 4 at 3.3 V powers the microSD (error `0x107` without it). The factory firmware of
   the 10.1" still fails the second one.
3. **C6 firmware versions must match the host library.** The 4.3" ships esp_hosted slave
   2.12.12; the 10.1" ships 2.3.0 and already warns about it in its own factory log.
4. **The PPA 2D accelerator can hang forever on rev v1.3.** `bedside_clock` carries patches
   that bound the wait and fall back to CPU rotation ([src/bedside_clock/patches](src/bedside_clock/patches/)).
5. **Hardware AES corrupts TLS records in PSRAM** under sustained streaming; software AES is
   configured on purpose ([the investigation](devices/guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md)).

## Licence

Code and documentation written for this repository: [MIT](LICENSE). Vendored Espressif and
community components keep their own licences (Apache-2.0, MIT, OFL, CC BY 4.0); the 10.1"
touch driver is consumed as a GPL-2.0-or-later registry dependency and is never vendored. The
full list, including what is deliberately *not* redistributed, is in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
