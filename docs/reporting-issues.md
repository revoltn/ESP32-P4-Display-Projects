# Reporting issues

A report that contains the items below is usually answered with a fix or a precise question;
one that does not is usually answered with a request for them. The issue forms under
`.github/ISSUE_TEMPLATE/` ask for the same things.

## 1. What to include

| Item | How to get it | Why it matters |
|---|---|---|
| Board, SKU and rear label text | Read the sticker: `JC4880P443C_I_W` (4.3") or `JC8012P4A1C_I_W_Y` (10.1"), plus any revision marking | Guition ships batches with different C6 firmware and, apparently, different panels |
| Chip identification | `esptool.py --port COM3 chip_id`, paste the `Chip is ...` line | Rev v1.3 vs v3.x changes everything |
| ESP-IDF version | `idf.py --version`, or the `app_init: ESP-IDF:` boot line | Only v5.5.5 is supported |
| Exact `idf.py` command | Copy from your shell history, including `-B`, `-D SDKCONFIG...` | Variants build differently |
| Which defaults files | `sdkconfig.defaults` alone, or plus `sdkconfig.mww.defaults`, plus any local overlay (contents with secrets removed) | A one-line config difference has explained whole investigations |
| Local changes | `git status --short` and `git diff --stat` | Including patched managed components |
| Serial log from reset | `python tools\readcom.py COM3 20 reset > boot.log` from an exported shell; see [howto/serial-capture-and-logs.md](howto/serial-capture-and-logs.md) | The esp_hosted handshake block and the reset reason are in the first seconds |
| The event journal (bedside_clock) | `GET /api/v1/events` or a photo of Settings > Specifications | Survives reboots; shows alarm decisions and stalls |
| The device log (bedside_clock) | `GET /api/v1/log` | Classified stream errors, probes, repairs |
| Core dump, if the device said one is stored | `idf.py -p COM3 coredump-info > coredump.txt` (same `-B` as the build) | Backtraces of all tasks at the panic |
| Heap at the time | `GET /api/v1/status`, the `heap` object | DMA-capable internal RAM predicts audio failures |
| Photo or short video of the screen | Phone camera | For display, touch and rotation problems nothing else shows the symptom |
| What you expected and what happened | Plain words, one paragraph | |

For hardware bring-up reports (new board, new batch, new revision) add the factory boot log
captured before you flashed anything, and the `transport:` handshake block in particular.

## 2. How to redact

Before pasting, replace these; nothing in a log is needed verbatim to debug the problem.

| Secret | Where it appears | Replace with |
|---|---|---|
| WiFi SSID and password | boot log (`Connecting to "..."`), `GET /api/v1/status` (`wifi.ssid`), settings dumps, sdkconfig overlays | `<ssid>`, `<password>` |
| API key | your own command lines, `X-Api-Key` headers, `tools/wake_clips.py --key` | `<api-key>` |
| Server URLs with credentials | Navidrome and AI server addresses, especially anything with `u=`/`p=`/`t=`/`s=` query parameters or a bearer token | `https://<music-server>` |
| MAC addresses | `Base MAC`, `MAC:` in esptool output, BLE scan lists (other people's devices) | `xx:xx:xx:xx:xx:xx` |
| LAN IP addresses | `got ip:`, `wifi.ip`, your API examples | Keep if you like; they are not secret, but be consistent |
| Song titles and playlist names | playback logs | Keep unless they identify a person |

The device never logs passwords or the API key, and `GET /api/v1/settings` returns secrets as
`*_set` booleans only; most redaction is about your own shell history.

Quick scrub in PowerShell:

```powershell
(Get-Content boot.log) -replace '([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}', 'xx:xx:xx:xx:xx:xx' `
    -replace 'Connecting to "[^"]+"', 'Connecting to "<ssid>"' | Set-Content -Encoding utf8 boot.redacted.log
```

```bash
sed -E 's/([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}/xx:xx:xx:xx:xx:xx/g; s/Connecting to "[^"]+"/Connecting to "<ssid>"/g' boot.log > boot.redacted.log
```

## 3. What a good minimal repro looks like

1. A clean checkout at a named commit, built with the documented command and the stock
   defaults files (plus one overlay you paste in full, secrets removed).
2. The shortest sequence of actions that shows the problem, numbered, with the time between
   steps if it matters ("wait 20 s", "play three songs").
3. One observable: a log line, a photo, an API response. "It feels slower" is a start, a
   number is a report.
4. If it is intermittent: how many attempts, how many failures, over what time.
5. If you already bisected something (a config, a component version), the table of what you
   tried and the result for each. Theories are welcome, but keep them separate from
   observations.

Patterns that have worked here: an API-driven load (`play_random` plus `next` every 20 s)
with a 100 s serial capture and a count of a specific error string; one Kconfig change per
build; a photo of the screen next to the log timestamp.

## 4. Labels

| Label | Use |
|---|---|
| `bug` | Something documented as working does not |
| `hardware` | Board, revision, batch, C6 firmware, wiring, power |
| `board:jc4880p443c`, `board:jc8012p4a1c` | Which board; add both if it is generic |
| `app:bedside_clock`, `app:sample` | Which firmware |
| `docs` | Wrong, missing or unclear documentation |
| `enhancement` | A feature request |
| `needs-info` | Maintainer is waiting for items from section 1 |
| `upstream` | Root cause is in ESP-IDF or a component; link the upstream issue |

## 5. Security

A key in plain HTTP on your LAN is a design limit, not a vulnerability; it is documented in
[howto/http-api.md](howto/http-api.md). If you find something that lets a client without the
key change state or read secrets, do not open a public issue; use the contact in the root
README.

## 6. What not to report here

Problems inside ESP-IDF, esp_hosted or LVGL that reproduce without this repository belong
upstream; link them from an issue here if they affect these boards (the hardware AES
corruption is an example, see
[11 section 11](../devices/guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md)
for a drafted upstream report). Vendor package contents, licences and download problems go to
the vendor; this repository links to their material and does not host it.
