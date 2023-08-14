/* Copyright (c) 2023  Hunter Whyte */
#include <stdint.h>
#include <string.h>
#include "app_error.h"
#include "app_timer.h"

#include "nordic_common.h"
#include "nrf_ble_lesc.h"
#include "nrf_delay.h"
#include "nrf_drv_gpiote.h"
#include "nrf_drv_saadc.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ant.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "bracelet.h"
#include "bracelet_ant.h"
#include "bracelet_ble.h"
#include "mma865.h"
#include "nfc.h"
#include "ws2812.h"

APP_TIMER_DEF(led_timer_id);
APP_TIMER_DEF(longpress_timer_id);
APP_TIMER_DEF(cooldown_timer_id);
APP_TIMER_DEF(battery_timer_id);
APP_TIMER_DEF(shutdown_timer_id);
APP_TIMER_DEF(advertising_timer_id);

// static uint8_t current_group = 0;
static control_state_e state = INACTIVE;
static bool longpress = false; /* button longpress timer active */
static bool cooldown = false;
static bool initialized = false;
static button_mode_e button_mode = BTN_NUM_MODES;

uint8_t group = 0;

/* ######################### EVENT HANDLERS ######################### */
static void switch_state(control_state_e new_state) {
  switch (new_state) {
    case (SHUTDOWN):
      break;
    case (INACTIVE):
      mma865_standby();
      ws2812_set_all_rgb(0, 0, 10);
      ws2812_set_mode(WS2812_STATIC);
      break;
    case (BUTTONS):
      mma865_standby();
      break;
    case (ADVERTISING):
      mma865_standby();
      advertising_start();
      /* pulse blue to indicate advertising */
      ws2812_set_all_rgb(0, 0, 255);
      ws2812_set_mode(WS2812_PULSE);
      break;
    case (BLE):
      mma865_standby();
      break;
    case (ANT):
      mma865_standby();
      if (state == BLE) {
        ble_disconnect();
      }
      break;
  }
  state = new_state;
}

static void cycle_button_mode() {
  button_mode += 1;
  if (button_mode >= BTN_NUM_MODES) {
    button_mode = 0;
  }

  switch (button_mode) {
    case (BTN_STATIC_PURPLE):
      mma865_standby();
      ws2812_set_all_rgb(255, 0, 255);
      ws2812_set_mode(WS2812_STATIC);
      break;
    case (BTN_BLINKING_GREEN):
      mma865_standby();
      ws2812_set_all_rgb(0, 255, 0);
      ws2812_set_mode(WS2812_BLINK);
      break;
    case (BTN_PULSING_RED):
      mma865_standby();
      ws2812_set_all_rgb(255, 0, 0);
      ws2812_set_mode(WS2812_PULSE);
      break;
    case (BTN_RAINBOW):
      mma865_standby();
      ws2812_set_mode(WS2812_RAINBOW);
      break;
    case (BTN_SHIFTING_RED_TO_PURPLE):
      mma865_standby();
      ws2812_set_transition(255, 255, 255, 100, 255, 100);
      ws2812_set_mode(WS2812_COLOR_SHIFT);
      break;
    case (BTN_SHIFTING_GREEN_TO_BLUE):
      mma865_standby();
      ws2812_set_transition(81, 255, 255, 160, 255, 255);
      ws2812_set_mode(WS2812_COLOR_SHIFT);
      break;
    case (BTN_CLAP_TOGGLE):
      mma865_active();
      ws2812_set_mode(WS2812_CLAP_TOGGLE);
      break;
    case (BTN_CLAP_PULSE):
      mma865_active();
      ws2812_set_mode(WS2812_CLAP_PULSE);
      break;
    case (BTN_WAVE_RAINBOW):
      mma865_active();
      ws2812_set_mode(WS2812_WAVE_RAINBOW);
      break;
    default:
      break;
  }
}

static void advertising_timer_handler(void* p_context) {
  advertising_stop();
  switch_state(INACTIVE);
}

static void led_timer_handler(void* p_context) {
  ws2812_tick();
}

static void cooldown_timer_handler(void* p_context) {
  cooldown = false;
}

