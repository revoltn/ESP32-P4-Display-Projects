# Device Specifications Log

Output of the **Device Specs** query from the `hello_world` app (now `src/sample_hello_world`), captured from the serial log (COM3) on 2026-08-30. Static datasheet facts are mixed with values queried live on the device (chip revision, flash/PSRAM size, free heap, versions, MAC, uptime).

## Overview

| | |
|---|---|
| **Board** | Guition JC4880P443C 4.3" smart display |
| **Main SoC** | ESP32-P4 rev v1.3 (dual-core RISC-V, no radio) |
| **Co-processor** | ESP32-C6 (WiFi 6 + BLE) over SDIO |
| **Memory** | 16 MB flash, 32 MB PSRAM |
| **Features** | 480x800 MIPI-DSI touch display, audio in/out, WiFi 6 / BLE 5, USB OTG HS, H.264 encoder, JPEG codec, PPA 2D graphics accelerator, camera ISP |

## Processor

- 2 x RISC-V HP cores @ 360 MHz (RV32IMAFC)
- 1 x RISC-V LP core @ 40 MHz (ultra-low-power domain)
- Single-precision FPU + AI/DSP instruction extensions
- Not available: Xtensa cores, full MMU (no Linux-class virtual memory), out-of-order execution

## Memory & Storage

- 768 KB on-chip HP SRAM + 32 KB LP SRAM + 8 KB TCM
- 32 MB PSRAM @ 200 MHz (framebuffers live here)
- 16 MB QIO flash @ 80 MHz
- Free heap at capture: 293 KB internal (largest block 120 KB), 29702 KB PSRAM free
- Not available: SD card slot, eMMC (SDMMC host exists in silicon but no socket on this board)

## Display & Graphics

- 4.3" 480x800 portrait IPS, ST7701 controller
- MIPI-DSI, 2 data lanes @ 1 Gbps, RGB565 16-bit color
- GT911 capacitive touch, 5-point multitouch
- PPA hardware accelerator: blend, scale, rotate, mirror
- 2D-DMA engine for memory-to-memory pixel moves
- UI stack: LVGL 9.5.0 (software rendered)
- Not available: GPU, OpenGL / OpenGL ES / Vulkan / EGL, hardware 3D of any kind, HDMI or DisplayPort output

## Video, Imaging & Transcoding

- H.264 hardware ENCODER, up to 1080p (encode only)
- JPEG hardware codec: both encode and decode
- MIPI-CSI 2-lane camera input with on-chip ISP (auto-exposure, auto-white-balance, demosaic, CCM)
- Not available: H.264 hardware DECODE — so no hardware transcode pipeline (decode falls back to software), no H.265/HEVC, no VP9/AV1
- Camera module: an **OV02C10 IS fitted** and verified working 2026-08-30 (live preview; earlier "no camera module fitted" claim here was wrong — see [09-camera.md](09-camera.md))

## Audio

- ES8311 mono codec on I2S (up to 96 kHz capable; this app runs 16000 Hz / 16-bit mono)
- Onboard analog microphone
- NS4150 3 W class-D amplifier (PA enable GPIO11)
- Speaker is EXTERNAL via MX1.25 2P connector
- Not available: headphone jack, S/PDIF, built-in speaker

## Wireless (via ESP32-C6 co-processor)

- WiFi 6 (802.11ax, 2.4 GHz, 20 MHz); b/g/n compatible
- Bluetooth 5 LE (NimBLE host on P4, HCI over SDIO)
- C6 slave firmware: esp_hosted 2.12.12, BLE-only HCI (live version is logged when WiFi/BLE first runs)
- Not available: 5 GHz WiFi, Bluetooth Classic (A2DP/SPP), Zigbee/Thread (802.15.4 exists in C6 silicon but is not exposed by the hosted firmware)

## Connectivity & I/O

- USB 2.0 OTG High-Speed (480 Mbps) + USB-Serial/JTAG (the USB-C port this board flashes over)
- Shared I2C bus on GPIO7/8 (touch + audio codec)
- RS-485 header (TX on GPIO26)
- Ethernet RMII MAC in silicon (GPIO28-31/49-52)
- Not available: Ethernet PHY / RJ45 jack, CAN transceiver (TWAI controller present but unwired)

## Security (silicon capabilities)

- Secure Boot v2, flash encryption (XTS-AES)
- AES / SHA / RSA / ECC hardware accelerators
- Digital signature peripheral, true RNG
- Note: none of these are enabled in this dev build

## Software / Runtime

- App: hello_world 1 (built Aug 30 2026 11:45:08)
- ESP-IDF: v5.5.4 (at capture)
- Base MAC (eFuse): xx:xx:xx:xx:xx:xx (redacted)
- Last reset: USB reset
- Uptime at capture: 7 s

### Toolchain/library update, 2026-09-02 (not yet reflected in the capture above)

Upstream review of every firmware/library version against the Espressif registry and
ESP-IDF releases. Applied: ESP-IDF v5.5.4 → **v5.5.5**, esp_lvgl_port 2.7.2 → **2.9.0**;
both apps rebuilt clean. Everything else confirmed current (lvgl 9.5.0, esp-sr 2.5.2,
esp_hosted 2.12.12 = last 2.x, matched to the C6 slave) or deliberately deferred
(esp_hosted 3.0.x needs a C6 slave reflash; esp_video/esp_cam_sensor 2.4.x would drop the
vendor-custom OV02C10 driver; esp_lcd_st7701 2.x would drop the vendor init patch).
Details: [03-working-components.md](03-working-components.md).
