# Build and flash recipes

Every `idf.py` invocation used in this repository, with the traps that cost time. Prerequisite:
ESP-IDF 5.5.5 exported in the current shell ([getting-started.md](../getting-started.md) section 2).

```powershell
& $env:USERPROFILE\esp\esp-idf\export.ps1
```

```bash
. ~/esp/esp-idf/export.sh
```

Replace `COM3` with your port (`COM4` for the 10.1" on the author's PC, `/dev/ttyACM0` on Linux).

## The projects

| Project | Path | CMake project name (ELF) | Build dirs | Partition table |
|---|---|---|---|---|
| Bedside clock | `src/bedside_clock` | `bedside_v01` | `build` (WakeNet), `build_mww` (microWakeWord) | nvs, phy_init, 8 MB factory app, 4 MB `model` (esp-sr), 128 KB `coredump` |
| Bring-up sample | `src/sample_hello_world` | `hello_world` | `build` | nvs, phy_init, 4 MB factory app |
| Wake-word bench | `src/sample_wakeword_bench` | `mww_test` | `build` (esp-nn PIE), `build_ref` (ANSI C) | same layout as the clock |

All three target the 4.3" JC4880P443C. Each project's `sdkconfig.defaults` is the source of
truth; the generated `sdkconfig` is not committed.

## Everyday commands

| Task | Command |
|---|---|
| Select the chip (first time, or after deleting `build/`) | `idf.py set-target esp32p4` |
| Build | `idf.py build` |
| Flash everything (bootloader, partition table, app, and the `model` partition for the clock) | `idf.py -p COM3 flash` |
| Flash only the app (fast, after code changes) | `idf.py -p COM3 app-flash` |
| Monitor | `idf.py -p COM3 monitor` (Ctrl-] exits, Ctrl-T Ctrl-H lists shortcuts) |
| Build, flash, monitor in one go | `idf.py -p COM3 flash monitor` |
| Faster flashing | add `-b 921600` |
| Edit configuration interactively | `idf.py menuconfig` |
| Binary size | `idf.py size`, `idf.py size-components` |
| Read a stored core dump | `idf.py -p COM3 coredump-info` (see [serial-capture-and-logs.md](serial-capture-and-logs.md)) |

### app-flash versus flash

`app-flash` writes only the app partition. Use `flash` when any of these changed:

- `partitions.csv` or `sdkconfig` entries that affect the bootloader,
- the esp-sr models (`CONFIG_SR_WN_*`, `CONFIG_SR_MN_*`) in `bedside_clock`: they live in the
  `model` partition that only a full `flash` writes. After an `erase-flash`, an `app-flash`
  alone leaves the clock without voice models; the app logs the missing model instead of
  panicking, but voice stays off until you `flash`,
- you are switching between projects with different partition tables (sample to clock or back).

Switching between the clock and the wake-word bench is `app-flash` safe: they share a layout.

## Build variants with separate build directories

The `-B` build directory, `-D SDKCONFIG` and `-D SDKCONFIG_DEFAULTS` must be given on every
command for that variant, including `flash`, `monitor` and `coredump-info`.

bedside_clock, microWakeWord variant (`main/Kconfig.projbuild`, `sdkconfig.mww.defaults`):

```powershell
idf.py -B build_mww -D SDKCONFIG=sdkconfig.mww -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.mww.defaults" build
idf.py -B build_mww -p COM3 flash monitor
idf.py -B build_mww -p COM3 app-flash
```

bedside_clock on the **10.1" Guition JC8012P4A1C** (board overlay `sdkconfig.jc8012p4a1c.defaults`,
layered after the shared defaults; the 10.1" unit enumerates as COM4 on the author's PC):

```powershell
idf.py -B build_jc8012p4a1c_mww -D SDKCONFIG=sdkconfig.jc8012p4a1c_mww -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.jc8012p4a1c.defaults;sdkconfig.mww.defaults" build
idf.py -B build_jc8012p4a1c_mww -p COM4 flash monitor
```

The overlay selects the BSP's 1280x800 branch (`CONFIG_BSP_LCD_TYPE_1280_800`, JD9365 panel,
GSL3680 touch, PA on GPIO20) and the **panel revision**: `CONFIG_BSP_JD9365_PANEL_V2` for
units whose rear label number is 2628 or higher (DPI 70 MHz, own init table),
`CONFIG_BSP_JD9365_PANEL_V1` for 2627 and below (DPI 60 MHz, driver default table). Both are
visible in `menuconfig` under the BSP display settings. A wrong revision gives a black or
torn panel and nothing worse; rebuild with the other one. Without the overlay every command
builds the 4.3" board, exactly as before.

sample_wakeword_bench, ANSI-C reference kernels:

```powershell
idf.py -B build_ref -D SDKCONFIG=sdkconfig.ref -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.ref.defaults" build
```

Local, untracked overlays (WiFi credentials for the sample, experiments) follow the same
pattern: `-D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.local.defaults"`.

In bash, quote the semicolon list the same way: `-D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.mww.defaults"`.

## When configuration changes do not take effect

`sdkconfig` is generated once from the defaults files and then kept. After editing any
`sdkconfig*.defaults`, or after adding a local component that introduces new Kconfig symbols
(the camera driver, for example), delete the generated file so it is rebuilt:

