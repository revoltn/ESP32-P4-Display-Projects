#pragma once

#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start the alarm scheduler task (checks every alarm slot every second). */
void alarm_init(void);

/* True while an alarm is ringing (ambient dimming should hold off). */
bool alarm_is_active(void);

/* Slot (0-based) that is ringing right now, -1 when idle. */
int alarm_ringing_index(void);

/* Seconds until the ringing alarm stops by itself (settings
 * max_alarm_seconds); 0 when idle or when the ring is unlimited. */
int alarm_ring_remaining_s(void);

/* One upcoming (or missed) ring. index is the 0-based slot. */
typedef struct {
    int    index;
    time_t at;
    bool   snooze;     /* the ring is a snooze re-ring, not the scheduled time */
} alarm_next_t;

/* Earliest upcoming ring across all slots and pending snoozes. Lock-free
 * copy of a snapshot the scheduler publishes every second: safe from the
 * LVGL tick and the HTTP server. False when nothing is armed or the clock
 * is not valid yet. */
bool alarm_next(alarm_next_t *out);

/* Last alarm that stopped by itself (max_alarm_seconds expired) and has not
 * been acknowledged. Cleared by alarm_ack_missed(), by a dismiss, or by the
 * next ring. */
bool alarm_last_missed(alarm_next_t *out);
void alarm_ack_missed(void);

/* Voice entry points: same effect as the Snooze/Dismiss buttons.  Pure
 * event-group signalling - callable from any task, no LVGL lock needed.
 * No-ops when the alarm is not ringing. */
void alarm_voice_snooze(void);
void alarm_voice_dismiss(void);

#ifdef __cplusplus
}
#endif
