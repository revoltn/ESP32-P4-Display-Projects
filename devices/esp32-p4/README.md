# ESP32-P4: chip-level knowledge for the Guition P4 + C6 boards

Scope: facts about the ESP32-P4 that hold for both Guition boards in this repo, the 4.3"
[JC4880P443C](../guition-jc4880p443c/README.md) and the 10.1"
[JC8012P4A1C](../guition-jc8012p4a1c/README.md). Both carry an ESP32-P4 **silicon rev v1.3**
with an ESP32-C6 radio co-processor on SDIO (C6 side: [../esp32-c6/README.md](../esp32-c6/README.md)).
Board pins, connectors and panel init tables live in the board directories, not here. Every
claim cites a repo file or an external URL; anything not confirmed on hardware is marked
*unverified*. Paths like `src/bedside_clock/...` are repo-relative; IDF paths refer to the
ESP-IDF v5.5.5 tree.

## 1. What the P4 is

| Block | Fact | Source |
|---|---|---|
| CPU | 2 x RISC-V HP cores (RV32IMAFC + FPU, hardware loops, PIE SIMD) plus 1 LP RISC-V core | IDF `components/soc/esp32p4/include/soc/soc_caps.h` (`SOC_CPU_CORES_NUM 2`, `SOC_CPU_HAS_FPU`, `SOC_CPU_HAS_HWLOOP`, `SOC_CPU_HAS_PIE`, `SOC_LP_CORE_SUPPORTED`); [device-specifications-log.md](../guition-jc4880p443c/docs/device-specifications-log.md) |
| Clock | IDF defaults the HP cores to **360 MHz when `ESP32P4_SELECTS_REV_LESS_V3` is set, 400 MHz otherwise** | IDF `components/esp_system/port/soc/esp32p4/Kconfig.cpu` lines 12-16; both boards boot at 360 MHz ([10.1" factory log](../guition-jc8012p4a1c/logs/factory-boot-2026-10-02.txt) `cpu freq: 360000000 Hz`; `src/bedside_clock/sdkconfig` `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=360`). 400 MHz on rev v1.x is *unverified* here (ESPHome gates it behind `enable_idf_experimental_features`, [08-esphome-findings.md](../guition-jc4880p443c/docs/08-esphome-findings.md)) |
| Radio | **None.** Wi-Fi/BLE come from the C6 over esp_hosted | [01-hardware.md](../guition-jc4880p443c/docs/01-hardware.md); soc_caps has no `SOC_WIFI_SUPPORTED`/`SOC_BT_SUPPORTED` |
| Internal RAM | 768 KB HP SRAM + 32 KB LP SRAM + 8 KB TCM; the heap pools a real app sees are far smaller (section 3) | specs log; 10.1" log `heap_init` lines |
| External RAM | 16-line ("hex") PSRAM, 32 MB on both boards, 200 MHz, XIP of `.text`/`.rodata` supported | IDF `components/esp_psram/esp32p4/Kconfig.spiram` offers only `SPIRAM_MODE_HEX`; 10.1" log `hex_psram: ... BitMode 0x01 (X16 Mode)`, `Found 32MB PSRAM device`, `Speed: 200MHz`, `.text xip on psram`. Note: [01-hardware.md](../guition-jc4880p443c/docs/01-hardware.md) says "octal" for the 4.3"; the generated `sdkconfig` says `CONFIG_SPIRAM_MODE_HEX=y`, so "octal" there is loose wording |
| Display out | MIPI-DSI host (2 data lanes used on both boards) with a DPI bridge; no parallel RGB on these boards | `SOC_MIPI_DSI_SUPPORTED`; section 5 |
| Camera in | MIPI-CSI + on-chip ISP (AE/AWB/demosaic) | `SOC_MIPI_CSI_SUPPORTED`, `SOC_ISP_SUPPORTED`; [09-camera.md](../guition-jc4880p443c/docs/09-camera.md) |
| 2D graphics | PPA (blend/scale/rotate/mirror) + 2D-DMA | `SOC_PPA_SUPPORTED`, `SOC_DMA2D_SUPPORTED`; section 6 |
| Video codecs | H.264 **encoder only** in hardware (>30 fps 1080p), JPEG encode **and** decode | `src/bedside_clock/managed_components/espressif__esp_h264/README.md` ("hardware encoder ... designed specifically for the esp32p4"); `SOC_JPEG_ENCODE_SUPPORTED`, `SOC_JPEG_DECODE_SUPPORTED`; no hardware H.264 decode per the specs log |
| USB | Two USB-OTG (DWC) controllers, one high-speed and one full-speed, plus a separate USB-Serial/JTAG peripheral | `SOC_USB_OTG_PERIPH_NUM (2U)`, `SOC_USB_SERIAL_JTAG_SUPPORTED`; IDF `components/hal/esp32p4/include/hal/usb_dwc_ll.h:24` (`USB_DWC_LL_GET_HW(1)` = FS, else HS); section 8 |
| Storage buses | SDMMC host with 2 slots (one goes to the C6 as SDIO, the other to a TF card) | `SOC_SDMMC_HOST_SUPPORTED`, `SOC_SDMMC_NUM_SLOTS 2`; [01-hardware.md](../guition-jc4880p443c/docs/01-hardware.md) |
| Other | 3 x TWAI, RMII Ethernet MAC (no PHY on either board), UHCI, 4 programmable LDOs | `SOC_TWAI_CONTROLLER_NUM 3`, `SOC_EMAC_SUPPORTED`; LDOs: section 4 |
| Crypto | AES with DMA, SHA, RSA, ECC, Secure Boot v2, flash encryption; the AES-DMA path has a live bug (section 7) | `SOC_AES_SUPPORT_DMA`; specs log |

## 2. Silicon revisions

IDF states the split bluntly: "Revisions higher than 3.0 (included) and revisions less than
3.0 have huge hardware difference. Revisions higher than 3.0 (included) is not compatible
with 0.x and 1.x" and marks the two families as mutually exclusive build targets
(IDF `components/esp_hw_support/port/esp32p4/Kconfig.hw_support` lines 1-12).

| Revision | Status | Notes |
|---|---|---|
| v0.0, v0.1 | early engineering samples | IDF choices `ESP32P4_REV_MIN_0` / `_1` (under the <3.0 gate) |
| **v1.x** | "engineering sample" era, still shipping in products | **Both Guition boards are v1.3.** Also the early ESP32-P4-Function-EV-Board and M5Stack Tab5 per ESPHome `boards.py` ("ES pre rev.300", `reference/esphome/upstream/esphome/components/esp32/boards.py` lines 1795-1806, 2236) and the EspControl note that all six of its P4 devices need the flag ([07-espcontrol-findings.md](../guition-jc4880p443c/docs/07-espcontrol-findings.md)) |
| v3.0 | production | `ESP32P4_REV_MIN_300` |
| v3.1 | production, **IDF 5.5 default minimum** | `ESP32P4_REV_MIN_301` is the Kconfig default; max supported is v3.99 (or v1.99 under the gate) |

What changed between v1.x and v3.x, as far as this repo can source it:

- 2D-DMA: the 4th TX channel and 3rd RX channel exist only on P4 >= v3.0 (soc_caps.h lines 234-235).
- The ESPHome default board for `variant: esp32p4` is the rev-3 board; omitting the flag "produces a non-booting image on rev 1.x silicon" ([08-esphome-findings.md](../guition-jc4880p443c/docs/08-esphome-findings.md)).
- [01-hardware.md](../guition-jc4880p443c/docs/01-hardware.md) summarises v3.0 as "a new power rail on pin 54, fixed a USB PHY power issue, and changed 50+ registers/memory architecture". That text has no further citation in the repo: treat as *unverified*; the authoritative list is the Espressif errata (section 11).
- esp-sr ships distinct binary libraries for the two families: `lib/esp32p4`, `lib/esp32p4_less_v3`, `lib/esp32p4_less_v3_idf6` (`src/bedside_clock/managed_components/espressif__esp-sr/CMakeLists.txt` lines 8-15), selected automatically from `ESP32P4_SELECTS_REV_LESS_V3`.

How to read the revision:

- esptool during flash: `Chip is ESP32-P4 (revision v1.3)` ([01-hardware.md](../guition-jc4880p443c/docs/01-hardware.md)).
- Boot log: `efuse_init: Min chip rev: v1.0`, `Max chip rev: v1.99`, `Chip rev: v1.3` ([10.1" factory log](../guition-jc8012p4a1c/logs/factory-boot-2026-10-02.txt)). Min/Max are what the image was built for; Chip rev is the eFuse.
- ESPHome `debug:` component prints `Revision: 100` for v1.0, `300` for v3.0 (`reference/esphome/upstream/esphome/components/esp32/__init__.py` lines 1145-1162).

The build settings, per toolchain:

```
# ESP-IDF sdkconfig.defaults (both apps in this repo)
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y   # the gate; without it REV_MIN_100 is invisible and ignored
CONFIG_ESP32P4_REV_MIN_100=y           # -> CONFIG_ESP32P4_REV_MIN_FULL=100, REV_MAX_FULL=199
```

- Symptom of forgetting it: `A fatal error occurred: bootloader/bootloader.bin requires chip revision in range [v3.1 - v3.99] (this chip is revision v1.3)` ([05-troubleshooting.md section 2](../guition-jc4880p443c/docs/05-troubleshooting.md)). Delete `sdkconfig` after adding the lines so it regenerates.
- ESPHome: `esp32: engineering_sample: true` (sets the same Kconfig; ESPHome warns "Defaulting to ESP32-P4 production silicon (rev3)" when unset, `__init__.py` lines 1145-1162).
- Arduino / pioarduino >= 55.03.36-1: `chip_variant: esp32p4_es` in the board definition; the symptom there is "Illegal instruction" in the 2nd-stage bootloader rather than a refused flash ([05 section 2](../guition-jc4880p443c/docs/05-troubleshooting.md)).
- ESP-IDF 6.1 moves the default to v3.0; the gate still builds for v1.x ([02-toolchain-and-build.md](../guition-jc4880p443c/docs/02-toolchain-and-build.md)). *Unverified* in this repo: no 6.x build has been attempted.
- Image cross-compatibility is refused by the esptool/bootloader checks; this repo has no v3 chip, so "a v1.x image on v3 silicon" has not been tried (*unverified*).

## 3. Memory map facts that drive design

- **Internal RAM is the scarce resource.** The vendor 10.1" demo boots with these malloc pools: 79 KiB RETENT_RAM, 18 KiB + 256 KiB RAM, 31 KiB RTCRAM, 7 KiB TCM (factory log `heap_init`). The 4.3" alarm-clock app idles at 80-115 KB internal free and dropped to ~40 KB while streaming until `CONFIG_LWIP_L2_TO_L3_COPY=y` raised it to ~76 KB ([11-tls-stream-corruption-investigation.md](../guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md) sections 7-8).
- **Some DMA descriptors must be internal.** The I2S driver re-allocates ~6 x 1 KB descriptors per direction from `MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA` on every sample-rate change; when that fails the stock `esp_codec_dev_open()` still enables the channel and the core faults in `i2s_rx_channel_start` ([05 section 10](../guition-jc4880p443c/docs/05-troubleshooting.md), [11 section 8](../guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md)). Keep >= ~12 KB of DMA-capable internal RAM free and watch the largest block, not the total ([reliability-checklist.md section 10](../../docs/reliability-checklist.md)).
- **PSRAM is DMA-capable but cache-line bound.** `SOC_PSRAM_DMA_CAPABLE 1`; the L2 cache is 256 KB with a **128-byte line** (`CONFIG_CACHE_L2_CACHE_256KB=y`, `CONFIG_CACHE_L2_CACHE_LINE_128B=y`; generated `CONFIG_CACHE_L2_CACHE_LINE_SIZE=128`, `CONFIG_CACHE_L1_CACHE_LINE_SIZE=64`, `src/bedside_clock/sdkconfig`). Any DMA buffer in PSRAM needs address **and** length aligned to 128 B: PPA output buffers ([09-camera.md](../guition-jc4880p443c/docs/09-camera.md)), the esp_hosted SDIO mempool (a 64 B-aligned block "sporadically fails sdmmc_host_check_buffer_alignment ... and an 'Unrecoverable host sdio state' reboot", `src/bedside_clock/components/espressif__esp_hosted/host/port/esp/freertos/src/port_esp_hosted_host_os.c` lines 128-150, local patch). `esp_cache_get_alignment()` lives in `esp_private/esp_cache_private.h`, not `esp_cache.h` ([05 section 5](../guition-jc4880p443c/docs/05-troubleshooting.md)).
- **XIP from PSRAM** (`CONFIG_SPIRAM_XIP_FROM_PSRAM=y`) is the proven vendor setting; it frees internal RAM at the price of PSRAM bandwidth that the display and camera also compete for.
- **Move statics to PSRAM** with `EXT_RAM_BSS_ATTR` + `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`; LVGL on the system allocator (`CONFIG_LV_USE_CLIB_MALLOC=y`) so large glyph buffers land in PSRAM (`src/bedside_clock/sdkconfig.defaults`).
- **Protect internal RAM from PSRAM spill-over**: `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`, `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536`, `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` ([reliability-checklist.md section 10](../../docs/reliability-checklist.md)). `CONFIG_SPIRAM_MEMTEST=n` for boot speed; the vendor image runs the test (`SPI SRAM memory test OK` in the 10.1" log).
- **Flash**: 16 MB Boya chip on both boards, QIO 80 MHz in our builds, DIO in the factory images; the boot warning `Detected boya flash chip but using generic driver` is benign (`CONFIG_SPI_FLASH_SUPPORT_BOYA_CHIP` optional) ([05 benign table](../guition-jc4880p443c/docs/05-troubleshooting.md); 10.1" log; `devices/guition-jc4880p443c/community/seobaeksol-JC4880P443C_I_W/07_flash_and_factory_images.md`).
- **PSRAM die seen on the 10.1"**: AP (vendor id 0x0d), 256 Mbit, generation 4, 14-cycle read latency at 200 MHz (factory log `hex_psram` lines). The 4.3" prints only `Found 32MB PSRAM device`; same die *unverified*.

## 4. On-chip LDO channels

The P4 has 4 programmable LDO outputs (`LDO_VO1..VO4`), acquired with `esp_ldo_acquire_channel()`
(IDF LDO docs, section 11). On both Guition designs two are load-bearing:

| Channel | Powers | Setting | Symptom when missing | Source |
|---|---|---|---|---|
| **3** (`LDO_VO3`) | `VDD_MIPI_DPHY`, the DSI PHY | 2500 mV before any DSI/panel init | permanent black screen, the #1 "bricked board" cause | BSP `include/bsp/display.h:64-65` (`BSP_MIPI_DSI_PHY_PWR_LDO_CHAN 3`, `2500`) under `src/bedside_clock/components/espressif__esp32_p4_function_ev_board/`; [reliability-checklist.md section 3](../../docs/reliability-checklist.md); 10.1": EspControl `device.yaml` lines 267-272 and the p1ngb4ck ESPHome example (`esp_ldo channel 3, 2.5V`) |
| **4** (`LDO_VO4`) | `TF_VCC`, the microSD slot | 3300 mV via `sd_pwr_ctrl_new_on_chip_ldo` (`.ldo_chan_id = 4`) | `sdmmc_init_ocr: send_op_cond returned 0x107` | BSP `esp32_p4_function_ev_board.c:140-144`; [05 section 8](../guition-jc4880p443c/docs/05-troubleshooting.md) |

- Channels are exclusive once acquired as adjustable; letting the SD driver grab ch3 collides with the display (`esp_ldo_acquire_channel ... already in use`, [05 section 8](../guition-jc4880p443c/docs/05-troubleshooting.md)).
- The 10.1" factory image logs `ldo: The voltage value 0 is out of the recommended range [500, 2700]` right before its SD mount fails with 0x107. Cause *unverified* (an unset LDO config, or simply no card inserted).

## 5. Display pipeline (DSI -> DPI -> panel)

| | 4.3" JC4880P443C | 10.1" JC8012P4A1C |
|---|---|---|
| Panel controller | ST7701(S) | JD9365 (a "V2" panel variant exists, same JD9365) |
| DSI | 2 lanes @ 500 Mbps, DPI 34 MHz (ESPHome model); the vendored BSP in this repo hard-codes 750 Mbps with DPI 28 MHz | 2 lanes @ 1 Gbps, DPI 60 MHz (ESPHome); vendor BSP 4.1.1 hard-codes 1.5 Gbps, DPI 60 MHz (V2 panel: 70 MHz) |
| Source | ESPHome `mipi_dsi/models/guition.py` lines 38-62 (`reference/esphome/upstream/esphome/components/`); [01-hardware.md](../guition-jc4880p443c/docs/01-hardware.md) | same file lines 102-125 and 322-345; 10.1" log `Display resolution 800x1280`, `jd9365: LCD ID: 93 65 04` |

- **Frame buffers live in PSRAM**, RGB565, two DPI buffers (`CONFIG_BSP_LCD_DPI_BUFFER_NUMS=2`, `buff_spiram`, no DMA flag) so internal RAM stays for Wi-Fi/BT/audio ([reliability-checklist.md section 3](../../docs/reliability-checklist.md)).
- **Underrun symptom**: the DPI ISR logs `can't fetch data from external memory fast enough, underrun happens`; the IDF comment says the panel "may already becomes blue" and the only fix is more memory bandwidth (AXI-ICM priority) (IDF `components/esp_lcd/dsi/esp_lcd_panel_dpi.c` lines 113-116). Every PSRAM consumer (XIP, camera buffers, PPA, SDIO DMA, software AES) competes. Not recorded as hit in this repo (*unverified* on these boards).
- **No hardware rotation on the DSI path.** `E lcd_panel: esp_lcd_panel_swap_xy(50): swap_xy is not supported by this panel` appears in every working boot on both boards (4.3": [05 benign table](../guition-jc4880p443c/docs/05-troubleshooting.md); 10.1": factory log). Rotate in LVGL: esp_lvgl_port software rotation, optionally offloaded to the PPA (`CONFIG_LVGL_PORT_ENABLE_PPA=y`, "big scroll-smoothness win", `src/bedside_clock/sdkconfig.defaults`).
- **Tearing vs rotation is a trade.** `CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR` + `DIRECT_MODE` (hello_world, needed for the camera canvas) blocks esp_lvgl_port rotation; the clock app turns tear-avoidance off to rotate to landscape (`src/bedside_clock/sdkconfig.defaults` comment; [09-camera.md](../guition-jc4880p443c/docs/09-camera.md)).
- esp_lvgl_port >= 2.8 needs the `on_frame_buf_complete` DPI callback, present only from **IDF 5.5.5** ([05 section 1](../guition-jc4880p443c/docs/05-troubleshooting.md)).
- Backlight is LEDC PWM; `W ledc: GPIO 23 is not usable, maybe conflict with others` is benign on both boards (4.3" docs; 10.1" log). Init the backlight at 0 % and raise it after the first frame; field-proven dimming is 100 Hz / 80 % cap on the 4.3" ([reliability-checklist.md section 3](../../docs/reliability-checklist.md)).

## 6. PPA (2D accelerator) and the rev v1.3 hang

Driver facts learned with the camera and rotation paths ([09-camera.md](../guition-jc4880p443c/docs/09-camera.md)):

- Scale factors are quantised to 1/16 steps (`ppa_srm.c`, FRAG_MAX=16); 0.5 and 0.75 are exact, 800/1080 is not.
- For 90/270 degree rotation the output block is `w = scale_y*block_h, h = scale_x*block_w` and must fit `out.pic_w/h`.
- Output buffer address and `buffer_size` must both be 128 B aligned; allocate with `heap_caps_aligned_calloc(128, ..., MALLOC_CAP_SPIRAM)`.

**The hang.** With the clock app rotating every frame through the PPA, `taskLVGL` was found by a
JTAG backtrace parked in `ppa_do_operation` for 7+ minutes while a song streamed: the
transaction-done ISR never arrived. IDF's blocking mode is `xSemaphoreTake(trans_elm->sem,
portMAX_DELAY)` (IDF `components/esp_driver_ppa/src/ppa_core.c:467-468`), so the LVGL task,
which also polls touch on these boards, froze for good
(`src/bedside_clock/managed_components/espressif__esp_lvgl_port/src/common/ppa/lcd_ppa.c`
lines 18-28; `src/bedside_clock/main/ui_watch.c` header). Mitigation in this repo, kept as local
patches: issue the rotate `PPA_TRANS_MODE_NON_BLOCKING`, wait at most 500 ms, treat
`ESP_ERR_TIMEOUT` as "PPA dead" and fall back to CPU rotation for the rest of the boot
(`lcd_ppa.c:221-233`; `esp_lvgl_port_disp.c:79, 475-478, 720-729`), plus a 30 s LVGL
heartbeat watchdog that aborts into a flash core dump (`ui_watch.c`). Whether the hang is
rev-v1.3-specific or load-dependent is *unverified*; it has not been reproduced on demand and
there is no upstream issue reference in the repo. It was ruled out as the TLS corruption source
([11 section 5](../guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md)).

## 7. Silicon / driver bugs hit on rev v1.3

| Bug | Symptom | Root cause | Workaround | Source |
|---|---|---|---|---|
| Hardware AES-DMA corrupts TLS records in PSRAM | `SSL - Verification of the message MAC failed` (-0x7180) every 0.15-0.6 MB of HTTPS at 2-3 MB/s, TCP checksums fine | AES-DMA decrypting the PSRAM-resident mbedTLS record buffer (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`) returns wrong plaintext under load; IDF v5.5.5 already contains the Jan-2026 cache-coherency fixes (525ef3a, 0742f3f) and still fails | `CONFIG_MBEDTLS_HARDWARE_AES=n` (software AES ~0.6 MB/s, zero errors in 26 MB); re-test after every IDF upgrade with >= 25 MB sustained download | [11-tls-stream-corruption-investigation.md](../guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md); `src/bedside_clock/sdkconfig.defaults` |
| PPA SRM rotate never completes | frozen UI + touch, task blocked in `ppa_do_operation` | ISR never fires (why: unknown) | bounded wait + CPU fallback, section 6 | `lcd_ppa.c` patch header |
| esp_hosted PSRAM mempool 64 B aligned | sporadic `ESP_ERR_INVALID_ARG` in the SDIO write path, `Unrecoverable host sdio state` reboot | L2 line is 128 B, hosted asked for 64 | `hosted_malloc_align` raised to `CONFIG_CACHE_L2_CACHE_LINE_SIZE` (local patch, re-apply on component refresh) | `port_esp_hosted_host_os.c:128-150`; `sdkconfig.defaults` comment |
| esp_codec_dev ignores failed `set_fmt` | `i2s_alloc_dma_desc(541): allocate DMA buffer failed` then Load access fault in `i2s_rx_channel_start` | driver bug exposed by low internal RAM | local patch: `esp_codec_dev_open` fails instead of enabling a NULL descriptor list | [05 section 10](../guition-jc4880p443c/docs/05-troubleshooting.md) |
| SDIO clock changes the AES failure rate, not the outcome | 20 MHz cut TLS errors ~10x | bus contention with the AES-DMA | kept at 20 MHz on the 4.3" (see the C6 README) | [11 section 5](../guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md) |

Rule from the checklist: bisect one Kconfig per build with a scripted load; theories that
change the rate but not the outcome are not the cause ([05 diagnostic techniques](../guition-jc4880p443c/docs/05-troubleshooting.md)).

## 8. USB facts

- **Two sockets, two different peripherals.** On the 4.3" the socket that enumerates as a COM port is the P4's **USB-Serial/JTAG** on the `USB1P1` (full-speed) pins; the other socket is the native **high-speed OTG** PHY (`ESP_USB`) ([01-hardware.md](../guition-jc4880p443c/docs/01-hardware.md); [interface-types.md](../guition-jc4880p443c/docs/interface-types.md)). The 10.1" has the same two-socket layout in its schematic (`vendor/package/5-Schematic/6_USB.png`), wiring *unverified* by us.
- **USB host works on the high-speed socket only**, i.e. the one that does NOT give the COM port. Installing the host stack leaves the serial console intact, proof that the OTG controller and USB-Serial/JTAG are separate blocks ([10-bedside-v01-review.md section 1e](../guition-jc4880p443c/docs/10-bedside-v01-review.md)). Verified by ear with a USB speaker on 2026-09-27 (section 1f).
- **Do not set `peripheral_map = BIT0|BIT1`.** IDF 5.5 supports one root port ("Right now we support only one peripheral", IDF `components/usb/usb_host.c:479-483`); when bit 1 is set it wins and `USB_DWC_LL_GET_HW(1)` is the **full-speed** controller, whose pins reach neither socket. Serial keeps working, which is the tell. Default `peripheral_map` + port powered at install is the working config; `root_port_unpowered` + manual power cycling is unnecessary and `hub_root_stop()` asserts (`src/bedside_clock/main/usb_audio.c`, "WHICH SOCKET" comment).
- Hub support is a Kconfig (`CONFIG_USB_HOST_HUBS_SUPPORTED=y`) and the host stack costs ~6.4 KB internal RAM on install ([10 section 1e](../guition-jc4880p443c/docs/10-bedside-v01-review.md)).
- VBUS: on the 4.3" both sockets' VBUS pins sit on the shared `USB5V_IN` net, so a board powered through one socket presents 5 V on the other ([01-hardware.md](../guition-jc4880p443c/docs/01-hardware.md)). 10.1": *unverified*.
- USB-Serial/JTAG gives `idf.py flash` auto-reset with no boot button, OpenOCD/GDB over the same cable (how the PPA backtrace was taken), and ROM download mode; the vendor flash tool uses "LoadMode USB" on this port ([02-toolchain-and-build.md](../guition-jc4880p443c/docs/02-toolchain-and-build.md); seobaeksol `07_flash_and_factory_images.md`).

## 9. Camera / ISP (chip-generic parts)

- Both boards fit an OmniVision OV02C10 on MIPI-CSI (4.3": [09-camera.md](../guition-jc4880p443c/docs/09-camera.md); 10.1": factory log `ov02c10: Detected Camera sensor PID=0x5602`).
- `CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=y` is functionally required: without the `isp_task` (AE/AWB) RAW10 -> RGB565 frames come out dark/green.
- The proven mode is 1-lane RAW10 1920x1080 @ 30 fps even though 2 lanes are wired; a 1288x728 mode cuts the two frame buffers from ~8.3 MB to ~3.6 MB of PSRAM (`src/bedside_clock/sdkconfig.defaults`).
- The OV02C10 driver is vendor-custom (esp_cam_sensor 2.1.0 vendored); it is absent from Espressif's esp-video-components, so upgrading the camera stack drops it ([03-working-components.md](../guition-jc4880p443c/docs/03-working-components.md)).
- Reuse the one I2C master for SCCB (`init_sccb = false`); a second driver on the shared bus is a known failure mode.

## 10. Toolchain

- **ESP-IDF v5.5.5** on both apps; 5.5.x is what the vendor demos target, and 5.5.5 adds `on_frame_buf_complete` ([02-toolchain-and-build.md](../guition-jc4880p443c/docs/02-toolchain-and-build.md)). Manifests pin `idf: ">=5.5.5"`.
- **Why not 6.x yet**: major migration, default revision moves to v3.0, esp_hosted 3.x needs a C6 reflash, the camera stack upgrade drops the OV02C10 driver, and the AES workaround must be re-validated ([03-working-components.md](../guition-jc4880p443c/docs/03-working-components.md) upstream review).
- Known-good component set: vendor-patched BSP 5.2.3, esp_lvgl_port ~2.9.0, lvgl 9.5.0, esp_hosted 2.12.12 (last 2.x), esp_wifi_remote 1.6.4, esp-sr 2.5.2, esp_codec_dev 1.5.11; commit `dependencies.lock`.
- **esp-sr and esp-nn run on rev v1.3**: WakeNet9 + MultiNet7 via the `esp32p4_less_v3` libs, and tflite-micro with esp-nn PIE kernels measured at 0.55 ms per 30 ms invoke with no illegal-instruction faults over 60 s soaks (`src/sample_wakeword_bench/README.md`).
- Post-mortem stack: ELF core dump to a `coredump` partition, task WDT 60 s with panic, run-time stats for a UI stall dump (`src/bedside_clock/sdkconfig.defaults`). The 5 s default WDT can fire during C6 bring-up ([reliability-checklist.md section 2](../../docs/reliability-checklist.md)).
- Windows: `set(CMAKE_OBJECT_PATH_MAX 512)` in the top CMakeLists, and kill orphaned `ninja`/`cmake` processes after an interrupted build ([05 section 4](../guition-jc4880p443c/docs/05-troubleshooting.md)).

## 11. Links (URLs only)

- ESP32-P4 datasheet: https://www.espressif.com/sites/default/files/documentation/esp32-p4_datasheet_en.pdf
- ESP32-P4 technical reference manual: https://www.espressif.com/sites/default/files/documentation/esp32-p4_technical_reference_manual_en.pdf
- ESP32-P4 errata (revision differences): https://docs.espressif.com/projects/esp-chip-errata/en/latest/esp32p4/index.html
- ESP-IDF v5.5.5 for ESP32-P4: https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/index.html (versions and chip-revision compatibility: https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/versions.html and https://github.com/espressif/esp-idf/blob/master/COMPATIBILITY.md)
- IDF peripheral docs used above: LDO https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/peripherals/ldo_regulator.html, PPA https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/peripherals/ppa.html, USB host https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/peripherals/usb_host.html
- esp-bsp (the `esp32_p4_function_ev_board` BSP both vendor trees patch): https://github.com/espressif/esp-bsp
- esp-iot-solution (JD9365 driver, USB extend-screen example): https://github.com/espressif/esp-iot-solution
- esp_h264 component: https://components.espressif.com/components/espressif/esp_h264
- esp-sr and esp-nn: https://github.com/espressif/esp-sr and https://github.com/espressif/esp-nn
- ESPHome P4 platform (`engineering_sample`): https://esphome.io/components/esp32.html

## 12. Verified on

- Guition JC4880P443C, P4 rev v1.3, IDF v5.5.5, 2026-08-29 through 2026-09-27: everything in sections 2-10 with a 4.3" source was observed on that unit.
- Guition JC8012P4A1C, P4 rev v1.3: only the vendor factory firmware boot log of 2026-10-02 so far (`esp_brookesia_demo` 1.0.1, IDF v5.5.4-dirty). Nothing in this repo has been built for or flashed to the 10.1" yet; statements about it come from that log, the vendor package and the ESPHome/EspControl configs.
- Not verified anywhere in this repo: any v3.x silicon, IDF 6.x, 400 MHz on rev v1.x, a DPI underrun, hardware AES on a later IDF.
