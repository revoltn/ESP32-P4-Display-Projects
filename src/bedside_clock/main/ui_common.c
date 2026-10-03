/* Shared tile-grid menu widgets - one look for every button menu. */
#include <string.h>
#include "ui_common.h"

#define GRID_COL_BTN  0x2C5F8A
#define GRID_COL_TEXT 0xE8EDF2

void ui_grid_config(lv_obj_t *cont)
{
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(cont, 12, 0);
}

lv_obj_t *ui_grid_tile(lv_obj_t *parent, const char *symbol,
                       const char *text, lv_event_cb_t cb, void *user_data)
{
    return ui_grid_tile_font(parent, symbol, &lv_font_montserrat_28, text, cb,
                             user_data);
}

lv_obj_t *ui_grid_tile_font(lv_obj_t *parent, const char *symbol,
                            const lv_font_t *icon_font, const char *text,
                            lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, lv_pct(31), 116);
    lv_obj_set_style_bg_color(btn, lv_color_hex(GRID_COL_BTN), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, icon_font, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -18);

    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(GRID_COL_TEXT), 0);
    lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -10);
    return btn;
}

lv_obj_t *ui_sidebar_btn(lv_obj_t *side, const char *text, int height,
                         uint32_t bg_color, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(side);
    lv_obj_set_size(btn, lv_pct(100), height);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_color), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_center(l);
    return btn;
}

void ui_sidebar_btn_enable(lv_obj_t *btn, bool en, uint32_t active_color)
{
    if (btn == NULL) {
        return;
    }
    if (en) {
        lv_obj_remove_state(btn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(btn, lv_color_hex(active_color), 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(UI_COL_TEXT), 0);
    } else {
        lv_obj_add_state(btn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_DIS_BG), 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(UI_COL_DIS_TXT), 0);
    }
}

void ui_sidebar_btn_set_text(lv_obj_t *btn, const char *text)
{
    if (btn == NULL) {
        return;
    }
    lv_obj_t *l = lv_obj_get_child(btn, 0);
    if (l != NULL && strcmp(lv_label_get_text(l), text) != 0) {
        lv_label_set_text(l, text);
    }
}
