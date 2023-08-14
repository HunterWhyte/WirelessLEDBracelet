#include "app_uart.h"
#include "app_util.h"
#include "app_util_platform.h"

#include "nrf_drv_clock.h"
#include "nrf_drv_usbd.h"
#include "nrf_log.h"

#include "boards.h"
#include "bsp.h"

#include "app_usbd.h"
#include "app_usbd_cdc_acm.h"
#include "app_usbd_core.h"
#include "app_usbd_serial_num.h"
#include "app_usbd_string_desc.h"

#include "controller_ant.h"
#include "controller_usbd.h"

// DEFINES --------------------------------
#define CDC_ACM_COMM_INTERFACE 0
#define CDC_ACM_COMM_EPIN NRF_DRV_USBD_EPIN2

#define CDC_ACM_DATA_INTERFACE 1
#define CDC_ACM_DATA_EPIN NRF_DRV_USBD_EPIN1
#define CDC_ACM_DATA_EPOUT NRF_DRV_USBD_EPOUT1

#define CDC_DATA_ARRAY_LEN 256

#define ENDLINE_STRING "\r\n"

// PRIVATE FUNCTION PROTOTYPES ------------
static void cdc_acm_user_ev_handler(app_usbd_class_inst_t const* p_inst,
                                    app_usbd_cdc_acm_user_event_t event);
static void usbd_user_ev_handler(app_usbd_event_type_t event);

// GLOBALS --------------------------------
static char m_cdc_data_array[CDC_DATA_ARRAY_LEN];
APP_USBD_CDC_ACM_GLOBAL_DEF(m_app_cdc_acm, cdc_acm_user_ev_handler, CDC_ACM_COMM_INTERFACE,
                            CDC_ACM_DATA_INTERFACE, CDC_ACM_COMM_EPIN, CDC_ACM_DATA_EPIN,
                            CDC_ACM_DATA_EPOUT, APP_USBD_CDC_COMM_PROTOCOL_AT_V250);
static bool m_usb_connected = false;

// FUNCTION DEFINITIONS --------------------
static void cdc_acm_user_ev_handler(app_usbd_class_inst_t const* p_inst,
                                    app_usbd_cdc_acm_user_event_t event) {
  app_usbd_cdc_acm_t const* p_cdc_acm = app_usbd_cdc_acm_class_get(p_inst);

  switch (event) {
    case APP_USBD_CDC_ACM_USER_EVT_PORT_OPEN: {
      /*Set up the first transfer*/
      ret_code_t ret = app_usbd_cdc_acm_read(&m_app_cdc_acm, m_cdc_data_array, 1);
      UNUSED_VARIABLE(ret);
      NRF_LOG_INFO("CDC ACM port opened");
      break;
    }

    case APP_USBD_CDC_ACM_USER_EVT_PORT_CLOSE:
      NRF_LOG_INFO("CDC ACM port closed");
      break;

    case APP_USBD_CDC_ACM_USER_EVT_TX_DONE:
      NRF_LOG_INFO("CDC ACM tx done");
      break;

    case APP_USBD_CDC_ACM_USER_EVT_RX_DONE:
      NRF_LOG_INFO("CDC ACM rx done");
      ret_code_t ret;
      static uint8_t index = 0;
      index++;
      bsp_board_led_invert(BSP_BOARD_LED_1);

      do {
        if ((m_cdc_data_array[index - 1] == '\n') || (m_cdc_data_array[index - 1] == '\r') ||
            (index >= (CDC_DATA_ARRAY_LEN))) {
          if (index > 1) {
            NRF_LOG_INFO("got %s", m_cdc_data_array);
            NRF_LOG_HEXDUMP_DEBUG(m_cdc_data_array, index);

            do {
              uint16_t length = (uint16_t)index;
              if (length + sizeof(ENDLINE_STRING) < CDC_DATA_ARRAY_LEN) {
                memcpy(m_cdc_data_array + length, ENDLINE_STRING, sizeof(ENDLINE_STRING));
                length += sizeof(ENDLINE_STRING);
              }

              // write back
              ret_code_t ret =
                  app_usbd_cdc_acm_write(&m_app_cdc_acm, m_cdc_data_array, CDC_DATA_ARRAY_LEN);
              // write to ant
              ant_update_payload(m_cdc_data_array[0], m_cdc_data_array[1], m_cdc_data_array[2],
                                 m_cdc_data_array[3], m_cdc_data_array[4]);
              if (ret != NRF_SUCCESS) {
                NRF_LOG_INFO("CDC ACM unavailable, data received: %s", m_cdc_data_array);
              }
            } while (ret == NRF_ERROR_BUSY);  // if busy just keep trying
          }

          index = 0;
        }

        /*Get amount of data transferred*/
        size_t size = app_usbd_cdc_acm_rx_size(p_cdc_acm);
        NRF_LOG_INFO("RX: size: %lu char: %d", size, m_cdc_data_array[index - 1]);

        /* Fetch data until internal buffer is empty */
        ret = app_usbd_cdc_acm_read(&m_app_cdc_acm, &m_cdc_data_array[index], 1);
        if (ret == NRF_SUCCESS) {
          index++;
        }
      } while (ret == NRF_SUCCESS);

      break;
    default:
      break;
  }
}

