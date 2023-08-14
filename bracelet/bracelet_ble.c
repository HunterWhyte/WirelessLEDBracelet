/* Copyright (c) 2023  Hunter Whyte */
#include "app_error.h"
#include "app_timer.h"

#include "nrf_log.h"
#include "nrf_sdh_ble.h"

#include "boards.h"
#include "bsp.h"

#include "ble.h"
#include "ble_advdata.h"
#include "ble_conn_params.h"
#include "ble_conn_state.h"
#include "ble_err.h"
#include "ble_hci.h"
#include "ble_nus.h"
#include "ble_srv_common.h"
#include "nfc_ble_pair_lib.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_qwr.h"
#include "peer_manager.h"
#include "peer_manager_handler.h"

#include "bracelet_ble.h"
#include "bracelet.h"

/* ###################### PRIVATE FUNCTION PROTOTYPES ###################### */
static void gatt_init(void);
static void gap_params_init(void);
static void advertising_init(void);
static void services_init(void);
static void conn_params_init(void);

static void on_conn_params_evt(ble_conn_params_evt_t* p_evt);
static void conn_params_error_handler(uint32_t nrf_error);
static void nrf_qwr_error_handler(uint32_t nrf_error);
static void nus_data_handler(ble_nus_evt_t* p_evt);
static void on_adv_evt(ble_adv_evt_t ble_adv_evt);
// static void pm_evt_handler(pm_evt_t const* p_evt);
void gatt_evt_handler(nrf_ble_gatt_t* p_gatt, nrf_ble_gatt_evt_t const* p_evt);

/* ################################ GLOBALS ################################ */
BLE_NUS_DEF(m_nus, NRF_SDH_BLE_TOTAL_LINK_COUNT);
NRF_BLE_GATT_DEF(m_gatt); /* GATT module instance. */
NRF_BLE_QWR_DEF(m_qwr);   /* Context for the Queued Write module. */
BLE_ADVERTISING_DEF(m_advertising);

static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID; /* Handle of the current connection. */
static uint16_t m_ble_nus_max_data_len = BLE_GATT_ATT_MTU_DEFAULT - 3;
ble_uuid_t m_adv_uuids[] = {
    {BLE_UUID_NUS_SERVICE, BLE_UUID_TYPE_BLE},
};

/* ############################# BLE CONTROL ############################ */
void ble_send(char* data_array, uint8_t length) {
  ret_code_t ret_code;
  uint16_t len = length;
  NRF_LOG_INFO("Sending:");
  NRF_LOG_HEXDUMP_INFO(data_array, length);
  ret_code = ble_nus_data_send(&m_nus, (uint8_t*)data_array, &len, m_conn_handle);
  if ((ret_code != NRF_ERROR_INVALID_STATE) && (ret_code != NRF_ERROR_RESOURCES) &&
      (ret_code != NRF_ERROR_NOT_FOUND)) {
    APP_ERROR_CHECK(ret_code);
  }
}

void advertising_start(void) {
  uint32_t ret_code = ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
  APP_ERROR_CHECK(ret_code);
}

void advertising_stop(void) {
  uint32_t ret_code = sd_ble_gap_adv_stop(m_advertising.adv_handle);
  APP_ERROR_CHECK(ret_code);
}

void ble_disconnect(void) {
  uint32_t ret_code =
      sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
  APP_ERROR_CHECK(ret_code);
}

/* ############################# EVENT HANDLERS ############################ */
void ble_evt_handler(ble_evt_t const* p_ble_evt, void* p_context) {
  ret_code_t ret_code;

  switch (p_ble_evt->header.evt_id) {
    case BLE_GAP_EVT_CONNECTED:
      NRF_LOG_INFO("Connected");
      ble_connect_handler();
      m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
      ret_code = nrf_ble_qwr_conn_handle_assign(&m_qwr, m_conn_handle);
      APP_ERROR_CHECK(ret_code);
      check_battery();
      break;

    case BLE_GAP_EVT_DISCONNECTED:
      NRF_LOG_INFO("Disconnected");
      ble_disconnect_handler();
      /* LED indication will be changed when advertising starts. */
      m_conn_handle = BLE_CONN_HANDLE_INVALID;
      break;

    case BLE_GAP_EVT_PHY_UPDATE_REQUEST: {
      NRF_LOG_DEBUG("PHY update request.");
      ble_gap_phys_t const phys = {
          .rx_phys = BLE_GAP_PHY_AUTO,
          .tx_phys = BLE_GAP_PHY_AUTO,
      };
      ret_code = sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys);
      APP_ERROR_CHECK(ret_code);
    } break;

    case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
      /* Pairing not supported */
      ret_code = sd_ble_gap_sec_params_reply(m_conn_handle, BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP,
                                             NULL, NULL);
      APP_ERROR_CHECK(ret_code);
      break;

    case BLE_GATTS_EVT_SYS_ATTR_MISSING:
      /* No system attributes have been stored. */
      ret_code = sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0, 0);
      APP_ERROR_CHECK(ret_code);
      break;

    case BLE_GATTC_EVT_TIMEOUT:
      /* Disconnect on GATT Client timeout event. */
      ret_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
                                       BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
      APP_ERROR_CHECK(ret_code);
      break;

    case BLE_GATTS_EVT_TIMEOUT:
      /* Disconnect on GATT Server timeout event. */
      ret_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
                                       BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
      APP_ERROR_CHECK(ret_code);
      break;
    default:
      break;
  }
}

