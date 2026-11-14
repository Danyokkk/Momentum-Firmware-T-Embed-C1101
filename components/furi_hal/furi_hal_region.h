#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t start;
    uint32_t end;
    int8_t power_limit;
    uint8_t duty_cycle;
} FuriHalRegionBand;

typedef struct {
    char country_code[4];
    uint16_t bands_count;
    FuriHalRegionBand bands[];
} FuriHalRegion;

const FuriHalRegion* furi_hal_region_get(void);
void furi_hal_region_set(FuriHalRegion* region);
bool furi_hal_region_is_provisioned(void);
const char* furi_hal_region_get_name(void);
bool furi_hal_region_is_frequency_allowed(uint32_t frequency);
const FuriHalRegionBand* furi_hal_region_get_band(uint32_t frequency);

/** Choose a preset from furi_hal_region_table.h and store it in NVS.
 *  Index 0 is "Not set", which disables Sub-GHz transmit.
 *  @return true if the choice was persisted (it is applied either way). */
bool furi_hal_region_select(size_t preset_index);

/** Index of the active preset in furi_hal_region_table.h. */
size_t furi_hal_region_get_selected(void);

#ifdef __cplusplus
}
#endif
