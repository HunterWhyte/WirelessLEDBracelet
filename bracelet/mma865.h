/* Copyright (c) 2023  Hunter Whyte */
#ifndef MMA865_H
#define MMA865_H

/* Number of possible TWI addresses. */
#define TWI_ADDRESSES 127

#define MMA865_ADDR 0x1DU
#define MMA865_REG_STATUS 0x00U
#define MMA865_REG_OUT_X_MSB 0x01U
#define MMA865_REG_OUT_X_LSB 0x02U
#define MMA865_REG_OUT_Y_MSB 0x03U
#define MMA865_REG_OUT_Y_LSB 0x04U
#define MMA865_REG_OUT_Z_MSB 0x05U
#define MMA865_REG_OUT_Z_LSB 0x06U
#define MMA865_REG_SYSMOD 0x0BU
#define MMA865_XYZ_DATA_CFG 0x0EU
#define MMA865_CTRL_REG1 0x2AU
#define MMA865_CTRL_REG2 0x2BU
#define MMA865_CTRL_REG3 0x2CU
#define MMA865_CTRL_REG4 0x2DU
#define MMA865_CTRL_REG5 0x2EU

#define SCL_PIN 27
#define SDA_PIN 26

/* convert signed integer v to its absolute value without branching t is a temp uint16_t
   https://graphics.stanford.edu/~seander/bithacks.html#IntegerAbs */
#define ABS(v, t) \
  do {            \
    t = v >> 15;  \
    v ^= t;       \
    v += t & 1;   \
  } while (0)

void mma865_init(void);
void mma865_handler(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action);
void mma865_standby(void);
void mma865_active(void);

#endif /* MMA865_H */