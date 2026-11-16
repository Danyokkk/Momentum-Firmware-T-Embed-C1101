/* Host test for furi_hal_rtc_alarm_due() (components/furi_hal/
 * furi_hal_rtc_alarm_logic.h): the daily-alarm firing decision, tested without
 * the ESP RTC. */
#include <stdio.h>
#include "furi_hal_rtc_alarm_logic.h"

static int failures = 0;
#define CHECK(cond, ...)       \
    do {                       \
        if(!(cond)) {          \
            printf("FAIL: ");  \
            printf(__VA_ARGS__);\
            printf("\n");      \
            failures++;        \
        }                      \
    } while(0)

int main(void) {
    const int alarm = 7 * 60 + 30; /* 07:30 */
    int last = -1;

    /* Before the minute: no fire, stays armed. */
    CHECK(!furi_hal_rtc_alarm_due(7 * 60 + 29, alarm, &last), "fired a minute early");
    CHECK(last == -1, "disarmed early");

    /* Enter the minute: fires once. */
    CHECK(furi_hal_rtc_alarm_due(alarm, alarm, &last), "did not fire on match");
    CHECK(last == alarm, "did not record firing");

    /* Same minute, further ticks: no repeat. */
    CHECK(!furi_hal_rtc_alarm_due(alarm, alarm, &last), "fired twice in the same minute");
    CHECK(!furi_hal_rtc_alarm_due(alarm, alarm, &last), "fired again in the same minute");

    /* Leave the minute: re-arms. */
    CHECK(!furi_hal_rtc_alarm_due(alarm + 1, alarm, &last), "fired after the minute");
    CHECK(last == -1, "did not re-arm after leaving the minute");

    /* Next day, same minute: fires again. */
    CHECK(furi_hal_rtc_alarm_due(alarm, alarm, &last), "did not fire the next day");

    /* Midnight alarm boundary. */
    last = -1;
    CHECK(furi_hal_rtc_alarm_due(0, 0, &last), "midnight alarm did not fire");
    CHECK(!furi_hal_rtc_alarm_due(0, 0, &last), "midnight alarm repeated");

    if(failures) {
        printf("FAIL: %d RTC-alarm check(s) failed\n", failures);
        return 1;
    }
    printf("PASS: RTC alarm firing logic (host only; the 1 Hz timer and the "
           "notification are a hardware check)\n");
    return 0;
}
