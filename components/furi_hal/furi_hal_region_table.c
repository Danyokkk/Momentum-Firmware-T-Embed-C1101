/**
 * Sub-GHz transmit region presets. Pure data and lookup, no hardware access,
 * so it is also compiled into the host test (tests/subghz_region).
 *
 * Band edges are the transmission bands Flipper Devices publishes for Flipper
 * Zero, per region group: https://docs.flipper.net/zero/sub-ghz/frequencies
 * (read 2026-11-14). They are a convenience, not legal advice: the operator is
 * responsible for the rules that apply where the device is used, including
 * power and duty-cycle limits, which this table does not enforce.
 */
#include "furi_hal_region_table.h"

#include <string.h>

/* Only .start/.end are enforced. power_limit (dBm) and duty_cycle (%) keep the
 * values the port's previous single "allow everything" entry used. */
#define BAND(start_hz, end_hz) \
    { .start = (start_hz), .end = (end_hz), .power_limit = 12, .duty_cycle = 50 }

/* No region chosen: nothing may be transmitted. This is the default. */
static const FuriHalRegion region_unset = {
    .country_code = "--",
    .bands_count = 0,
};

static const FuriHalRegion region_eu = {
    .country_code = "EU",
    .bands_count = 2,
    .bands = {BAND(433050000, 434790000), BAND(868150000, 868550000)},
};

static const FuriHalRegion region_us = {
    .country_code = "US",
    .bands_count = 3,
    .bands =
        {BAND(304100000, 321950000), BAND(433050000, 434790000), BAND(915000000, 928000000)},
};

static const FuriHalRegion region_ae = {
    .country_code = "AE",
    .bands_count = 1,
    .bands = {BAND(420000000, 440000000)},
};

static const FuriHalRegion region_tw = {
    .country_code = "TW",
    .bands_count = 3,
    .bands =
        {BAND(304500000, 321950000), BAND(433075000, 434775000), BAND(915000000, 927950000)},
};

static const FuriHalRegion region_sg = {
    .country_code = "SG",
    .bands_count = 4,
    .bands =
        {BAND(300000000, 300300000),
         BAND(312000000, 316000000),
         BAND(433500000, 434790000),
         BAND(444400000, 444800000)},
};

static const FuriHalRegion region_in = {
    .country_code = "IN",
    .bands_count = 1,
    .bands = {BAND(433050000, 434790000)},
};

static const FuriHalRegion region_ph = {
    .country_code = "PH",
    .bands_count = 1,
    .bands = {BAND(430000000, 440000000)},
};

static const FuriHalRegion region_cn = {
    .country_code = "CN",
    .bands_count = 3,
    .bands =
        {BAND(314000000, 316000000), BAND(430000000, 432000000), BAND(433050000, 434790000)},
};

static const FuriHalRegion region_ww = {
    .country_code = "WW",
    .bands_count = 2,
    .bands = {BAND(312000000, 315250000), BAND(920500000, 923500000)},
};

typedef struct {
    const FuriHalRegion* region;
    const char* label;
} RegionPreset;

/* Index 0 must stay the unset entry. */
static const RegionPreset presets[] = {
    {&region_unset, "Not set"},
    {&region_eu, "EU/UK/RU+"},
    {&region_us, "US/CA/AU+"},
    {&region_ae, "UAE"},
    {&region_tw, "Taiwan"},
    {&region_sg, "Singapore"},
    {&region_in, "India/Israel"},
    {&region_ph, "Philippines"},
    {&region_cn, "China"},
    {&region_ww, "Other"},
};

size_t furi_hal_region_table_count(void) {
    return sizeof(presets) / sizeof(presets[0]);
}

const FuriHalRegion* furi_hal_region_table_get(size_t index) {
    return index < furi_hal_region_table_count() ? presets[index].region : &region_unset;
}

const char* furi_hal_region_table_label(size_t index) {
    return index < furi_hal_region_table_count() ? presets[index].label : presets[0].label;
}

size_t furi_hal_region_table_find(const char* country_code) {
    if(country_code) {
        for(size_t i = 0; i < furi_hal_region_table_count(); i++) {
            if(strcmp(presets[i].region->country_code, country_code) == 0) return i;
        }
    }
    return 0;
}

const FuriHalRegionBand*
    furi_hal_region_table_band(const FuriHalRegion* region, uint32_t frequency) {
    if(!region) return NULL;
    for(uint16_t i = 0; i < region->bands_count; i++) {
        if(frequency >= region->bands[i].start && frequency <= region->bands[i].end) {
            return &region->bands[i];
        }
    }
    return NULL;
}
