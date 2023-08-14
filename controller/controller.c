#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "app_error.h"
#include "app_timer.h"

#include "bsp.h"
#include "hardfault.h"
#include "nordic_common.h"
#include "nrf.h"

#include "nrf_delay.h"
#include "nrf_drv_clock.h"
#include "nrf_drv_power.h"
#include "nrf_pwr_mgmt.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "controller_usbd.h"
#include "controller_ant.h"

const char* button_pressed = "PRESSED BUTTON 3\r\n";

static void bsp_event_callback(bsp_event_t ev) {
  switch ((unsigned int)ev) {
    case CONCAT_2(BSP_EVENT_KEY_, 0): {
      NRF_LOG_INFO("button 0 pressed")
      bsp_board_led_on(BSP_BOARD_LED_0);
      break;
    }
    case CONCAT_2(BSP_EVENT_KEY_, 1): {
      NRF_LOG_INFO("button 1 pressed")
      bsp_board_led_off(BSP_BOARD_LED_0);
      break;
    }
    case CONCAT_2(BSP_EVENT_KEY_, 2): {
      NRF_LOG_INFO("button 2 pressed")
      break;
    }
    case CONCAT_2(BSP_EVENT_KEY_, 3): {
      NRF_LOG_INFO("button 3 pressed");
      usbd_write(button_pressed, strlen(button_pressed));
      break;
    }
    default:
      return;
  }
}

static void utils_setup(void) {
  ret_code_t err_code = app_timer_init();
  APP_ERROR_CHECK(err_code);

  err_code = bsp_init(BSP_INIT_LEDS | BSP_INIT_BUTTONS, bsp_event_callback);
  APP_ERROR_CHECK(err_code);

  err_code = nrf_pwr_mgmt_init();
  APP_ERROR_CHECK(err_code);
}

static void log_init(void) {
  ret_code_t err_code = NRF_LOG_INIT(NULL);
  APP_ERROR_CHECK(err_code);

  NRF_LOG_DEFAULT_BACKENDS_INIT();
}

int main(void) {
  log_init();
  utils_setup();

  usbd_init();
  ant_init();

  usbd_start();
  NRF_LOG_INFO("USBD started");
  ant_start();
  NRF_LOG_INFO("ANT tx started");

  for (;;) {
    usbd_process();
    NRF_LOG_FLUSH();
  }
}
