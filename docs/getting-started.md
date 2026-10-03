# Getting started

From an unopened Guition box to a board running code from this repository. Windows 11 with
PowerShell is the primary environment because that is where everything here was verified;
Linux and macOS notes are included where the commands differ.

Time budget: ESP-IDF install 20 to 40 minutes (download bound), first build of
`sample_hello_world` about 5 minutes, first build of `bedside_clock` about 10 minutes.

## 1. What you need

| Item | Notes |
|---|---|
| A supported board | Guition **JC4880P443C** (4.3", 480x800) is fully supported. Guition **JC8012P4A1C** (10.1", 800x1280) runs factory firmware only today; see section 10 |
| USB-C data cable | Charge-only cables are the most common "no COM port" cause. Both boards power themselves from USB; a supply of at least 600 mA is needed while flashing |
| External speaker (4.3" only) | The 4.3" board has **no speaker**. Plug a small speaker into the MX1.25 2-pin connector (NS4150 3 W class-D amplifier on board) or you will hear nothing. The 10.1" has a built-in speaker |
| PC | Windows 11 (PowerShell 5.1 or 7), or Linux/macOS. About 6 GB of disk for ESP-IDF plus toolchains. Git |
| Optional | A Navidrome (Subsonic API) music server and an OpenAI-compatible AI server (LocalAI) for the clock's music and voice features. Nothing in the repository depends on them |

Both boards carry an **ESP32-P4 silicon revision v1.3** plus an **ESP32-C6** for WiFi and
BLE. The P4 revision matters for every build; see section 5.

## 2. Install ESP-IDF 5.5.5

Exactly 5.5.5. Older 5.5.x fails to compile `esp_lvgl_port` 2.9 (the `on_frame_buf_complete`
DPI callback is missing before 5.5.5) and the project manifests require `idf: ">=5.5.5"` so a
stale checkout fails fast. ESP-IDF 6.x is a major migration and has not been attempted.

Windows (PowerShell):

```powershell
git clone -b v5.5.5 --recursive --depth 1 --shallow-submodules `
    https://github.com/espressif/esp-idf.git $env:USERPROFILE\esp\esp-idf
& $env:USERPROFILE\esp\esp-idf\install.ps1 esp32p4
```

Linux/macOS (bash):

```bash
git clone -b v5.5.5 --recursive --depth 1 --shallow-submodules \
    https://github.com/espressif/esp-idf.git ~/esp/esp-idf
~/esp/esp-idf/install.sh esp32p4
```

Then, **in every new shell** before any `idf.py` command:

```powershell
& $env:USERPROFILE\esp\esp-idf\export.ps1
```

```bash
. ~/esp/esp-idf/export.sh
```

Windows long paths: managed components produce deep object paths. The projects already set
`set(CMAKE_OBJECT_PATH_MAX 512)` in their top-level `CMakeLists.txt`; additionally keep the
checkout path short (a drive root or `C:\dev`) and enable long paths in Git:

```powershell
git config --global core.longpaths true
```

Details and the reasoning behind the IDF version:
[02-toolchain-and-build.md](../devices/guition-jc4880p443c/docs/02-toolchain-and-build.md).

## 3. Clone the repository

```powershell
git clone <repository-url> esp32-p4-guition
cd esp32-p4-guition
```

What you get and do not get:

| In the clone | Not in the clone (gitignored, see the README in each folder for the source) |
|---|---|
| `src/` firmware sources, `dependencies.lock` per project | `build*/`, `managed_components/`, generated `sdkconfig` |
| `devices/*/docs`, `devices/*/README.md` | Vendor packages (`devices/*/vendor/package/`), schematic images, factory firmware binaries |
| `reference/*/README.md` | The ESPHome and EspControl source trees themselves |

The first `idf.py` build downloads the managed components from the Espressif registry into
`managed_components/`, pinned by `dependencies.lock`.

## 4. Which board do I have

| | JC4880P443C (4.3") | JC8012P4A1C (10.1") |
|---|---|---|
| Vendor SKU on the rear label | `JC4880P443C_I_W` (module `JC-ESP32P4-M3`) | `JC8012P4A1C_I_W_Y` |
| Panel | 480x800 portrait, ST7701S over MIPI-DSI | 800x1280 portrait, JD9365 over MIPI-DSI |
| Touch | Goodix GT911 (I2C 0x5D) | Silead GSL3680 (needs a firmware upload at boot) |
| Speaker | External, MX1.25 2-pin | Built in |
| C6 slave firmware as shipped | esp_hosted 2.12.12 | esp_hosted 2.3.0 (too old for the host pinned here; see [howto/update-c6-firmware.md](howto/update-c6-firmware.md)) |
| Status in this repo | Full support, two apps | Factory firmware only; port planned |
| COM port on the author's PC | COM3 | COM4 |

Find the port on Windows:

```powershell
Get-CimInstance Win32_SerialPort | Select-Object Name, DeviceID
```

Linux: `ls /dev/ttyACM*`. macOS: `ls /dev/cu.usbmodem*`. The board enumerates as a plain
"USB Serial Device" through the P4's built-in USB-Serial/JTAG; no driver is needed. Each
board has two USB-C sockets: one is the USB-Serial/JTAG port (full speed, the one that
gives the COM port), the other is the P4's high-speed OTG port used for USB host. If nothing
appears, try the other socket and another cable.

Confirm the chip and its revision (after `export.ps1`):

```powershell
esptool.py --port COM3 chip_id
```

Expected lines (the rest of the output varies):

```
Detecting chip type... ESP32-P4
Chip is ESP32-P4 (revision v1.3)
Features: High-Performance MCU
```

`revision v1.3` is what every `sdkconfig.defaults` in this repository is built for. If you see
`v3.x`, you have newer silicon: remove `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` and
`CONFIG_ESP32P4_REV_MIN_100=y` from the project's `sdkconfig.defaults`, delete `sdkconfig`,
rebuild, and please report it (see [reporting-issues.md](reporting-issues.md)); nobody has
tested that combination here.

## 5. Back up the factory image before you flash

The factory firmware is the only known-good image for the board, it is not redistributed in
this repository, and the vendor's restore procedure needs their Windows tool. Read the whole
16 MB flash once and keep it:

```powershell
esptool.py --chip esp32p4 --port COM3 --baud 921600 read_flash 0x0 0x1000000 `
    devices\guition-jc4880p443c\firmware\factory-backup-$(Get-Date -Format yyyy-MM-dd).bin
```

```bash
esptool.py --chip esp32p4 --port /dev/ttyACM0 --baud 921600 read_flash 0x0 0x1000000 \
    devices/guition-jc4880p443c/firmware/factory-backup-$(date +%F).bin
```

Takes a few minutes. `devices/*/firmware/*.bin` is gitignored, so the file stays local. While
you are at it, capture the factory boot log too (section 7 shows how); the 10.1" factory log
in [devices/guition-jc8012p4a1c/logs/](../devices/guition-jc8012p4a1c/logs/) is such a capture.

Restoring later: `esptool.py --chip esp32p4 --port COM3 --baud 921600 write_flash 0x0 <file>`.
Never mix a merged factory image at offset 0x0 with the development layout (bootloader at
0x2000, partition table at 0x8000, app at 0x20000) in one session.

## 6. First build: sample_hello_world (4.3" only)

A menu with one feature per peripheral: WiFi, BLE scan, camera preview, organ synth, mic
record and playback, USB audio, device specifications. Good for proving the board.

```powershell
& $env:USERPROFILE\esp\esp-idf\export.ps1
cd src\sample_hello_world
idf.py set-target esp32p4        # first time only; also fetches managed components
idf.py build
idf.py -p COM3 flash monitor     # Ctrl-] leaves the monitor
```

The build configuration comes from `sdkconfig.defaults`; the generated `sdkconfig` is not
committed. If a build error mentions `on_frame_buf_complete`, your IDF is older than 5.5.5.

## 7. Build bedside_clock

Two variants share one source tree and one `sdkconfig.defaults`:

| Variant | Wake word engine | Build directory | Command |
|---|---|---|---|
| default | esp-sr WakeNet9 "Hi ESP" | `build` | `idf.py build` |
| `mww` | microWakeWord (tflite-micro), models embedded from `main/models/` | `build_mww` | see below |

```powershell
& $env:USERPROFILE\esp\esp-idf\export.ps1
cd src\bedside_clock
idf.py set-target esp32p4
idf.py build
idf.py -p COM3 flash monitor
```

The microWakeWord variant (from `main/Kconfig.projbuild`):

```powershell
idf.py -B build_mww -D SDKCONFIG=sdkconfig.mww -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.mww.defaults" build
idf.py -B build_mww -p COM3 flash monitor
```

Every later `idf.py` command for that variant needs the same `-B build_mww`. The first flash
of either variant must be a full `flash`, not `app-flash`: the esp-sr voice models live in a
4 MB `model` partition that only a full flash writes. All recipes, including `app-flash`,
erase and clean-after-moving, are in [howto/build-and-flash.md](howto/build-and-flash.md).

## 8. What a healthy first boot looks like

In the monitor, in this order (timestamps and exact numbers vary):

```
I esp_psram: Found 32MB PSRAM device
I esp_psram: Speed: 200MHz
I efuse_init: Chip rev:         v1.3
I app_init: ESP-IDF:          v5.5.5
W spi_flash: Detected boya flash chip but using generic driver      <- benign
W ledc: GPIO 23 is not usable, maybe conflict with others           <- benign
I ESP32_P4_EV: Setting LCD backlight: 100%
E lcd_panel: esp_lcd_panel_swap_xy(50): swap_xy is not supported by this panel   <- benign
I transport: Identified slave [esp32c6]
I transport: capabilities: 0xd
I transport:     * WLAN
I transport:       - HCI over SDIO
I transport:       - BLE only
```

The `transport:` block is the esp_hosted handshake with the C6. Read it before debugging
anything wireless: it tells you the slave's version (a `Version mismatch` warning there is
the first thing to fix if BLE is involved) and that Bluetooth Classic does not exist on this
board. The benign lines are explained in [troubleshooting.md](troubleshooting.md).

