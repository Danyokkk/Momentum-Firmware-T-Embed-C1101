/**
 * Sub-GHz transmit region presets (see furi_hal_region_table.c).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "furi_hal_region.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Number of presets, including index 0 = "Not set". */
size_t furi_hal_region_table_count(void);

/** Preset by index; out-of-range returns the "Not set" region. */
const FuriHalRegion* furi_hal_region_table_get(size_t index);

/** Short label for a settings list. */
const char* furi_hal_region_table_label(size_t index);

/** Index of the preset with this country code; 0 ("Not set") if unknown. */
size_t furi_hal_region_table_find(const char* country_code);

/** Band of `region` containing `frequency` (Hz, edges inclusive), or NULL. */
const FuriHalRegionBand*
    furi_hal_region_table_band(const FuriHalRegion* region, uint32_t frequency);

#ifdef __cplusplus
}
#endif
