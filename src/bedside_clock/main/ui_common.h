#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared menu-grid building blocks (ui_common.c), used by the settings
 * menu and the Play screen so every tile menu looks the same. Callers hold
 * the LVGL lock (widget-creation context). */

/* Turn a flex container into a wrapping 3-wide tile grid. */
void ui_grid_config(lv_obj_t *cont);

/* One 31% x 116 tile: icon (montserrat_28 symbol) over a name label. */
lv_obj_t *ui_grid_tile(lv_obj_t *parent, const char *symbol,
                       const char *text, lv_event_cb_t cb, void *user_data);

/* Same tile, but with the icon drawn in `icon_font`. Needed for glyphs that
 * montserrat does not carry - see main/fonts/app_icons.h. */
lv_obj_t *ui_grid_tile_font(lv_obj_t *parent, const char *symbol,
                            const lv_font_t *icon_font, const char *text,
                            lv_event_cb_t cb, void *user_data);

/* Shared disabled-look palette for sidebar buttons. */
#define UI_COL_TEXT    0xE8EDF2
#define UI_COL_DIS_BG  0x1A1E24
#define UI_COL_DIS_TXT 0x4A4F55

/* Full-width sidebar button: centered montserrat_20 label ("SYMBOL\nText"),
 * the recipe the settings and Play overlays share. */
lv_obj_t *ui_sidebar_btn(lv_obj_t *side, const char *text, int height,
                         uint32_t bg_color, lv_event_cb_t cb);

/* Grey a sidebar button out (or restore it to active_color). */
void ui_sidebar_btn_enable(lv_obj_t *btn, bool en, uint32_t active_color);

/* Swap a sidebar button's label (e.g. Pause <-> Resume). */
void ui_sidebar_btn_set_text(lv_obj_t *btn, const char *text);

#ifdef __cplusplus
}
#endif
