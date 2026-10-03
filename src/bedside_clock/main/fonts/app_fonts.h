#pragma once

#include "lvgl.h"

/* Text fonts for server-supplied strings (Navidrome song titles, artists,
 * playlist names, transcribed song names).
 *
 * LVGL's built-in lv_font_montserrat_* cover ASCII only, so Cyrillic and
 * accented Latin ("Björk") drew missing-glyph boxes. montserrat_ext_NN are
 * supplements generated from the same Montserrat-Medium face by
 * tools/fonts/gen_fonts.sh carrying ONLY Latin-1 Supplement, Latin Extended-A
 * and Cyrillic (U+00A0-00FF, U+0100-017F, U+0400-045F, U+0490-0491).
 *
 * app_font_NN is a runtime copy of lv_font_montserrat_NN with .fallback set to
 * the matching supplement, so LVGL's glyph lookup (lv_font_get_glyph_dsc walks
 * the fallback chain) finds Latin in the built-in and the rest in the
 * supplement, with the built-in's metrics. Any label that can show text from
 * the music server MUST use &app_font_NN instead of &lv_font_montserrat_NN.
 * Setting it on an lv_list propagates to the row labels (text_font inherits).
 *
 * app_fonts_init() must run once before the first such label is created
 * (app_main.c does this right after the display comes up). Scripts outside
 * those ranges (Greek, CJK, emoji) still render as placeholder boxes. */
LV_FONT_DECLARE(montserrat_ext_14);
LV_FONT_DECLARE(montserrat_ext_16);
LV_FONT_DECLARE(montserrat_ext_20);
LV_FONT_DECLARE(montserrat_ext_28);

extern lv_font_t app_font_14;
extern lv_font_t app_font_16;
extern lv_font_t app_font_20;
extern lv_font_t app_font_28;

void app_fonts_init(void);
