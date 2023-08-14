/* Copyright (c) 2023  Hunter Whyte */
#ifndef BRACELET_H
#define BRACELET_H

#define BUTTON_DETECTION_DELAY APP_TIMER_TICKS(50)
#define BTN_PIN 13
#define ACC_INT1 3
#define LONGPRESS_MS 3000
#define COOLDOWN_MS 50
#define SAMPLES_IN_BUFFER 5

#define MIN_BATTERY_VOLTAGE 723 /* 3.50V */
#define MAX_BATTERY_VOLTAGE 860 /* 4.15V */

void ant_data_handler(uint8_t control, uint8_t red, uint8_t green, uint8_t blue);
void ant_disconnect_handler(void);
void ble_data_handler(uint8_t control, uint8_t red, uint8_t green, uint8_t blue);
void ble_connect_handler(void);
void ble_disconnect_handler(void);

void acc_data_handler(int16_t a_x, int16_t a_y, int16_t a_z, int16_t jerk_x, int16_t jerk_y,
                      int16_t jerk_z);
void check_battery(void);

typedef enum control_state {
  SHUTDOWN,
  INACTIVE,
  BUTTONS,
  ADVERTISING,
  BLE,
  ANT,
} control_state_e;

typedef enum button_mode {
  BTN_STATIC_PURPLE,
  BTN_BLINKING_GREEN,
  BTN_PULSING_RED,
  BTN_RAINBOW,
  BTN_SHIFTING_RED_TO_PURPLE,
  BTN_SHIFTING_GREEN_TO_BLUE,
  BTN_CLAP_TOGGLE,
  BTN_CLAP_PULSE,
  BTN_WAVE_RAINBOW,
  BTN_NUM_MODES
} button_mode_e;

void set_group(uint8_t g);

#endif /* BRACELET_H */