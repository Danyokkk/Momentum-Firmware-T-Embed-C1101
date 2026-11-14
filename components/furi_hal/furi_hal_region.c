/**
 * Sub-GHz transmit region.
 *
 * Upstream's version was a stub that allowed 0-1 GHz everywhere. Here the
 * operator chooses a region preset (Sub-GHz > Radio Settings > TX Region). The
 * choice is stored in NVS and furi_hal_subghz_is_tx_allowed() consults it, so
 * every transmit path is covered: the app, the CLI, scripts and external apps.
 *
 * Until a region is chosen nothing can be transmitted. Receiving is never
 * restricted here.
 */
#include "furi_hal_region.h"
#include "furi_hal_region_table.h"

#include <stddef.h>
#include <string.h>

#include <core/common_defines.h>
#include <nvs.h>

#define REGION_NVS_NAMESPACE "furi_region"
#define REGION_NVS_KEY       "code"

static const FuriHalRegion* region_current = NULL; /* NULL = not loaded yet */

static void furi_hal_region_load(void) {
    char code[sizeof(((FuriHalRegion*)0)->country_code)] = {0};
    size_t length = sizeof(code);
    nvs_handle_t handle;
    size_t index = 0;
    if(nvs_open(REGION_NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        if(nvs_get_str(handle, REGION_NVS_KEY, code, &length) == ESP_OK) {
            index = furi_hal_region_table_find(code);
        }
        nvs_close(handle);
    }
    region_current = furi_hal_region_table_get(index);
}

const FuriHalRegion* furi_hal_region_get(void) {
    if(!region_current) furi_hal_region_load();
    return region_current;
}

void furi_hal_region_set(FuriHalRegion* region) {
    /* Upstream API: install an arbitrary band list (its jammer used this to
     * unlock everything). Only presets can be selected in this firmware; see
     * furi_hal_region_select(). */
    UNUSED(region);
}

bool furi_hal_region_select(size_t preset_index) {
    if(preset_index >= furi_hal_region_table_count()) return false;
    const FuriHalRegion* region = furi_hal_region_table_get(preset_index);
    nvs_handle_t handle;
    bool stored = false;
    if(nvs_open(REGION_NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        stored = nvs_set_str(handle, REGION_NVS_KEY, region->country_code) == ESP_OK &&
                 nvs_commit(handle) == ESP_OK;
        nvs_close(handle);
    }
    /* Apply even if persisting failed, so the choice at least holds until reboot. */
    region_current = region;
    return stored;
}

size_t furi_hal_region_get_selected(void) {
    return furi_hal_region_table_find(furi_hal_region_get()->country_code);
}

bool furi_hal_region_is_provisioned(void) {
    return furi_hal_region_get()->bands_count > 0;
}

const char* furi_hal_region_get_name(void) {
    return furi_hal_region_get()->country_code;
}

bool furi_hal_region_is_frequency_allowed(uint32_t frequency) {
    return furi_hal_region_table_band(furi_hal_region_get(), frequency) != NULL;
}

const FuriHalRegionBand* furi_hal_region_get_band(uint32_t frequency) {
    return furi_hal_region_table_band(furi_hal_region_get(), frequency);
}
