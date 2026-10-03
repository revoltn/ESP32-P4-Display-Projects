#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Build the clock screen. Caller must hold the LVGL lock. */
void ui_clock_create(void);

/* Re-apply the clock_face setting's layout (caller holds the LVGL lock). */
void ui_clock_apply_face(void);

/* Tint the big time digits from white toward deep orange as the backlight
 * dims. pct = the brightness percent just applied (1..100). Safe from any
 * task (takes the LVGL lock internally); no-op before the clock exists. */
void ui_clock_set_dim_level(int pct);

#ifdef __cplusplus
}
#endif
