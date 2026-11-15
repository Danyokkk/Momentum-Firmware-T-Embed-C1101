/* Host replay test for the POCSAG pager decoder (receive only).
 *
 *   test_pocsag_decode <fixtures-dir>
 *
 * Each fixture is an edge stream produced by pocsag_encode.py, an independent
 * POCSAG framer (not the decoder's own code). Feeding it to the decoder and
 * recovering the address and message text cross-checks the sync lock, batch
 * handling and character assembly. The decoder performs no BCH error
 * correction, and these fixtures are synthetic, so this is not proof against a
 * real pager transmission; on-air reception is a separate hardware check.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pocsag.h"

/* Link-only: pcsg_generic's serialize path references this (defined in
 * blocks/generic.c, which is not compiled here because it uses furi_string
 * overloads this light stub does not carry). The decode tests never serialize,
 * so it is never called. */
SubGhzProtocolStatus subghz_block_generic_serialize_common(
    const char* protocol_name,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    (void)protocol_name;
    (void)flipper_format;
    (void)preset;
    return SubGhzProtocolStatusError;
}

static int failures = 0;

#define CHECK(cond, ...)                      \
    do {                                      \
        if(!(cond)) {                         \
            printf("FAIL: ");                 \
            printf(__VA_ARGS__);              \
            printf("\n");                     \
            failures++;                       \
        }                                     \
    } while(0)

static char* read_file(const char* dir, const char* name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE* file = fopen(path, "rb");
    if(!file) {
        printf("FAIL: cannot open %s\n", path);
        failures++;
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char* text = malloc((size_t)size + 1);
    if(fread(text, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        free(text);
        return NULL;
    }
    text[size] = '\0';
    fclose(file);
    return text;
}

/* The decoder resets its state right after firing this callback, so the app
 * reads the decode here, not later. We mirror that: capture get_string now. */
typedef struct {
    int decoded;
    FuriString* text;
} Capture;

static void on_decode(SubGhzProtocolDecoderBase* base, void* context) {
    Capture* cap = context;
    cap->decoded++;
    furi_string_reset(cap->text);
    subghz_protocol_pocsag.decoder->get_string(base, cap->text);
}

/* Pull "RIC <n>", "FUNC <n>", "MSG <text...>" out of a .expect file. */
static void parse_expect(const char* text, unsigned long* ric, int* func, char* msg, size_t msg_size) {
    *ric = 0;
    *func = -1;
    msg[0] = '\0';
    const char* cursor = text;
    char line[256];
    while(*cursor) {
        const char* nl = strchr(cursor, '\n');
        size_t len = nl ? (size_t)(nl - cursor) : strlen(cursor);
        if(len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, cursor, len);
        line[len] = '\0';
        while(len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) line[--len] = '\0';
        if(strncmp(line, "RIC ", 4) == 0)
            *ric = strtoul(line + 4, NULL, 10);
        else if(strncmp(line, "FUNC ", 5) == 0)
            *func = atoi(line + 5);
        else if(strncmp(line, "MSG ", 4) == 0)
            snprintf(msg, msg_size, "%s", line + 4);
        if(!nl) break;
        cursor = nl + 1;
    }
}

static int run_case(const char* dir, const char* name) {
    char edges_name[128], expect_name[128];
    snprintf(edges_name, sizeof(edges_name), "%s.edges", name);
    snprintf(expect_name, sizeof(expect_name), "%s.expect", name);
    char* edges = read_file(dir, edges_name);
    char* expect = read_file(dir, expect_name);
    if(!edges || !expect) {
        free(edges);
        free(expect);
        return 0;
    }

    unsigned long ric = 0;
    int func = -1;
    char msg[256];
    parse_expect(expect, &ric, &func, msg, sizeof(msg));

    SubGhzProtocolDecoderBase* base = subghz_protocol_pocsag.decoder->alloc(NULL);
    Capture cap = {0, furi_string_alloc()};
    base->callback = on_decode;
    base->context = &cap;

    size_t edge_count = 0;
    const char* cursor = edges;
    while(*cursor) {
        int level = 0;
        unsigned long duration = 0;
        if(sscanf(cursor, "%d %lu", &level, &duration) == 2) {
            subghz_protocol_pocsag.decoder->feed(base, level != 0, (uint32_t)duration);
            edge_count++;
        }
        const char* nl = strchr(cursor, '\n');
        if(!nl) break;
        cursor = nl + 1;
    }

    const char* out = furi_string_get_cstr(cap.text);
    char addr_needle[64];
    snprintf(addr_needle, sizeof(addr_needle), "Addr: %lu", ric);
    CHECK(cap.decoded > 0, "%s: decoder never fired its callback (no end-of-packet)", name);
    CHECK(strstr(out, "POCSAG") != NULL, "%s: output does not name POCSAG:\n%s", name, out);
    CHECK(strstr(out, addr_needle) != NULL, "%s: expected '%s' in:\n%s", name, addr_needle, out);
    if(func != 1) /* ALERT1 carries no text */
        CHECK(strstr(out, msg) != NULL, "%s: expected message '%s' in:\n%s", name, msg, out);

    printf("  %-12s edges=%zu addr=%lu msg=\"%s\" -> ok\n", name, edge_count, ric, msg);

    furi_string_free(cap.text);
    subghz_protocol_pocsag.decoder->free(base);
    free(edges);
    free(expect);
    return 1;
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : ".";
    const char* cases[] = {"alnum_text", "alnum_long", "numeric", "alnum_512"};
    size_t n = sizeof(cases) / sizeof(cases[0]);

    /* Shape checks on the registered protocol. */
    CHECK(subghz_protocol_pocsag.type == SubGhzProtocolTypeStatic, "POCSAG type is not Static");
    CHECK((subghz_protocol_pocsag.flag & SubGhzProtocolFlag_Send) == 0,
          "POCSAG must not carry the Send flag (receive only)");
    CHECK(subghz_protocol_pocsag.encoder->deserialize == NULL,
          "POCSAG must have no encoder");
    CHECK(subghz_protocol_pocsag.decoder && subghz_protocol_pocsag.decoder->feed &&
              subghz_protocol_pocsag.decoder->get_string,
          "POCSAG decoder vtable incomplete");

    size_t ran = 0;
    for(size_t i = 0; i < n; i++)
        ran += (size_t)run_case(dir, cases[i]);

    printf("coverage: cases=%zu ran=%zu\n", n, ran);
    if(ran != n) {
        printf("FAIL: only %zu of %zu POCSAG fixtures ran\n", ran, n);
        failures++;
    }
    if(failures) {
        printf("FAIL: %d POCSAG check(s) failed\n", failures);
        return 1;
    }
    printf("PASS: POCSAG decode (host only; synthetic fixtures; on-air reception is a hardware check)\n");
    return 0;
}
