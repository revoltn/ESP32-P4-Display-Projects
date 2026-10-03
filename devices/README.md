# devices/ - hardware truth

One folder per chip and per development board. Firmware under `src/` consumes the
`board.json` files here (see
[docs/architecture/board-profiles-plan.md](../docs/architecture/board-profiles-plan.md));
humans read the `README.md` board cards. Nothing in this tree is copied from a vendor
without a licence: vendor packages and community repositories live in gitignored
`vendor/` and `community/` sub-folders with a tracked `README.md` that records the
origin, version, licence and SHA256 so authors keep their credit.

## Boards

| Folder | Board | Panel | Touch | Status in this repo |
|---|---|---|---|---|
| [guition-jc4880p443c/](guition-jc4880p443c/) | Guition JC4880P443C_I_W, 4.3" | 480x800 ST7701S, MIPI-DSI 2 lanes | GT911 (polled) | `bedside_clock` in daily use; `sample_hello_world`; `sample_wakeword_bench` |
| [guition-jc8012p4a1c/](guition-jc8012p4a1c/) | Guition JC8012P4A1C_I_W_Y, 10.1" | 800x1280 JD9365, MIPI-DSI 2 lanes | GSL3680 (firmware upload, INT wired) | identified and documented 2026-10-02; factory firmware only; port planned |

Both boards: ESP32-P4 **rev v1.3** (16 MB flash, 32 MB PSRAM) + ESP32-C6 over SDIO.

## Chips

| Folder | Covers |
|---|---|
| [esp32-p4/](esp32-p4/) | Silicon revisions, LDO channels, PSRAM/cache, MIPI-DSI pipeline, PPA hang, AES-DMA TLS corruption, USB host facts, toolchain |
| [esp32-c6/](esp32-c6/) | esp_hosted co-processor: version coupling, slave firmware lifecycle, BLE recipe, recovery |

## Folder template

```
devices/<board>/
  README.md          board card: identity, verified facts, spec, pin map, gotchas, status
  board.json         machine-readable profile (schema v1) consumed by firmware builds
  docs/              bring-up notes, investigations, logs of what was tried
  firmware/          README with factory/co-processor image inventory + SHA256 (images not hosted)
  logs/              captured serial logs (MACs redacted)
  vendor/            README + manifest (tracked); package/ (gitignored vendor download)
  community/         README (tracked); one gitignored sub-folder per third-party repo
```

## Differences at a glance

| | 4.3" JC4880P443C | 10.1" JC8012P4A1C |
|---|---|---|
| Panel / reset | ST7701S / GPIO5 | JD9365 / GPIO27 |
| DSI | 2 lanes, 750 Mbps (BSP), DPI 28 MHz | 2 lanes, 1.5 Gbps (BSP), DPI 60 MHz |
| Touch | GT911 0x5D, INT unwired | GSL3680 0x40, RST 22, INT 21, firmware upload |
| Speaker | external MX1.25 | built in |
| PA enable / I2S DIN | GPIO11 / GPIO48 | GPIO20 / GPIO11 |
| Mics | 1 analog | 2 analog (single ES8311 ADC) |
| Battery ADC | GPIO53 | GPIO52 |
| RTC battery holder | no | yes |
| C6 slave firmware as shipped | esp_hosted 2.12.12 | esp_hosted 2.3.0 |
| Same | I2C 7/8, SDIO 18/19/14-17 reset 54, backlight 23, console 37/38, LDO ch3 (DSI) + ch4 (SD), OV02C10 camera, ES8311 | |
