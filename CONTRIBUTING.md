# Contributing

Thanks for looking. This repository is small on purpose: two Guition ESP32-P4 boards, one
product firmware, one bring-up sample, one benchmark, and documentation that tries to be
right. Contributions that keep it that way are welcome.

## Scope

| In scope | Out of scope |
|---|---|
| Fixes and features for `src/bedside_clock` and `src/sample_hello_world` on the supported boards | Ports to boards without an ESP32-P4 |
| The 10.1" JC8012P4A1C port along the [board-profile plan](docs/architecture/board-profiles-plan.md) | Vendoring whole third-party trees (link them from `reference/` instead) |
| Documentation corrections, especially where a document says "assumed" and you measured it | Redistributing vendor binaries, schematics or datasheets |
| Reliability work that retires an item in [docs/reliability-checklist.md](docs/reliability-checklist.md) | Features that need an internet service to boot |

If you are unsure, open an issue first; see [docs/reporting-issues.md](docs/reporting-issues.md).

## Development setup

[docs/getting-started.md](docs/getting-started.md) covers the toolchain (ESP-IDF v5.5.5
exactly), the first build and the first flash; [docs/howto/build-and-flash.md](docs/howto/build-and-flash.md)
has every `idf.py` recipe. Builds are verified on Windows 11 with PowerShell; Linux and macOS
should work and reports either way are useful.

## Branches and commits

- Work on a branch named for the change (`fix/sd-ldo-channel`, `docs/10in-touch`,
  `feat/board-profile-jc8012`). The default branch is only updated by pull request.
- One logical change per commit, imperative subject line under 72 characters, a body that
  says why. Reference issues with `Fixes #N` where true.
- Do not commit generated files: `build*/`, `managed_components/`, `sdkconfig`. Do commit
  `dependencies.lock` when you deliberately changed a component version, and say so in the
  commit body together with the reason.
- Never commit credentials. The sample's WiFi settings come from Kconfig or an untracked
  overlay; the clock's secrets live in NVS. Run [docs/howto/publish-checklist.md](docs/howto/publish-checklist.md)
  section 1 before pushing.

## Code style

The firmware is C (one C++ file for the tflite-micro engine), ESP-IDF idioms, four-space
indentation, braces on every control statement, `snake_case`, module prefixes
(`wifi_mgr_*`, `nav_*`, `evlog_*`). Match the file you are in. The conventions that matter
more than formatting:

| Rule | Why |
|---|---|
| **LVGL callbacks never do feature work.** They post a job to the worker queue and return. All `lv_*` calls from other tasks go through the display lock with a finite timeout and a skip-on-timeout path | Touch is polled by the LVGL task; a blocked LVGL task is a dead device |
| **Blocking calls live on the worker or a dedicated task**, never on the LVGL task, the event loop, an `esp_timer` callback or the HTTP server task | `esp_wifi_*` and every server call are blocking RPCs or TLS |
| **No `ESP_ERROR_CHECK` on runtime conditions** (C6 missing, server down, allocation failed). Log, degrade, continue | A dead co-processor must read "wireless unavailable", not boot-loop |
| **Large buffers in PSRAM** (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`); internal RAM is for DMA descriptors and stacks that need it. Treat every DMA allocation as fallible | The board idles at under 120 KB internal free |
| **No board literals in `main/` once the board profile lands** (pins, panel size, LDO channels, speaker type). Until then, keep new ones next to the existing ones in the BSP layer and flag them in the PR | The 10.1" port depends on it |
| **Settings schema is append-only**: bump `SETTINGS_VERSION`, add a migration block, keep the legacy mirror fields | Field devices must upgrade and downgrade without losing alarms |
| **Journal decisions, not just errors** (`evlog()` for anything a user would ask about the next morning); log read-once error details at the failure site | The device is debugged over WiFi at 06:30, not over a cable |
| **Local patches to managed components** are marked `LOCAL PATCH (<app>, <date>)` with a comment that says what breaks without them, and listed in `THIRD_PARTY_NOTICES.md` | A component refresh must not silently drop them (Apache-2.0 also requires the notice) |

Every rule above traces to a real failure in [docs/learnings.md](docs/learnings.md) and
[docs/reliability-checklist.md](docs/reliability-checklist.md).

## Documentation expectations

A change is not done until the documents that describe the old behaviour are updated:

- Board facts (pins, versions, what works) go in the device folder, `devices/<board>/`.
  Mark what you measured and what you assumed; the honesty ledger
  (`06-status-and-assumptions.md`) is the model.
- App behaviour and API changes go in the app's docs and in
  [docs/howto/http-api.md](docs/howto/http-api.md) when the API is involved.
- Add a line to `CHANGELOG.md` for anything a user would notice, with the date and the
  hardware it was verified on.
- New documents get a row in [docs/README.md](docs/README.md).
- Style: plain, precise, no filler, tables and numbered steps where they help, every command
  in a fenced block, PowerShell first with a bash equivalent where it differs, LF line endings,
  no em-dashes.

## Licence of contributions

By submitting a contribution you agree that it is licensed under the repository's
[MIT licence](LICENSE). Third-party code you bring in must have a compatible licence and an
entry in `THIRD_PARTY_NOTICES.md` (origin, version or commit, licence, what was changed). GPL
code cannot be linked into the firmware; PolyForm Noncommercial code (EspControl) may be
learned from but not copied; see [docs/licensing.md](docs/licensing.md).

## Hardware verification statement

Every pull request that touches firmware states what it was run on, in this form, in the PR
description (the template asks for it):

```
Tested on: Guition JC4880P443C, ESP32-P4 rev v1.3, rear label JC4880P443C_I_W, C6 slave 2.12.12
Build:     idf.py -B build_mww ... (ESP-IDF v5.5.5)
Checked:   boot clean, WiFi connect, alarm ring with song, 30 min idle, no watchdog
Not run:   camera, USB audio
```

"Not tested on hardware" is an acceptable statement for documentation-only changes and for
code a maintainer can run; it is not acceptable for changes to display, touch, audio, the C6
link or power sequencing. If you only have one of the two boards, say which.

## Pull request checklist

1. Builds cleanly with the documented command (`idf.py build`, and `-B build_mww` if `bedside_clock` is touched).
2. Hardware verification statement filled in.
3. No secrets, no generated files, no vendor binaries ([publish checklist](docs/howto/publish-checklist.md)).
4. Documents and `CHANGELOG.md` updated; `docs/README.md` index updated if a document was added.
5. Local patches to managed components, if any, marked and listed in `THIRD_PARTY_NOTICES.md`.
6. If you changed `dependencies.lock`, the PR body says which component moved and why.
