#!/bin/sh
# Regenerate the Giant clock-face fonts (digits + '-' + ':' only) from
# Dangrek (OFL, Google Fonts; the user's pick). Sizes chosen for the 800x480
# landscape panel: hours ink ~330 px tall (two digits = 2/3 of the row),
# minute digits ~156 px tall (two stacked = hours height). Needs node/npx.
# Any glyph taller/wider than 255 px needs CONFIG_LV_FONT_FMT_TXT_LARGE=y.
cd "$(dirname "$0")" || exit 1
npx --yes lv_font_conv --font Dangrek-Regular.ttf -r 0x2D,0x30-0x39 \
    --size 478 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h \
    -o ../../main/fonts/dangrek_478.c
npx --yes lv_font_conv --font Dangrek-Regular.ttf -r 0x2D,0x30-0x3A \
    --size 226 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h \
    -o ../../main/fonts/dangrek_226.c

# Service-indicator icons (Font Awesome 6 Free Solid, CC BY 4.0 - see
# LICENSE-FontAwesome.txt). 28 px to match the montserrat_28 symbols the top
# row and the settings tiles already use on the 800x480 panel. Codepoints and
# their ICON_* macros live in main/fonts/app_icons.h - keep the two in sync.
#   f130 microphone  (voice)
#   f2db microchip   (AI)
npx --yes lv_font_conv --font fa-solid-900.ttf -r 0xf130,0xf2db \
    --size 28 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h \
    -o ../../main/fonts/app_icons_28.c

# Text supplements for server-supplied strings (song titles, artists, playlist
# names from Navidrome). LVGL's built-in montserrat_* fonts are ASCII-only, so
# anything else drew the missing-glyph box. These carry ONLY the extra ranges
# (Latin-1 Supplement, Latin Extended-A, Cyrillic incl. Ё/ё and Ґ/ґ) and are
# chained behind the built-ins as LVGL fallback fonts by main/fonts/app_fonts.c.
# Same face and bpp as the built-ins (Montserrat-Medium.ttf is a copy of
# lvgl/scripts/built_in_font/Montserrat-Medium.ttf, OFL) so mixed text matches.
# Sizes = the montserrat sizes those labels use. ~130-150 KB of flash in total.
EXT_RANGES=0xA0-0xFF,0x100-0x17F,0x400-0x45F,0x490-0x491
for sz in 14 16 20 28; do
    npx --yes lv_font_conv --font Montserrat-Medium.ttf -r "$EXT_RANGES" \
        --size "$sz" --bpp 4 --format lvgl --no-compress --lv-include lvgl.h \
        -o "../../main/fonts/montserrat_ext_${sz}.c"
done