/* check battery voltage, goto inactive mode if necessary */
static void shutdown_timer_handler(void* p_context) {
  ws2812_off();
  nrf_drv_gpiote_uninit();
  sd_power_system_off();
}

static void longpress_timer_handler(void* p_context) {
  switch (state) {
    case SHUTDOWN:
      /* ignore everything */
      break;
    case INACTIVE:
    case BUTTONS:
      NRF_LOG_INFO("3s long press");
      longpress = false;
      switch_state(ADVERTISING);
      break;
    case ADVERTISING:
      break;
    case BLE:
      NRF_LOG_INFO("3s long press");
      longpress = false;
      state = BUTTONS;
      break;
    case ANT:
      break;
    default:
      break;
  }
}

/* user push button handler */
static void button_handler(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
  bool down = !nrf_drv_gpiote_in_is_set(BTN_PIN);
  NRF_LOG_INFO("button handler %d:%d", pin, down);

  switch (state) {
    case SHUTDOWN:
      /* ignore everything */
      break;
    case INACTIVE:
      state = BUTTONS;
    case BUTTONS:
      if (down) {
        longpress = true;
        app_timer_start(longpress_timer_id, APP_TIMER_TICKS(LONGPRESS_MS), NULL);
      } else if (longpress) {
        /* if longpress hasn't activated yet, if it has then just eat input */
        app_timer_stop(longpress_timer_id);
        longpress = false;
        /* handle button press */
        cycle_button_mode();
      }
      break;
    case ADVERTISING:
      /* stop advertising */
      if (down) {
        advertising_stop();
        switch_state(INACTIVE);
      }
      break;
    case BLE:
      /* long press disables BLE connection */
      if (down) {
        app_timer_start(longpress_timer_id, APP_TIMER_TICKS(LONGPRESS_MS), NULL);
        longpress = true;
      } else if (longpress) {
        /* if longpress hasn't activated yet, if it has then just eat input */
        app_timer_stop(longpress_timer_id);
        longpress = false;
        /* do nothing */
      }
      break;
    case ANT:
      /* do nothing */
      break;
    default:
      break;
  }
}

/* check battery voltage, switch to shutdown mode if necessary */
void check_battery(void) {
  ret_code_t ret_code;
  nrf_saadc_value_t sample;
  static char battery;
  /* already in progress of shutting down */
  if (state == SHUTDOWN) {
    return;
  }
  ret_code = nrf_drv_saadc_sample_convert(0, &sample);
  APP_ERROR_CHECK(ret_code);
  NRF_LOG_INFO("battery read %d", sample);
  /* battery percentage */
  battery = 100 * ((((float)sample) - (float)MIN_BATTERY_VOLTAGE) /
                   ((float)MAX_BATTERY_VOLTAGE - (float)MIN_BATTERY_VOLTAGE));
  NRF_LOG_INFO("battery: %d", battery);
  ble_send(&battery, 1);

  if (sample < MIN_BATTERY_VOLTAGE) {
    state = SHUTDOWN;
    ws2812_set_all_rgb(3, 0, 0);
    ws2812_on();
    app_timer_start(shutdown_timer_id, APP_TIMER_TICKS(5000), NULL);
  }
}

static void battery_timer_handler(void* p_context) {
  check_battery();
}

void ble_data_handler(uint8_t control, uint8_t red, uint8_t green, uint8_t blue) {
  if (state == ADVERTISING || state == INACTIVE || state == BUTTONS) {
    switch_state(BLE);
  }
  NRF_LOG_INFO("%d, %d, %d", red, green, blue);
  ws2812_set_all_rgb(red, green, blue);

  switch (control) {
    case 0:
      mma865_standby();
      ws2812_set_mode(WS2812_STATIC);
      break;
    case 1:
      mma865_standby();
      ws2812_set_mode(WS2812_PULSE);
      break;
    case 2:
      mma865_standby();
      ws2812_set_mode(WS2812_BLINK);
      break;
    case 3:
      mma865_active();
      ws2812_set_mode(WS2812_CLAP_PULSE);
      break;
  }
}

