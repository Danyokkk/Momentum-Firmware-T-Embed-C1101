/* Host-test stand-in for <furi_hal.h>. */
#pragma once

#include <stdint.h>

typedef enum {
    FuriHalRtcLocaleUnitsMetric = 0x0,
    FuriHalRtcLocaleUnitsImperial = 0x1,
} FuriHalRtcLocaleUnits;

extern FuriHalRtcLocaleUnits host_locale_units;
extern uint32_t host_rtc_timestamp;

static inline FuriHalRtcLocaleUnits furi_hal_rtc_get_locale_units(void) {
    return host_locale_units;
}
static inline uint32_t furi_hal_rtc_get_timestamp(void) {
    return host_rtc_timestamp;
}
