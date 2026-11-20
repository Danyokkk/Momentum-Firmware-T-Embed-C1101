/* Host test: Prastel 42-bit rolling-code scrambling and frame packing.
 *
 * No radio, no device, no HAL. The scramble/unscramble/pack/unpack/button math
 * is the part of the port most likely to carry a bug, so it is reproduced here
 * BYTE-IDENTICAL to lib/subghz/protocols/prastel.c and exercised end to end:
 * an encoded key must decode back to the same serial (masked), button and
 * counter. The custom-button remap and FlipperFormat glue are UI/plumbing and
 * are validated by the firmware build instead. See run.py.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define SUBGHZ_PROTOCOL_PRASTEL_SERIAL_MASK (0x001FFFFFUL)

/* ---- verbatim from prastel.c -------------------------------------------- */

static uint8_t reverse_byte(uint8_t v) {
    v = (uint8_t)((v >> 4) | (v << 4));
    v = (uint8_t)(((v & 0xCC) >> 2) | ((v & 0x33) << 2));
    v = (uint8_t)(((v & 0xAA) >> 1) | ((v & 0x55) << 1));
    return v;
}

static uint8_t parity(uint8_t v) {
    v ^= (uint8_t)(v >> 4);
    return (uint8_t)((0x6996u >> (v & 0x0F)) & 1u);
}

static void unscramble(uint8_t p[7]) {
    uint8_t carry = 0;
    for(uint8_t round = 0; round < 3; round++) {
        for(uint8_t i = 2; i <= 6; i++) {
            uint8_t cur = p[i];
            p[i] = (uint8_t)((carry << 7) | (cur >> 1));
            carry = cur & 1;
        }
    }

    p[0] = reverse_byte(p[0]);
    uint8_t t2 = p[2] & 0xDF;
    p[1] = reverse_byte(p[1]);
    p[3] = reverse_byte(p[3]);
    p[4] = reverse_byte(p[4]);
    uint8_t swapped = (uint8_t)((t2 << 4) | (t2 >> 4));
    p[2] = reverse_byte((uint8_t)~swapped);

    uint8_t p6 = p[6];
    p[5] = reverse_byte(p[5]);
    if(p6 & 1) p[5] ^= 1;

    uint8_t t = (uint8_t)(((uint8_t)(p[5] * 21) + 1) * 21);
    uint8_t x = reverse_byte(p6);
    uint8_t n = (uint8_t)(t + 1);
    if(n != 0) {
        uint8_t limit = (uint8_t)~t;
        uint8_t i = 0;
        do {
            x = (uint8_t)(x * 21 + 1);
            i++;
        } while(limit > i);
    }
    p[5] = n ^ 0xA7;
    p[6] = x ^ 0x69;
}

static void scramble(uint8_t p[7]) {
    uint8_t n = (uint8_t)(p[5] ^ 0xA7);
    uint8_t x = (uint8_t)(p[6] ^ 0x69);

    for(uint8_t i = 0; n > i; i++) {
        x = (uint8_t)(x * 21 + 1);
    }
    for(uint8_t i = 0; i < 254; i++) {
        n = (uint8_t)(n * 21 + 1);
    }

    const bool flip = (x & 0x80) != 0;

    p[0] = reverse_byte(p[0]);
    p[1] = reverse_byte(p[1]);
    p[3] = reverse_byte(p[3]);
    p[4] = reverse_byte(p[4]);

    if(flip) n ^= 1;
    if(p[2] == 0) p[2] = 0xF0;
    uint8_t w = (uint8_t)~reverse_byte(p[2]);
    p[2] = (uint8_t)(((w << 4) & 0xD0) | ((w >> 4) & 0x0F));
    p[5] = reverse_byte(n);
    p[6] = reverse_byte(x);

    const uint8_t parity_5 = parity(p[5]);
    const uint8_t parity_6 = parity(p[6]);

    uint8_t carry = 0;
    for(uint8_t round = 0; round < 3; round++) {
        for(int8_t i = 6; i >= 2; i--) {
            uint8_t cur = p[i];
            p[i] = (uint8_t)((cur << 1) | carry);
            carry = (uint8_t)(cur >> 7);
        }
    }

    if(parity_5 == parity_6) {
        p[6] &= (uint8_t)~0x04;
    } else {
        p[6] |= 0x04;
    }
}

static void unpack(uint64_t data, uint8_t p[7]) {
    uint64_t payload = ((uint64_t)((data >> 30) & 0xFFF) << 32) | (uint32_t)(data << 2);
    for(uint8_t i = 0; i < 7; i++) {
        p[i] = (uint8_t)(payload >> (0x30u - 8u * i));
    }
}

static uint64_t pack(const uint8_t p[7]) {
    uint64_t payload = 0;
    for(uint8_t i = 0; i < 7; i++) {
        payload |= (uint64_t)p[i] << (0x30u - 8u * i);
    }
    return (payload >> 2) & 0x3FFFFFFFFFFULL;
}