static uint8_t lastcontrol, lastred, lastgreen, lastblue;
void ant_data_handler(uint8_t control, uint8_t red, uint8_t green, uint8_t blue) {
  if (state != ANT) {
    switch_state(ANT);
  }
  if (lastcontrol != control || lastred != red || lastgreen != green || lastblue != blue) {
    lastred = red;
    lastgreen = green;
    lastblue = blue;
    lastcontrol = control;

    switch (control) {
      case 0:
        mma865_standby();
        ws2812_set_mode(WS2812_STATIC);
        break;
      case 1:
        mma865_standby();
        ws2812_set_mode(WS2812_PULSE);
        break;
      case 2:
        mma865_standby();
        ws2812_set_mode(WS2812_BLINK);
        break;
      case 3:
        mma865_active();
        ws2812_set_mode(WS2812_CLAP_PULSE); /* TODO motion clap mode */
        break;
        /* TODO ble advertising not actually working */
    }
    ws2812_set_all_rgb(red, green, blue);
  }
  NRF_LOG_INFO("%d, %d, %d, %d", control, red, green, blue);
}

void ant_disconnect_handler(void) {
  if (state == ANT) {
    switch_state(INACTIVE);
  }
  ws2812_set_all_rgb(0, 0, 10);
}

void ble_connect_handler(void) {
  if (state == ADVERTISING) {
    switch_state(BLE);
  }
  ws2812_set_all_rgb(10, 0, 10);
  ws2812_set_mode(WS2812_STATIC);
}

void ble_disconnect_handler(void) {
  if (state == BLE) {
    switch_state(INACTIVE);
  }
}

void acc_data_handler(int16_t a_x, int16_t a_y, int16_t a_z, int16_t jerk_x, int16_t jerk_y,
                      int16_t jerk_z) {
  /* taking actual magnitude of 3D vector too slow to do in an ISR like this, 
      approximate by checking max of individual dimensions*/
  if ((button_mode == BTN_CLAP_TOGGLE) || (button_mode == BTN_CLAP_PULSE) || (state == BLE)) {
    if (!cooldown && (jerk_x > 10000 || jerk_y > 10000 || jerk_z > 10000)) {
      // NRF_LOG_INFO("threshold passed");
      ws2812_movement_flag = true;
      cooldown = true;
      app_timer_start(cooldown_timer_id, APP_TIMER_TICKS(COOLDOWN_MS), NULL);
    }
  } else if (button_mode == BTN_WAVE_RAINBOW) {
    if ((jerk_x > 1000 || jerk_y > 1000 || jerk_z > 1000)) {
      // NRF_LOG_INFO("threshold passed");
      ws2812_movement_flag = true;
    }
  }
}

void set_group(uint8_t g) {
  group = g;
  if (initialized) {
    ant_set_group(g);
  }
}

void adc_callback(const nrf_drv_saadc_evt_t* evt) {}
/* ######################### INITIALIZATION ######################### */
/* Set up user timers */
static void timers_init(void) {
  ret_code_t ret_code;
  ret_code = app_timer_init();
  APP_ERROR_CHECK(ret_code);
  ret_code = app_timer_create(&led_timer_id, APP_TIMER_MODE_REPEATED, led_timer_handler);
  APP_ERROR_CHECK(ret_code);
  ret_code =
      app_timer_create(&longpress_timer_id, APP_TIMER_MODE_SINGLE_SHOT, longpress_timer_handler);
  APP_ERROR_CHECK(ret_code);
  app_timer_create(&cooldown_timer_id, APP_TIMER_MODE_SINGLE_SHOT, cooldown_timer_handler);
  APP_ERROR_CHECK(ret_code);
  app_timer_create(&battery_timer_id, APP_TIMER_MODE_REPEATED, battery_timer_handler);
  APP_ERROR_CHECK(ret_code);
  app_timer_create(&shutdown_timer_id, APP_TIMER_MODE_SINGLE_SHOT, shutdown_timer_handler);
  APP_ERROR_CHECK(ret_code);
  app_timer_create(&advertising_timer_id, APP_TIMER_MODE_SINGLE_SHOT, advertising_timer_handler);
  APP_ERROR_CHECK(ret_code);
}

