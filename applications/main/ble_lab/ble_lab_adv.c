#include "ble_lab_adv.h"

#include <string.h>

/* Bluetooth Core Specification Supplement, Part A: AD types used here. */
#define AD_FLAGS            0x01
#define AD_UUID16_PARTIAL   0x02
#define AD_UUID16_COMPLETE  0x03
#define AD_NAME_SHORT       0x08
#define AD_NAME_COMPLETE    0x09
#define AD_TX_POWER         0x0A
#define AD_MANUFACTURER     0xFF

#define COMPANY_APPLE   0x004Cu
#define COMPANY_SAMSUNG 0x0075u

void ble_lab_adv_reset(BleLabAdv* adv) {
    memset(adv, 0, sizeof(*adv));
    adv->company_id = BLE_LAB_NO_COMPANY;
    adv->kind = BleLabKindUnknown;
}

static void ble_lab_adv_set_name(BleLabAdv* adv, const uint8_t* value, size_t len, bool complete) {
    /* A complete name always wins; a shortened one never replaces it. */
    if(adv->name[0] && adv->name_complete && !complete) return;
    size_t out = 0;
    for(size_t i = 0; i < len && out < BLE_LAB_NAME_MAX - 1; i++) {
        uint8_t c = value[i];
        /* Names come from strangers: keep printable ASCII, replace the rest so
         * nothing odd reaches the display or a CSV file. */
        adv->name[out++] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    adv->name[out] = '\0';
    adv->name_complete = complete;
}

static void ble_lab_adv_classify_manufacturer(BleLabAdv* adv, const uint8_t* value, size_t len) {
    if(len < 2) return;
    uint32_t company = (uint32_t)value[0] | ((uint32_t)value[1] << 8);
    if(adv->company_id == BLE_LAB_NO_COMPANY) adv->company_id = company;

    if(company == COMPANY_APPLE && len >= 3) {
        switch(value[2]) {
        case 0x12:
            adv->kind = BleLabKindAppleFindMy;
            break;
        case 0x07:
            if(adv->kind == BleLabKindUnknown) adv->kind = BleLabKindAppleAudio;
            break;
        case 0x10:
            if(adv->kind == BleLabKindUnknown) adv->kind = BleLabKindAppleNearby;
            break;
        default:
            break;
        }
    } else if(company == COMPANY_SAMSUNG) {
        if(adv->kind == BleLabKindUnknown) adv->kind = BleLabKindSamsungTag;
    }
}

void ble_lab_adv_parse(BleLabAdv* adv, const uint8_t* data, size_t len) {
    if(!adv || !data) return;
    size_t pos = 0;
    while(pos < len) {
        uint8_t field_len = data[pos];
        if(field_len == 0) break; /* zero-length field ends the payload */
        if(pos + 1 + (size_t)field_len > len) {
            adv->malformed = true;
            break;
        }
        uint8_t type = data[pos + 1];
        const uint8_t* value = &data[pos + 2];
        size_t value_len = (size_t)field_len - 1;

        switch(type) {
        case AD_FLAGS:
            if(value_len >= 1) {
                adv->flags = value[0];
                adv->has_flags = true;
            }
            break;
        case AD_NAME_SHORT:
            ble_lab_adv_set_name(adv, value, value_len, false);
            break;
        case AD_NAME_COMPLETE:
            ble_lab_adv_set_name(adv, value, value_len, true);
            break;
        case AD_TX_POWER:
            if(value_len >= 1) {
                adv->tx_power = (int8_t)value[0];
                adv->has_tx_power = true;
            }
            break;
        case AD_UUID16_PARTIAL:
        case AD_UUID16_COMPLETE:
            for(size_t i = 0; i + 1 < value_len; i += 2) {
                uint16_t uuid = (uint16_t)(value[i] | (value[i + 1] << 8));
                if(adv->service16_count < sizeof(adv->service16) / sizeof(adv->service16[0])) {
                    adv->service16[adv->service16_count++] = uuid;
                }
                if((uuid == 0xFEED || uuid == 0xFD84) && adv->kind == BleLabKindUnknown) {
                    adv->kind = BleLabKindTile;
                }
            }
            break;
        case AD_MANUFACTURER:
            ble_lab_adv_classify_manufacturer(adv, value, value_len);
            break;
        default:
            break;
        }
        pos += 1 + (size_t)field_len;
    }
}

const char* ble_lab_kind_label(BleLabKind kind) {
    switch(kind) {
    case BleLabKindAppleFindMy:
        return "FindMy tag";
    case BleLabKindAppleAudio:
        return "Apple audio";
    case BleLabKindAppleNearby:
        return "Apple device";
    case BleLabKindSamsungTag:
        return "Samsung";
    case BleLabKindTile:
        return "Tile";
    default:
        return "";
    }
}

const char* ble_lab_company_name(uint32_t company_id) {
    switch(company_id) {
    case 0x004C:
        return "Apple";
    case 0x0006:
        return "Microsoft";
    case 0x0075:
        return "Samsung";
    case 0x00E0:
        return "Google";
    case 0x0059:
        return "Nordic";
    case 0x02E5:
        return "Espressif";
    case 0x0157:
        return "Xiaomi/Huami";
    case 0x0087:
        return "Garmin";
    default:
        return NULL;
    }
}
