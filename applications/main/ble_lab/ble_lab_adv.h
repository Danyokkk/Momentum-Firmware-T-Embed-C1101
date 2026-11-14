/**
 * BLE advertisement parsing for BLE Lab. Pure functions over the raw bytes of
 * an advertising or scan-response payload: no Bluetooth stack, no allocation.
 * Also compiled into the host test (tests/ble_lab).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_LAB_NAME_MAX   24
#define BLE_LAB_NO_COMPANY 0xFFFFFFFFu

/* What kind of device the payload looks like. This is a hint from public,
 * unencrypted advertising fields, useful for noticing a tag travelling with
 * you. It is not proof of anything. */
typedef enum {
    BleLabKindUnknown,
    BleLabKindAppleFindMy, /* Apple 0x004C, type 0x12 (AirTag / Find My accessory) */
    BleLabKindAppleAudio, /* Apple 0x004C, type 0x07 (AirPods, Beats) */
    BleLabKindAppleNearby, /* Apple 0x004C, type 0x10 (iPhone, Watch) */
    BleLabKindSamsungTag, /* Samsung 0x0075 */
    BleLabKindTile, /* service UUID 0xFEED / 0xFD84 */
} BleLabKind;

typedef struct {
    char name[BLE_LAB_NAME_MAX]; /* local name, sanitised, "" if absent */
    bool name_complete; /* complete (0x09) rather than shortened (0x08) */
    uint32_t company_id; /* Bluetooth SIG company id, or BLE_LAB_NO_COMPANY */
    uint8_t flags; /* AD type 0x01 value */
    bool has_flags;
    int8_t tx_power; /* AD type 0x0A, valid when has_tx_power */
    bool has_tx_power;
    uint16_t service16[4]; /* first 16-bit service UUIDs seen */
    uint8_t service16_count;
    BleLabKind kind;
    bool malformed; /* a field ran past the end of the payload */
} BleLabAdv;

/** Reset to "nothing parsed". */
void ble_lab_adv_reset(BleLabAdv* adv);

/**
 * Parse one advertising or scan-response payload and merge it into `adv`.
 * Call once for the advertisement and once for the scan response, if any.
 * Safe for any input: `data` may be NULL when `len` is 0, and truncated or
 * hostile payloads set `malformed` instead of reading out of bounds.
 */
void ble_lab_adv_parse(BleLabAdv* adv, const uint8_t* data, size_t len);

/** Short label for a kind; "" for unknown. */
const char* ble_lab_kind_label(BleLabKind kind);

/** Company name for the handful of ids recognised here, or NULL. */
const char* ble_lab_company_name(uint32_t company_id);

#ifdef __cplusplus
}
#endif
