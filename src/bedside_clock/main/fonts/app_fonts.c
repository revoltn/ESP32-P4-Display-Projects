/* Fallback-chained text fonts: built-in Montserrat (ASCII) in front, the
 * generated Cyrillic / accented-Latin supplement behind. See app_fonts.h. */
#include "fonts/app_fonts.h"

lv_font_t app_font_14;
lv_font_t app_font_16;
lv_font_t app_font_20;
lv_font_t app_font_28;

/* The built-ins are const, so chain via a copy. The copy keeps pointing at
 * LVGL's static glyph descriptor (font->dsc), which is all the glyph callbacks
 * dereference, so the copy is a complete, standalone font. If the built-in
 * already had a fallback we would lose it here - it does not. */
static void chain(lv_font_t *dst, const lv_font_t *primary,
                  const lv_font_t *supplement)
{
    *dst = *primary;
    dst->fallback = supplement;
}

void app_fonts_init(void)
{
    chain(&app_font_14, &lv_font_montserrat_14, &montserrat_ext_14);
    chain(&app_font_16, &lv_font_montserrat_16, &montserrat_ext_16);
    chain(&app_font_20, &lv_font_montserrat_20, &montserrat_ext_20);
    chain(&app_font_28, &lv_font_montserrat_28, &montserrat_ext_28);
}
