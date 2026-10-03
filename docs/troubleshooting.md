# Troubleshooting

Symptom, cause, fix, organised by the stage you are stuck in. Log text is quoted exactly as
the sources recorded it so that a search lands here. Each section links to the board document
that holds the full story. If the symptom is not here, capture the evidence described in
[howto/serial-capture-and-logs.md](howto/serial-capture-and-logs.md) and open an issue
([reporting-issues.md](reporting-issues.md)).

Before anything else: read the **benign log lines** table at the end. Several alarming-looking
lines are printed by every working build.

## 1. Flashing and USB

| Symptom | Cause | Fix |
|---|---|---|
| No COM port appears | Charge-only cable, or the cable is in the high-speed OTG socket instead of the USB-Serial/JTAG socket | Use a data cable; try the other USB-C socket. No driver is needed; the board shows up as "USB Serial Device" |
| `A fatal error occurred: bootloader/bootloader.bin requires chip revision in range [v3.1 - v3.99] (this chip is revision v1.3)` | IDF 5.5 builds for P4 rev 3.1 by default; this silicon is rev v1.3 and the two are binary-incompatible | `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` **and** `CONFIG_ESP32P4_REV_MIN_100=y` in `sdkconfig.defaults` (the second alone does nothing, the first unhides it). Delete `sdkconfig`, rebuild. ESPHome: `engineering_sample: true`; Arduino/PlatformIO: `chip_variant: esp32p4_es` |
| "Illegal instruction" inside the second-stage bootloader (Arduino, ESPHome) | Same cause as above with a toolchain that does not check the revision before flashing | Same fix |
| `could not open port 'COM3'` / access denied | Another process owns the port (`idf.py monitor`, `readcom.py`, a terminal) | Close it. One process per port |
| USB port disappears after flashing your own firmware | The app reconfigured or crashed the USB-Serial/JTAG peripheral | Manual download mode: hold BOOT (GPIO35) while pressing RST, then flash a known-good image |
| `A fatal error occurred: The chip stopped responding` while flashing the C6 over UART | The running P4 resets the C6 over GPIO54 during its WiFi retry | Park the P4 in download mode first (`esptool --chip esp32p4 --port <P4> --before default_reset --after no_reset flash_id`), then flash the C6. [05 section 7](../devices/guition-jc4880p443c/docs/05-troubleshooting.md) |
| Board looks bricked after a factory restore attempt | Factory merged image (offset 0x0) mixed with the development layout (bootloader 0x2000, table 0x8000, app 0x20000) | Erase, then flash one layout completely. Keep a `read_flash` backup ([getting-started.md](getting-started.md) section 5) |

## 2. Boot and black screen

| Symptom | Cause | Fix |
|---|---|---|
| Serial log is healthy, screen stays black, no backlight | Hand-rolled display init without **LDO channel 3 at 2500 mV** before the DSI PHY; the vendored BSP does this for you | Use the vendored BSP, or acquire LDO ch3 before `esp_lcd_new_dsi_bus`. Also check the backlight is turned on after panel init (GPIO23) |
| `esp_ldo_acquire_channel ... already in use` | The SD driver was told to grab its own LDO channel and collided with the display's ch3 | SD power is LDO **ch4** at 3300 mV; never let the SD code take ch3 |
| `bedside_clock` or the sample flashed onto the 10.1" board: black screen, no touch | Different panel (JD9365, 800x1280) and touch (GSL3680) | Not supported yet; see [getting-started.md](getting-started.md) section 10 and the [board-profile plan](architecture/board-profiles-plan.md) |
| Boot aborts with `HS_MP: mempool create failed: no mem` or an `sdio_mempool_create` assert | esp_hosted tried to allocate its pool from internal RAM after LVGL took it | `CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y` and `CONFIG_ESP_HOSTED_DFLT_TASK_FROM_SPIRAM=y` ([reliability-checklist.md](reliability-checklist.md) section 4) |
| `Unrecoverable host sdio state` reboots under sustained WiFi traffic | PSRAM mempool buffers not aligned to the 128-byte L2 cache line | The local `hosted_malloc_align` patch in `managed_components/espressif__esp_hosted` (re-apply after a component refresh) |
| Status line `Recovered from crash (N)`; voice and camera off; API not running | Crash-loop breaker: N consecutive abnormal resets, safe mode at 3 | Read the event journal and the core dump ([howto/serial-capture-and-logs.md](howto/serial-capture-and-logs.md) section 4), fix the cause, re-enable the API in Settings |
| `Settings: N stored value(s) were out of range and repaired` | Settings blob from an older schema version was migrated and sanitised | Informational. Check the repaired values in Settings |
| Settings revert after a reboot | NVS not committed before `esp_restart()` | `nvs_commit()` before every intentional reboot ([reliability-checklist.md](reliability-checklist.md) section 1) |

