#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Build the menu screen. Caller must hold the LVGL lock. */
void ui_menu_create(void);

/* Enable/disable the menu buttons (takes the LVGL lock itself). */
void ui_menu_set_busy(bool busy);

#ifdef __cplusplus
}
#endif
