/**
 * BLE Lab radio layer: passive scanning only.
 *
 * Brings the Bluetooth controller and host up in scan-only use, listens for
 * advertisements with a PASSIVE scan (no scan requests, no connections, no
 * advertising of its own) and keeps a small table of what it heard.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ble_lab_adv.h"

#define BLE_LAB_MAX_DEVICES 40

typedef struct {
    uint8_t addr[6];
    bool addr_random; /* random (privacy) address rather than a public one */
    int8_t rssi; /* most recent */
    int8_t rssi_best;
    uint32_t first_seen; /* ticks */
    uint32_t last_seen; /* ticks */
    uint16_t packets;
    BleLabAdv adv;
} BleLabDevice;

/** Start passive scanning. Pauses the system Bluetooth service meanwhile. */
bool ble_lab_radio_start(void);

/** Stop scanning and hand Bluetooth back to the system service. */
void ble_lab_radio_stop(void);

/** Forget everything heard so far. */
void ble_lab_radio_clear(void);

/**
 * Copy the device table, strongest signal first.
 * @return number of entries written to `out` (at most `max`).
 */
uint16_t ble_lab_radio_snapshot(BleLabDevice* out, uint16_t max);
