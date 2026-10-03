#pragma once

#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create the TZ lock; call once from app_main before any task reads time. */
void clock_sync_early_init(void);

/* localtime_r guarded against a concurrent tzset() (timezone change). Use
 * this instead of bare localtime_r anywhere outside this module. */
void clock_sync_localtime(const time_t *t, struct tm *out);

/* mktime guarded the same way (mutates tm; tm_isdst = -1 recommended). */
time_t clock_sync_mktime(struct tm *tm);

/* Apply the timezone from settings and start SNTP (resyncs hourly).
 * Safe to call again after the timezone setting changes. */
void clock_sync_start(void);

/* True when the wall clock is usable: SNTP has set it, or the RTC carried a
 * plausible time across a (non power-on) reset. */
bool clock_sync_time_valid(void);

/* True once SNTP has corrected the clock at least once this boot. */
bool clock_sync_is_synced(void);

#ifdef __cplusplus
}
#endif
