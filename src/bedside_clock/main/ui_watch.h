#pragma once

/* LVGL health watch: an lv_timer heartbeat checked by an esp_timer that
 * runs independently of the LVGL task, plus an INFO-level trace of touch
 * press/release events. When the UI stops reacting, the serial log then
 * says which of three things happened: the LVGL task is Blocked (a mutex
 * it should never wait on), Running (spinning on a flush or starved), or
 * healthy with no touch reports (the touch controller itself). Call once
 * from app_main under bsp_display_lock after the clock UI exists. */
void ui_watch_start(void);
