# Add a board

Two things make a board part of this repository: a `devices/<board>/` folder that documents
it, and (eventually) a board profile that lets `src/bedside_clock` build for it without
touching `main/`. The second part is a work in progress; read
[architecture/board-profiles-plan.md](../architecture/board-profiles-plan.md) first. It defines
what a profile contains (panel, touch, audio, pins, LDO channels, PSRAM and flash sizes, C6
link parameters) and the rule that `main/` stops carrying board literals once it lands.

## The device folder template

Mirror `devices/guition-jc4880p443c/` and `devices/guition-jc8012p4a1c/`:

| Path | Content | Committed? |
|---|---|---|
| `README.md` | Board card: vendor SKU and rear label text, panel, touch, audio, speaker, memory, C6 slave version as shipped, COM port behaviour, support status, photo of the label | yes |
| `board.json` | Machine-readable profile: the same facts as the card in the shape the board-profile plan specifies | yes |
| `docs/` | Bring-up notes, per-peripheral findings, a troubleshooting file with exact log text, a verified-vs-assumed ledger | yes |
| `docs/schematics/` | Index of sheets with SHA256 and the vendor URL; image files themselves are gitignored (no redistribution licence) | index only |
| `firmware/README.md` | Which factory image was read from which unit, SHA256, how to restore; `*.bin` is gitignored | README only |
| `logs/` | Factory boot log and other reference captures, redacted (MAC, SSID) | yes |
| `vendor/README.md` or `INVENTORY.md` | Where the vendor package comes from, size, checksums, licence census of what is inside; `vendor/package/` is gitignored | README only |
| `community/README.md` | Community repositories consulted: URL, commit, licence, what was taken from each; clones under `community/*/` are gitignored | README only |

## Order of work that has paid off

1. **Back up the factory image and capture its boot log** before anything else
   ([getting-started.md](../getting-started.md) section 5). The 10.1" log answered the panel,
   touch, C6 version and SD questions without a single line of new code.
2. Read the esp_hosted handshake block; decide whether the C6 needs an update
   ([update-c6-firmware.md](update-c6-firmware.md)).
3. Find the vendor's working IDF demo and **reuse its patched BSP and panel driver verbatim**
   as local components, rather than hand-rolling init tables
   ([03-working-components.md](../../devices/guition-jc4880p443c/docs/03-working-components.md)).
4. Inventory licences before copying: the 10.1" touch stack carries a GPL-2.0 point-ID
   algorithm and an unlicensed firmware blob ([licensing.md](../licensing.md)).
5. Bring up one peripheral at a time with the sample app pattern (queue plus worker, LVGL
   callbacks never do feature work), and write down what was verified versus assumed.
6. Sign off against [reliability-checklist.md](../reliability-checklist.md) before calling the
   board supported.

## What to send upstream

A new board or revision report goes in as a "Hardware bring-up" issue
([reporting-issues.md](../reporting-issues.md)); a port goes in as a pull request with the
hardware verification statement filled in ([CONTRIBUTING.md](../../CONTRIBUTING.md)).
