/* Host tests for the weather-station decoders ported from Momentum.
 *
 * Real captures (tests/subghz_ws/fixtures, from Momentum's unit-test resources)
 * are replayed pulse by pulse through the same feed() entry point the receiver
 * uses on the device. Decoders without a real capture get a synthetic waveform
 * (tests/subghz_ws/fixtures/synthetic, written by make_synthetic_fixtures.py);
 * README.md says what those do and do not prove. Nothing here touches a radio.
 *
 *   test_ws_decode <fixtures-dir>
 *   test_ws_decode --replay <file.sub>...   show what all decoders make of a file
 */
#include <furi.h>
#include <furi_hal.h>
#include <lib/flipper_format/flipper_format.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/blocks/generic.h>

#include <lib/toolbox/level_duration.h>

#include <math.h>

#include "acurite_592txr.h"
#include "acurite_5n1.h"
#include "acurite_606tx.h"
#include "acurite_609txc.h"
#include "acurite_986.h"
#include "ambient_weather.h"
#include "auriol_ahfl.h"
#include "auriol_hg0601a.h"
#include "bresser_3ch.h"
#include "emos_e601x.h"
#include "gt_wt_02.h"
#include "gt_wt_03.h"
#include "infactory.h"
#include "kedsum_th.h"
#include "lacrosse_tx.h"
#include "lacrosse_tx141thbv2.h"
#include "nexus_th.h"
#include "oregon2.h"
#include "oregon3.h"
#include "oregon_v1.h"
#include "solight_te44.h"
#include "thermopro_tx4.h"
#include "tx_8300.h"
#include "vauno_en8822c.h"
#include "wendox_w6726.h"

/* Same order as lib/subghz/protocols/protocol_items.c. */
static const SubGhzProtocol* const ws_protocols[] = {
    &ws_protocol_infactory,           &ws_protocol_thermopro_tx4,
    &ws_protocol_nexus_th,            &ws_protocol_gt_wt_02,
    &ws_protocol_gt_wt_03,            &ws_protocol_acurite_606tx,
    &ws_protocol_acurite_609txc,      &ws_protocol_acurite_986,
    &ws_protocol_bresser_3ch,         &ws_protocol_lacrosse_tx,
    &ws_protocol_lacrosse_tx141thbv2, &ws_protocol_oregon2,
    &ws_protocol_oregon3,             &ws_protocol_acurite_592txr,
    &ws_protocol_ambient_weather,     &ws_protocol_solight_te44,
    &ws_protocol_auriol_th,           &ws_protocol_oregon_v1,
    &ws_protocol_tx_8300,             &ws_protocol_wendox_w6726,
    &ws_protocol_auriol_ahfl,         &ws_protocol_kedsum_th,
    &ws_protocol_emose601x,           &ws_protocol_acurite_5n1,
    &ws_protocol_vauno_en8822c,
};
#define WS_COUNT COUNT_OF(ws_protocols)

static int failures = 0;
static int checks = 0;
static const char* fixtures_dir = ".";

#define CHECK(cond, ...)                           \
    do {                                           \
        checks++;                                  \
        if(!(cond)) {                              \
            failures++;                            \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                   \
            printf("\n");                          \
        }                                          \
    } while(0)

/* ------------------------------------------------------------- helpers --- */

