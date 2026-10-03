# reference/

Third-party source trees that the board documentation cites. They are kept on disk so that a
reference such as "`esp32_ble/ble.cpp` lines 183-252" can be checked, and so that a future upstream
change can be diffed against what the docs described.

## What is tracked and what is not

From the root `.gitignore`:

```
reference/*/upstream/
reference/*/*.zip
```

Only each entry's `README.md` is committed (upstream URL, snapshot date and version, licence, SHA256
of the archive, why it is kept). The source itself is not redistributed: one entry is GPLv3/MIT mixed
and the other is noncommercial-only, and neither needs to be in this repository for anyone else to
obtain it. To rebuild a local copy, download the archive named in the entry's README, verify the
hash, and extract it so that the archive's single top folder becomes `upstream/`.

## Entries

| Entry | Upstream | Snapshot | Licence | Used for |
|---|---|---|---|---|
| [esphome/](esphome/README.md) | <https://github.com/esphome/esphome>, `dev` branch | `esphome-dev.zip`, downloaded 2026-08-30, `__version__ = "2026.9.0-dev"` | ESPHome licence: MIT for Python and everything else, **GPLv3 for the C++ runtime** | Native `mipi_dsi` models `JC4880P443` and `JC8012P4A1`, the `esp32_hosted` component (pins esp_hosted 2.12.12), the hosted BLE bring-up sequence, `gsl3670`, 28 ESP32-P4 test fixtures. Findings: [devices/guition-jc4880p443c/docs/08-esphome-findings.md](../devices/guition-jc4880p443c/docs/08-esphome-findings.md) |
| [espcontrol/](espcontrol/README.md) | <https://github.com/jtenniswood/espcontrol>, `main` branch | `espcontrol-main.zip`, downloaded 2026-08-30 | **PolyForm Noncommercial 1.0.0**, Required Notice: Copyright (c) 2026 jtenniswood | Production ESPHome firmware for both Guition boards: device YAML for `jc4880p443`, `jc8012p4a1` and `jc8012p4a1-v2`, the wire-free C6 recovery component, field-proven hardening values. Findings: [devices/guition-jc4880p443c/docs/07-espcontrol-findings.md](../devices/guition-jc4880p443c/docs/07-espcontrol-findings.md) |

## Rules

- Patterns, pin values and configuration numbers may be learned from either tree and re-expressed in
  our own code with a citation in the commit or the doc.
- No GPLv3 C++ from ESPHome and no code at all from EspControl is copied into `src/`. ESPHome Python
  or YAML may be adapted under MIT with attribution.
- A new entry needs: `reference/<name>/README.md` with upstream URL, exact version or commit (or the
  archive's newest file date when the archive carries no commit), licence with any required notice
  reproduced, SHA256 and size of the archive, and the list of paths the docs rely on.

## Also in this folder

- [archive-inventory-2026-08-30.md](archive-inventory-2026-08-30.md): the log of what was archived or
  trimmed on 2026-08-30 (it was `_archived/README.md` before the 2026-10-02 reorganisation), with the
  paths rewritten to the current layout.
