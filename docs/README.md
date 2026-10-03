# Documentation index

Everything in this repository that is meant to be read, in the order that saves you the most
time. Hardware facts were measured on real boards; where a document says "assumed", it is.

Naming note: older documents, CMake `project()` names and log tags still say `hello_world`
and `bedside_v01`. They are the same code as `src/sample_hello_world` and `src/bedside_clock`
(renamed 2026-10-02). The ELF files are therefore `build/hello_world.elf` and
`build/bedside_v01.elf`.

## Reading paths

### "I just got a board"

1. [getting-started.md](getting-started.md): toolchain, which board you have, backup, first build.
2. The board card for your board: [4.3" JC4880P443C](../devices/guition-jc4880p443c/README.md)
   or [10.1" JC8012P4A1C](../devices/guition-jc8012p4a1c/README.md).
3. [troubleshooting.md](troubleshooting.md), section "Benign log lines", before you chase anything.
4. [reliability-checklist.md](reliability-checklist.md) when you start writing firmware of your own.

### "I want to build bedside_clock"

1. [getting-started.md](getting-started.md), sections 6 to 9.
2. [howto/build-and-flash.md](howto/build-and-flash.md) for every `idf.py` recipe, including the `build_mww` variant.
3. [howto/http-api.md](howto/http-api.md) to drive the clock from a PC.
4. [src/bedside_clock/docs/VOICE.md](../src/bedside_clock/docs/VOICE.md) for what you can say to it.
5. [howto/add-a-wake-word.md](howto/add-a-wake-word.md) if "Hi ESP" is not the phrase you want.

### "I am porting to the 10.1 inch board"

1. [devices/guition-jc8012p4a1c/README.md](../devices/guition-jc8012p4a1c/README.md) and its
   [bring-up notes](../devices/guition-jc8012p4a1c/docs/bring-up-notes.md).
2. [architecture/board-profiles-plan.md](architecture/board-profiles-plan.md): how board-specific
   code is being moved out of `main/`.
3. [howto/add-a-board.md](howto/add-a-board.md): the device folder template.
4. [howto/update-c6-firmware.md](howto/update-c6-firmware.md): the 10.1" ships an older C6 slave.
5. [learnings.md](learnings.md): what the 4.3" bring-up already paid for.

### "Something is broken"

1. [troubleshooting.md](troubleshooting.md): symptom, cause, fix, by stage.
2. [howto/serial-capture-and-logs.md](howto/serial-capture-and-logs.md): get a log, a journal, a core dump.
3. The deep dives: [05-troubleshooting.md](../devices/guition-jc4880p443c/docs/05-troubleshooting.md),
   [04-bluetooth.md](../devices/guition-jc4880p443c/docs/04-bluetooth.md),
   [11-tls-stream-corruption-investigation.md](../devices/guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md).
4. [reporting-issues.md](reporting-issues.md) if it is still broken.

### "I want to contribute"

1. [../CONTRIBUTING.md](../CONTRIBUTING.md).
2. [reliability-checklist.md](reliability-checklist.md): the rules every firmware here follows.
3. [licensing.md](licensing.md) and [../THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) before adding third-party code.
4. [howto/publish-checklist.md](howto/publish-checklist.md) before you push.

## Every document in the repository

### Root

| Document | Purpose |
|---|---|
| [../README.md](../README.md) | Repository overview: what is here, which boards, where to start |
| [../LICENSE](../LICENSE) | MIT, for the code and documentation written for this repository |
| [../THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) | Licences and provenance of vendored and referenced third-party code |
| [../CHANGELOG.md](../CHANGELOG.md) | Dated, user-visible changes |
| [../CONTRIBUTING.md](../CONTRIBUTING.md) | Scope, conventions, hardware verification statement, licence of contributions |

### docs/

| Document | Purpose |
|---|---|
| [getting-started.md](getting-started.md) | From an unopened box to a flashed board: toolchain, backup, first builds, WiFi and service setup |
| [troubleshooting.md](troubleshooting.md) | Symptom to cause to fix, organised by stage, with exact log text |
| [learnings.md](learnings.md) | Numbered hard-won lessons with the story and the evidence |
| [reporting-issues.md](reporting-issues.md) | What a useful bug report contains and how to redact it |
| [reliability-checklist.md](reliability-checklist.md) | MUST rules (boot, watchdogs, display, C6 link, BT, WiFi, audio, OTA, config, memory), each tied to a real failure |
| [licensing.md](licensing.md) | Which licence applies where, including the GPL touch driver and non-commercial reference code |
| [architecture/board-profiles-plan.md](architecture/board-profiles-plan.md) | Plan for moving board literals out of `main/` so one app builds for both boards |
| [howto/build-and-flash.md](howto/build-and-flash.md) | All `idf.py` recipes: variants, build dirs, app-flash vs flash, erase, clean, stale cache |
| [howto/serial-capture-and-logs.md](howto/serial-capture-and-logs.md) | `idf.py monitor`, `readcom.py`, device log and journal over HTTP, core dumps, JTAG backtrace |
| [howto/http-api.md](howto/http-api.md) | The bedside_clock HTTP control API: auth, endpoints, PowerShell and curl examples |
| [howto/add-a-wake-word.md](howto/add-a-wake-word.md) | Embedding another microWakeWord model |
| [howto/update-c6-firmware.md](howto/update-c6-firmware.md) | Why and how the ESP32-C6 slave firmware gets updated |
| [howto/add-a-board.md](howto/add-a-board.md) | The `devices/<board>/` folder template and the board-profile plan |
| [howto/publish-checklist.md](howto/publish-checklist.md) | Secrets grep, licence check, gitignore spot checks, size check |

### devices/

| Document | Purpose |
|---|---|
| [../devices/README.md](../devices/README.md) | Board matrix: what is supported on which board, at what level |
| [../devices/esp32-p4/README.md](../devices/esp32-p4/README.md) | The ESP32-P4 as used here: silicon revision, PSRAM, peripherals, known silicon-level issues |
| [../devices/esp32-c6/README.md](../devices/esp32-c6/README.md) | The ESP32-C6 co-processor: esp_hosted slave firmware, versions, update paths |

#### Guition JC4880P443C (4.3", 480x800)

| Document | Purpose |
|---|---|
| [README.md](../devices/guition-jc4880p443c/README.md) | Board card: SKU, label, what works, COM port, quick facts |
| [docs/01-hardware.md](../devices/guition-jc4880p443c/docs/01-hardware.md) | Measured pin map, display, touch, audio, C6 link, SD, USB, power, schematic sheet index |
| [docs/02-toolchain-and-build.md](../devices/guition-jc4880p443c/docs/02-toolchain-and-build.md) | ESP-IDF 5.5.5 on Windows, required sdkconfig entries, Windows gotchas |
| [docs/03-working-components.md](../devices/guition-jc4880p443c/docs/03-working-components.md) | Component versions that work together, what was upgraded and what was deliberately not |
| [docs/04-bluetooth.md](../devices/guition-jc4880p443c/docs/04-bluetooth.md) | BLE over the C6: three stacked root causes and the verified recipe |
| [docs/05-troubleshooting.md](../devices/guition-jc4880p443c/docs/05-troubleshooting.md) | Every bring-up problem with exact error text, plus diagnostic techniques |
| [docs/06-status-and-assumptions.md](../devices/guition-jc4880p443c/docs/06-status-and-assumptions.md) | Verified vs assumed vs untested |
| [docs/07-espcontrol-findings.md](../devices/guition-jc4880p443c/docs/07-espcontrol-findings.md) | Survey of EspControl (production ESPHome firmware for this board) |
| [docs/08-esphome-findings.md](../devices/guition-jc4880p443c/docs/08-esphome-findings.md) | Survey of ESPHome core: native board support, what to copy |
| [docs/09-camera.md](../devices/guition-jc4880p443c/docs/09-camera.md) | OV02C10 over MIPI-CSI: component trap, sdkconfig, pipeline, PPA rules |
| [docs/10-bedside-v01-review.md](../devices/guition-jc4880p443c/docs/10-bedside-v01-review.md) | 2026-09-02 code review of the clock app: fixes, open items, scorecard |
| [docs/11-tls-stream-corruption-investigation.md](../devices/guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md) | Case study: hardware AES corrupts TLS records in PSRAM; the bisection and the fix |
| [docs/device-specifications-log.md](../devices/guition-jc4880p443c/docs/device-specifications-log.md) | Live "Device Specs" capture: what the silicon has and does not have |
| [docs/interface-types.md](../devices/guition-jc4880p443c/docs/interface-types.md) | The eight external connectors: electrical limits and good uses |
| [firmware/README.md](../devices/guition-jc4880p443c/firmware/README.md) | Factory image backups and restore notes (binaries are not committed) |
| [vendor/README.md](../devices/guition-jc4880p443c/vendor/README.md) | Where the vendor package comes from, checksums, what is inside |
| [community/README.md](../devices/guition-jc4880p443c/community/README.md) | Community repositories consulted, with their licences |

#### Guition JC8012P4A1C (10.1", 800x1280)

| Document | Purpose |
|---|---|
| [README.md](../devices/guition-jc8012p4a1c/README.md) | Board card: SKU, label, factory state, COM port |
| [docs/bring-up-notes.md](../devices/guition-jc8012p4a1c/docs/bring-up-notes.md) | What the factory firmware reveals and the port plan |
| [docs/touch-gsl3680.md](../devices/guition-jc8012p4a1c/docs/touch-gsl3680.md) | The Silead GSL3680 touch controller: firmware load, licence problem |
| [docs/c6-firmware.md](../devices/guition-jc8012p4a1c/docs/c6-firmware.md) | The C6 slave as shipped (2.3.0) and how to bring it to 2.12.x |
| [docs/revisions.md](../devices/guition-jc8012p4a1c/docs/revisions.md) | Board and silicon revisions seen |
| [logs/factory-boot-2026-10-02.txt](../devices/guition-jc8012p4a1c/logs/factory-boot-2026-10-02.txt) | Serial capture of the factory firmware booting |
| [vendor/INVENTORY.md](../devices/guition-jc8012p4a1c/vendor/INVENTORY.md) | Provenance and licence census of the vendor package |

### reference/

| Document | Purpose |
|---|---|
| [esphome/README.md](../reference/esphome/README.md) | Why the ESPHome core tree is consulted, version, licence; content itself is not committed |
| [espcontrol/README.md](../reference/espcontrol/README.md) | Same for EspControl (PolyForm Noncommercial: patterns yes, code no) |
| [archive-inventory-2026-08-30.md](../reference/archive-inventory-2026-08-30.md) | What was archived from the original working tree and why |

### src/

| Document | Purpose |
|---|---|
| [bedside_clock/](../src/bedside_clock/) | The product: alarm clock with Navidrome music, voice control, HTTP API, camera ambient dimming |
| [bedside_clock/docs/VOICE.md](../src/bedside_clock/docs/VOICE.md) | User guide to voice control: the phrases, the tuning, the failure modes |
| [bedside_clock/docs/VOICE_AND_AI_PIPELINE.md](../src/bedside_clock/docs/VOICE_AND_AI_PIPELINE.md) | Developer guide: models, tasks, two pipelines, AI matching, training |
| [bedside_clock/main/models/README.md](../src/bedside_clock/main/models/README.md) | The embedded wake-word model registry |
| [bedside_clock/tools/wakeword_train/README.md](../src/bedside_clock/tools/wakeword_train/README.md) | Training a microWakeWord model: recipe, environment, seven lessons |
| [sample_hello_world/](../src/sample_hello_world/) | First bring-up sample: menu of one feature per peripheral (WiFi, BLE, camera, audio, USB audio, specs) |
| [sample_wakeword_bench/README.md](../src/sample_wakeword_bench/README.md) | microWakeWord on rev v1.3 silicon: timing, arena, correctness results |
