/* Host-test stand-in for <locale/locale.h>; same formulas as the service. */
#pragma once

static inline float locale_fahrenheit_to_celsius(float temp_f) {
    return (temp_f - 32.f) / 1.8f;
}
static inline float locale_celsius_to_fahrenheit(float temp_c) {
    return (temp_c * 1.8f + 32.f);
}
