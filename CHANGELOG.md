# Changelog

All notable changes to this repository. Dates are ISO 8601.

## 2026-10-02 - repository restructure for publication

### Added
- `devices/` tree: chip notes (`esp32-p4`, `esp32-c6`) and board folders
  (`guition-jc4880p443c`, `guition-jc8012p4a1c`) with board cards, `board.json`
  profiles, firmware inventories, vendor/community provenance READMEs.
- Second board identified and documented: **Guition JC8012P4A1C_I_W_Y 10.1"**
  (ESP32-P4 rev v1.3 + C6, JD9365 800x1280, GSL3680 touch, ES8311, OV02C10),
  factory boot log captured, vendor package mirrored locally (gitignored).
- `docs/architecture/board-profiles-plan.md`: design for building `bedside_clock`
  for several boards from one tree, driven by `devices/<board>/board.json`.
- `docs/`: getting started, how-tos, troubleshooting, learnings, reporting issues,
  licensing; `CONTRIBUTING.md`; GitHub issue and PR templates.
- `LICENSE` (MIT) and `THIRD_PARTY_NOTICES.md`; per-project `NOTICE.md`.
- `src/bedside_clock/patches/`: the local esp_lvgl_port and esp_codec_dev patches
  exported from `managed_components/` so a clean clone builds the same firmware.
- `.gitignore` / `.gitattributes`: build state, generated sdkconfig, local secrets,
  and every third-party reference tree are excluded; pointer READMEs are tracked.

### Changed
- Project folders renamed: `src/bedside_v01` -> `src/bedside_clock`,
  `src/hello_world` -> `src/sample_hello_world`, `src/mww_test` ->
  `src/sample_wakeword_bench` (CMake `project()` names follow). Build directories
  must be recreated (`idf.py fullclean`).
- The 4.3" bring-up documentation series (`docs/01` to `11`) moved to
  `devices/guition-jc4880p443c/docs/`; `MUST-IMPLEMENT.md` is now
  `docs/reliability-checklist.md`.
- Reference material relocated: ESPHome and EspControl snapshots to
  `reference/<name>/upstream/`; the 4.3" vendor package to
  `devices/guition-jc4880p443c/vendor/package/`; community repositories to
  `devices/*/community/<owner-repo>/` (all gitignored, each with a tracked README).
- Credentials, LAN addresses, API keys and user paths removed from sources, tools
  and docs; `sample_hello_world` WiFi and Navidrome settings moved to Kconfig
  (`SAMPLE_*`), `bedside_clock` ships no default AI endpoint or key.
- Apache-2.0 section 4(b) "modified file" notices added to the vendored files this
  repository changed (`bsp_extra`, `esp_hosted` port, `esp-audio-player`).
- Montserrat OFL licence text added next to the font sources.

### Removed
- `src/hello_world/specs.md` (contained credentials; replaced by a README).
- Redundant `fonts/Dangrek.zip`, `dependencies.lock.pre-5.5.5.bak` files, compiled
  Python caches.

### Known gaps after this change
- `bedside_clock` and the samples still target the 4.3" board only; the 10.1" port
  follows the plan above (nothing flashed to the 10.1" yet).
- The 10.1" C6 co-processor runs esp_hosted slave 2.3.0 and must be reflashed to
  2.12.x before the host library in `bedside_clock` will talk to it.
- The rear label of the 10.1" unit has not been read, so V1 vs V2 panel is unknown.

## Earlier history (summary, pre-git)

- 2026-08-29/30: 4.3" bring-up, `hello_world` sample verified (display, touch, WiFi,
  BLE, camera, audio paths).
- 2026-09-01 onward: `bedside_v01` alarm clock (settings schema v1 to v11), HTTP API,
  Navidrome playback, TLS corruption investigation (software AES), PPA hang
  mitigation, voice control (WakeNet, then microWakeWord "Hey Mr Clock"), event
  journal, three alarm slots, USB audio sink.
- Details: `devices/guition-jc4880p443c/docs/` and `src/bedside_clock/docs/`.
