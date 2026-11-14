/* Host tests for the weather-station decoders ported from Momentum.
 *
 * Real captures (tests/subghz_ws/fixtures, from Momentum's unit-test resources)
 * are replayed pulse by pulse through the same feed() entry point the receiver
 * uses on the device. Nothing here touches a radio.
 *
 *   test_ws_decode <fixtures-dir>
 */
#include <furi.h>
#include <furi_hal.h>
#include <lib/flipper_format/flipper_format.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/blocks/generic.h>

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
    snprintf(path, sizeof(path), "%s/%s", fixtures_dir, name);
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

static bool same_field(const FlipperFormat* a, const FlipperFormat* b, const char* key) {
    const char* left = ff_host_value(a, key);
    const char* right = ff_host_value(b, key);
    if(!left || !right) return false;
    if(strcmp(key, "Temp") == 0) return fabsf(strtof(left, NULL) - strtof(right, NULL)) < 0.01f;
    return strcmp(left, right) == 0;
}

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
 * the capture holds one frame that passes Oregon2's sync and checksum. Anything
 * else decoding here is a false positive. */
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

int main(int argc, char** argv) {
    if(argc > 1) fixtures_dir = argv[1];

    test_table();
    test_capture("bresser_3ch_raw.sub", "bresser_3ch.sub", "Bresser-3CH");
    test_capture("bresser_3ch_v0_raw.sub", "bresser_3ch_v0.sub", "Bresser-3CH");
    test_capture("solight_te44_raw.sub", "solight_te44.sub", "Solight TE44");
    test_capture("vauno_en8822c_raw.sub", "vauno_en8822c.sub", "Vauno-EN8822C");
    test_capture("acurite_592txr.sub", NULL, "Acurite 592TXR");
    test_header_layout();
    test_key_roundtrip("bresser_3ch.sub");
    test_key_roundtrip("bresser_3ch_v0.sub");
    test_key_roundtrip("solight_te44.sub");
    test_key_roundtrip("vauno_en8822c.sub");
    test_key_rejects_damage();
    test_noise("test_random_raw.sub", "Oregon2", 1);
    test_fuzz();

    printf("%d checks, %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}