static char* read_fixture(const char* name) {
    char path[512];
    snprintf(path, sizeof(path), "%s%s%s", fixtures_dir, fixtures_dir[0] ? "/" : "", name);
    FILE* file = fopen(path, "rb");
    if(!file) {
        printf("cannot open fixture %s\n", path);
        exit(2);
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char* text = malloc((size_t)size + 1);
    furi_check(text);
    furi_check(fread(text, 1, (size_t)size, file) == (size_t)size);
    text[size] = '\0';
    fclose(file);
    return text;
}

typedef void (*PulseSink)(bool level, uint32_t duration, void* context);

/* Walk every "RAW_Data:" line: positive = high for N us, negative = low. */
static size_t replay_raw(const char* text, PulseSink sink, void* context) {
    size_t pulses = 0;
    const char* cursor = text;
    while((cursor = strstr(cursor, "RAW_Data:")) != NULL) {
        cursor += strlen("RAW_Data:");
        while(*cursor && *cursor != '\n') {
            char* end = NULL;
            long value = strtol(cursor, &end, 10);
            if(end == cursor) {
                cursor++;
                continue;
            }
            cursor = end;
            if(value == 0) continue;
            sink(value > 0, (uint32_t)labs(value), context);
            pulses++;
        }
    }
    return pulses;
}

typedef struct {
    const SubGhzProtocol* protocol;
    SubGhzProtocolDecoderBase* base;
    unsigned decoded; /* callback invocations */
    FlipperFormat* first; /* serialized form of the first decode */
} Slot;

static SubGhzRadioPreset make_preset(void) {
    SubGhzRadioPreset preset = {0};
    preset.name = furi_string_alloc();
    furi_string_set(preset.name, "AM650");
    preset.frequency = 433920000;
    return preset;
}

static void on_decode(SubGhzProtocolDecoderBase* base, void* context) {
    Slot* slot = context;
    slot->decoded++;
    if(!slot->first) {
        slot->first = flipper_format_string_alloc();
        SubGhzRadioPreset preset = make_preset();
        SubGhzProtocolStatus status =
            slot->protocol->decoder->serialize(base, slot->first, &preset);
        furi_string_free(preset.name);
        CHECK(status == SubGhzProtocolStatusOk, "%s: serialize after decode returned %d",
              slot->protocol->name, (int)status);
    }
}

static void slot_open(Slot* slot, const SubGhzProtocol* protocol) {
    memset(slot, 0, sizeof(*slot));
    slot->protocol = protocol;
    slot->base = protocol->decoder->alloc(NULL);
    furi_check(slot->base);
    slot->base->callback = on_decode;
    slot->base->context = slot;
}

static void slot_close(Slot* slot) {
    if(slot->first) flipper_format_free(slot->first);
    slot->protocol->decoder->free(slot->base);
}

static void feed_one(bool level, uint32_t duration, void* context) {
    Slot* slot = context;
    slot->protocol->decoder->feed(slot->base, level, duration);
}

static void feed_all(bool level, uint32_t duration, void* context) {
    Slot* slots = context;
    for(size_t i = 0; i < WS_COUNT; i++) {
        slots[i].protocol->decoder->feed(slots[i].base, level, duration);
    }
}

static const SubGhzProtocol* find_protocol(const char* name) {
    for(size_t i = 0; i < WS_COUNT; i++) {
        if(strcmp(ws_protocols[i]->name, name) == 0) return ws_protocols[i];
    }
    return NULL;
}

static size_t protocol_index(const SubGhzProtocol* protocol) {
    for(size_t i = 0; i < WS_COUNT; i++) {
        if(ws_protocols[i] == protocol) return i;
    }
    furi_crash("protocol is not in the table");
    return 0;
}

static bool field_matches(
    const FlipperFormat* a,
    const FlipperFormat* b,
    const char* key,
    float temp_tolerance) {
    const char* left = ff_host_value(a, key);
    const char* right = ff_host_value(b, key);
    if(!left || !right) return false;
    if(strcmp(key, "Temp") == 0) {
        return fabsf(strtof(left, NULL) - strtof(right, NULL)) < temp_tolerance;
    }
    return strcmp(left, right) == 0;
}

static bool same_field(const FlipperFormat* a, const FlipperFormat* b, const char* key) {
    return field_matches(a, b, key, 0.01f);
}

static FlipperFormat* load_key_fixture(const char* name) {
    char* text = read_fixture(name);
    FlipperFormat* key = flipper_format_string_alloc();
    furi_check(ff_host_load_text(key, text));
    free(text);
    return key;
}

static const char* shown(const FlipperFormat* ff, const char* key) {
    const char* value = ff ? ff_host_value(ff, key) : NULL;
    return value ? value : "(none)";
}

/* Which decoders have a fixture-backed positive test that actually decoded. */
static bool covered_real[WS_COUNT];
static bool covered_synthetic[WS_COUNT];
static bool covered_roundtrip[WS_COUNT];

/* --------------------------------------------------------------- tests --- */

/* A real capture decodes, and the decoded reading equals the reading stored in
 * the matching upstream key file (everything except the capture timestamp). */
static void test_capture(const char* raw_name, const char* key_name, const char* protocol_name) {
    printf("capture %-24s -> %s\n", raw_name, protocol_name);
    const SubGhzProtocol* protocol = find_protocol(protocol_name);
    CHECK(protocol != NULL, "protocol %s is not in the table", protocol_name);
    if(!protocol) return;

    Slot slot;
    slot_open(&slot, protocol);
    char* raw = read_fixture(raw_name);
    size_t pulses = replay_raw(raw, feed_one, &slot);
    free(raw);
    CHECK(pulses > 50, "%s: only %zu pulses in fixture", raw_name, pulses);
    CHECK(slot.decoded >= 1, "%s: no frame decoded from %zu pulses", protocol_name, pulses);
    if(slot.decoded) covered_real[protocol_index(protocol)] = true;

    if(slot.first && key_name) {
        char* key_text = read_fixture(key_name);
        FlipperFormat* expected = flipper_format_string_alloc();
        furi_check(ff_host_load_text(expected, key_text));
        free(key_text);
        static const char* const fields[] = {"Protocol", "Id", "Bit", "Data", "Batt",
                                             "Hum",      "Ch", "Btn", "Temp"};
        for(size_t i = 0; i < COUNT_OF(fields); i++) {
            CHECK(same_field(slot.first, expected, fields[i]),
                  "%s: field %s decoded as '%s', key file has '%s'", protocol_name, fields[i],
                  ff_host_value(slot.first, fields[i]) ? ff_host_value(slot.first, fields[i]) : "(none)",
                  ff_host_value(expected, fields[i]) ? ff_host_value(expected, fields[i]) : "(none)");
        }
        flipper_format_free(expected);
    }
    if(slot.first) {
        printf("    decoded %u frame(s): Id=%s Ch=%s Temp=%s Hum=%s Batt=%s\n", slot.decoded,
               ff_host_value(slot.first, "Id"), ff_host_value(slot.first, "Ch"),
               ff_host_value(slot.first, "Temp"), ff_host_value(slot.first, "Hum"),
               ff_host_value(slot.first, "Batt"));
    }
    slot_close(&slot);
}

/* The port's key-file header: field order and no Lat/Lon. */
static void test_header_layout(void) {
    printf("key-file header layout\n");
    Slot slot;
    slot_open(&slot, find_protocol("Bresser-3CH"));
    char* raw = read_fixture("bresser_3ch_raw.sub");
    replay_raw(raw, feed_one, &slot);
    free(raw);
    CHECK(slot.first != NULL, "no decode to inspect");
    if(slot.first) {
        static const char* const order[] = {"Filetype", "Version", "Frequency", "Preset",
                                            "Protocol", "Id",      "Bit",       "Data",
                                            "Batt",     "Hum",     "Ts",        "Ch",
                                            "Btn",      "Temp"};
        CHECK(ff_host_count(slot.first) == COUNT_OF(order), "expected %zu fields, got %zu",
              COUNT_OF(order), ff_host_count(slot.first));
        for(size_t i = 0; i < COUNT_OF(order) && i < ff_host_count(slot.first); i++) {
            CHECK(strcmp(ff_host_key_at(slot.first, i), order[i]) == 0,
                  "field %zu is '%s', expected '%s'", i, ff_host_key_at(slot.first, i), order[i]);
        }
        CHECK(strcmp(ff_host_value(slot.first, "Filetype"), "Flipper SubGhz Key File") == 0,
              "wrong file type");
        CHECK(strcmp(ff_host_value(slot.first, "Preset"), "FuriHalSubGhzPresetOok650Async") == 0,
              "preset name not translated: %s", ff_host_value(slot.first, "Preset"));
        CHECK(strcmp(ff_host_value(slot.first, "Frequency"), "433920000") == 0, "wrong frequency");
        CHECK(ff_host_value(slot.first, "Lat") == NULL, "unexpected Lat field");
    }
    slot_close(&slot);
}

/* An upstream key file (which also carries Latitute/Longitude or Lat/Lon lines
 * this port does not write) loads, and saving it again keeps the reading. */
static void test_key_roundtrip(const char* key_name) {
    printf("key file %-23s load -> save -> compare\n", key_name);
    char* text = read_fixture(key_name);
    FlipperFormat* original = flipper_format_string_alloc();
    furi_check(ff_host_load_text(original, text));
    free(text);
    const char* protocol_name = ff_host_value(original, "Protocol");
    const SubGhzProtocol* protocol = protocol_name ? find_protocol(protocol_name) : NULL;
    CHECK(protocol != NULL, "%s: unknown protocol '%s'", key_name, protocol_name ? protocol_name : "");
    if(!protocol) {
        flipper_format_free(original);
        return;
    }

    Slot slot;
    slot_open(&slot, protocol);
    SubGhzProtocolStatus status = protocol->decoder->deserialize(slot.base, original);
    CHECK(status == SubGhzProtocolStatusOk, "%s: deserialize returned %d", key_name, (int)status);

    FlipperFormat* saved = flipper_format_string_alloc();
    SubGhzRadioPreset preset = make_preset();
    status = protocol->decoder->serialize(slot.base, saved, &preset);
    furi_string_free(preset.name);
    CHECK(status == SubGhzProtocolStatusOk, "%s: serialize returned %d", key_name, (int)status);
    /* "Ts" is deliberately not compared: ws_block_generic_serialize() stamps the
     * time of saving (upstream behaviour), which is checked on its own here. */
    static const char* const fields[] = {"Protocol", "Id", "Bit", "Data", "Batt",
                                         "Hum",      "Ch", "Btn", "Temp"};
    char now[16];
    snprintf(now, sizeof(now), "%lu", (unsigned long)host_rtc_timestamp);
    CHECK(ff_host_value(saved, "Ts") && strcmp(ff_host_value(saved, "Ts"), now) == 0,
          "%s: saved Ts is '%s', expected the RTC time %s", key_name,
          ff_host_value(saved, "Ts") ? ff_host_value(saved, "Ts") : "(none)", now);
    for(size_t i = 0; i < COUNT_OF(fields); i++) {
        CHECK(same_field(saved, original, fields[i]), "%s: %s changed across load/save ('%s' -> '%s')",
              key_name, fields[i],
              ff_host_value(original, fields[i]) ? ff_host_value(original, fields[i]) : "(none)",
              ff_host_value(saved, fields[i]) ? ff_host_value(saved, fields[i]) : "(none)");
    }

    FuriString* shown = furi_string_alloc();
    protocol->decoder->get_string(slot.base, shown);
    CHECK(strstr(furi_string_get_cstr(shown), protocol->name) != NULL,
          "%s: get_string does not name the protocol", key_name);
    CHECK(furi_string_size(shown) < 256, "%s: get_string is %zu bytes", key_name,
          furi_string_size(shown));
    furi_string_free(shown);

    flipper_format_free(saved);
    flipper_format_free(original);
    slot_close(&slot);
}

/* Truncated and corrupt key files are rejected, not half-loaded. */
static void test_key_rejects_damage(void) {
    printf("damaged key files are rejected\n");
    const SubGhzProtocol* protocol = find_protocol("Bresser-3CH");
    static const char* const damaged[] = {
        /* missing Data */
        "Filetype: Flipper SubGhz Key File\nVersion: 1\nProtocol: Bresser-3CH\nId: 23\nBit: 40\n",
        /* Data too short */
        "Protocol: Bresser-3CH\nId: 23\nBit: 40\nData: 00 00 00\nBatt: 0\nHum: 64\nTs: 1\nCh: 2\nBtn: 0\nTemp: 18.2\n",
        /* non-numeric Bit */
        "Protocol: Bresser-3CH\nId: 23\nBit: forty\nData: 00 00 00 17 26 0C 40 89\nBatt: 0\nHum: 64\nTs: 1\nCh: 2\nBtn: 0\nTemp: 18.2\n",
        /* wrong bit count for this protocol */
        "Protocol: Bresser-3CH\nId: 23\nBit: 12\nData: 00 00 00 17 26 0C 40 89\nBatt: 0\nHum: 64\nTs: 1\nCh: 2\nBtn: 0\nTemp: 18.2\n",
        /* fields out of order: Temp before Id */
        "Protocol: Bresser-3CH\nTemp: 18.2\nBit: 40\nData: 00 00 00 17 26 0C 40 89\nBatt: 0\nHum: 64\nTs: 1\nCh: 2\nBtn: 0\nId: 23\n",
        "",
    };
    for(size_t i = 0; i < COUNT_OF(damaged); i++) {
        Slot slot;
        slot_open(&slot, protocol);
        FlipperFormat* ff = flipper_format_string_alloc();
        furi_check(ff_host_load_text(ff, damaged[i]));
        SubGhzProtocolStatus status = protocol->decoder->deserialize(slot.base, ff);
        CHECK(status != SubGhzProtocolStatusOk, "damaged key file #%zu was accepted", i);
        flipper_format_free(ff);
        slot_close(&slot);
    }
}

/* Upstream's mixed capture (test_random_raw.sub: ~95k pulses of real traffic
 * from many unrelated remotes, which upstream's full registry decodes 328
 * times) through all 25 weather decoders at once, as the receiver runs them.
 * It must not crash or trip an assert, and only `allowed` may report frames:
 * Oregon2 reports one. Oregon2 has no working integrity check (it receives a
 * checksum and ignores it), so that frame may be a real Oregon sensor in the
 * capture or a false positive; it is pinned here as known behaviour. Any other
 * decoder reporting a frame is a regression. */
static void test_noise(const char* raw_name, const char* allowed, unsigned max_expected) {
    printf("noise   %-24s through %zu decoders\n", raw_name, (size_t)WS_COUNT);
    Slot slots[WS_COUNT];
    for(size_t i = 0; i < WS_COUNT; i++) slot_open(&slots[i], ws_protocols[i]);
    char* raw = read_fixture(raw_name);
    size_t pulses = replay_raw(raw, feed_all, slots);
    free(raw);
    unsigned total = 0;
    for(size_t i = 0; i < WS_COUNT; i++) {
        if(slots[i].decoded) {
            printf("    %-16s %u frame(s)\n", ws_protocols[i]->name, slots[i].decoded);
            CHECK(strcmp(ws_protocols[i]->name, allowed) == 0,
                  "%s decoded %u frame(s) from a capture that has none of its signals",
                  ws_protocols[i]->name, slots[i].decoded);
        }
        total += slots[i].decoded;
    }
    printf("    %zu pulses, %u decode(s) in total\n", pulses, total);
    CHECK(pulses > 1000, "noise fixture too small");
    CHECK(total <= max_expected, "%u decodes from the noise capture (limit %u)", total, max_expected);
    for(size_t i = 0; i < WS_COUNT; i++) slot_close(&slots[i]);
}

/* Deterministic pseudo-random pulse trains, including extreme durations and
 * mid-stream resets. A robustness check: survive, do not hang, do not assert. */
static void test_fuzz(void) {
    printf("fuzz    2,000,000 synthetic pulses through %zu decoders\n", (size_t)WS_COUNT);
    Slot slots[WS_COUNT];
    for(size_t i = 0; i < WS_COUNT; i++) slot_open(&slots[i], ws_protocols[i]);
    uint32_t state = 0x1234ABCDu;
    bool level = true;
    for(uint32_t n = 0; n < 2000000u; n++) {
        state = state * 1664525u + 1013904223u;
        uint32_t pick = state >> 24;
        uint32_t duration;
        if(pick < 180) {
            duration = 100 + ((state >> 8) % 2400); /* plausible symbol lengths */
        } else if(pick < 240) {
            duration = 2400 + ((state >> 8) % 12000); /* gaps and sync pulses */
        } else if(pick < 250) {
            duration = (state >> 8) % 40; /* glitches, including 0 */
        } else {
            duration = 0xFFFFFFFFu - ((state >> 8) % 1000); /* overflow territory */
        }
        if((state & 0x3FF) != 0) level = !level; /* occasionally repeat a level */
        feed_all(level, duration, slots);
        if((n % 250000u) == 249999u) {
            for(size_t i = 0; i < WS_COUNT; i++) {
                slots[i].protocol->decoder->reset(slots[i].base);
            }
        }
    }
    unsigned total = 0;
    for(size_t i = 0; i < WS_COUNT; i++) {
        if(slots[i].decoded) {
            printf("    %-16s %u accidental frame(s)\n", ws_protocols[i]->name, slots[i].decoded);
        }
        total += slots[i].decoded;
        slot_close(&slots[i]);
    }
    printf("    survived, %u accidental decode(s)\n", total);
    checks++;
}

/* Registry sanity for the 25 entries: unique names, receive path complete. */
static void test_table(void) {
    printf("protocol table\n");
    for(size_t i = 0; i < WS_COUNT; i++) {
        const SubGhzProtocol* p = ws_protocols[i];
        CHECK(p->name && p->name[0], "entry %zu has no name", i);
        CHECK(p->decoder && p->decoder->alloc && p->decoder->free && p->decoder->feed &&
                  p->decoder->reset && p->decoder->serialize && p->decoder->deserialize &&
                  p->decoder->get_string,
              "%s: decoder interface incomplete", p->name);
        CHECK((p->flag & SubGhzProtocolFlag_Decodable) != 0, "%s: not flagged Decodable", p->name);
        CHECK(p->filter == SubGhzProtocolFilter_Weather, "%s: not tagged as Weather", p->name);
        for(size_t j = i + 1; j < WS_COUNT; j++) {
            CHECK(strcmp(p->name, ws_protocols[j]->name) != 0, "duplicate name %s", p->name);
        }
        /* Imperial display path must not crash either. */
        Slot slot;
        slot_open(&slot, p);
        FuriString* shown = furi_string_alloc();
        host_locale_units = FuriHalRtcLocaleUnitsImperial;
        p->decoder->get_string(slot.base, shown);
        host_locale_units = FuriHalRtcLocaleUnitsMetric;
        furi_string_free(shown);
        slot_close(&slot);
    }
}

/* ---------------------------------------------------- synthetic fixtures --- */

/* One entry per case of make_synthetic_fixtures.py. The waveform, the encoded
 * reading (<slug>_synth.sub) and the reasoning behind both live in the
 * generator; this table only says what the test may expect of each case. */
typedef struct {
    const char* slug; /* fixtures/synthetic/<slug>_synth*.sub */
    const char* protocol; /* decoder the waveform was built for */
    bool integrity; /* decoder has a checksum/CRC/parity, so a corrupt fixture exists */
    unsigned unchecked; /* fields the expected file leaves out on purpose (see README.md) */
    bool reloads; /* the key file saved from a decode loads again (false: known defect, reported only) */
} SynthCase;

static const SynthCase synth_cases[] = {
    {"infactory", "inFactory-TH", true, 0, true},
    {"thermopro_tx4", "ThermoPRO-TX4", false, 0, true},
    {"nexus_th", "Nexus-TH", false, 0, true},
    {"gt_wt_02", "GT-WT02", true, 0, true},
    {"gt_wt_02_id9", "GT-WT02", true, 0, true}, /* see synth_allowed_overlaps */
    {"gt_wt_03", "GT-WT03", true, 0, true},
    {"acurite_606tx", "Acurite-606TX", true, 0, true},
    {"acurite_609txc", "Acurite-609TXC", true, 0, true},
    {"acurite_986", "Acurite-986", true, 0, true},
    {"lacrosse_tx", "LaCrosse_TX", true, 0, true},
    {"lacrosse_tx_hum", "LaCrosse_TX", true, 1, true}, /* Temp: not part of a humidity message */
    {"lacrosse_tx141thbv2", "TX141THBv2", true, 0, true},
    {"ambient_weather", "Ambient_Weather", true, 0, true},
    /* Auriol AHFL, Batt: code contradicts its header comment. Nibble sums 50, 87 and 75; the
     * last is the frame printed in the header of auriol_ahfl.c. Upstream compared the 6-bit
     * checksum with the unmasked sum and so refused the second and third. */
    {"auriol_ahfl", "Auriol AHFL", true, 1, true},
    {"auriol_ahfl_high_sum", "Auriol AHFL", true, 1, true},
    {"auriol_ahfl_header_example", "Auriol AHFL", true, 1, true},
    {"auriol_hg06061", "Auriol HG06061", false, 2, true}, /* Id, Batt: shifts do not fit the frame */
    {"emos_e601x", "EMOS E601x", true, 1, true}, /* Batt: read from the checksum byte */
    {"kedsum_th", "Kedsum-TH", true, 0, true},
    {"tx_8300", "TX8300", true, 0, true},
    {"oregon_v1", "Oregon-v1", true, 1, true}, /* Batt: read from the un-reversed word */
    {"wendox_w6726", "Wendox W6726", true, 0, true},
    {"acurite_5n1", "Acurite 5n1", true, 0, true},
    {"oregon2", "Oregon2", false, 0, true},
    {"oregon3", "Oregon3", false, 0, true},
};

/* Genuine protocol overlaps: fixtures that a second decoder is entitled to decode
 * as well, because on air the two protocols cannot be told apart. Everything
 * not listed here is a failure when it cross-decodes. */
typedef struct {
    const char* slug; /* the fixture */
    const char* also; /* the other decoder that fires on it */
} AllowedOverlap;

static const AllowedOverlap synth_allowed_overlaps[] = {
    /* GT-WT02 and ThermoPRO-TX4 are the same waveform: 37 bits, 500 us pulse,
     * 2000/4000 us gaps, 9000 us sync. GT-WT02 has a checksum; ThermoPRO-TX4 has
     * none and takes any frame whose first nibble is 1001 or 0110. In a GT-WT02
     * frame that nibble is the top of the random sensor id, so one GT-WT02 sensor
     * in eight is also reported as a ThermoPRO-TX4 with nonsense values (here
     * -65.0 C, 254 %). The reverse needs a 1-in-64 checksum accident. */
    {"gt_wt_02_id9", "ThermoPRO-TX4"},
};

static bool overlap_allowed(const char* slug, const char* also) {
    for(size_t i = 0; i < COUNT_OF(synth_allowed_overlaps); i++) {
        if(strcmp(synth_allowed_overlaps[i].slug, slug) == 0 &&
           strcmp(synth_allowed_overlaps[i].also, also) == 0) {
            return true;
        }
    }
    return false;
}

static const char* const reading_fields[] = {"Protocol", "Id",  "Bit", "Data", "Batt",
                                             "Hum",      "Ch",  "Btn", "Temp"};

/* Compare a decode with the reading the generator encoded. Returns how many of
 * the standard fields the expected file does not carry. */
static unsigned check_reading(
    const FlipperFormat* decoded,
    const FlipperFormat* expected,
    const char* what) {
    unsigned absent = 0;
    for(size_t i = 0; i < COUNT_OF(reading_fields); i++) {
        const char* key = reading_fields[i];
        if(!ff_host_value(expected, key)) {
            absent++;
            continue;
        }
        CHECK(field_matches(decoded, expected, key, 0.05f), "%s: %s decoded as '%s', encoded '%s'",
              what, key, shown(decoded, key), shown(expected, key));
    }
    return absent;
}

static const char* asserted(const FlipperFormat* expected, const char* key) {
    const char* value = ff_host_value(expected, key);
    return value ? value : "unchecked";
}

static void synth_name(char* out, size_t size, const SynthCase* c, const char* suffix) {
    snprintf(out, size, "synthetic/%s_synth%s.sub", c->slug, suffix);
}

/* What the receiver saves after a decode must load again and show the same reading. */
static void check_saved_key_reloads(
    const SynthCase* c,
    const SubGhzProtocol* protocol,
    FlipperFormat* saved) {
    Slot slot;
    slot_open(&slot, protocol);
    SubGhzProtocolStatus status = protocol->decoder->deserialize(slot.base, saved);
    if(!c->reloads) {
        /* Reported, not counted: README.md, "Findings". */
        printf("    known issue: the key file saved from this decode %s (status %d)\n",
               status == SubGhzProtocolStatusOk ? "now loads; set `reloads`" : "is refused on load",
               (int)status);
        slot_close(&slot);
        return;
    }
    CHECK(status == SubGhzProtocolStatusOk, "%s: its own saved key file does not load (%d)",
          c->slug, (int)status);
    if(status == SubGhzProtocolStatusOk) {
        FlipperFormat* again = flipper_format_string_alloc();
        SubGhzRadioPreset preset = make_preset();
        status = protocol->decoder->serialize(slot.base, again, &preset);
        furi_string_free(preset.name);
        CHECK(status == SubGhzProtocolStatusOk, "%s: serialize after load returned %d", c->slug,
              (int)status);
        CHECK(ff_host_count(again) == ff_host_count(saved), "%s: %zu fields saved, %zu after reload",
              c->slug, ff_host_count(saved), ff_host_count(again));
        for(size_t i = 0; i < ff_host_count(saved) && i < ff_host_count(again); i++) {
            const char* key = ff_host_key_at(saved, i);
            CHECK(strcmp(ff_host_key_at(again, i), key) == 0 && same_field(again, saved, key),
                  "%s: %s changed across save/load ('%s' -> '%s')", c->slug, key,
                  shown(saved, key), shown(again, key));
        }
        flipper_format_free(again);
    }
    slot_close(&slot);
}

/* The fixture through its own decoder only. */
static unsigned synth_alone(
    const SynthCase* c,
    const SubGhzProtocol* protocol,
    const FlipperFormat* expected,
    const char* suffix) {
    char name[160];
    synth_name(name, sizeof(name), c, suffix);
    Slot slot;
    slot_open(&slot, protocol);
    char* raw = read_fixture(name);
    size_t pulses = replay_raw(raw, feed_one, &slot);
    free(raw);
    CHECK(pulses > 50, "%s: only %zu pulses", name, pulses);
    unsigned decoded = slot.decoded;
    if(expected) {
        CHECK(decoded >= 1, "%s: %s decoded nothing from %zu pulses", name, c->protocol, pulses);
        if(slot.first) {
            unsigned absent = check_reading(slot.first, expected, name);
            CHECK(absent == c->unchecked, "%s: expected file leaves out %u field(s), table says %u",
                  name, absent, c->unchecked);
            if(strcmp(suffix, "_raw") == 0) check_saved_key_reloads(c, protocol, slot.first);
        }
    } else {
        CHECK(decoded == 0, "%s: %s accepted %u frame(s) with a broken checksum", name,
              c->protocol, decoded);
    }
    slot_close(&slot);
    return decoded;
}

/* The fixture through all 25 decoders at once, as the receiver runs them. Any
 * decoder other than the intended one that reports a frame is a cross-decode:
 * always printed, and a failure unless the pair is in synth_allowed_overlaps. */
static void synth_together(
    const SynthCase* c,
    const SubGhzProtocol* protocol,
    const FlipperFormat* expected,
    const char* suffix) {
    char name[160];
    synth_name(name, sizeof(name), c, suffix);
    Slot slots[WS_COUNT];
    for(size_t i = 0; i < WS_COUNT; i++) slot_open(&slots[i], ws_protocols[i]);
    char* raw = read_fixture(name);
    replay_raw(raw, feed_all, slots);
    free(raw);
    for(size_t i = 0; i < WS_COUNT; i++) {
        if(ws_protocols[i] == protocol) {
            if(expected) {
                CHECK(slots[i].decoded >= 1, "%s: %s decoded nothing while the other decoders ran",
                      name, c->protocol);
                if(slots[i].first) check_reading(slots[i].first, expected, name);
            } else {
                CHECK(slots[i].decoded == 0, "%s: %s accepted a broken checksum", name, c->protocol);
            }
        } else if(slots[i].decoded) {
            bool allowed = overlap_allowed(c->slug, ws_protocols[i]->name);
            printf("    %s %s: also decoded by %s (%u frame(s), Id=%s Temp=%s Hum=%s)\n",
                   allowed ? "known overlap" : "CROSS-DECODE", name, ws_protocols[i]->name,
                   slots[i].decoded, shown(slots[i].first, "Id"), shown(slots[i].first, "Temp"),
                   shown(slots[i].first, "Hum"));
            CHECK(allowed, "%s is meant for %s but %s decoded it too", name, c->protocol,
                  ws_protocols[i]->name);
        } else if(expected && overlap_allowed(c->slug, ws_protocols[i]->name)) {
            printf("    note: %s no longer decodes %s; drop the synth_allowed_overlaps entry\n",
                   ws_protocols[i]->name, name);
        }
    }
    for(size_t i = 0; i < WS_COUNT; i++) slot_close(&slots[i]);
}

static void test_synthetic(const SynthCase* c) {
    printf("synth   %-22s -> %s\n", c->slug, c->protocol);
    const SubGhzProtocol* protocol = find_protocol(c->protocol);
    CHECK(protocol != NULL, "protocol %s is not in the table", c->protocol);
    if(!protocol) return;

    char name[160];
    synth_name(name, sizeof(name), c, "");
    FlipperFormat* expected = load_key_fixture(name);
    CHECK(strcmp(shown(expected, "Protocol"), c->protocol) == 0, "%s is for %s, table says %s",
          name, shown(expected, "Protocol"), c->protocol);

    unsigned nominal = synth_alone(c, protocol, expected, "_raw");
    unsigned jitter = synth_alone(c, protocol, expected, "_jitter_raw");
    synth_together(c, protocol, expected, "_raw");
    if(nominal) covered_synthetic[protocol_index(protocol)] = true;
    printf("    %u frame(s) nominal, %u with jitter: Id=%s Ch=%s Temp=%s Hum=%s Batt=%s Btn=%s\n",
           nominal, jitter, asserted(expected, "Id"), asserted(expected, "Ch"),
           asserted(expected, "Temp"), asserted(expected, "Hum"), asserted(expected, "Batt"),
           asserted(expected, "Btn"));

    if(c->integrity) {
        synth_alone(c, protocol, NULL, "_corrupt_raw");
        synth_together(c, protocol, NULL, "_corrupt_raw");
    }
    flipper_format_free(expected);
}

/* LaCrosse_TX frames with a valid checksum whose message type is neither
 * temperature (0) nor humidity (E): all 14 other values, two frames of each.
 * Upstream called furi_crash() in ws_protocol_lacrosse_tx_remote_controller()
 * for these, so anything on air with the right 4-bit sum halted the device.
 * Here furi_crash() aborts the test process, so merely getting past the replay
 * is the main assertion; the decoder must also report nothing, and must still
 * decode a normal frame afterwards. */
static void test_lacrosse_tx_unknown_type(void) {
    const char* name = "synthetic/lacrosse_tx_unknown_type_raw.sub";
    printf("reject  %s\n", name);
    const SubGhzProtocol* protocol = find_protocol("LaCrosse_TX");
    furi_check(protocol);

    Slot slot;
    slot_open(&slot, protocol);
    char* raw = read_fixture(name);
    size_t pulses = replay_raw(raw, feed_one, &slot);
    /* 14 types x 2 frames x 44 bits x (pulse + gap), plus the lead-in */
    CHECK(pulses == 14 * 2 * 44 * 2 + 1, "%s: %zu pulses, expected 28 whole frames", name, pulses);
    CHECK(slot.decoded == 0, "%s: LaCrosse_TX reported %u frame(s) of a type it does not know",
          name, slot.decoded);

    /* Same decoder instance, now a frame it does know: it was not left stuck. */
    char* good = read_fixture("synthetic/lacrosse_tx_synth_raw.sub");
    replay_raw(good, feed_one, &slot);
    free(good);
    CHECK(slot.decoded >= 1, "LaCrosse_TX decodes nothing after the unknown-type frames");
    if(slot.first) {
        FlipperFormat* expected = load_key_fixture("synthetic/lacrosse_tx_synth.sub");
        check_reading(slot.first, expected, "lacrosse_tx_synth_raw.sub after unknown types");
        flipper_format_free(expected);
    }
    slot_close(&slot);

    /* And with the whole receiver running: nobody else mistakes them for a reading. */
    Slot slots[WS_COUNT];
    for(size_t i = 0; i < WS_COUNT; i++) slot_open(&slots[i], ws_protocols[i]);
    replay_raw(raw, feed_all, slots);
    for(size_t i = 0; i < WS_COUNT; i++) {
        CHECK(slots[i].decoded == 0, "%s: %s reported %u frame(s)", name, ws_protocols[i]->name,
              slots[i].decoded);
        slot_close(&slots[i]);
    }
    free(raw);
    printf("    %zu pulses, 28 well-formed frames of 14 unknown types: no abort, no decode\n",
           pulses);
}

/* ------------------------------------------------- encoder -> decoder loop --- */

/* Load a key file into the protocol's own (upstream) encoder, play everything
 * the encoder yields into the decoder, and expect the reading back.
 *
 * `tail` says whether the test adds the end-of-frame mark a real Acurite sensor
 * sends (a short pulse and a long gap, see fixtures/acurite_592txr.sub). The
 * upstream Acurite 592TXR encoder stops after the last data bit, and without
 * that mark its own decoder never emits a frame; README.md, "Findings". */
static void test_encoder_roundtrip(const char* key_name, const char* protocol_name, bool tail) {
    printf("encoder %-24s -> %s decoder\n", key_name, protocol_name);
    const SubGhzProtocol* protocol = find_protocol(protocol_name);
    CHECK(protocol != NULL, "protocol %s is not in the table", protocol_name);
    if(!protocol) return;
    const SubGhzProtocolEncoder* encoder = protocol->encoder;
    CHECK(encoder && encoder->alloc && encoder->free && encoder->deserialize && encoder->yield &&
              encoder->stop,
          "%s: encoder interface incomplete", protocol_name);
    if(!encoder || !encoder->alloc) return;

    FlipperFormat* key = load_key_fixture(key_name);
    void* context = encoder->alloc(NULL);
    furi_check(context);
    SubGhzProtocolStatus status = encoder->deserialize(context, key);
    CHECK(status == SubGhzProtocolStatusOk, "%s: encoder deserialize returned %d", key_name,
          (int)status);

    Slot slot;
    slot_open(&slot, protocol);
    size_t pulses = 0;
    uint64_t on_air_us = 0;
    bool alternating = true;
    bool last_level = false;
    while(status == SubGhzProtocolStatusOk && pulses < 100000) {
        LevelDuration step = encoder->yield(context);
        if(level_duration_is_reset(step)) break;
        bool level = level_duration_get_level(step);
        if(pulses && level == last_level) alternating = false;
        last_level = level;
        on_air_us += level_duration_get_duration(step);
        feed_one(level, level_duration_get_duration(step), &slot);
        pulses++;
    }
    CHECK(pulses > 100 && pulses < 100000, "%s: encoder yielded %zu pulses", key_name, pulses);
    CHECK(alternating, "%s: encoder yielded two pulses of the same level in a row", key_name);
    CHECK(level_duration_is_reset(encoder->yield(context)), "%s: encoder restarted after its end",
          key_name);
    unsigned unaided = slot.decoded;
    if(tail) {
        feed_one(true, 200, &slot);
        feed_one(false, 2200, &slot);
    }
    printf("    %zu pulses, %lu ms on air; %u frame(s) decoded from the encoder output alone%s\n",
           pulses, (unsigned long)(on_air_us / 1000), unaided,
           tail ? ", then end mark added" : "");
    CHECK(slot.decoded >= 1, "%s: decoder saw no frame in its own encoder's output", key_name);
    if(slot.first) {
        check_reading(slot.first, key, key_name);
        covered_roundtrip[protocol_index(protocol)] = true;
    }
    slot_close(&slot);
    encoder->stop(context);
    CHECK(level_duration_is_reset(encoder->yield(context)), "%s: encoder runs after stop()",
          key_name);
    encoder->free(context);
    flipper_format_free(key);
}

/* ------------------------------------------------------------------ probes --- */

/* Waveforms we believe a real sensor could send but the decoder turns down.
 * Reported, never counted: each one is a finding described in README.md, kept
 * here so that a fix upstream shows up as a changed number. */
static void probe(const char* raw_name, const char* protocol_name, const char* what) {
    Slot slot;
    slot_open(&slot, find_protocol(protocol_name));
    char* raw = read_fixture(raw_name);
    replay_raw(raw, feed_one, &slot);
    free(raw);
    printf("probe   %s: %s decoded %u frame(s) -- %s\n", raw_name, protocol_name, slot.decoded,
           what);
    slot_close(&slot);
}

/* ---------------------------------------------------------------- coverage --- */

static void test_coverage(void) {
    unsigned real = 0, synthetic = 0, uncovered = 0, roundtrip = 0;
    for(size_t i = 0; i < WS_COUNT; i++) {
        CHECK(covered_real[i] || covered_synthetic[i], "%s has no fixture-backed positive test",
              ws_protocols[i]->name);
        if(covered_real[i]) {
            real++;
        } else if(covered_synthetic[i]) {
            synthetic++;
        } else {
            uncovered++;
        }
        if(covered_roundtrip[i]) roundtrip++;
    }
    printf("no checksum, CRC or parity at all (a damaged frame cannot be told from a good one):");
    for(size_t i = 0; i < COUNT_OF(synth_cases); i++) {
        if(!synth_cases[i].integrity) printf(" %s;", synth_cases[i].protocol);
    }
    printf("\n");
    /* run.py reads this line. */
    printf("coverage: decoders=%zu real=%u synthetic=%u uncovered=%u roundtrip=%u\n",
           (size_t)WS_COUNT, real, synthetic, uncovered, roundtrip);
}

/* `test_ws_decode --replay file.sub ...`: what every decoder makes of a file. */
static int replay_files(int count, char** paths) {
    fixtures_dir = "";
    for(int n = 0; n < count; n++) {
        Slot slots[WS_COUNT];
        for(size_t i = 0; i < WS_COUNT; i++) slot_open(&slots[i], ws_protocols[i]);
        char* raw = read_fixture(paths[n]);
        size_t pulses = replay_raw(raw, feed_all, slots);
        free(raw);
        printf("%s: %zu pulses\n", paths[n], pulses);
        for(size_t i = 0; i < WS_COUNT; i++) {
            if(slots[i].decoded) {
                printf("  %-16s %u frame(s):", ws_protocols[i]->name, slots[i].decoded);
                for(size_t f = 1; f < COUNT_OF(reading_fields); f++) {
                    printf(" %s=%s", reading_fields[f], shown(slots[i].first, reading_fields[f]));
                }
                printf("\n");
            }
            slot_close(&slots[i]);
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    if(argc > 2 && strcmp(argv[1], "--replay") == 0) return replay_files(argc - 2, argv + 2);
    if(argc > 1) fixtures_dir = argv[1];

    test_table();
    test_capture("bresser_3ch_raw.sub", "bresser_3ch.sub", "Bresser-3CH");
    test_capture("bresser_3ch_v0_raw.sub", "bresser_3ch_v0.sub", "Bresser-3CH");
    test_capture("solight_te44_raw.sub", "solight_te44.sub", "Solight TE44");
    test_capture("vauno_en8822c_raw.sub", "vauno_en8822c.sub", "Vauno-EN8822C");
    /* The key file is the capture's reading re-encoded by make_synthetic_fixtures.py:
     * a real frame confirming the generator's bit layout, parity and checksum. */
    test_capture("acurite_592txr.sub", "synthetic/acurite_592txr_key.sub", "Acurite 592TXR");
    test_header_layout();
    test_key_roundtrip("bresser_3ch.sub");
    test_key_roundtrip("bresser_3ch_v0.sub");
    test_key_roundtrip("solight_te44.sub");
    test_key_roundtrip("vauno_en8822c.sub");
    test_key_rejects_damage();
    test_noise("test_random_raw.sub", "Oregon2", 1);
    test_fuzz();

    for(size_t i = 0; i < COUNT_OF(synth_cases); i++) test_synthetic(&synth_cases[i]);
    test_lacrosse_tx_unknown_type();
    test_encoder_roundtrip("synthetic/nexus_th_synth.sub", "Nexus-TH", false);
    test_encoder_roundtrip("synthetic/acurite_592txr_key.sub", "Acurite 592TXR", true);
    test_key_roundtrip("synthetic/nexus_th_synth.sub");
    test_key_roundtrip("synthetic/acurite_592txr_key.sub");
    probe("synthetic/probe_oregon3_24_ones_raw.sub", "Oregon3",
          "preamble of 24 ones as documented for v3.0 (the suite sends 25)");
    probe("synthetic/probe_oregon_v1_first_bit_1_raw.sub", "Oregon-v1",
          "plain Manchester frame whose first bit is 1");
    test_coverage();

    printf("%d checks, %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}
