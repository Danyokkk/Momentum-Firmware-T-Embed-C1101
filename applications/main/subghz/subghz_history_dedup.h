/**
 * @file subghz_history_dedup.h
 * Pure, dependency-free predicates for Sub-GHz receiver-history de-duplication.
 *
 * Two policies live here so they can be unit-tested on the host with no furi,
 * no HAL and no GUI (see tests/subghz_history_dedup/):
 *   - the pre-existing "consecutive" policy: suppress a signal that is identical
 *     to the immediately preceding one inside a short time window;
 *   - the opt-in "Remove Duplicates" policy: suppress a signal whose hash
 *     already appears anywhere in the current receiver history.
 *
 * Receive-side only. Nothing here transmits.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/** True if @p new_hash equals @p last_hash and arrives within @p window_ms of
 *  the previous signal. Unsigned subtraction tolerates timestamp wrap-around. */
static inline bool subghz_history_dedup_is_consecutive(
    uint32_t new_hash,
    uint32_t last_hash,
    uint32_t now_ms,
    uint32_t last_ms,
    uint32_t window_ms) {
    return (new_hash == last_hash) && ((uint32_t)(now_ms - last_ms) < window_ms);
}

/** True if @p new_hash is already present in hashes[0..count). */
static inline bool
    subghz_history_dedup_in_list(const uint32_t* hashes, size_t count, uint32_t new_hash) {
    for(size_t i = 0; i < count; i++) {
        if(hashes[i] == new_hash) return true;
    }
    return false;
}