```powershell
Remove-Item sdkconfig              # or sdkconfig.mww for the variant
idf.py build
```

Symptom of forgetting: `CONFIG_CAMERA_OV02C10` is "unknown" or a new option silently stays at
its old value.

## Erasing the flash

```powershell
idf.py -p COM3 erase-flash
```

This removes, besides the app: the NVS settings (WiFi credentials, alarms, server URLs, API key,
the crash counter), the NVS mirror of the event journal, the stored core dump, and the esp-sr
`model` partition. Follow it with a full `flash`, never only `app-flash`.

## Cleaning, and the stale build directory after a move

Folders in this repository were renamed on 2026-10-02 (`src/bedside_v01` is now
`src/bedside_clock`, `src/hello_world` is `src/sample_hello_world`). A build directory carried
along from before the move fails immediately with:

```
CMake Error: The current CMakeCache.txt directory .../src/bedside_clock/build/CMakeCache.txt is different than the directory .../src/bedside_v01/build where CMakeCache.txt was created. This may result in binaries being created in the wrong place. If you are not sure, reedit the CMakeCache.txt
CMake Error: The source ".../src/bedside_clock/CMakeLists.txt" does not match the source ".../src/bedside_v01/CMakeLists.txt" used to generate cache.  Re-run cmake with a different source directory.
```

Fix: remove the build directory and start over (the download of managed components is cached
by `dependencies.lock`, so this is a few minutes, not a re-download of the toolchain):

```powershell
Remove-Item -Recurse -Force build, build_mww -ErrorAction SilentlyContinue
idf.py set-target esp32p4
idf.py build
```

```bash
rm -rf build build_mww && idf.py set-target esp32p4 && idf.py build
```

`idf.py fullclean` is the gentler option for an unmoved tree (it empties `build/` but keeps the
directory). `idf.py clean` only removes compiled objects and is rarely what you want.

## Managed components, local patches and the lock file

`dependencies.lock` is committed on purpose: it freezes a resolution that is known to build
and run. A registry refresh once broke the build (`esp_lvgl_port` 2.9.0 against IDF 5.5.4).

To refresh one component deliberately:

```powershell
Remove-Item dependencies.lock
Remove-Item -Recurse -Force managed_components\espressif__esp_lvgl_port
idf.py reconfigure
```

**Before you do that for `bedside_clock`, know that three managed components carry local
patches** (marked `LOCAL PATCH` in the source) that a refresh silently discards:

| Component | Patch | What breaks without it |
|---|---|---|
| `espressif__esp_lvgl_port` (`src/lvgl9/esp_lvgl_port_disp.c`, `lcd_ppa.c`) | Bounded wait on the PPA rotate and a permanent fall-back to CPU rotation after a failure | The LVGL task can block forever inside the PPA driver on rev v1.3 silicon: frozen screen and dead touch while music keeps playing |
| `espressif__esp_codec_dev` (`esp_codec_dev.c`) | `esp_codec_dev_open()` fails when `set_fmt` fails | A failed I2S DMA descriptor allocation at a sample-rate change ends in a NULL load and a reboot at the start of every song |
| `espressif__esp_hosted` (`hosted_malloc_align`, 128-byte alignment) | PSRAM mempool buffers aligned to the L2 cache line | `Unrecoverable host sdio state` reboots under sustained SDIO traffic with `CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y` |

Diff the refreshed copy against the old one and re-apply the marked hunks. The vendored
components under `components/` (the board BSP, `esp_lcd_st7701`, `esp_cam_sensor` 2.1.0,
`bsp_extra`) are not touched by a refresh; do not edit the BSP's manifest, its unused
dependencies must keep resolving. Background:
[03-working-components.md](../../devices/guition-jc4880p443c/docs/03-working-components.md).

## Windows-specific traps

| Symptom | Cause and fix |
|---|---|
| `ninja: error: WriteFile(.ninja_lock): Unable to create file. Permission denied` or `failed recompaction: Permission denied` | A killed or timed-out `idf.py build` left `cmake`, `ninja` or the IDF `python` running with handles on `build/`. `Get-Process | Where-Object { $_.Name -match 'ninja|cmake' }`, stop them, rebuild. If `build/` was half-deleted meanwhile, delete it fully |
| Object path too long errors deep inside `managed_components` | `set(CMAKE_OBJECT_PATH_MAX 512)` is already in each top-level `CMakeLists.txt`; keep the checkout path short and set `git config --global core.longpaths true` |
| `could not open port 'COM3'` / access denied | Another process holds the port: a second `idf.py monitor`, `readcom.py`, or a serial terminal. Close it. Only one process can own the port |
| Flash refused with `requires chip revision in range [v3.1 - v3.99] (this chip is revision v1.3)` | The rev selection was lost (usually a deleted `sdkconfig.defaults` line or a build from a foreign defaults file). Both `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` and `CONFIG_ESP32P4_REV_MIN_100=y` are required; delete `sdkconfig` and rebuild |

## Flashing two boards from one PC

Nothing is shared between ports; give `-p` every time and keep separate shells per board. The
build directory is per project, not per board, so flashing the same build to COM3 and COM4 in
turn is fine. Note that the 10.1" board must not receive the 4.3" images yet
([getting-started.md](../getting-started.md) section 10).
