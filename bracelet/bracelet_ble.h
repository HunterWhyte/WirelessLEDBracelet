/* Copyright (c) 2023  Hunter Whyte */
#ifndef BRACELET_BLE_H
#define BRACELET_BLE_H

/* Name of device. Will be included in the advertising data. */
#define DEVICE_NAME "LED Bracelet"
#define MANUFACTURER_NAME ":)"
/* A tag identifying the SoftDevice BLE configuration. */
#define APP_BLE_CONN_CFG_TAG 1
/* The advertising interval (in units of 0.625 ms; this value corresponds to 40 ms). */
#define APP_ADV_INTERVAL 64
/* The advertising time-out (in units of seconds). When set to 0, we will never time out. */
#define APP_ADV_DURATION 18000

/* Minimum acceptable connection interval */
#define MIN_CONN_INTERVAL MSEC_TO_UNITS(20, UNIT_1_25_MS)
/* Maximum acceptable connection interval */
#define MAX_CONN_INTERVAL MSEC_TO_UNITS(75, UNIT_1_25_MS)
/* Slave latency. */
#define SLAVE_LATENCY 0
/* Connection supervisory time-out (4 seconds). */
#define CONN_SUP_TIMEOUT MSEC_TO_UNITS(4000, UNIT_10_MS)

/* Time from initiating event (connect or start of notification) to first time sd_ble_gap_conn_param_update is called (15 seconds). */
#define FIRST_CONN_PARAMS_UPDATE_DELAY APP_TIMER_TICKS(5000)
/* Time between each call to sd_ble_gap_conn_param_update after the first call (5 seconds). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY APP_TIMER_TICKS(30000)
/* Number of attempts before giving up the connection parameter negotiation. */
#define MAX_CONN_PARAMS_UPDATE_COUNT 3

/* PUBLIC FUNCTION PROTOTYPES */
void ble_init(void);
void advertising_start(void);
void advertising_stop(void);
void ble_disconnect(void);
void ble_evt_handler(ble_evt_t const* p_ble_evt, void* p_context);
void ble_send(char* data_array, uint8_t length);

#endif  /* BRACELET_BLE_H */