/* initialize softdevice for BLE and ANT */
static void softdevice_setup(void) {
  ret_code_t ret_code;
  uint32_t ram_start;

  ret_code = nrf_sdh_enable_request();
  APP_ERROR_CHECK(ret_code);

  ASSERT(nrf_sdh_is_enabled());

  /* Configure the BLE stack using the default settings. */
  ram_start = 0;
  ret_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
  APP_ERROR_CHECK(ret_code);

  ret_code = nrf_sdh_ble_enable(&ram_start);
  APP_ERROR_CHECK(ret_code);

  ret_code = nrf_sdh_ant_enable();
  APP_ERROR_CHECK(ret_code);

  /* Register BLE and ANT event handlers */
  NRF_SDH_BLE_OBSERVER(m_ble_observer, 3, ble_evt_handler, NULL);
  NRF_SDH_ANT_OBSERVER(m_ant_observer, 1, ant_evt_handler, NULL);
}

static void power_management_init(void) {
  ret_code_t ret_code;
  ret_code = nrf_pwr_mgmt_init();
  APP_ERROR_CHECK(ret_code);
}

/* set up logging to use UART backend */
static void log_init(void) {
  ret_code_t ret_code;
  ret_code = NRF_LOG_INIT(NULL);
  APP_ERROR_CHECK(ret_code);
  NRF_LOG_DEFAULT_BACKENDS_INIT();
}

/* set up ADC for checking battery level */
static void adc_init(void) {
  ret_code_t ret_code;
  nrf_saadc_value_t sample;
  nrf_saadc_channel_config_t channel_config =
      NRF_DRV_SAADC_DEFAULT_CHANNEL_CONFIG_SE(NRF_SAADC_INPUT_AIN0);

  ret_code = nrf_drv_saadc_init(NULL, adc_callback);
  APP_ERROR_CHECK(ret_code);

  ret_code = nrf_drv_saadc_channel_init(0, &channel_config);
  APP_ERROR_CHECK(ret_code);

  ret_code = nrf_drv_saadc_sample_convert(0, &sample);
  APP_ERROR_CHECK(ret_code);
  NRF_LOG_INFO("sample read %d", sample);
}

/* Set up GPIO pins for push button and interrupt from accelerometer */
static void gpio_init(void) {
  ret_code_t ret_code;

  ret_code = nrf_drv_gpiote_init();
  APP_ERROR_CHECK(ret_code);

  /* push button interrupt */
  nrf_drv_gpiote_in_config_t btn_config = GPIOTE_CONFIG_IN_SENSE_TOGGLE(false);
  btn_config.pull = NRF_GPIO_PIN_PULLUP;
  ret_code = nrf_drv_gpiote_in_init(BTN_PIN, &btn_config, button_handler);
  APP_ERROR_CHECK(ret_code);
  nrf_drv_gpiote_in_event_enable(BTN_PIN, true);

  /* accelerometer interrupt */
  nrf_drv_gpiote_in_config_t acc_config = GPIOTE_CONFIG_IN_SENSE_HITOLO(false);
  acc_config.pull = NRF_GPIO_PIN_PULLUP; /* active low */
  ret_code = nrf_drv_gpiote_in_init(ACC_INT1, &acc_config, mma865_handler);
  APP_ERROR_CHECK(ret_code);
  nrf_drv_gpiote_in_event_enable(ACC_INT1, true);
}

/* main entry point */
int main(void) {
  log_init();
  power_management_init();

  state = INACTIVE;

  /* initialize peripherals */
  ws2812_init();
  ws2812_set_all_rgb(0, 0, 10);

  gpio_init();
  timers_init();
  adc_init();

  softdevice_setup();
  nfc_init();

  ble_init();
  ant_rx_broadcast_setup(group);

  mma865_init();

  app_timer_start(battery_timer_id, APP_TIMER_TICKS(30000), NULL);
  app_timer_start(led_timer_id, APP_TIMER_TICKS(25), NULL);
  check_battery();
  initialized = true;
  for (;;) {
    nrf_ble_lesc_request_handler();
    if (NRF_LOG_PROCESS() == false) {
      nrf_pwr_mgmt_run();
    }
  }
}