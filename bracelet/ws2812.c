/* Copyright (c) 2023  Hunter Whyte */
#include <stdbool.h>
#include "nrf.h"
#include "nrf_drv_pwm.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "ws2812.h"

static nrf_drv_pwm_t pwm_instance = NRF_DRV_PWM_INSTANCE(0);
static nrf_pwm_values_common_t pwm_sequence_values[TOTAL_CYCLES];
static nrf_pwm_sequence_t pwm_sequence;
static rgb_color_t rgb_colors[NUM_LEDS];
static hsv_color_t hsv_colors[NUM_LEDS];
static color_gen_mode_e mode = WS2812_STATIC;
static hsv_color_t shifting_colors[2] = {{0, 250, 20}, {220, 250, 20}};
bool ws2812_movement_flag = false;

rgb_color_t hsv_to_rgb(hsv_color_t hsv);
hsv_color_t rgb_to_hsv(rgb_color_t rgb);
uint8_t lerp_8 (uint8_t a, uint8_t b, uint8_t t);

/* ####################### COLOR GENERATION ####################### */
void ws2812_cycle_mode(void) {
  mode += 1;
  if (mode >= WS2812_NUM_MODES) {
    mode = 0;
  }
}

void ws2812_set_mode(color_gen_mode_e m) {
  mode = m;
}

color_gen_mode_e ws2812_get_mode(void) {
  return mode;
}

void ws2812_tick(void) {
  uint8_t i, blink_val;
  uint8_t sine;
  uint8_t shift_h, shift_s, shift_v;
  // uint8_t larger_v;

  static uint8_t clap_toggle_hue = 0;
  static uint8_t clap_pulse_brightness = 0;
  static uint8_t counter = 0;

  switch (mode) {
    case WS2812_STATIC:
      for (i = 0; i < NUM_LEDS; i++) {
        ws2812_set_rgb(i, rgb_colors[i].red, rgb_colors[i].green, rgb_colors[i].blue);
        ws2812_write();
      }
      break;
    case WS2812_BLINK:
      counter += 2;
      blink_val = (uint8_t)(counter * 2) > 128 ? 0 : 255;
      for (i = 0; i < NUM_LEDS; i++) {
        ws2812_set_hsv(i, hsv_colors[i].hue, hsv_colors[i].saturation, blink_val);
        ws2812_write();
      }
      break;
    case WS2812_PULSE:
      counter += 2;
      sine = sine_lut[counter];
      for (i = 0; i < NUM_LEDS; i++) {
        ws2812_set_hsv(i, hsv_colors[i].hue, hsv_colors[i].saturation, sine);
        ws2812_write();
      }
      break;
    case WS2812_RAINBOW:
      counter += 2;
      /* TODO: figure out why this is jittering so much */
      ws2812_set_hsv(0, counter, 250, 250);
      ws2812_write();
      ws2812_set_hsv(1, counter + 84, 250, 250);
      ws2812_write();
      ws2812_set_hsv(2, counter + 172, 250, 250);
      ws2812_write();
      break;
    case WS2812_COLOR_SHIFT:
      /* lerp between the two hsv colors using sine curve 0-255 */
      counter += 1;
      sine = sine_lut[counter];

      shift_h = lerp_8(shifting_colors[0].hue, shifting_colors[1].hue, sine);
      shift_v = lerp_8(shifting_colors[0].saturation, shifting_colors[1].saturation, sine);
      shift_s = lerp_8(shifting_colors[0].value, shifting_colors[1].value, sine);

      ws2812_set_all_hsv(shift_h, shift_s, shift_v);
      break;
    case WS2812_CLAP_TOGGLE:
      if (ws2812_movement_flag) {
        clap_toggle_hue += 25;
        ws2812_movement_flag = false;
      }
      ws2812_set_all_hsv(clap_toggle_hue, 255, 255);
      break;
    case WS2812_CLAP_PULSE:
      if (ws2812_movement_flag) {
        clap_pulse_brightness = 255;
        ws2812_movement_flag = false;
      } else {
        if (clap_pulse_brightness > 16) {
          clap_pulse_brightness -= 16;
        } else {
          clap_pulse_brightness = 1;
        }
      }
      ws2812_set_all_hsv(hsv_colors[0].hue, hsv_colors[0].saturation, clap_pulse_brightness);
      break;
    case WS2812_WAVE_RAINBOW:
      if (ws2812_movement_flag) {
        clap_toggle_hue += 10;
        ws2812_set_all_hsv(clap_toggle_hue, 250, 255);
        ws2812_write();
        ws2812_movement_flag = false;
      }
      break;
    default:
      break;
  }
}

