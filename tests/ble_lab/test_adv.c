/* Host test for BLE Lab's advertisement parser (pure functions, no radio). */
#include <ble_lab_adv.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        checks++;                                         \
        if(!(cond)) {                                     \
            failures++;                                   \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while(0)

#define PARSE(adv, ...)                                    \
    do {                                                   \
        const uint8_t bytes_[] = {__VA_ARGS__};            \
        ble_lab_adv_parse(&(adv), bytes_, sizeof(bytes_)); \
    } while(0)

int main(void) {
    BleLabAdv adv;

    /* Flags + complete name + TX power. */
    ble_lab_adv_reset(&adv);
    PARSE(adv, 0x02, 0x01, 0x06, 0x05, 0x09, 0x54, 0x65, 0x73, 0x74, 0x02, 0x0A, 0xF4);
    CHECK(adv.has_flags && adv.flags == 0x06, "flags");
    CHECK(strcmp(adv.name, "Test") == 0 && adv.name_complete, "name %s", adv.name);
    CHECK(adv.has_tx_power && adv.tx_power == -12, "tx power %d", adv.tx_power);
    CHECK(adv.company_id == BLE_LAB_NO_COMPANY, "company without manufacturer data");
    CHECK(adv.kind == BleLabKindUnknown && !adv.malformed, "plain device misclassified");

    /* A shortened name is replaced by a complete one from the scan response,
     * and never the other way round. */
    ble_lab_adv_reset(&adv);
    PARSE(adv, 0x03, 0x08, 0x41, 0x62);
    CHECK(strcmp(adv.name, "Ab") == 0 && !adv.name_complete, "short name");
    PARSE(adv, 0x05, 0x09, 0x41, 0x62, 0x63, 0x64);
    CHECK(strcmp(adv.name, "Abcd") == 0 && adv.name_complete, "complete name wins");
    PARSE(adv, 0x02, 0x08, 0x5A);
    CHECK(strcmp(adv.name, "Abcd") == 0, "short name replaced a complete one");

    /* Names are untrusted: control and non-ASCII bytes are replaced, length bounded. */
    ble_lab_adv_reset(&adv);
    PARSE(adv, 0x06, 0x09, 0x61, 0x00, 0x0A, 0xFF, 0x62);
    CHECK(strcmp(adv.name, "a???b") == 0, "sanitised name %s", adv.name);
    {
        uint8_t big[2 + 60];
        big[0] = 61;
        big[1] = 0x09;
        memset(big + 2, 0x78, 60);
        ble_lab_adv_reset(&adv);
        ble_lab_adv_parse(&adv, big, sizeof(big));
        CHECK(strlen(adv.name) == BLE_LAB_NAME_MAX - 1, "name not bounded: %zu", strlen(adv.name));
    }

    /* Vendor and kind hints. */
    ble_lab_adv_reset(&adv);
    PARSE(adv, 0x07, 0xFF, 0x4C, 0x00, 0x12, 0x19, 0x10, 0x00);
    CHECK(adv.company_id == 0x004C && adv.kind == BleLabKindAppleFindMy, "Find My");
    CHECK(strcmp(ble_lab_company_name(adv.company_id), "Apple") == 0, "company name");
    ble_lab_adv_reset(&adv);
    PARSE(adv, 0x06, 0xFF, 0x4C, 0x00, 0x07, 0x19, 0x01);
    CHECK(adv.kind == BleLabKindAppleAudio, "Apple audio");
    ble_lab_adv_reset(&adv);
    PARSE(adv, 0x05, 0xFF, 0x4C, 0x00, 0x10, 0x05);
    CHECK(adv.kind == BleLabKindAppleNearby, "Apple nearby");
    /* A Find My field later in the same payload outranks the earlier hint. */
    PARSE(adv, 0x04, 0xFF, 0x4C, 0x00, 0x12);
    CHECK(adv.kind == BleLabKindAppleFindMy, "Find My should outrank nearby");
    ble_lab_adv_reset(&adv);
    PARSE(adv, 0x04, 0xFF, 0x75, 0x00, 0x42);
    CHECK(adv.kind == BleLabKindSamsungTag && adv.company_id == 0x0075, "Samsung");
    ble_lab_adv_reset(&adv);
    PARSE(adv, 0x05, 0x03, 0x0F, 0x18, 0xED, 0xFE);
    CHECK(adv.kind == BleLabKindTile, "Tile by service UUID");
    CHECK(
        adv.service16_count == 2 && adv.service16[0] == 0x180F && adv.service16[1] == 0xFEED,
        "service list");
    ble_lab_adv_reset(&adv);
    PARSE(adv, 0x05, 0xFF, 0x59, 0x00, 0x12, 0x34);
    CHECK(
        adv.kind == BleLabKindUnknown && adv.company_id == 0x0059,
        "non-Apple 0x12 is not a tag");
    CHECK(ble_lab_company_name(0xABCD) == NULL, "unknown company should be NULL");
    CHECK(ble_lab_kind_label(BleLabKindUnknown)[0] == 0, "unknown kind label");

    /* A service list longer than the table: the count saturates. */
    ble_lab_adv_reset(&adv);
    PARSE(adv, 0x0D, 0x02, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0);
    CHECK(adv.service16_count == 4, "service count %u", adv.service16_count);

    /* Malformed input: a length past the end, a zero-length terminator, odd bytes. */
    ble_lab_adv_reset(&adv);
    PARSE(adv, 0x02, 0x01, 0x06, 0x09, 0x09, 0x61, 0x62);
    CHECK(adv.malformed && adv.has_flags && adv.name[0] == 0, "truncated field");
    ble_lab_adv_reset(&adv);
    PARSE(adv, 0x00, 0x05, 0x09, 0x78, 0x78, 0x78, 0x78);
    CHECK(!adv.malformed && adv.name[0] == 0, "zero length should end the payload");
    ble_lab_adv_reset(&adv);
    PARSE(adv, 0x01, 0x09);
    CHECK(!adv.malformed && adv.name[0] == 0, "empty name field");
    ble_lab_adv_reset(&adv);
    PARSE(adv, 0x02, 0xFF, 0x4C);
    CHECK(
        adv.company_id == BLE_LAB_NO_COMPANY && adv.kind == BleLabKindUnknown,
        "one-byte manufacturer data");
    ble_lab_adv_reset(&adv);
    PARSE(adv, 0x1F);
    CHECK(adv.malformed, "lone length byte");
    ble_lab_adv_parse(&adv, NULL, 0);
    {
        const uint8_t one = 0x78;
        ble_lab_adv_parse(NULL, &one, 1);
    }
    checks++;

    /* Fuzz: every payload length 0..62 with deterministic pseudo-random bytes,
     * allocated exactly so an out-of-bounds read trips the sanitizer. */
    uint32_t state = 0xC0FFEEu;
    for(int round = 0; round < 20000; round++) {
        size_t len = (size_t)(round % 63);
        uint8_t* buf = malloc(len ? len : 1);
        for(size_t i = 0; i < len; i++) {
            state = state * 1664525u + 1013904223u;
            buf[i] = (uint8_t)(state >> 24);
        }
        ble_lab_adv_reset(&adv);
        ble_lab_adv_parse(&adv, buf, len);
        bool broken = strlen(adv.name) >= BLE_LAB_NAME_MAX || adv.service16_count > 4;
        free(buf);
        if(broken) {
            CHECK(false, "fuzz round %d broke an invariant", round);
            break;
        }
    }
    checks++;

    printf("%d checks, %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}
