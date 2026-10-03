/* Device Specifications: categorized spec sheet printed to the scrolling log.
 * High-level overview first, then per-category capabilities INCLUDING what
 * this hardware does not have. Static datasheet facts are mixed with values
 * queried live (chip rev, flash/PSRAM size, heap, versions, MAC, uptime). */
#include <stdarg.h>
#include <stdio.h>
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
#include "evlog.h"

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

/* The whole sheet is built here first and pushed to the UI in ONE
 * ui_log_bulk() call - ~75 individual ui_log()s each re-wrap the growing
 * 16 KB label under the LVGL lock and freeze the UI for seconds. */
#define SPEC_BUF_CAP 12288 /* spec sheet ~4 KB + up to 40 journal lines */
static char *s_spec_buf;
static size_t s_spec_len;

static void speclog(const char *fmt, ...)
{
    if (s_spec_buf == NULL || s_spec_len + 2 >= SPEC_BUF_CAP) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(s_spec_buf + s_spec_len,
                      SPEC_BUF_CAP - s_spec_len - 2, fmt, ap);
    va_end(ap);
    if (w > 0) {
        size_t room = SPEC_BUF_CAP - s_spec_len - 2;
        s_spec_len += ((size_t)w < room) ? (size_t)w : room;
    }
    s_spec_buf[s_spec_len++] = '\n';
    s_spec_buf[s_spec_len] = '\0';
}

static void section(const char *name)
{
    speclog(" ");
    speclog("--- %s ---", name);
}