//#define EYE_SAVER
/* ######################### LED CONTROL ######################### */
/* write rgb_colors array to pwm sequence array and start pwm playback */
void ws2812_write(void) {
  uint16_t i, j;
  uint16_t seq_offset; /* offset for current sequence */

  /* for each LED */
  for (i = 0; i < (sizeof(rgb_colors) / sizeof(rgb_color_t)); i++) {
    seq_offset = RESET_CYCLES + (i * CYCLES_PER_LED);
    /* check bits for each color */
    for (j = 0; j < 8; j++) {
      /* clear last value and write polarity */
      pwm_sequence_values[seq_offset + j + G_OFFSET] = PWM_FALLING_EDGE;
      pwm_sequence_values[seq_offset + j + R_OFFSET] = PWM_FALLING_EDGE;
      pwm_sequence_values[seq_offset + j + B_OFFSET] = PWM_FALLING_EDGE;
#ifdef EYE_SAVER
      /* green */
      pwm_sequence_values[seq_offset + j + G_OFFSET] |=
          (((rgb_colors[i].green / 4) & (0x80 >> j)) ? ONE_HIGH_TICKS : ZERO_HIGH_TICKS);
      /* red */
      pwm_sequence_values[seq_offset + j + R_OFFSET] |=
          (((rgb_colors[i].red / 4) & (0x80 >> j)) ? ONE_HIGH_TICKS : ZERO_HIGH_TICKS);
      /* blue */
      pwm_sequence_values[seq_offset + j + B_OFFSET] |=
          (((rgb_colors[i].blue / 4) & (0x80 >> j)) ? ONE_HIGH_TICKS : ZERO_HIGH_TICKS);
#else
      /* green */
      pwm_sequence_values[seq_offset + j + G_OFFSET] |=
          ((rgb_colors[i].green & (0x80 >> j)) ? ONE_HIGH_TICKS : ZERO_HIGH_TICKS);
      /* red */
      pwm_sequence_values[seq_offset + j + R_OFFSET] |=
          ((rgb_colors[i].red & (0x80 >> j)) ? ONE_HIGH_TICKS : ZERO_HIGH_TICKS);
      /* blue */
      pwm_sequence_values[seq_offset + j + B_OFFSET] |=
          ((rgb_colors[i].blue & (0x80 >> j)) ? ONE_HIGH_TICKS : ZERO_HIGH_TICKS);
#endif
    }
  }

  /* start playback */
  nrf_drv_pwm_simple_playback(&pwm_instance, &pwm_sequence, 1, 0);
}

/* set the color for a single LED using red, green, blue */
/* ws2812_write() must be called afterwards to drive the updated color to LED */
void ws2812_set_rgb(uint8_t index, uint8_t red, uint8_t green, uint8_t blue) {
  rgb_colors[index].red = red;
  rgb_colors[index].green = green;
  rgb_colors[index].blue = blue;
  hsv_colors[index] = rgb_to_hsv(rgb_colors[index]);
}

/* set color for a single LED using hue, saturation, value */
/* ws2812_write() must be called afterwards to drive the updated color to LED */
void ws2812_set_hsv(uint8_t index, uint8_t hue, uint8_t saturation, uint8_t value) {
  hsv_colors[index].hue = hue;
  hsv_colors[index].saturation = saturation;
  hsv_colors[index].value = value;
  rgb_colors[index] = hsv_to_rgb(hsv_colors[index]);
}

/* sets all LEDs to the same rgb color and drives the updated color to the LEDs */
void ws2812_set_all_rgb(uint8_t red, uint8_t green, uint8_t blue) {
  uint8_t i;
  for (i = 0; i < NUM_LEDS; i++) {
    ws2812_set_rgb(i, red, green, blue);
  }
  ws2812_write();
}

/* sets all LEDs to the same hsv color and drives the updated color to the LEDs */
void ws2812_set_all_hsv(uint8_t h, uint8_t s, uint8_t v) {
  uint8_t i;
  for (i = 0; i < NUM_LEDS; i++) {
    ws2812_set_hsv(i, h, s, v);
  }
  ws2812_write();
}

/* sets the hsv values for COLOR_SHIFT */
void ws2812_set_transition(uint8_t h1, uint8_t s1, uint8_t v1, uint8_t h2, uint8_t s2, uint8_t v2) {
  shifting_colors[0] = (hsv_color_t){h1, s1, v1};
  shifting_colors[1] = (hsv_color_t){h2, s2, v2};
}

