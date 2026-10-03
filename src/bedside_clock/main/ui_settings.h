#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Open the full-screen settings overlay (call from an LVGL callback). */
void ui_settings_open(void);

/* True while the overlay is on screen (ambient dimming holds off). */
bool ui_settings_is_open(void);

/* --- voice entry points: caller MUST hold the LVGL lock ---
 * page ids are the fixed VA_OPEN_* offsets from voice_actions.h, independent
 * of the menu tile order: 0 Volume, 1 WiFi, 2 Timezone, 3 Music API,
 * 4 Alarm, 5 Clock Face, 6 Brightness, 7 Specifications, 8 Voice,
 * 9 Alarm sound. */
void ui_settings_voice_open_page(int page_id);
void ui_settings_voice_back(void);
void ui_settings_voice_save(void);
void ui_settings_voice_page(bool up);

#ifdef __cplusplus
}
#endif
