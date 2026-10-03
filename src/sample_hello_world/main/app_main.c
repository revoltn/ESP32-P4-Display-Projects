/* JC4880P443C sample_hello_world demo — touch menu that exercises each board
 * feature in turn (WiFi, BT, audio, camera, USB audio, Navidrome). */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"

#include "app_common.h"
#include "ui_menu.h"

static const char *TAG = "app_main";

static QueueHandle_t s_feature_queue;
static bool s_audio_init_done;

void app_request_feature(feature_id_t id)
{
    if (s_feature_queue) {
        xQueueSend(s_feature_queue, &id, 0);
    }
}

bool app_audio_ensure_init(void)
{
    if (s_audio_init_done) {
        return true;
    }
    ui_log("Initializing ES8311 audio codec...");
    ui_log("Note: speaker is EXTERNAL (MX1.25 2P connector) - make sure one is plugged in");
    esp_err_t err = bsp_extra_codec_init();
    if (err != ESP_OK) {
        ui_log("Audio codec init FAILED: %s", esp_err_to_name(err));
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(200)); /* settle time, per vendor demo */
    s_audio_init_done = true;
    return true;
}

/* MUST-IMPLEMENT §1: crash-loop breaker. Detect an abnormal reset, keep a
 * consecutive-crash counter in NVS (committed immediately — flush discipline),
 * and surface it instead of silently rebooting into the same state.
 * Returns the consecutive crash count (0 = clean boot). */
#define CRASH_NVS_NS  "hello_world"
#define CRASH_NVS_KEY "crash_count"

static uint32_t crash_guard_check(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    bool abnormal = (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
                     reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT ||
                     reason == ESP_RST_BROWNOUT);

    nvs_handle_t nvs;
    if (nvs_open(CRASH_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) {
        return 0;
    }
    uint32_t count = 0;
    nvs_get_u32(nvs, CRASH_NVS_KEY, &count);
    count = abnormal ? count + 1 : 0;
    nvs_set_u32(nvs, CRASH_NVS_KEY, count);
    nvs_commit(nvs);
    nvs_close(nvs);

    if (abnormal) {
        ESP_LOGW(TAG, "Abnormal reset (reason %d), consecutive crash count %lu",
                 reason, (unsigned long)count);
    }
    return count;
}

/* Zero the crash counter once the system has stayed up long enough to be
 * considered stable (mirrors safe-mode "mark successful" semantics). */
static void crash_guard_mark_stable(void *arg)
{
    nvs_handle_t nvs;
    if (nvs_open(CRASH_NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u32(nvs, CRASH_NVS_KEY, 0);
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI(TAG, "30 s uptime reached - crash counter cleared");
    }
}

static void worker_task(void *arg)
{
    feature_id_t id;
    for (;;) {
        if (xQueueReceive(s_feature_queue, &id, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        ui_menu_set_busy(true);
        switch (id) {
        case FEAT_WIFI:  feature_wifi_run();  break;
        case FEAT_BT:    feature_bt_run();    break;
        case FEAT_MUSIC: feature_music_run(); break;
        case FEAT_ECHO:  feature_echo_run();  break;
        case FEAT_SPECS: feature_specs_run(); break;
        case FEAT_USB_AUDIO: feature_usb_audio_run(); break;
        case FEAT_CAMERA: feature_camera_run(); break;
        case FEAT_NAVIDROME: feature_navidrome_run(); break;
        default: break;
        }
        ui_menu_set_busy(false);
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    uint32_t crash_count = crash_guard_check();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * 80,
        .double_buffer = false,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = false,
        }
    };
    bsp_display_start_with_config(&cfg);
    /* 80% cap + on only after panel init (MUST-IMPLEMENT §3; field-proven value) */
    bsp_display_brightness_set(80);

    bsp_display_lock(0);
    ui_menu_create();
    bsp_display_unlock();

    s_feature_queue = xQueueCreate(4, sizeof(feature_id_t));
    assert(s_feature_queue);
    /* 16 KB: the Navidrome feature runs mbedTLS handshakes + cJSON on this task. */
    xTaskCreate(worker_task, "feature_worker", 16384, NULL, 5, NULL);

    const esp_timer_create_args_t stable_args = {
        .callback = crash_guard_mark_stable,
        .name = "crash_stable",
    };
    esp_timer_handle_t stable_timer;
    if (esp_timer_create(&stable_args, &stable_timer) == ESP_OK) {
        esp_timer_start_once(stable_timer, 30 * 1000000ULL);
    }

    ESP_LOGI(TAG, "setup done");
    ui_log("JC4880P443C sample_hello_world ready");
    if (crash_count > 0) {
        ui_log("WARNING: previous boot ended in a crash (%lu in a row)%s",
               (unsigned long)crash_count,
               crash_count >= 3 ? " - CRASH LOOP, check serial log" : "");
        ui_set_status("Recovered from crash (%lu)", (unsigned long)crash_count);
    } else {
        ui_set_status("Ready");
    }
}