static uint8_t btn_from_raw(uint8_t raw) {
    static const uint8_t btn_map[8] = {0x04, 0x00, 0x00, 0x00, 0x03, 0x00, 0x02, 0x01};
    uint8_t idx = (uint8_t)((raw & 0x0F) - 7);
    return (idx <= 7) ? btn_map[idx] : 0;
}

static uint8_t btn_to_raw(uint8_t btn) {
    static const uint8_t raw_map[5] = {0x00, 0x0E, 0x0D, 0x0B, 0x07};
    return (btn >= 1 && btn <= 4) ? raw_map[btn] : 0;
}

/* encode_key() frame builder, verbatim layout from prastel.c */
static uint64_t encode_key(uint32_t serial_in, uint8_t btn, uint16_t cnt) {
    uint8_t p[7] = {0};
    const uint32_t serial = serial_in & SUBGHZ_PROTOCOL_PRASTEL_SERIAL_MASK;
    p[1] = (uint8_t)((serial >> 8) & 0xF0);
    p[2] = (uint8_t)(0x70 | ((serial >> 8) & 0x0F) | ((serial >> 13) & 0x80));
    p[3] = (uint8_t)serial;
    p[4] = (uint8_t)(((serial >> 12) & 0xF0) | btn_to_raw(btn));
    p[5] = (uint8_t)cnt;
    p[6] = (uint8_t)(cnt >> 8);
    scramble(p);
    return pack(p);
}

/* remote_controller() field extraction, verbatim (minus custom-btn plumbing) */
static void decode_key(uint64_t data, uint32_t* serial, uint8_t* btn, uint16_t* cnt) {
    uint8_t p[7];
    unpack(data, p);
    unscramble(p);
    *serial = (((uint32_t)p[1] & 0xF0) << 8) | p[3] | (((uint32_t)p[4] << 12) & 0xF0000) |
              (((uint32_t)p[2] & 0x0F) << 8) | (((uint32_t)p[2] & 0x80) << 13);
    *btn = btn_from_raw(p[4]);
    *cnt = (uint16_t)(p[5] | (p[6] << 8));
}

/* ---- test --------------------------------------------------------------- */

#define CHECK(cond)                                          \
    do {                                                     \
        if(!(cond)) {                                        \
            printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            return 1;                                        \
        }                                                    \
    } while(0)

int main(void) {
    /* Button map is a bijection over the four buttons. */
    for(uint8_t b = 1; b <= 4; b++) {
        CHECK(btn_from_raw(btn_to_raw(b)) == b);
    }

    /* pack() / unpack() roundtrip on the 42-bit payload. */
    for(uint64_t d = 0; d < 0x4000; d++) {
        uint64_t x = (d * 0x9E3779B97F4A7C15ULL) & 0x3FFFFFFFFFFULL;
        uint8_t p[7];
        unpack(x, p);
        CHECK(pack(p) == x);
    }

    /* End-to-end: an encoded key decodes back to the same serial (masked),
     * button and counter. This is the property that fails if scramble and
     * unscramble are not exact inverses. */
    const uint32_t serials[] = {0x000000, 0x1FFFFF, 0x12345, 0x0ABCDE, 0x155555, 0x0AAAAA};
    const uint16_t cnts[] = {0x0000, 0x0001, 0x00FF, 0x1234, 0xABCD, 0xFFFF, 0x8000};
    size_t n = 0;
    for(size_t si = 0; si < sizeof(serials) / sizeof(serials[0]); si++) {
        for(uint8_t btn = 1; btn <= 4; btn++) {
            for(size_t ci = 0; ci < sizeof(cnts) / sizeof(cnts[0]); ci++) {
                uint32_t s_in = serials[si];
                uint64_t key = encode_key(s_in, btn, cnts[ci]);
                CHECK(key <= 0x3FFFFFFFFFFULL);
                uint32_t s_out;
                uint8_t b_out;
                uint16_t c_out;
                decode_key(key, &s_out, &b_out, &c_out);
                CHECK(s_out == (s_in & SUBGHZ_PROTOCOL_PRASTEL_SERIAL_MASK));
                CHECK(b_out == btn);
                CHECK(c_out == cnts[ci]);
                n++;
            }
        }
    }

    /* Broader counter sweep on one remote to stress the LCG counter path. */
    for(uint32_t c = 0; c <= 0xFFFF; c += 7) {
        uint64_t key = encode_key(0x1A2B3, 2, (uint16_t)c);
        uint32_t s_out;
        uint8_t b_out;
        uint16_t c_out;
        decode_key(key, &s_out, &b_out, &c_out);
        CHECK(s_out == (0x1A2B3UL & SUBGHZ_PROTOCOL_PRASTEL_SERIAL_MASK));
        CHECK(b_out == 2);
        CHECK(c_out == (uint16_t)c);
        n++;
    }

    printf("PASS: subghz prastel host test (%zu encode/decode roundtrips)\n", n);
    return 0;
}
