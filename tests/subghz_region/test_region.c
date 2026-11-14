/* Host test for the Sub-GHz transmit region presets (pure table logic). */
#include <furi_hal_region_table.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                               \
    do {                                               \
        checks++;                                      \
        if(!(cond)) {                                  \
            failures++;                                \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                       \
            printf("\n");                              \
        }                                              \
    } while(0)

static bool allowed(const char* code, uint32_t hz) {
    size_t index = furi_hal_region_table_find(code);
    return furi_hal_region_table_band(furi_hal_region_table_get(index), hz) != NULL;
}

int main(void) {
    size_t count = furi_hal_region_table_count();
    CHECK(count >= 4, "expected several presets, got %zu", count);

    /* Index 0 is "not set" and permits nothing: the safe default. */
    const FuriHalRegion* unset = furi_hal_region_table_get(0);
    CHECK(unset->bands_count == 0, "preset 0 has %u bands", unset->bands_count);
    for(uint32_t hz = 280000000u; hz <= 960000000u; hz += 50000u) {
        if(furi_hal_region_table_band(unset, hz)) {
            CHECK(false, "unset region allows %lu Hz", (unsigned long)hz);
            break;
        }
    }
    checks++;

    /* Unknown, empty and NULL codes fall back to "not set", never to a region. */
    CHECK(furi_hal_region_table_find("ZZ") == 0, "unknown code did not map to unset");
    CHECK(furi_hal_region_table_find("") == 0, "empty code did not map to unset");
    CHECK(furi_hal_region_table_find(NULL) == 0, "NULL code did not map to unset");
    CHECK(furi_hal_region_table_find("eu") == 0, "codes must match exactly");
    CHECK(furi_hal_region_table_get(count + 5) == unset, "out-of-range index is not unset");
    CHECK(furi_hal_region_table_band(NULL, 433920000u) == NULL, "NULL region allowed a band");

    /* Table hygiene: unique codes, labels present, bands ordered and sane. */
    for(size_t i = 0; i < count; i++) {
        const FuriHalRegion* region = furi_hal_region_table_get(i);
        const char* label = furi_hal_region_table_label(i);
        CHECK(label && label[0] && strlen(label) <= 12, "preset %zu: bad label", i);
        CHECK(strlen(region->country_code) == 2, "preset %zu: code is not 2 chars", i);
        CHECK(furi_hal_region_table_find(region->country_code) == i, "preset %zu: code not unique", i);
        CHECK(i == 0 || region->bands_count > 0, "preset %zu: no bands", i);
        for(uint16_t b = 0; b < region->bands_count; b++) {
            const FuriHalRegionBand* band = &region->bands[b];
            CHECK(band->start < band->end, "%s band %u: start >= end", region->country_code, b);
            /* Nothing outside what the CC1101 front end covers. */
            CHECK(band->start >= 300000000u && band->end <= 928000000u,
                  "%s band %u outside 300-928 MHz", region->country_code, b);
            CHECK(band->end - band->start <= 20000000u, "%s band %u wider than 20 MHz",
                  region->country_code, b);
            if(b > 0) {
                CHECK(region->bands[b - 1].end < band->start, "%s bands overlap or are unordered",
                      region->country_code);
            }
            /* Edges are inclusive; one Hz outside is not. */
            CHECK(furi_hal_region_table_band(region, band->start) == band, "start edge");
            CHECK(furi_hal_region_table_band(region, band->end) == band, "end edge");
            CHECK(furi_hal_region_table_band(region, band->start - 1) != band, "below start");
            CHECK(furi_hal_region_table_band(region, band->end + 1) != band, "above end");
        }
    }

    /* Spot checks against the published table. */
    CHECK(allowed("EU", 433920000u), "EU 433.92");
    CHECK(allowed("EU", 868350000u), "EU 868.35");
    CHECK(!allowed("EU", 315000000u), "EU must not allow 315");
    CHECK(!allowed("EU", 915000000u), "EU must not allow 915");
    CHECK(!allowed("EU", 868000000u), "EU must not allow 868.00 (band starts 868.15)");
    CHECK(allowed("US", 315000000u), "US 315");
    CHECK(allowed("US", 433920000u), "US 433.92");
    CHECK(allowed("US", 915000000u), "US 915");
    CHECK(!allowed("US", 868350000u), "US must not allow 868.35");
    CHECK(!allowed("US", 390000000u), "US must not allow 390");
    CHECK(allowed("WW", 315000000u) && !allowed("WW", 433920000u), "rest-of-world bands");
    CHECK(!allowed("--", 433920000u), "unset allows 433.92");

    printf("%d checks, %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}
