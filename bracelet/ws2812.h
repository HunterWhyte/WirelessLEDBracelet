/* Copyright (c) 2023  Hunter Whyte */
#ifndef WS2812_H
#define WS2812_H

#define NPDOUT 20 /* pin driving data in of first ws2812B */
#define NPVOUT 18 /* pin driving low-side driver to control power to LEDs */
/*
The WS2812B LEDs use a proprietary protocol. A sequence of 24 bits controls
the brightness of each LED In the following order:
G7 G6 G5 G4 G3 G2 G1 G0 R7 R6 R5 R4 R3 R2 R1 R0 B7 B6 B5 B4 B3 B2 B1 B0
G is green, R is red, and B is blue.
Each sequence must be preceeded by a reset which is the signal being held low 
for at least 50us. Following the reset code the following timings are defined for
representing 1s and 0s in the sequence, all values have error tolerance of +-150ns
  ONE  (1) = 0.8us high -> 0.45us low
  ZERO (0) = 0.4us high -> 0.85us low
The total period of a single bit is 1.25us for both 1 and 0.
To control multiple WS2812s they are daisy chained together, multiple 24-bit 
sets of color data are sent in a row.
In the case of 3 LEDs, we send three 24 bit colors in a row. The first color is
consumed by the first LED in the chain, and the remaining colors are forwarded.
Then the second LED consumes the second 24 bit sequence and so on.
In order to generate the sequences the PWM peripheral is used. We can feed an
array of duty cycle values, which will get played back, advancing one value each
period. The period is set by the PWM top value if we set the PWM top value to be
the length of a single bit (1.25us) then set the PWM peripheral to have a falling
edge polarity we can control whether the bit is high or low with the duty cycle
value.
*/

/* Using 16MHz PWM clock, 1 tick = 62.5ns */
#define CYCLE_TICKS 20  /* 1.25us/62.5ns = 20 ticks */
#define RESET_TICKS 800 /* 50us/62.5ns   = 800 ticks, or 20 full cycles */
#define RESET_CYCLES (RESET_TICKS / CYCLE_TICKS)
#define ONE_HIGH_TICKS 13 /* 0.8us/62.5ns  = 12.8. 13 ticks = 0.8125us */
#define ZERO_HIGH_TICKS 6 /* 0.4us/62.5ns  = 6.4.   6 ticks = 0.0.375us */
#define NUM_LEDS 3
#define CYCLES_PER_LED 24
#define CYCLES_PER_SEQUENCE (CYCLES_PER_LED * NUM_LEDS)
#define TOTAL_CYCLES (CYCLES_PER_SEQUENCE + RESET_CYCLES)
/* top bit of 16 bit sequence value controls polarity */
#define PWM_FALLING_EDGE 0x8000; /* 1 = falling edge, 0 is rising edge */
#define G_OFFSET 0
#define R_OFFSET 8
#define B_OFFSET 16

typedef struct rgb_color {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} rgb_color_t;

typedef struct hsv_color {
  uint8_t hue;
  uint8_t saturation;
  uint8_t value;
} hsv_color_t;

typedef enum color_gen_mode {
  WS2812_STATIC,
  WS2812_BLINK,
  WS2812_PULSE,
  WS2812_RAINBOW,
  WS2812_COLOR_SHIFT,
  WS2812_CLAP_TOGGLE,
  WS2812_CLAP_PULSE,
  WS2812_WAVE_RAINBOW,
  WS2812_NUM_MODES
} color_gen_mode_e;

void ws2812_cycle_mode(void);
void ws2812_set_mode(color_gen_mode_e m);
color_gen_mode_e ws2812_get_mode(void);

void ws2812_init(void);
void ws2812_off(void);
void ws2812_on(void);

void ws2812_set_all_rgb(uint8_t red, uint8_t green, uint8_t blue);
void ws2812_set_rgb(uint8_t index, uint8_t red, uint8_t green, uint8_t blue);