static void usbd_user_ev_handler(app_usbd_event_type_t event) {
  switch (event) {
    case APP_USBD_EVT_DRV_SUSPEND:
      NRF_LOG_INFO("APP_USBD_EVT_DRV_SUSPEND");
      break;

    case APP_USBD_EVT_DRV_RESUME:
      NRF_LOG_INFO("APP_USBD_EVT_DRV_RESUME");
      break;

    case APP_USBD_EVT_STARTED:
      NRF_LOG_INFO("APP_USBD_EVT_STARTED");
      break;

    case APP_USBD_EVT_STOPPED:
      NRF_LOG_INFO("APP_USBD_EVT_STOPPED");
      app_usbd_disable();
      break;

    case APP_USBD_EVT_POWER_DETECTED:
      NRF_LOG_INFO("USB power detected");

      if (!nrf_drv_usbd_is_enabled()) {
        NRF_LOG_INFO("enabling usbd");
        app_usbd_enable();
      }
      break;

    case APP_USBD_EVT_POWER_REMOVED: {
      NRF_LOG_INFO("USB power removed");
      app_usbd_stop();
    } break;

    case APP_USBD_EVT_POWER_READY: {
      NRF_LOG_INFO("USB ready");
      m_usb_connected = true;
      app_usbd_start();
    } break;

    default:
      break;
  }
}

void usbd_init(void) {
  ret_code_t ret;
  static const app_usbd_config_t usbd_config = {.ev_state_proc = usbd_user_ev_handler};

  app_usbd_serial_num_generate();
  ret = nrf_drv_clock_init();
  APP_ERROR_CHECK(ret);
  ret = app_usbd_init(&usbd_config);
  APP_ERROR_CHECK(ret);
  app_usbd_class_inst_t const* class_cdc_acm = app_usbd_cdc_acm_class_inst_get(&m_app_cdc_acm);
  ret = app_usbd_class_append(class_cdc_acm);
  APP_ERROR_CHECK(ret);
  NRF_LOG_INFO("USBD intitialized");
}

void usbd_start(void) {
  ret_code_t ret;
  ret = app_usbd_power_events_enable();
  APP_ERROR_CHECK(ret);
  app_usbd_enable();
  app_usbd_start();
  NRF_LOG_INFO("USBD start");
}

void usbd_write(const void* pbuf, size_t length) {
  ret_code_t ret;
  ret = app_usbd_cdc_acm_write(&m_app_cdc_acm, pbuf, length);
  if (ret != NRF_SUCCESS) {
    NRF_LOG_INFO("CDC ACM unavailable");
  }
}

void usbd_process(void) {
  while (app_usbd_event_queue_process()) {
    NRF_LOG_INFO("processing usbd queue");  // Nothing to do
  }
  if (!nrf_drv_usbd_is_enabled()) {
    NRF_LOG_INFO("nrf_drv_usbd_is_enabled(), %d", nrf_drv_usbd_is_enabled());
  }
}