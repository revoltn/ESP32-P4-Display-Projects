#pragma once

#include "lvgl.h"

/* Service-indicator icons, generated from Font Awesome 6 Free Solid by
 * tools/fonts/gen_fonts.sh (CC BY 4.0; see tools/fonts/LICENSE-FontAwesome.txt).
 * 28 px, matching the montserrat_28 symbols the clock's top row and the
 * settings tiles already use.
 *
 * These are NOT in montserrat, so any label showing one MUST have its font
 * set to &app_icons_28 - otherwise LVGL draws a missing-glyph box. On the
 * settings tiles use ui_grid_tile_font() rather than ui_grid_tile().
 *
 * Add a glyph: put its codepoint in gen_fonts.sh's -r list, re-run the
 * script, and add the matching UTF-8 macro here. */
LV_FONT_DECLARE(app_icons_28);

#define ICON_MIC   "\xEF\x84\xB0"   /* U+F130 microphone - voice / speech */
#define ICON_CHIP  "\xEF\x8B\x9B"   /* U+F2DB microchip  - AI server */
