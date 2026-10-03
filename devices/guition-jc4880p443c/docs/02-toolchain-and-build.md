# Toolchain & Build Workflow (Windows)

Verified on Windows 11, 2026-08-29/30.

## ESP-IDF installation (one-time, already done on this machine)

ESP-IDF **v5.5.5** installed at `%USERPROFILE%\esp\esp-idf`:

```powershell
git clone -b v5.5.5 --recursive --depth 1 --shallow-submodules `
    https://github.com/espressif/esp-idf.git $env:USERPROFILE\esp\esp-idf
& $env:USERPROFILE\esp\esp-idf\install.ps1 esp32p4
```

Why the 5.5 line: the vendor's working demo tree is
`devices/guition-jc4880p443c/vendor/package/1-Demo/idf_examples/ESP-IDF_5.5.4/` (local only, see
[../vendor/README.md](../vendor/README.md)) and its manifests require IDF ≥5.5.2.
Why 5.5.5 specifically (upgraded from 5.5.4 on 2026-09-02): it is the current 5.5.x bugfix
release and adds the `on_frame_buf_complete` DPI callback that `esp_lvgl_port` ≥2.8 requires.
Both app manifests now pin `idf: ">=5.5.5"` so a stale 5.5.4 checkout fails fast.
(ESP-IDF 6.x exists but is a major migration — note: 6.1 changes the default P4 silicon rev
to v3.0; our `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` already covers the rev v1.3 chip.)

## Every build shell

```powershell
& $env:USERPROFILE\esp\esp-idf\export.ps1
cd <repo>\src\sample_hello_world       # or src\bedside_clock
```

## Build / flash / monitor

```powershell
idf.py set-target esp32p4      # first time only; downloads managed components
idf.py build
idf.py -p COM3 flash monitor   # Ctrl-] exits the monitor
```

The board is on **COM3** (USB-Serial/JTAG; auto-reset works, no boot-mode button needed).

## Required sdkconfig entries (the non-obvious ones)

From [src/sample_hello_world/sdkconfig.defaults](../../../src/sample_hello_world/sdkconfig.defaults):

```
# The board's P4 is silicon rev v1.3. IDF 5.5 defaults to rev >=3.1 and
# esptool then REFUSES to flash ("requires chip revision in range v3.1-v3.99").
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
CONFIG_ESP32P4_REV_MIN_100=y

# Vendor repurposed this Kconfig branch to mean the 480x800 ST7701 panel
CONFIG_BSP_LCD_TYPE_1024_600=y

# PSRAM: 200 MHz + XIP, proven by the factory demo
CONFIG_SPIRAM=y
CONFIG_SPIRAM_SPEED_200M=y
CONFIG_SPIRAM_XIP_FROM_PSRAM=y
CONFIG_CACHE_L2_CACHE_256KB=y
CONFIG_CACHE_L2_CACHE_LINE_128B=y

# esp_hosted SDIO link to the C6 (pins derive from this board preset)
CONFIG_ESP_HOSTED_ENABLED=y
CONFIG_ESP_HOSTED_P4_DEV_BOARD_FUNC_BOARD=y
CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE=y
CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE=54
```

Bluetooth entries are documented separately in [04-bluetooth.md](04-bluetooth.md).

## Windows-specific gotchas

- Top-level `CMakeLists.txt` needs `set(CMAKE_OBJECT_PATH_MAX 512)` (long-path guard;
  the vendor demo does the same).
- One build failed with `ninja: error: WriteFile(.ninja_lock): Permission denied` —
  the retry succeeded unchanged. **Assumption: transient file-lock race** (antivirus or
  a lingering process handle); not investigated further because it never recurred.

## Partition table

`src/sample_hello_world` uses a simple non-OTA table (recordings live in PSRAM, no storage
partition needed): nvs @0xb000 (24 KB), phy_init, factory app 4 MB. App currently uses
~1.5 MB of it. The factory demo instead uses a 7 MB factory app + 6 MB SPIFFS layout —
copy that if you need on-flash assets.

## Serial capture without the interactive monitor

`idf.py monitor` is interactive; for scripted log capture the IDF Python env has pyserial:

```powershell
& %USERPROFILE%\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe src\bedside_clock\tools\readcom.py COM3 15 reset
```

(`readcom.py` = tiny pyserial reader in `src/bedside_clock/tools/`; `reset` toggles RTS so you
catch the boot log. Only one process may hold COM3 — close `idf.py monitor` first. For a
background capture while you drive the device over the HTTP API, see
[11-tls-stream-corruption-investigation.md §10](11-tls-stream-corruption-investigation.md).)
