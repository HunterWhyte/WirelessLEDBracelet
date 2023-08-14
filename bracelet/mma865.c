/* Copyright (c) 2023  Hunter Whyte */
/* https://www.nxp.com/docs/en/data-sheet/MMA8653FC.pdf */
/* https://www.nxp.com/docs/en/application-note/AN4083.pdf */
#include <stdbool.h>

#include "nrf_drv_gpiote.h"
#include "nrf_drv_twi.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"

#include "bracelet.h"
#include "mma865.h"
#include "ws2812.h"

/* I2C instance. */
static const nrf_drv_twi_t m_twi = NRF_DRV_TWI_INSTANCE(0);
static uint8_t values[6];
static int16_t a_x, a_y, a_z, jerk_x, jerk_y, jerk_z;
static int16_t max_jerk = 0;
static bool active = false;

/* ######################### HELPERS ######################### */
/* write 1 byte to a given register */
ret_code_t mma865_register_write(uint8_t register_address, uint8_t value) {
  uint8_t w2_data[2];
  w2_data[0] = register_address;
  w2_data[1] = value;
  return nrf_drv_twi_tx(&m_twi, MMA865_ADDR, w2_data, 2, false);
}

/* read n byte from a given register */
ret_code_t mma865_register_read(uint8_t register_address, uint8_t* destination,
                                uint8_t number_of_bytes) {
  ret_code_t ret_code;
  ret_code = nrf_drv_twi_tx(&m_twi, MMA865_ADDR, &register_address, 1, true);
  if (ret_code) {
    return ret_code;
  }
  ret_code = nrf_drv_twi_rx(&m_twi, MMA865_ADDR, destination, number_of_bytes);
  return ret_code;
}

/* Put the sensor into Standby Mode by clearing the Active bit of the System Control 1 Register */
void mma865_standby(void) {
  uint8_t n;
  ret_code_t ret_code;
  if (!active) {
    return;
  }
  ret_code = mma865_register_read(MMA865_CTRL_REG1, &n, 1);
  NRF_LOG_INFO("read from mma865:%x = %x", MMA865_CTRL_REG1, n);
  APP_ERROR_CHECK(ret_code);
  ret_code = mma865_register_write(MMA865_CTRL_REG1, n & ~0x1);
  APP_ERROR_CHECK(ret_code);
  active = false;
}

/* Put the sensor into Active Mode by setting the Active bit of the System Control 1 Register */
void mma865_active(void) {
  uint8_t n;
  ret_code_t ret_code;
  if (active) {
    return;
  }
  ret_code = mma865_register_read(MMA865_CTRL_REG1, &n, 1);
  NRF_LOG_INFO("read from mma865:%x = %x", MMA865_CTRL_REG1, n);
  APP_ERROR_CHECK(ret_code);
  ret_code = mma865_register_write(MMA865_CTRL_REG1, n | 0x1);
  APP_ERROR_CHECK(ret_code);
  active = true;
}

/* ######################### EVENT HANDLERS ######################### */
/* interrupt INT1 triggered */
void mma865_handler(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
  int16_t x, y, z;
  uint16_t temp;
  ret_code_t ret_code = mma865_register_read(MMA865_REG_OUT_X_MSB, values, 6);
  APP_ERROR_CHECK(ret_code);

  /* combine high and low bytes for x,y,z into signed 16 bit int */
  x = values[0] << 8;
  x |= values[1];

  y = values[2] << 8;
  y |= values[3];

  z = values[4] << 8;
  z |= values[5];

  jerk_x = x - a_x;
  jerk_y = y - a_y;
  jerk_z = z - a_z;

  a_x = x;
  a_y = y;
  a_z = z;

  ABS(jerk_x, temp);
  ABS(jerk_y, temp);
  ABS(jerk_z, temp);

  if (jerk_x > max_jerk) {
    NRF_LOG_INFO("x jerk surpassed max: %d ", jerk_x);
    max_jerk = jerk_x;
  }
  if (jerk_y > max_jerk) {
    NRF_LOG_INFO("y jerk surpassed max: %d ", jerk_y);
    max_jerk = jerk_y;
  }
  if (jerk_z > max_jerk) {
    NRF_LOG_INFO("z jerk surpassed max: %d ", jerk_z);
    max_jerk = jerk_z;
  }

  acc_data_handler(a_x, a_y, a_z, jerk_x, jerk_y, jerk_z);
}

/* ######################### INITIALIZATION ######################### */
void twi_init(void) {
  ret_code_t ret_code;

  nrf_drv_twi_config_t twi_config;
  twi_config.scl = SCL_PIN;
  twi_config.sda = SDA_PIN;
  twi_config.frequency = NRF_DRV_TWI_FREQ_100K;
  twi_config.interrupt_priority = APP_IRQ_PRIORITY_HIGH;
  twi_config.clear_bus_init = false;

  /* if twi_handler is NULL then blocking mode is enabled */
  ret_code = nrf_drv_twi_init(&m_twi, &twi_config, NULL, NULL);
  APP_ERROR_CHECK(ret_code);

  nrf_drv_twi_enable(&m_twi);
}

void mma865_init(void) {
  ret_code_t ret_code;
  uint8_t n;

  twi_init();

  /* put device into standby mode, and configure */
  mma865_standby();

  /* set sensitivity */
  ret_code = mma865_register_read(MMA865_XYZ_DATA_CFG, &n, 1);
  APP_ERROR_CHECK(ret_code);
  n &= ~0x3;
  n |= 0x2;
  ret_code = mma865_register_write(MMA865_XYZ_DATA_CFG, n);
  APP_ERROR_CHECK(ret_code);

  /* Set the data rate */
  ret_code = mma865_register_read(MMA865_CTRL_REG1, &n, 1);
  APP_ERROR_CHECK(ret_code);
  n &= ~0x38;
  n |= 0x20; /* 50Hz sample rate - this can mess with LEDs, what about ANT? */
  ret_code = mma865_register_write(MMA865_CTRL_REG1, n);
  APP_ERROR_CHECK(ret_code);

  /* set oversampling mode to normal */
  ret_code = mma865_register_read(MMA865_CTRL_REG2, &n, 1);
  APP_ERROR_CHECK(ret_code);
  n &= ~0x3;
  ret_code = mma865_register_write(MMA865_CTRL_REG2, n);
  APP_ERROR_CHECK(ret_code);

  /* set fast read mode off */
  ret_code = mma865_register_read(MMA865_CTRL_REG1, &n, 1);
  APP_ERROR_CHECK(ret_code);
  n &= ~0x2;
  ret_code = mma865_register_write(MMA865_CTRL_REG1, n);
  APP_ERROR_CHECK(ret_code);

  /* enable data ready interrupts */
  /* Configure the INT pins for Open Drain and Active Low */
  mma865_register_write(MMA865_CTRL_REG3, 0x1);
  /* Enable the Data Ready Interrupt and route it to INT1 */
  mma865_register_write(MMA865_CTRL_REG4, 0x1);
  mma865_register_write(MMA865_CTRL_REG5, 0x1);
}