void ws2812_set_all_hsv(uint8_t hue, uint8_t saturation, uint8_t value);
void ws2812_set_hsv(uint8_t index, uint8_t hue, uint8_t saturation, uint8_t value);
void ws2812_set_transition(uint8_t h1, uint8_t s1, uint8_t v1, uint8_t h2, uint8_t s2, uint8_t v2);

void ws2812_write(void);
void ws2812_tick(void);
void ws2812_detect_motion(void);

static const uint8_t sine_lut[256] = {
    0x0,  0x0,  0x0,  0x1,  0x1,  0x1,  0x2,  0x2,  0x3,  0x4,  0x5,  0x5,  0x6,  0x7,  0x9,  0xa,
    0xb,  0xc,  0xe,  0xf,  0x11, 0x12, 0x14, 0x15, 0x17, 0x19, 0x1b, 0x1d, 0x1f, 0x21, 0x23, 0x25,
    0x28, 0x2a, 0x2c, 0x2f, 0x31, 0x34, 0x36, 0x39, 0x3b, 0x3e, 0x41, 0x43, 0x46, 0x49, 0x4c, 0x4f,
    0x52, 0x55, 0x58, 0x5a, 0x5d, 0x61, 0x64, 0x67, 0x6a, 0x6d, 0x70, 0x73, 0x76, 0x79, 0x7c, 0x80,
    0x83, 0x86, 0x89, 0x8c, 0x8f, 0x92, 0x95, 0x98, 0x9b, 0x9e, 0xa2, 0xa5, 0xa7, 0xaa, 0xad, 0xb0,
    0xb3, 0xb6, 0xb9, 0xbc, 0xbe, 0xc1, 0xc4, 0xc6, 0xc9, 0xcb, 0xce, 0xd0, 0xd3, 0xd5, 0xd7, 0xda,
    0xdc, 0xde, 0xe0, 0xe2, 0xe4, 0xe6, 0xe8, 0xea, 0xeb, 0xed, 0xee, 0xf0, 0xf1, 0xf3, 0xf4, 0xf5,
    0xf6, 0xf8, 0xf9, 0xfa, 0xfa, 0xfb, 0xfc, 0xfd, 0xfd, 0xfe, 0xfe, 0xfe, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xfe, 0xfe, 0xfe, 0xfd, 0xfd, 0xfc, 0xfb, 0xfa, 0xfa, 0xf9, 0xf8, 0xf6, 0xf5,
    0xf4, 0xf3, 0xf1, 0xf0, 0xee, 0xed, 0xeb, 0xea, 0xe8, 0xe6, 0xe4, 0xe2, 0xe0, 0xde, 0xdc, 0xda,
    0xd7, 0xd5, 0xd3, 0xd0, 0xce, 0xcb, 0xc9, 0xc6, 0xc4, 0xc1, 0xbe, 0xbc, 0xb9, 0xb6, 0xb3, 0xb0,
    0xad, 0xaa, 0xa7, 0xa5, 0xa2, 0x9e, 0x9b, 0x98, 0x95, 0x92, 0x8f, 0x8c, 0x89, 0x86, 0x83, 0x80,
    0x7c, 0x79, 0x76, 0x73, 0x70, 0x6d, 0x6a, 0x67, 0x64, 0x61, 0x5d, 0x5a, 0x58, 0x55, 0x52, 0x4f,
    0x4c, 0x49, 0x46, 0x43, 0x41, 0x3e, 0x3b, 0x39, 0x36, 0x34, 0x31, 0x2f, 0x2c, 0x2a, 0x28, 0x25,
    0x23, 0x21, 0x1f, 0x1d, 0x1b, 0x19, 0x17, 0x15, 0x14, 0x12, 0x11, 0xf,  0xe,  0xc,  0xb,  0xa,
    0x9,  0x7,  0x6,  0x5,  0x5,  0x4,  0x3,  0x2,  0x2,  0x1,  0x1,  0x1,  0x0,  0x0,  0x0,  0x0,
};

extern bool ws2812_movement_flag;

#endif /* WS2812_H */