#include "ble_lab_radio.h"

#include <furi.h>
#include <btshim.h>

#include <esp_attr.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_log.h>

#include <stdlib.h>
#include <string.h>

#define TAG "BleLab"

/* Device table lives in PSRAM where the board has it (ESP32-S3 profiles set
 * CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY); elsewhere the attribute is a
 * no-op. It is only touched from task context under s_mutex, never an ISR. */
static EXT_RAM_BSS_ATTR BleLabDevice s_devices[BLE_LAB_MAX_DEVICES];
static uint16_t s_count = 0;
static FuriMutex* s_mutex = NULL;
static volatile bool s_scanning = false;
static bool s_started = false;

static void ble_lab_gap_event(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    switch(event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if(param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            esp_ble_gap_start_scanning(0); /* until stopped */
        }
        break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        s_scanning = (param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS);
        break;
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        s_scanning = false;
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        if(param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) break;
        if(!s_mutex || furi_mutex_acquire(s_mutex, 20) != FuriStatusOk) break;

        BleLabDevice* device = NULL;
        for(uint16_t i = 0; i < s_count; i++) {
            if(memcmp(s_devices[i].addr, param->scan_rst.bda, 6) == 0) {
                device = &s_devices[i];
                break;
            }
        }
        const uint32_t now = furi_get_tick();
        if(!device && s_count < BLE_LAB_MAX_DEVICES) {
            device = &s_devices[s_count++];
            memset(device, 0, sizeof(*device));
            memcpy(device->addr, param->scan_rst.bda, 6);
            device->addr_random = (param->scan_rst.ble_addr_type != BLE_ADDR_TYPE_PUBLIC);
            device->rssi_best = (int8_t)param->scan_rst.rssi;
            device->first_seen = now;
            ble_lab_adv_reset(&device->adv);
        }
        if(device) {
            device->rssi = (int8_t)param->scan_rst.rssi;
            if(device->rssi > device->rssi_best) device->rssi_best = device->rssi;
            device->last_seen = now;
            if(device->packets < UINT16_MAX) device->packets++;
            /* ble_adv holds the advertisement followed by the scan response.
             * With a passive scan the response part is normally empty. */
            const uint8_t adv_len = param->scan_rst.adv_data_len;
            const uint8_t rsp_len = param->scan_rst.scan_rsp_len;
            if((size_t)adv_len + rsp_len <= sizeof(param->scan_rst.ble_adv)) {
                ble_lab_adv_parse(&device->adv, param->scan_rst.ble_adv, adv_len);
                ble_lab_adv_parse(&device->adv, param->scan_rst.ble_adv + adv_len, rsp_len);
            }
        }
        furi_mutex_release(s_mutex);
        break;
    }
    default:
        break;
    }
}

static void ble_lab_radio_teardown(void) {
    if(esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        esp_bluedroid_disable();
        furi_delay_ms(50);
        esp_bluedroid_deinit();
        furi_delay_ms(50);
    }
    if(esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
        esp_bt_controller_disable();
        furi_delay_ms(50);
    }
    if(esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
        esp_bt_controller_deinit();
        furi_delay_ms(50);
    }
    Bt* bt = furi_record_open(RECORD_BT);
    bt_start_stack(bt);
    furi_record_close(RECORD_BT);
}

bool ble_lab_radio_start(void) {
    if(s_started) return true;
    if(!s_mutex) s_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    ble_lab_radio_clear();

    /* The system service owns the stack normally; borrow it for the scan. */
    Bt* bt = furi_record_open(RECORD_BT);
    bt_stop_stack(bt);
    furi_record_close(RECORD_BT);
    furi_delay_ms(100);

    esp_bt_controller_config_t controller_config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_bt_controller_init(&controller_config);
    if(err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    }
    if(err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        esp_bluedroid_config_t host_config = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
        err = esp_bluedroid_init_with_cfg(&host_config);
    }
    if(err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        err = esp_bluedroid_enable();
    }
    if(err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        err = esp_ble_gap_register_callback(ble_lab_gap_event);
    }
    if(err == ESP_OK) {
        /* PASSIVE: receive advertisements only. No scan requests are sent, so
         * the devices being listed cannot tell that anything is listening. */
        esp_ble_scan_params_t scan_params = {
            .scan_type = BLE_SCAN_TYPE_PASSIVE,
            .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
            .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
            .scan_interval = 0x50, /* 50 ms */
            .scan_window = 0x30, /* 30 ms */
            .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
        };
        err = esp_ble_gap_set_scan_params(&scan_params);
    }
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "start failed: %s", esp_err_to_name(err));
        ble_lab_radio_teardown();
        return false;
    }
    s_started = true;
    return true;
}

void ble_lab_radio_stop(void) {
    if(!s_started) return;
    if(s_scanning) {
        esp_ble_gap_stop_scanning();
        for(int i = 0; i < 40 && s_scanning; i++) furi_delay_ms(5);
    }
    ble_lab_radio_teardown();
    s_started = false;
}

void ble_lab_radio_clear(void) {
    if(s_mutex) furi_mutex_acquire(s_mutex, FuriWaitForever);
    s_count = 0;
    memset(s_devices, 0, sizeof(s_devices));
    if(s_mutex) furi_mutex_release(s_mutex);
}

static int ble_lab_sort_by_rssi(const void* a, const void* b) {
    return (int)((const BleLabDevice*)b)->rssi - (int)((const BleLabDevice*)a)->rssi;
}

uint16_t ble_lab_radio_snapshot(BleLabDevice* out, uint16_t max) {
    if(!out || !max || !s_mutex) return 0;
    furi_mutex_acquire(s_mutex, FuriWaitForever);
    uint16_t count = s_count < max ? s_count : max;
    memcpy(out, s_devices, (size_t)count * sizeof(BleLabDevice));
    furi_mutex_release(s_mutex);
    qsort(out, count, sizeof(BleLabDevice), ble_lab_sort_by_rssi);
    return count;
}