## 3. Display: tearing, rotation, the PPA freeze

| Symptom | Cause | Fix |
|---|---|---|
| Screen frozen, touch dead, **music and the HTTP API keep working**; log shows `ui_watch` stall lines with `state=B`; after 30 s the device aborts and reboots with a core dump | PPA hardware rotation (`CONFIG_LVGL_PORT_ENABLE_PPA=y`) waited forever for the DMA-done interrupt on rev v1.3 silicon while PSRAM was under load (MP3 decode plus TLS plus song cache). Touch is polled by the same LVGL task, hence dead | The local `esp_lvgl_port` patch: bounded PPA wait and a permanent fall-back to CPU rotation, logged as `PPA rotation failed (...) - CPU rotation for the rest of this boot`. Or set `CONFIG_LVGL_PORT_ENABLE_PPA=n` and accept slower scrolling. [learnings.md](learnings.md) item 8 |
| Tearing during fast animation or camera preview | Software rotation path without tear avoidance | `CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR=y` + `CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE=y` (the sample). This **blocks** esp_lvgl_port software rotation, so the landscape clock cannot use it. Choose per app |
| Everything is sideways | Rotation applied twice, or the camera module's native orientation already matches the portrait panel | For the camera: rotation 0 is correct on the 4.3" module ([09-camera.md](../devices/guition-jc4880p443c/docs/09-camera.md)) |
| Camera frames dark or green | ISP pipeline controller not enabled | `CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=y` |
| No `CONFIG_CAMERA_OV02C10` symbol, or the sensor is not detected | Managed `esp_cam_sensor` 2.0.1 has no OV02C10 driver | Use the vendored 2.1.0 as a local component, delete `dependencies.lock` and the managed copy, delete `sdkconfig`, rebuild; the configure log must say `Using component placed at ...components\esp_cam_sensor` |
| Cyrillic or accented song titles render as boxes | LVGL's built-in Montserrat has ASCII only | `bedside_clock` chains `montserrat_ext_*` fallback fonts (`main/fonts/app_fonts.c`); labels that show server text must use `&app_font_NN`, and byte-bounded copies must be trimmed at a UTF-8 boundary (`utf8_trim_tail`) |

## 4. Touch

| Symptom | Cause | Fix |
|---|---|---|
| Touch dead but the screen still updates (clock ticks) | Rare; usually the LVGL task is alive and the touch controller is not. `ui_watch` traces press/release at INFO; no traces while LVGL beats means the controller | Check the shared I2C bus (GPIO7 SDA / GPIO8 SCL) for a second master, power-cycle |
| Touch dead **and** the screen is frozen | The LVGL task is blocked; touch is polled by that task (GT911 INT is not wired) | Section 3, first row |
| Intermittent NACKs, touch and codec both flaky | Two I2C master drivers on GPIO7/8. Touch (0x5D), ES8311 (0x18) and the camera's SCCB (0x36) share one bus | Exactly one driver; pass the BSP's handle to anything else (`bsp_i2c_get_handle()`) |
| `I GT911: I2C address initialization procedure skipped - using default GT9xx setup` | Normal: RST/INT are left unconnected and polling works with the default address | Nothing |
| 10.1": `gsl3680: gsl3680 startup_chip failed read 0xb0 = 5a,5a,5a,5a` in the factory log, then touch works | The GSL3680 needs a firmware blob uploaded over I2C at every boot; the factory driver retries | Port only with the licence question settled ([licensing.md](licensing.md)); notes in [touch-gsl3680.md](../devices/guition-jc8012p4a1c/docs/touch-gsl3680.md) |