On screen:

| App | First boot |
|---|---|
| `sample_hello_world` | The menu. Tap "Device Specifications" for a live hardware summary |
| `bedside_clock` | The clock face in landscape with a status line ("Time from RTC - waiting for sync" or similar), service indicators, and a Settings tile. Until WiFi is configured the time comes from the RTC and the indicators stay dim |

If `bedside_clock` says `Recovered from crash (N)`, the crash-loop breaker counted an abnormal
reset; after three in a row it starts in safe mode (voice and camera off, HTTP API not
auto-started). The reason is in the event journal (Settings > Specifications).

## 9. Configure WiFi

### bedside_clock

On the device: Settings > WiFi > Scan, pick a network, enter the password, Connect. The
credentials are stored in NVS and used on every boot (`wifi_mgr_autoconnect`; a background
retry runs every 30 s if the router is slow). The clock face shows the IP address.

Over the HTTP API (once the API is enabled, section 9.2): `PATCH /api/v1/settings` with
`wifi_ssid` and `wifi_pass`. Useful when moving a configured clock to another network.

The C6 radio is 2.4 GHz only. A 5 GHz-only SSID cannot work.

### sample_hello_world

The sample takes its credentials from Kconfig (`SAMPLE_WIFI_SSID`, `SAMPLE_WIFI_PASSWORD`;
see the sample's `main/Kconfig.projbuild` for the full list). Either:

```powershell
idf.py menuconfig        # Sample configuration > WiFi
```

or keep an untracked overlay next to `sdkconfig.defaults` and build with it:

```
# sdkconfig.local.defaults (do not commit)
CONFIG_SAMPLE_WIFI_SSID="my-network"
CONFIG_SAMPLE_WIFI_PASSWORD="my-password"
```

```powershell
idf.py -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.local.defaults" build
```

Delete `sdkconfig` whenever you change a defaults file, or the old value sticks.

### 9.1 Optional: music server (bedside_clock)

Settings > Music API: server URL (`https://host[:port]`, no trailing slash), user, password,
then "Test" (expects "Server OK - N playlists"). The client speaks the Subsonic API as
implemented by Navidrome. Nothing is configured by default; without a server the alarm
falls back to a built-in beep.

### 9.2 Optional: AI backend (bedside_clock)

Settings > AI: server type (OpenAI-compatible, LocalAI tested), server address
(`http(s)://host:port`), API key, chat model name. The clock probes `GET {ai_url}/v1/models`
and uses `/v1/chat/completions` for natural-language music matching and
`/v1/audio/transcriptions` (whisper) for free-speech queries and optional wake-word
verification. No server, key or model ships with the firmware; leave the fields empty and
the voice grammar commands still work fully on-device.

### 9.3 Optional: HTTP control API (bedside_clock)

Settings > API: set a key, then enable. The server listens on port 80 while WiFi has an IP.
Everything you can do with it is in [howto/http-api.md](howto/http-api.md).

## 10. The 10.1" board today

The JC8012P4A1C is in the repository as a documented, unported board. Its factory firmware
(an esp-brookesia phone demo, IDF v5.5.4) boots and runs; the captured log shows what the
port has to deal with:

| Factory log fact | Consequence for the port |
|---|---|
| `Display resolution 800x1280`, `jd9365: LCD ID: 93 65 04` | Different panel driver and init table than the 4.3" |
| `gsl3680: load fw success` | Touch needs a firmware blob uploaded over I2C at boot, and the point-ID algorithm is GPL-2.0 (see [licensing.md](licensing.md)) |
| `Version mismatch: Host [2.11.0] > Co-proc [2.3.0] ==> Upgrade co-proc` | The C6 slave must be updated before the `~2.12.0` host used here will talk to it reliably |
| `sdmmc_init_ocr: send_op_cond (1) returned 0x107` right after `ldo: The voltage value 0 is out of the recommended range` | Even the factory firmware never powers the SD slot (LDO channel 4) |
| `Chip rev: v1.3` | Same silicon revision rules as the 4.3" |

Do not flash `bedside_clock` or `sample_hello_world` onto it yet: the panel and touch
drivers are different and the result is a black screen. The plan for building one app for
both boards is [architecture/board-profiles-plan.md](architecture/board-profiles-plan.md);
the board's own notes are in [devices/guition-jc8012p4a1c/](../devices/guition-jc8012p4a1c/README.md).

## 11. Where to go next

| Goal | Read |
|---|---|
| Every build and flash recipe | [howto/build-and-flash.md](howto/build-and-flash.md) |
| Something does not work | [troubleshooting.md](troubleshooting.md) |
| Capture logs the way bug reports need them | [howto/serial-capture-and-logs.md](howto/serial-capture-and-logs.md) |
| Talk to the clock | [howto/http-api.md](howto/http-api.md), [VOICE.md](../src/bedside_clock/docs/VOICE.md) |
| Write your own firmware for the board | [reliability-checklist.md](reliability-checklist.md), [learnings.md](learnings.md) |
