/* Host test: Sub-GHz receiver-history dedup predicates. No device, no HAL —
 * the helpers are pure. See run.py. */
#include "subghz_history_dedup.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define CHECK(cond)                                          \
    do {                                                     \
        if(!(cond)) {                                        \
            printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            return 1;                                        \
        }                                                    \
    } while(0)

int main(void) {
    /* Consecutive policy: same hash within the window is a duplicate. */
    CHECK(subghz_history_dedup_is_consecutive(0xABCD, 0xABCD, 1000, 900, 500)); /* 100ms < 500 */
    CHECK(!subghz_history_dedup_is_consecutive(0xABCD, 0xABCD, 2000, 900, 500)); /* 1100ms > 500 */
    CHECK(!subghz_history_dedup_is_consecutive(0xABCD, 0x1234, 1000, 900, 500)); /* diff hash */
    CHECK(subghz_history_dedup_is_consecutive(5, 5, 500, 500, 500)); /* 0ms < 500 */
    /* Timestamp wrap-around: now just after wrap, last just before. */
    CHECK(subghz_history_dedup_is_consecutive(7, 7, 50, 0xFFFFFFF0, 500)); /* delta 0x60 = 96ms */

    /* Whole-history membership. */
    const uint32_t list[] = {0x11, 0x22, 0x33, 0x44};
    CHECK(!subghz_history_dedup_in_list(list, 0, 0x11)); /* empty list */
    CHECK(subghz_history_dedup_in_list(list, 4, 0x11)); /* first */
    CHECK(subghz_history_dedup_in_list(list, 4, 0x44)); /* last */
    CHECK(!subghz_history_dedup_in_list(list, 4, 0x55)); /* absent */
    CHECK(!subghz_history_dedup_in_list(list, 2, 0x33)); /* count bounds the scan */

    /* A,B,A: whole-history dedup catches the 3rd A; the consecutive policy does
     * not, because the last stored signal is B, not A. */
    uint32_t hist[8];
    size_t n = 0;
    const uint32_t A = 0xAAAA0001, B = 0xBBBB0002;
    CHECK(!subghz_history_dedup_in_list(hist, n, A));
    hist[n++] = A;
    CHECK(!subghz_history_dedup_in_list(hist, n, B));
    hist[n++] = B;
    CHECK(subghz_history_dedup_in_list(hist, n, A)); /* whole-history: duplicate */
    CHECK(!subghz_history_dedup_is_consecutive(A, B, 1000, 900, 500)); /* consecutive: not */

    printf("PASS: subghz history dedup (consecutive, whole-history, A-B-A)\n");
    return 0;
}