## 5. WiFi and the C6 link

| Symptom | Cause | Fix |
|---|---|---|
| `WiFi init FAILED (...) - C6 co-processor not responding` (clock) or no `transport: Identified slave [esp32c6]` block in the boot log | The SDIO link never came up: C6 not reset (GPIO54), wrong slot or pins, or an unreliable 40 MHz clock on this unit | Check the esp_hosted block in `sdkconfig.defaults`; drop `CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ` to 20000 first (the clock already ships 20 MHz; the sample runs 40) |
| `W transport: Version mismatch: Host [2.11.0] < Co-proc [2.12.0] ==> Upgrade host` | Host component older than the slave | Pin `espressif/esp_hosted: "~2.12.0"` in `main/idf_component.yml`, refresh the component. WiFi works despite the warning; BLE does not |
| `W transport: Version mismatch: Host [2.12.0] > Co-proc [2.3.0] ==> Upgrade co-proc to avoid RPC timeouts`, scans work but association loops with `H_SDIO_DRV: Dropping packet(s) from stream`, `Failed to push data to rx queue`, `Reason: 8 - ASSOC_LEAVE` | Slave firmware too old (the 10.1" ships 2.3.0; some 4.3" batches too) | Update the C6 ([howto/update-c6-firmware.md](howto/update-c6-firmware.md)). It is not the AP, the signal or your code |
| Connects to some networks, never to the "5G" one | The C6 is a 2.4 GHz radio | Use a 2.4 GHz SSID |
| Connected, then drops right after the first heavy fetch | A burst of requests immediately after `GOT_IP` starves the handshake | Yield about 1 s after connect, stagger refreshes ([reliability-checklist.md](reliability-checklist.md) section 6) |
| `esp_wifi_connect` blocks the caller for up to about 20 s | Every `esp_wifi_*` call is a blocking RPC to the C6 | Expected. Call from a worker task, never from an LVGL callback |
| Scan returns nothing, `scan failed` | WiFi driver not initialised because the C6 handshake failed earlier | First row |

## 6. Bluetooth (BLE over the C6)

| Symptom | Cause | Fix |
|---|---|---|
| Endless `E NimBLE: HCI wait for ack returned 19` and `BLE_HS_ETIMEOUT_HCI (HCI request timed out; controller unresponsive)` | Three stacked causes: host/slave version mismatch; NimBLE started before the transport was up; **the C6's BT controller is disabled by default since esp_hosted 2.5.2** | Host `~2.12.0`; `esp_hosted_connect_to_slave()` before `nimble_port_init()`; `esp_hosted_bt_controller_init()` + `esp_hosted_bt_controller_enable()` + a short settle. [04-bluetooth.md](../devices/guition-jc4880p443c/docs/04-bluetooth.md) |
| Need Bluetooth Classic (A2DP, SPP) | The handshake says `BLE only`; it is a hard limit of the slave firmware | Not available. For audio, use a USB speaker or a USB BT dongle that presents as a USB sound card |
| No BLE at all in `bedside_clock` | Compiled out (`CONFIG_BT_ENABLED=n`) because the app never used it | Re-enable per the sample's `sdkconfig.defaults` block and follow the bring-up order |

## 7. Audio

