# JC4880P443C documentation index

The numbered series is the bring-up record for the Guition JC4880P443C, written against the actual
board between 2026-08-29 and 2026-09-27. Each file states what was verified on hardware and what is
an assumption. The board card with the pin map, spec table and gotchas is [../README.md](../README.md).

## The numbered series

| # | File | One line |
|---|---|---|
| 01 | [01-hardware.md](01-hardware.md) | The board as measured: silicon rev v1.3, memory, display and touch, ES8311 pins, C6 SDIO link, SD power via LDO ch4, USB host facts, schematic sheet index |
| 02 | [02-toolchain-and-build.md](02-toolchain-and-build.md) | ESP-IDF 5.5.5 install on Windows, the build/flash/monitor loop, the non-obvious sdkconfig lines, partition tables, scripted serial capture |
| 03 | [03-working-components.md](03-working-components.md) | Which BSP and component versions work together, why the vendor-patched BSP is used verbatim, the 2026-09-02 upstream review, proven donor code |
| 04 | [04-bluetooth.md](04-bluetooth.md) | BLE over the C6: host/slave version match, transport-first init, controller enable; verified scan recipe; C6 firmware lifecycle |
| 05 | [05-troubleshooting.md](05-troubleshooting.md) | Every problem hit during bring-up with exact error text and fix, community C6 reflash and SD gotchas, benign log lines, diagnostic techniques |
| 06 | [06-status-and-assumptions.md](06-status-and-assumptions.md) | The honesty ledger: verified, assumed, confirmed unavailable, untested; the known-good baseline |
| 07 | [07-espcontrol-findings.md](07-espcontrol-findings.md) | Survey of EspControl, production ESPHome firmware for this board: hardware confirmations, hardening values, web server design, C6 recovery |
| 08 | [08-esphome-findings.md](08-esphome-findings.md) | Survey of ESPHome core: native display model, hosted and BLE support, P4 fixtures, coverage verdict against the vendor tree |
| 09 | [09-camera.md](09-camera.md) | OV02C10 over MIPI-CSI: the esp_cam_sensor 2.1.0 override, required sdkconfig, V4L2 to PPA to LVGL pipeline, PPA alignment rules |
| 10 | [10-bedside-v01-review.md](10-bedside-v01-review.md) | Code review log of bedside_clock (2026-09-02 onward): fixes shipped, open defects, reliability scorecard, HTTP API assessment |
| 11 | [11-tls-stream-corruption-investigation.md](11-tls-stream-corruption-investigation.md) | Case study: hardware AES-DMA corrupts TLS records in PSRAM on the P4; bisection table, fix, re-test procedure, upstream report draft |

## Extra documents

| File | One line |
|---|---|
| [device-specifications-log.md](device-specifications-log.md) | The device's self-report (chip, memory, peripherals, versions) captured over serial on 2026-08-30, plus the 2026-09-02 toolchain update |
| [interface-types.md](interface-types.md) | The eight external connectors (battery, two USB-C, JP1, CN2, J5, J4, CN3): wiring, voltage, speed, limitations, good uses, cross-interface conflicts |
| [schematics/README.md](schematics/README.md) | The five schematic sheets kept as webp (local only, gitignored), which vendor sheet each comes from, how to regenerate them |

## Reading order

- New to the board: 01, 02, 03, then skim the benign-log table in 05 and read 06.
- Adding Bluetooth: 04, then the ESPHome counterpart in 08.
- Adding the camera: 09, then the esp_cam_sensor row in 03.
- Anything that streams over HTTPS: 11, then 05 §9 and §10.
- Choosing ESPHome versus raw ESP-IDF: 08, then 07.
- Taking over bedside_clock: 10 (the §1 subsections are newest last), then
  [../../../docs/reliability-checklist.md](../../../docs/reliability-checklist.md).
- Wiring something to a connector: interface-types.md, then the schematic sheet it cites.

## Conventions

- Paths in backticks are relative to the repository root unless they start with `./` or `../`.
- "Verified" means observed on the author's unit on the date given; "community" means read in a
  third-party repository and not reproduced here; "assumption" is labelled as such.
- COM3 is the port on the author's PC; substitute your own.

## Names used in the older text

The repository was reorganised on 2026-10-02. The historical documents keep the names that were
current when they were written; this is the mapping.

| Name in the text | Where it is now |
|---|---|
| `src/hello_world` | `src/sample_hello_world` |
| `src/bedside_v01`, "bedside_v01" | `src/bedside_clock` (the CMake project name is still `bedside_v01`) |
| `src/mww_test` | `src/sample_wakeword_bench` |
| `MUST-IMPLEMENT.md` | `docs/reliability-checklist.md` at the repository root |
| `demo_docs_specs/` (vendor files) | `devices/guition-jc4880p443c/vendor/package/` (gitignored) |
| `demo_docs_specs/01_*.md` to `10_*.md`, `90_*.md`, `README.md` | `devices/guition-jc4880p443c/community/seobaeksol-JC4880P443C_I_W/` (third-party notes, not vendor documentation) |
| `_archived/demo_docs_specs/` | `devices/guition-jc4880p443c/vendor/package-archived-demos/` |
| `demos-docs-samples-2/` | `devices/guition-jc4880p443c/community/bigbag-JC4880P443C-examples/trimmed/` |
| `_archived/demos-docs-samples-2/` | `devices/guition-jc4880p443c/community/bigbag-JC4880P443C-examples/archived-examples/` |
| `demo-espcontrol-main/` | `reference/espcontrol/upstream/` (gitignored) |
| `demo-esphome-dev/` | `reference/esphome/upstream/` (gitignored) |
| `_archived/README.md` | `reference/archive-inventory-2026-08-30.md` |
| `docs/schematics/*.webp` | same place, now gitignored; see `schematics/README.md` |
