/* Device Specifications: categorized spec sheet printed to the scrolling log.
 * High-level overview first, then per-category capabilities INCLUDING what
 * this hardware does not have. Static datasheet facts are mixed with values
 * queried live (chip rev, flash/PSRAM size, heap, versions, MAC, uptime). */
#include <string.h>
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_idf_version.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "lvgl.h"
#include "bsp/display.h"

#include "app_common.h"

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_SW:        return "software reset";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_USB:       return "USB reset";
    case ESP_RST_JTAG:      return "JTAG reset";
    default:                return "other";
    }
}

static void section(const char *name)
{
    ui_log(" ");
    ui_log("--- %s ---", name);
}

void feature_specs_run(void)
{
    ui_set_status("Printing device specifications...");

    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    size_t psram_size = esp_psram_get_size();

    ui_log(" ");
    ui_log("=== DEVICE SPECIFICATIONS ===");
    ui_log("Board:    Guition JC4880P443C 4.3\" smart display");
    ui_log("Main SoC: ESP32-P4 rev v%d.%d (dual-core RISC-V, no radio)",
           chip.revision / 100, chip.revision % 100);
    ui_log("Co-proc:  ESP32-C6 (WiFi 6 + BLE) over SDIO");
    ui_log("Memory:   %lu MB flash, %u MB PSRAM",
           (unsigned long)(flash_size / (1024 * 1024)),
           (unsigned)(psram_size / (1024 * 1024)));
    ui_log("Features: 480x800 MIPI-DSI touch display, audio in/out,");
    ui_log("  WiFi 6 / BLE 5, USB OTG HS, H.264 encoder, JPEG codec,");
    ui_log("  PPA 2D graphics accelerator, camera ISP");

    section("Processor");
    ui_log("%d x RISC-V HP cores @ %d MHz (RV32IMAFC)",
           chip.cores, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    ui_log("1 x RISC-V LP core @ 40 MHz (ultra-low-power domain)");
    ui_log("Single-precision FPU + AI/DSP instruction extensions");
    ui_log("Not available: Xtensa cores, full MMU (no Linux-class");
    ui_log("  virtual memory), out-of-order execution");

    section("Memory & Storage");
    ui_log("768 KB on-chip HP SRAM + 32 KB LP SRAM + 8 KB TCM");
    ui_log("%u MB PSRAM @ 200 MHz (framebuffers live here)",
           (unsigned)(psram_size / (1024 * 1024)));
    ui_log("%lu MB QIO flash @ 80 MHz",
           (unsigned long)(flash_size / (1024 * 1024)));
    ui_log("Free heap now: %lu KB internal (largest block %u KB),",
           (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
    ui_log("  %lu KB PSRAM free",
           (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    ui_log("Not available: SD card slot, eMMC (SDMMC host exists");
    ui_log("  in silicon but no socket on this board)");

    section("Display & Graphics");
    ui_log("4.3\" %dx%d portrait IPS, ST7701 controller", BSP_LCD_H_RES, BSP_LCD_V_RES);
    ui_log("MIPI-DSI, 2 data lanes @ 1 Gbps, RGB565 16-bit color");
    ui_log("GT911 capacitive touch, 5-point multitouch");
    ui_log("PPA hardware accelerator: blend, scale, rotate, mirror");
    ui_log("2D-DMA engine for memory-to-memory pixel moves");
    ui_log("UI stack: LVGL %d.%d.%d (software rendered)",
           LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    ui_log("Not available: GPU, OpenGL / OpenGL ES / Vulkan / EGL,");
    ui_log("  hardware 3D of any kind, HDMI or DisplayPort output");

    section("Video, Imaging & Transcoding");
    ui_log("H.264 hardware ENCODER, up to 1080p (encode only)");
    ui_log("JPEG hardware codec: both encode and decode");
    ui_log("MIPI-CSI 2-lane camera input with on-chip ISP");
    ui_log("  (auto-exposure, auto-white-balance, demosaic, CCM)");
    ui_log("Not available: H.264 hardware DECODE - so no hardware");
    ui_log("  transcode pipeline (decode falls back to software),");
    ui_log("  no H.265/HEVC, no VP9/AV1; no camera module fitted");
    ui_log("  on this board");

    section("Audio");
    ui_log("ES8311 mono codec on I2S (up to 96 kHz capable;");
    ui_log("  this app runs %d Hz / %d-bit mono)", AUDIO_SAMPLE_RATE, AUDIO_BITS);
    ui_log("Onboard analog microphone");
    ui_log("NS4150 3 W class-D amplifier (PA enable GPIO11)");
    ui_log("Speaker is EXTERNAL via MX1.25 2P connector");
    ui_log("Not available: headphone jack, S/PDIF, built-in speaker");

    section("Wireless (via ESP32-C6 co-processor)");
    ui_log("WiFi 6 (802.11ax, 2.4 GHz, 20 MHz); b/g/n compatible");
    ui_log("Bluetooth 5 LE (NimBLE host on P4, HCI over SDIO)");
    ui_log("C6 slave firmware: esp_hosted 2.12.12, BLE-only HCI");
    ui_log("  (live version is logged when WiFi/BLE first runs)");
    ui_log("Not available: 5 GHz WiFi, Bluetooth Classic (A2DP/SPP),");
    ui_log("  Zigbee/Thread (802.15.4 exists in C6 silicon but is");
    ui_log("  not exposed by the hosted firmware)");

    section("Connectivity & I/O");
    ui_log("USB 2.0 OTG High-Speed (480 Mbps) + USB-Serial/JTAG");
    ui_log("  (the USB-C port this board flashes over)");
    ui_log("Shared I2C bus on GPIO7/8 (touch + audio codec)");
    ui_log("RS-485 header (TX on GPIO26)");
    ui_log("Ethernet RMII MAC in silicon (GPIO28-31/49-52)");
    ui_log("Not available: Ethernet PHY / RJ45 jack, CAN");
    ui_log("  transceiver (TWAI controller present but unwired)");

    section("Security (silicon capabilities)");
    ui_log("Secure Boot v2, flash encryption (XTS-AES)");
    ui_log("AES / SHA / RSA / ECC hardware accelerators");
    ui_log("Digital signature peripheral, true RNG");
    ui_log("Note: none of these are enabled in this dev build");

    section("Software / Runtime");
    const esp_app_desc_t *app = esp_app_get_description();
    ui_log("App: %s %s (built %s %s)",
           app->project_name, app->version, app->date, app->time);
    ui_log("ESP-IDF: %s", esp_get_idf_version());
    uint8_t mac[6] = { 0 };
    if (esp_read_mac(mac, ESP_MAC_BASE) == ESP_OK) {
        ui_log("Base MAC (eFuse): %02X:%02X:%02X:%02X:%02X:%02X",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    ui_log("Last reset: %s", reset_reason_str(esp_reset_reason()));
    ui_log("Uptime: %llu s", (unsigned long long)(esp_timer_get_time() / 1000000ULL));
    ui_log(" ");
    ui_log("=== END OF SPECIFICATIONS ===");

    ui_set_status("Specs printed - scroll up to read");
}