static void on_adv_evt(ble_adv_evt_t ble_adv_evt) {
  switch (ble_adv_evt) {
    case BLE_ADV_EVT_FAST:
      NRF_LOG_INFO("Fast advertising.");
      break;
    case BLE_ADV_EVT_IDLE:
      NRF_LOG_INFO("Advertising stopped.");
      break;

    default:
      break;
  }
}

static void on_conn_params_evt(ble_conn_params_evt_t* p_evt) {
  ret_code_t ret_code;

  if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED) {
    ret_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
    APP_ERROR_CHECK(ret_code);
  }
}

// static void pm_evt_handler(pm_evt_t const* p_evt) {
//   ret_code_t ret_code;

//   pm_handler_on_pm_evt(p_evt);
//   pm_handler_disconnect_on_sec_failure(p_evt);
//   pm_handler_flash_clean(p_evt);

//   switch (p_evt->evt_id) {
//     case PM_EVT_CONN_SEC_PARAMS_REQ: {
//       /* Send event to the NFC BLE pairing library as it may dynamically alternate
//       security parameters to achieve highest possible security level. */
//       ret_code = nfc_ble_pair_on_pm_params_req(p_evt);
//       NRF_LOG_INFO("nfc_ble_pair_on_pm_params_req %d", ret_code);
//       APP_ERROR_CHECK(ret_code);
//     } break;
//     default:
//       break;
//   }
// }

static void conn_params_error_handler(uint32_t nrf_error) {
  APP_ERROR_HANDLER(nrf_error);
}

static void nrf_qwr_error_handler(uint32_t nrf_error) {
  APP_ERROR_HANDLER(nrf_error);
}

void gatt_evt_handler(nrf_ble_gatt_t* p_gatt, nrf_ble_gatt_evt_t const* p_evt) {
  if ((m_conn_handle == p_evt->conn_handle) &&
      (p_evt->evt_id == NRF_BLE_GATT_EVT_ATT_MTU_UPDATED)) {
    m_ble_nus_max_data_len = p_evt->params.att_mtu_effective - OPCODE_LENGTH - HANDLE_LENGTH;
    NRF_LOG_INFO("Data len is set to 0x%X(%d)", m_ble_nus_max_data_len, m_ble_nus_max_data_len);
  }
  NRF_LOG_DEBUG("ATT MTU exchange completed. central 0x%x peripheral 0x%x",
                p_gatt->att_mtu_desired_central, p_gatt->att_mtu_desired_periph);
}

static void nus_data_handler(ble_nus_evt_t* p_evt) {
  if (p_evt->type == BLE_NUS_EVT_RX_DATA) {
    NRF_LOG_INFO("Received data from BLE NUS");
    NRF_LOG_HEXDUMP_INFO(p_evt->params.rx_data.p_data, p_evt->params.rx_data.length);
    if (p_evt->params.rx_data.length > 3) {
      ble_data_handler(p_evt->params.rx_data.p_data[0], p_evt->params.rx_data.p_data[1],
                       p_evt->params.rx_data.p_data[2], p_evt->params.rx_data.p_data[3]);
    }
  }
}

/* ############################# INITIALIZATION ############################ */
static void gatt_init(void) {
  ret_code_t ret_code = nrf_ble_gatt_init(&m_gatt, gatt_evt_handler);
  APP_ERROR_CHECK(ret_code);

  ret_code = nrf_ble_gatt_att_mtu_periph_set(&m_gatt, NRF_SDH_BLE_GATT_MAX_MTU_SIZE);
  APP_ERROR_CHECK(ret_code);
}