/* shut off power to all LEDs */
void ws2812_off(void) {
  nrf_gpio_pin_clear(NPVOUT);
}

/* turn on power to all LEDs */
void ws2812_on(void) {
  nrf_gpio_pin_set(NPVOUT);
}

/* ######################### INITIALIZATION ######################### */
void pwm_init(void) {
  uint32_t ret_code;
  uint16_t i;
  nrf_drv_pwm_config_t pwm_config;

  pwm_sequence.values.p_common = pwm_sequence_values;
  pwm_sequence.length = NRF_PWM_VALUES_LENGTH(pwm_sequence_values);
  pwm_sequence.repeats = 0;
  pwm_sequence.end_delay = 0;

  pwm_config.output_pins[0] = NPDOUT;
  pwm_config.output_pins[1] = NRF_DRV_PWM_PIN_NOT_USED;
  pwm_config.output_pins[2] = NRF_DRV_PWM_PIN_NOT_USED;
  pwm_config.output_pins[3] = NRF_DRV_PWM_PIN_NOT_USED;
  pwm_config.irq_priority = APP_IRQ_PRIORITY_LOW;
  pwm_config.base_clock = NRF_PWM_CLK_16MHz;
  pwm_config.count_mode = NRF_PWM_MODE_UP;
  pwm_config.top_value = CYCLE_TICKS;
  pwm_config.load_mode = NRF_PWM_LOAD_COMMON;
  pwm_config.step_mode = NRF_PWM_STEP_AUTO;

  nrf_gpio_cfg_output(NPDOUT);
  ret_code = nrf_drv_pwm_init(&pwm_instance, &pwm_config, NULL);
  APP_ERROR_CHECK(ret_code);

  /* set reset bits to 0% duty cycle */
  for (i = 0; i < RESET_CYCLES; i++) {
    pwm_sequence_values[i] = PWM_FALLING_EDGE;
  }
}

void ws2812_init(void) {
  nrf_gpio_cfg_output(NPVOUT);
  ws2812_on();
  pwm_init();
}

/* ######################### COLOR CONVERSION ######################### */

/* converts [hue, saturation, value] color to [red, green, blue] */
/* algorithm from: https://stackoverflow.com/a/14733008 */
rgb_color_t hsv_to_rgb(hsv_color_t hsv) {
  rgb_color_t rgb;
  uint8_t region, remainder, p, q, t, r, g, b, h, s, v;
  h = hsv.hue;
  s = hsv.saturation;
  v = hsv.value;

  if (s == 0) {
    r = v;
    g = v;
    b = v;
  } else {
    region = h / 43;
    remainder = (h - (region * 43)) * 6;

    p = (v * (255 - s)) >> 8;
    q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
      case 0:
        r = v;
        g = t;
        b = p;
        break;
      case 1:
        r = q;
        g = v;
        b = p;
        break;
      case 2:
        r = p;
        g = v;
        b = t;
        break;
      case 3:
        r = p;
        g = q;
        b = v;
        break;
      case 4:
        r = t;
        g = p;
        b = v;
        break;
      default:
        r = v;
        g = p;
        b = q;
        break;
    }
  }
  rgb.red = r;
  rgb.green = g;
  rgb.blue = b;

  return rgb;
}

/* converts [red, green, blue] color to [hue, saturation, value] */
/* algorithm from: https://stackoverflow.com/a/14733008 */
hsv_color_t rgb_to_hsv(rgb_color_t rgb) {
  hsv_color_t hsv;
  uint8_t min, max, r, g, b, h, s, v;

  r = rgb.red;
  g = rgb.green;
  b = rgb.blue;

  min = r < g ? (r < b ? r : b) : (g < b ? g : b);
  max = r > g ? (r > b ? r : b) : (g > b ? g : b);

  v = max;
  s = 255 * (int64_t)(max - min) / v;

  if (v == 0) {
    h = 0;
    s = 0;
  } else if (s == 0) {
    h = 0;
  } else if (max == r) {
    h = 0 + 43 * (g - b) / (max - min);
  } else if (max == g) {
    h = 85 + 43 * (b - r) / (max - min);
  } else {
    h = 171 + 43 * (r - g) / (max - min);
  }

  hsv.hue = h;
  hsv.saturation = s;
  hsv.value = v;

  return hsv;
}

uint8_t lerp_8 (uint8_t a, uint8_t b, uint8_t t){
  if(a > b){
    return b + ((((float)(a-b))/255.0)*t);
  } else {
    return a + ((((float)(b-a))/255.0)*t);
  }
}