| Symptom | Cause | Fix |
|---|---|---|
| No sound, no errors (4.3") | **The speaker is external.** Nothing is on the board | Plug a speaker into the MX1.25 2-pin connector |
| Codec writes succeed but output is silent or the first settings are ignored | Register writes right after codec init intermittently no-op | About 200 ms settle after `bsp_extra_codec_init()` before the first `set_fs`/volume |
| Reboot at song start: `E i2s_common: i2s_alloc_dma_desc(541): allocate DMA buffer failed`, `E I2S_IF: Failed to reconfig STD slot, ret: 257`, `Guru Meditation Error: Core 0 panic'ed (Load access fault)` in `i2s_rx_channel_start` | A sample-rate change re-allocates about six 1 KB DMA descriptors per direction from DMA-capable internal RAM; with about 40 KB free it failed and upstream `esp_codec_dev_open()` enabled the channel anyway | The local `esp_codec_dev` patch (open fails instead), `CONFIG_LWIP_L2_TO_L3_COPY=y` (freed about 35 KB during playback), watch `heap.dma_largest` in `GET /api/v1/status` (keep above about 12 KB). [05 section 10](../devices/guition-jc4880p443c/docs/05-troubleshooting.md), [11 section 8](../devices/guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md) |
| Wrong speed or garbled playback | Sample rate or slot mode mismatch between app and codec | One rate per app (the sample: 16 kHz mono); the clock reconfigures per song through `bsp_extra_codec_set_fs` |
| Alarm beeps instead of playing the song | The music server could not deliver the song: unreachable, wrong credentials, or a **stale playlist id** (Subsonic error 70) after the server re-created the playlist | The clock resolves the playlist by name once on error 70 and journals every fallback reason (`A1 dl fail ...` in `GET /api/v1/events`, details in `GET /api/v1/log`). Check Settings > Music API > Test |
| USB speaker not detected | Plugged into the socket that gives COM3 (that PHY belongs to USB-Serial/JTAG); or `peripheral_map` was set to `BIT0|BIT1`, which moves the host to the full-speed controller whose pins reach neither socket | Use the **high-speed socket** (the one that does not give COM3); keep the default `peripheral_map`; `CONFIG_USB_HOST_HUBS_SUPPORTED=y` if a hub is involved; read `GET /api/v1/usb` for what the device advertises |
| `W i2s_common: dma frame num is adjusted to 256 to align the dma buffer with 64, bufsize = 512` | Driver rounding | Benign |

## 8. microSD

| Symptom | Cause | Fix |
|---|---|---|
| `E sdmmc_common: sdmmc_init_ocr: send_op_cond (1) returned 0x107`, `vfs_fat_sdmmc: sdmmc_card_init failed (0x107)` | The card has no power: `TF_VCC` comes from P4 **LDO channel 4** through an always-on FET, there is no GPIO switch. The 10.1" factory log shows the same 0x107 right after `ldo: The voltage value 0 is out of the recommended range [500, 2700]` | Acquire LDO ch4 at 3300 mV before mounting (alongside the display's ch3, not instead of it) |
| Mount hangs or boot blocks with SPI-mode `SD.begin()` | The slot is **SDMMC 4-bit**, not SPI | SDMMC mode: CLK 43, CMD 44, D0-D3 39/40/41/42 (4.3"). [01-hardware.md](../devices/guition-jc4880p443c/docs/01-hardware.md) |

## 9. TLS and streaming

| Symptom | Cause | Fix |
|---|---|---|
| `E esp-tls-mbedtls: read error :-0x7180`, `E HTTP_CLIENT: transport_read: error - -1 | ESP_FAIL`, `tls_code=29056 (SSL - Verification of the message MAC failed) errno=0`, every 0.15 to 0.6 MB of a fast HTTPS download | The P4's **hardware AES-DMA** decrypting mbedTLS's PSRAM-resident record buffer (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`) returns wrong plaintext under load. TCP checksums pass, so nothing below TLS notices. IDF v5.5.5 still has it | `CONFIG_MBEDTLS_HARDWARE_AES=n` (software AES, about 0.6 MB/s, 40 times what 128 kbps playback needs). Re-test after every IDF upgrade with the procedure in [11 section 10](../devices/guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md). Not fixes: SDIO 20 MHz, `LWIP_L2_TO_L3_COPY`, PPA off |
| A bare `Navidrome: stream read error` and nothing else | Old client code that logged one fixed string | Current `navidrome_client.c` logs the classified cause, errno, TLS error, bytes, elapsed, attempt and RSSI once per failure; upgrade |
| Download stalls, `rc=-28679` | `-ESP_ERR_HTTP_EAGAIN`: the server went silent for 15 s with the connection intact | Read again (the client retries once); it is not a transport error |
| Crash on the first HTTPS request | mbedTLS blew the default task stack on the P4 | Calling task stack at least 8192, `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` |
| `failed (ESP_ERR_NO_MEM)`, `Could not load: ...` for one song | The song does not fit the PSRAM song cache even transcoded (multi-hour tracks) | Expected; the player moves on. Transcode fallback handles most oversize raw files |
| Resumed downloads duplicate audio or restart from zero | `Range` header persisting on a reused `esp_http_client` handle, response buffer not cleared | `esp_http_client_delete_header(c, "Range")` and `esp_http_client_clear_response_buffer()` before every open ([reliability-checklist.md](reliability-checklist.md) section 6) |

## 10. Watchdogs, panics, core dumps

| Symptom | Cause | Fix |
|---|---|---|
| Task watchdog fires during C6 bring-up, OTA erase, or the first WiFi connect on a slow panel | IDF default of 5 s | `CONFIG_ESP_TASK_WDT_TIMEOUT_S=60`, `CONFIG_ESP_TASK_WDT_PANIC=y` so a trip reboots and gets counted instead of only printing |
| `Abnormal reset (task watchdog), consecutive crash count N` at boot | The previous boot was ended by the watchdog | The core dump holds every task's backtrace: `idf.py -p COM3 coredump-info`. One stored field dump was a Task WDT on the alarm task during a long song download; the download now runs at a lower priority |
| `W app_main: core dump from an earlier crash is stored in flash` | A panic happened since the last time you read the dump | Pull it before the next panic overwrites it ([howto/serial-capture-and-logs.md](howto/serial-capture-and-logs.md) section 4) |
| `coredump-info` prints garbage or wrong symbols | ELF in `build/` does not match the flashed binary, or wrong `-B` | Compare `app_init: Compile time` in the boot log with the build; use `-B build_mww` for the variant |
| The UI froze with no crash and no dump | Not a crash; see section 3 for the PPA case. `ui_watch` names the LVGL task state and aborts after 30 s so that a dump exists | Read the `ui_watch` lines and the dump |
| The alarm did not ring and there is no serial log | Nobody was watching at 06:30 | The event journal survives reboots: `GET /api/v1/events` or Settings > Specifications. Alarm decisions, snoozes, downloads and failures are all there |

## 11. Build errors

| Symptom | Cause | Fix |
|---|---|---|
| `esp_lvgl_port_disp.c:160:16: error: 'esp_lcd_dpi_panel_event_callbacks_t' has no member named 'on_frame_buf_complete'` | `esp_lvgl_port` 2.8+ needs an IDF 5.5.5 DPI callback; you are on 5.5.4 or older | Install ESP-IDF v5.5.5 ([getting-started.md](getting-started.md) section 2). The manifests pin `idf: ">=5.5.5"` so the dependency step fails first on an old IDF |
| `error: implicit declaration of function 'esp_cache_get_alignment'` | The function lives in a private header | `#include "esp_private/esp_cache_private.h"` (component `esp_mm`) |
| `ninja: error: WriteFile(.ninja_lock): Unable to create file. Permission denied`, `ninja: error: failed recompaction: Permission denied`, `Remove-Item: ... .ninja_deps ... being used by another process` | Orphaned `cmake`/`ninja`/`python` processes from a killed build hold handles on `build/` | Stop them (`Get-Process | Where-Object { $_.Name -match 'ninja|cmake' }`), rebuild; delete `build/` fully if it was half removed |
| `CMake Error: The current CMakeCache.txt directory ... is different than the directory ... where CMakeCache.txt was created` and `The source "..." does not match the source "..." used to generate cache` | Build directory from before the 2026-10-02 folder renames | Delete `build/` and `build_mww/`, `idf.py set-target esp32p4`, build ([howto/build-and-flash.md](howto/build-and-flash.md)) |
| Object path too long errors deep in `managed_components` (Windows) | 260-character path limit | `set(CMAKE_OBJECT_PATH_MAX 512)` is in every top-level `CMakeLists.txt`; keep the checkout path short; `git config --global core.longpaths true` |
| Version-range warning for a local component override (`esp_cam_sensor` 2.1.0 vs the BSP's `2.0.*`) | Expected for a deliberate override | Harmless as long as the log says `Using component placed at ...components\esp_cam_sensor` |
| Registry resolves a newer component and the build or the device breaks | `dependencies.lock` was deleted or ignored | Restore it from git; refresh components one at a time and re-apply the local patches ([howto/build-and-flash.md](howto/build-and-flash.md)) |
| Voice does not start after `app-flash` onto an erased flash; log shows the model handle is NULL | The esp-sr models live in the `model` partition that only a full `flash` writes | `idf.py -p COM3 flash` once |
| `sdkconfig` keeps an old value after editing `sdkconfig.defaults` | Defaults are applied only when `sdkconfig` is generated | Delete `sdkconfig` (or `sdkconfig.mww`) and rebuild |

## Benign log lines (do not chase these)

| Log line | Verdict |
|---|---|
| `W ledc: GPIO 23 is not usable, maybe conflict with others` | Benign. The backlight on GPIO23 works right after it (`Setting LCD backlight: 100%`) |
| `E lcd_panel: esp_lcd_panel_swap_xy(50): swap_xy is not supported by this panel` | Benign. esp_lvgl_port probes hardware rotation, the DSI panel refuses, software rotation is used. Present in every working build on both boards |
| `I ESP32_P4_EV: Install EK79007 LCD control panel` (4.3") / `Install ILI9881C LCD control panel` (10.1" factory) | Misleading BSP text; the code path installs the ST7701 or JD9365. Cosmetic |
| `W spi_flash: Detected boya flash chip but using generic driver` | Benign. Optionally `CONFIG_SPI_FLASH_SUPPORT_BOYA_CHIP=y` |
| `I GT911: I2C address initialization procedure skipped - using default GT9xx setup` | Benign. Touch works in polling mode with the default address |
| `W i2c.master: Please check pull-up resistances whether be connected properly` | Benign on these boards; the bus has hardware pull-ups |
| `W i2s_common: dma frame num is adjusted to 256 to align the dma buffer with 64` | Benign driver rounding |
| `W transport: Version mismatch: Host [x] < Co-proc [y]` | **Not benign for Bluetooth.** WiFi tolerates it; BLE times out on every HCI command. Match the host component to the slave |
| `Recovered from crash (N)` once, after you deliberately reset or flashed mid-operation | Expected; the counter clears after 30 s of stable uptime. Repeated without a reason: section 10 |

## Deeper reading

- Every bring-up problem on the 4.3" with the full narrative: [05-troubleshooting.md](../devices/guition-jc4880p443c/docs/05-troubleshooting.md)
- BLE: [04-bluetooth.md](../devices/guition-jc4880p443c/docs/04-bluetooth.md)
- Camera: [09-camera.md](../devices/guition-jc4880p443c/docs/09-camera.md)
- TLS corruption case study: [11-tls-stream-corruption-investigation.md](../devices/guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md)
- The rules that prevent most of this: [reliability-checklist.md](reliability-checklist.md)
- The 10.1" board: [devices/guition-jc8012p4a1c/](../devices/guition-jc8012p4a1c/README.md)
