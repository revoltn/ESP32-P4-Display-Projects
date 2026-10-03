/* Touch menu UI: title, feature buttons, status line, scrolling log. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"

#include "app_common.h"
#include "ui_menu.h"

static const char *TAG = "ui";

#define LOG_BUF_CAP 16384

static lv_obj_t *s_status_label;
static lv_obj_t *s_log_cont;
static lv_obj_t *s_log_label;

static char s_log_buf[LOG_BUF_CAP];

static const struct {
    const char *label;
    feature_id_t id;
} MENU_ITEMS[] = {
    { "Connect WiFi",          FEAT_WIFI       },
    { "BLE Scan",              FEAT_BT         },
    { "Navidrome",             FEAT_NAVIDROME  },
    { "Play Toccata",          FEAT_MUSIC      },
    { "Record & Playback",     FEAT_ECHO       },
    { "Device Specifications", FEAT_SPECS      },
    { "USB Audio",             FEAT_USB_AUDIO  },
    { "Camera",                FEAT_CAMERA     },
};

#define MENU_COUNT ((int)(sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0])))

static lv_obj_t *s_buttons[sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0])];

static void btn_event_cb(lv_event_t *e)
{
    feature_id_t id = (feature_id_t)(uintptr_t)lv_event_get_user_data(e);
    app_request_feature(id);
}

void ui_menu_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(scr, 12, 0);
    lv_obj_set_style_pad_row(scr, 10, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "JC4880P443C Demo");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8EDF2), 0);

    /* Button grid: two per row (wrap flex), 1.5x the old 64px height. */
    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 10, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < MENU_COUNT; i++) {
        lv_obj_t *btn = lv_button_create(grid);
        lv_obj_set_size(btn, lv_pct(48), 96);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2C5F8A), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A3A55), LV_STATE_DISABLED);
        lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)MENU_ITEMS[i].id);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, MENU_ITEMS[i].label);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_center(lbl);
        s_buttons[i] = btn;
    }

    s_status_label = lv_label_create(scr);
    lv_label_set_text(s_status_label, "Starting...");
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_status_label, lv_pct(100));
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x7FD08A), 0);

    /* Scrollable log: a label inside a plain scroll container (a textarea's
     * cursor semantics fight manual scrolling). Drag to scroll; ui_log()
     * auto-follows only while the view is already at the bottom. */
    s_log_cont = lv_obj_create(scr);
    lv_obj_set_width(s_log_cont, lv_pct(100));
    lv_obj_set_flex_grow(s_log_cont, 1);
    lv_obj_set_style_bg_color(s_log_cont, lv_color_hex(0x181C22), 0);
    lv_obj_set_style_border_width(s_log_cont, 0, 0);
    lv_obj_set_style_radius(s_log_cont, 6, 0);
    lv_obj_set_style_pad_all(s_log_cont, 8, 0);
    lv_obj_set_scroll_dir(s_log_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_log_cont, LV_SCROLLBAR_MODE_AUTO);

    s_log_label = lv_label_create(s_log_cont);
    lv_obj_set_width(s_log_label, lv_pct(100));
    lv_label_set_long_mode(s_log_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_log_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_log_label, lv_color_hex(0xB8C2CC), 0);
    lv_label_set_text(s_log_label, "");
}

void ui_menu_set_busy(bool busy)
{
    if (!bsp_display_lock(1000)) {
        return;
    }
    for (int i = 0; i < MENU_COUNT; i++) {
        if (!s_buttons[i]) {
            continue;
        }
        if (busy) {
            lv_obj_add_state(s_buttons[i], LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(s_buttons[i], LV_STATE_DISABLED);
        }
    }
    bsp_display_unlock();
}

void ui_log(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line) - 2, fmt, ap);
    va_end(ap);

    ESP_LOGI(TAG, "%s", line);
    strcat(line, "\n");

    size_t add = strlen(line);
    size_t cur = strlen(s_log_buf);
    if (cur + add >= LOG_BUF_CAP) {
        /* drop the oldest half, keeping whole lines */
        const char *keep = s_log_buf + (cur / 2);
        const char *nl = strchr(keep, '\n');
        keep = nl ? nl + 1 : keep;
        memmove(s_log_buf, keep, strlen(keep) + 1);
        cur = strlen(s_log_buf);
    }
    memcpy(s_log_buf + cur, line, add + 1);

    if (s_log_label && bsp_display_lock(1000)) {
        /* follow new lines only if the user hasn't scrolled up */
        bool follow = lv_obj_get_scroll_bottom(s_log_cont) <= 40;
        lv_label_set_text(s_log_label, s_log_buf);
        if (follow) {
            lv_obj_update_layout(s_log_cont);
            lv_obj_scroll_to_y(s_log_cont,
                               lv_obj_get_scroll_y(s_log_cont) +
                                   lv_obj_get_scroll_bottom(s_log_cont),
                               LV_ANIM_OFF);
        }
        bsp_display_unlock();
    }
}

void ui_set_status(const char *fmt, ...)
{
    char line[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    if (s_status_label && bsp_display_lock(1000)) {
        lv_label_set_text(s_status_label, line);
        bsp_display_unlock();
    }
}
