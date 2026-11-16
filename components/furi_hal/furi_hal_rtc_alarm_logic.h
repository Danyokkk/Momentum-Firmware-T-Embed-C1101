#pragma once

/* Pure alarm-firing decision, split out so it can be host-tested without the
 * ESP RTC. A daily alarm fires once when the clock enters its minute; it re-arms
 * after the clock leaves that minute (so it fires again the next day). Times are
 * "minutes since midnight" (0..1439). *last_fired holds the last minute a firing
 * happened, or -1 for "armed". */
#include <stdbool.h>

static inline bool
    furi_hal_rtc_alarm_due(int now_minute_of_day, int alarm_minute_of_day, int* last_fired) {
    if(now_minute_of_day != alarm_minute_of_day) {
        *last_fired = -1; /* left the matching minute: re-arm */
        return false;
    }
    if(*last_fired == now_minute_of_day) {
        return false; /* already fired during this minute */
    }
    *last_fired = now_minute_of_day;
    return true;
}