static void gap_params_init(void) {
  ret_code_t ret_code;
  ble_gap_conn_params_t gap_conn_params;
  ble_gap_conn_sec_mode_t sec_mode;

  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

  ret_code =
      sd_ble_gap_device_name_set(&sec_mode, (const uint8_t*)DEVICE_NAME, strlen(DEVICE_NAME));
  APP_ERROR_CHECK(ret_code);

  memset(&gap_conn_params, 0, sizeof(gap_conn_params));

  gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
  gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
  gap_conn_params.slave_latency = SLAVE_LATENCY;
  gap_conn_params.conn_sup_timeout = CONN_SUP_TIMEOUT;

  ret_code = sd_ble_gap_ppcp_set(&gap_conn_params);
  APP_ERROR_CHECK(ret_code);
}

static void advertising_init(void) {
  ret_code_t ret_code;
  ble_advertising_init_t init;

  memset(&init, 0, sizeof(init));

  init.advdata.name_type = BLE_ADVDATA_FULL_NAME;
  init.advdata.include_appearance = true;
  init.advdata.flags = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;

  init.advdata.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
  init.advdata.uuids_complete.p_uuids = m_adv_uuids;

  init.config.ble_adv_fast_enabled = true;
  init.config.ble_adv_fast_interval = APP_ADV_INTERVAL;
  init.config.ble_adv_fast_timeout = APP_ADV_DURATION;
  init.evt_handler = on_adv_evt;

  ret_code = ble_advertising_init(&m_advertising, &init);
  NRF_LOG_INFO("ble_advertising_init %d", ret_code);
  APP_ERROR_CHECK(ret_code);

  ble_advertising_conn_cfg_tag_set(&m_advertising, APP_BLE_CONN_CFG_TAG);
}

static void services_init(void) {
  ret_code_t ret_code;
  ble_nus_init_t nus_init;
  nrf_ble_qwr_init_t qwr_init = {0};

  // Initialize Queued Write Module.
  qwr_init.error_handler = nrf_qwr_error_handler;

  ret_code = nrf_ble_qwr_init(&m_qwr, &qwr_init);
  APP_ERROR_CHECK(ret_code);

  // Initialize NUS.
  memset(&nus_init, 0, sizeof(nus_init));

  nus_init.data_handler = nus_data_handler;

  ret_code = ble_nus_init(&m_nus, &nus_init);
  APP_ERROR_CHECK(ret_code);
}

static void conn_params_init(void) {
  ret_code_t ret_code;
  ble_conn_params_init_t cp_init;

  memset(&cp_init, 0, sizeof(cp_init));

  cp_init.p_conn_params = NULL;
  cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
  cp_init.next_conn_params_update_delay = NEXT_CONN_PARAMS_UPDATE_DELAY;
  cp_init.max_conn_params_update_count = MAX_CONN_PARAMS_UPDATE_COUNT;
  cp_init.start_on_notify_cccd_handle = BLE_GATT_HANDLE_INVALID;
  cp_init.disconnect_on_fail = false;
  cp_init.evt_handler = on_conn_params_evt;
  cp_init.error_handler = conn_params_error_handler;

  ret_code = ble_conn_params_init(&cp_init);
  APP_ERROR_CHECK(ret_code);
}

// static void peer_manager_init() {
//   ret_code_t ret_code;

//   ret_code = pm_init();
//   APP_ERROR_CHECK(ret_code);

//   // if (erase_bonds) {
//   // ret_code = pm_peers_delete();
//   // APP_ERROR_CHECK(ret_code);
//   // }

//   ret_code = pm_register(pm_evt_handler);
//   APP_ERROR_CHECK(ret_code);
// }

// static void nfc_ble_pairing_init(void) {
//   ret_code_t ret_code;
//   ret_code = nfc_ble_pair_init(&m_advertising, (nfc_pairing_mode_t)NFC_PAIRING_MODE);
//   APP_ERROR_CHECK(ret_code);
// }

void ble_init(void) {
  gap_params_init();
  NRF_LOG_INFO("gap_params_init");
  gatt_init();
  NRF_LOG_INFO("gatt_init");
  services_init();
  NRF_LOG_INFO("services_init");
  // peer_manager_init();
  // NRF_LOG_INFO("peer_manager_init");
  advertising_init();
  NRF_LOG_INFO("advertising_init");
  // nfc_ble_pairing_init();
  // NRF_LOG_INFO("nfc_ble_pairing_init");
  conn_params_init();
  NRF_LOG_INFO("conn_params_init");
}