void feature_specs_run(void)
{
    ui_set_status("Printing device specifications...");

    s_spec_buf = heap_caps_malloc(SPEC_BUF_CAP, MALLOC_CAP_SPIRAM);
    if (s_spec_buf == NULL) {
        ui_log("specs: out of memory");
        return;
    }
    s_spec_len = 0;
    s_spec_buf[0] = '\0';

    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    size_t psram_size = esp_psram_get_size();

    speclog(" ");
    speclog("=== DEVICE SPECIFICATIONS ===");
#if CONFIG_BSP_LCD_TYPE_1280_800
    speclog("Board:    Guition JC8012P4A1C_I_W_Y 10.1\" smart display");
#else
    speclog("Board:    Guition JC4880P443C 4.3\" smart display");
#endif
    speclog("Main SoC: ESP32-P4 rev v%d.%d (dual-core RISC-V, no radio)",
           chip.revision / 100, chip.revision % 100);
    speclog("Co-proc:  ESP32-C6 (WiFi 6 + BLE) over SDIO");
    speclog("Memory:   %lu MB flash, %u MB PSRAM",
           (unsigned long)(flash_size / (1024 * 1024)),
           (unsigned)(psram_size / (1024 * 1024)));
    speclog("Features: %dx%d MIPI-DSI touch display, audio in/out,", BSP_LCD_H_RES, BSP_LCD_V_RES);
    speclog("  WiFi 6 / BLE 5, USB OTG HS, H.264 encoder, JPEG codec,");
    speclog("  PPA 2D graphics accelerator, camera ISP");

    section("Processor");
    speclog("%d x RISC-V HP cores @ %d MHz (RV32IMAFC)",
           chip.cores, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    speclog("1 x RISC-V LP core @ 40 MHz (ultra-low-power domain)");
    speclog("Single-precision FPU + AI/DSP instruction extensions");
    speclog("Not available: Xtensa cores, full MMU (no Linux-class");
    speclog("  virtual memory), out-of-order execution");

    section("Memory & Storage");
    speclog("768 KB on-chip HP SRAM + 32 KB LP SRAM + 8 KB TCM");
    speclog("%u MB PSRAM @ 200 MHz (framebuffers live here)",
           (unsigned)(psram_size / (1024 * 1024)));
    speclog("%lu MB QIO flash @ 80 MHz",
           (unsigned long)(flash_size / (1024 * 1024)));
    speclog("Free heap now: %lu KB internal (largest block %u KB),",
           (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
    speclog("  %lu KB PSRAM free",
           (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    speclog("TF card slot present (unused by this app; SDIO shared with the C6 link)");

    section("Display & Graphics");
#if CONFIG_BSP_LCD_TYPE_1280_800
    speclog("10.1\" %dx%d portrait IPS, JD9365 controller (panel %s)", BSP_LCD_H_RES, BSP_LCD_V_RES,
#if CONFIG_BSP_JD9365_PANEL_V2
            "V2");
#else
            "V1");
#endif
    speclog("MIPI-DSI, 2 data lanes @ %d Mbps, RGB565 16-bit color", BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS);
    speclog("GSL3680 capacitive touch (INT GPIO21), firmware uploaded at boot");
#else
    speclog("4.3\" %dx%d portrait IPS, ST7701 controller", BSP_LCD_H_RES, BSP_LCD_V_RES);
    speclog("MIPI-DSI, 2 data lanes @ %d Mbps, RGB565 16-bit color", BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS);
    speclog("GT911 capacitive touch, 5-point multitouch (polled, INT not wired)");
#endif
    speclog("PPA hardware accelerator: blend, scale, rotate, mirror");
    speclog("2D-DMA engine for memory-to-memory pixel moves");
    speclog("UI stack: LVGL %d.%d.%d (software rendered)",
           LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    speclog("Not available: GPU, OpenGL / OpenGL ES / Vulkan / EGL,");
    speclog("  hardware 3D of any kind, HDMI or DisplayPort output");

    section("Video, Imaging & Transcoding");
    speclog("H.264 hardware ENCODER, up to 1080p (encode only)");
    speclog("JPEG hardware codec: both encode and decode");
    speclog("MIPI-CSI 2-lane camera input with on-chip ISP");
    speclog("  (auto-exposure, auto-white-balance, demosaic, CCM)");
    speclog("Not available: H.264 hardware DECODE - so no hardware");
    speclog("  transcode pipeline (decode falls back to software),");
    speclog("  no H.265/HEVC, no VP9/AV1");
    speclog("OV02C10 camera module on MIPI-CSI (ambient light sensing)");

    section("Audio");
    speclog("ES8311 mono codec on I2S (up to 96 kHz capable;");
    speclog("  this app runs %d Hz / %d-bit mono)", AUDIO_SAMPLE_RATE, AUDIO_BITS);
    speclog("Onboard analog microphone");
#if CONFIG_BSP_LCD_TYPE_1280_800
    speclog("Class-D amplifier (PA enable GPIO20), BUILT-IN speaker");
    speclog("Not available: headphone jack, S/PDIF");
#else
    speclog("NS4150 3 W class-D amplifier (PA enable GPIO11)");
    speclog("Speaker is EXTERNAL via MX1.25 2P connector");
    speclog("Not available: headphone jack, S/PDIF, built-in speaker");
#endif

    section("Wireless (via ESP32-C6 co-processor)");
    speclog("WiFi 6 (802.11ax, 2.4 GHz, 20 MHz); b/g/n compatible");
    speclog("Bluetooth 5 LE (NimBLE host on P4, HCI over SDIO)");
    speclog("C6 slave firmware: esp_hosted 2.12.12, BLE-only HCI");
    speclog("  (live version is logged when WiFi/BLE first runs)");
    speclog("Not available: 5 GHz WiFi, Bluetooth Classic (A2DP/SPP),");
    speclog("  Zigbee/Thread (802.15.4 exists in C6 silicon but is");
    speclog("  not exposed by the hosted firmware)");

    section("Connectivity & I/O");
    speclog("USB 2.0 OTG High-Speed (480 Mbps) + USB-Serial/JTAG");
    speclog("  (the USB-C port this board flashes over)");
    speclog("Shared I2C bus on GPIO7/8 (touch + audio codec)");
    speclog("RS-485 header (TX on GPIO26)");
    speclog("Ethernet RMII MAC in silicon (GPIO28-31/49-52)");
    speclog("Not available: Ethernet PHY / RJ45 jack, CAN");
    speclog("  transceiver (TWAI controller present but unwired)");

    section("Security (silicon capabilities)");
    speclog("Secure Boot v2, flash encryption (XTS-AES)");
    speclog("AES / SHA / RSA / ECC hardware accelerators");
    speclog("Digital signature peripheral, true RNG");
    speclog("Note: none of these are enabled in this dev build");

    section("Software / Runtime");
    const esp_app_desc_t *app = esp_app_get_description();
    speclog("App: %s %s (built %s %s)",
           app->project_name, app->version, app->date, app->time);
    speclog("ESP-IDF: %s", esp_get_idf_version());
    uint8_t mac[6] = { 0 };
    if (esp_read_mac(mac, ESP_MAC_BASE) == ESP_OK) {
        speclog("Base MAC (eFuse): %02X:%02X:%02X:%02X:%02X:%02X",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    speclog("Last reset: %s", reset_reason_str(esp_reset_reason()));
    speclog("Uptime: %llu s", (unsigned long long)(esp_timer_get_time() / 1000000ULL));

    /* Persistent journal: survives reboots, so this is where "did the alarm
     * fire last night and what happened around it" gets answered. */
    section("Event journal (oldest first, survives reboots)");
    int n_ev = evlog_count();
    if (n_ev == 0) {
        speclog("(empty)");
    }
    for (int i = 0; i < n_ev; i++) {
        char line[96];
        if (evlog_format(i, line, sizeof(line))) {
            speclog("%s", line);
        }
    }
    speclog(" ");
    speclog("=== END OF SPECIFICATIONS ===");

    ui_log_bulk(s_spec_buf); /* one locked pass, one label re-layout */
    heap_caps_free(s_spec_buf);
    s_spec_buf = NULL;
    s_spec_len = 0;

    ui_set_status("Specs printed - scroll up to read");
}
