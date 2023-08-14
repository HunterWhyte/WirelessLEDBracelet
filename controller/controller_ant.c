#include <stdint.h>
#include <string.h>

#include "app_error.h"

#include "nrf_log.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ant.h"

#include "boards.h"
#include "bsp.h"

#include "ant_channel_config.h"
#include "ant_error.h"
#include "ant_interface.h"
#include "ant_parameters.h"

#include "common.h"
#include "controller_ant.h"

#define APP_ANT_OBSERVER_PRIO 1

group_data_t groups[NUM_CHANNELS * GROUPS_PER_CHANNEL];

void ant_evt_handler(ant_evt_t* p_ant_evt, void* p_context) {
  nrf_pwr_mgmt_feed();  // indicate that there is activity
  switch (p_ant_evt->event) {
    case EVENT_TX:
      break;
    default:
      break;
  }
}

static void ant_tx_broadcast_setup(void) {
  for (int i = 0; i < NUM_CHANNELS; i++) {
    ant_channel_config_t broadcast_channel_config = {
        .channel_number = i,
        .channel_type = CHANNEL_TYPE_MASTER,
        .ext_assign = 0x00,
        .rf_freq = RF_FREQ + i,
        .transmission_type = CHAN_ID_TRANS_TYPE,
        .device_type = CHAN_ID_DEV_TYPE,
        .device_number = CHAN_ID_DEV_NUM + i,
        .channel_period = CHAN_PERIOD,
        .network_number = ANT_NETWORK_NUM,
    };
    NRF_LOG_INFO("ant_channel_init");
    ret_code_t ret_code = ant_channel_init(&broadcast_channel_config);
    NRF_LOG_INFO("%d", ret_code);
    APP_ERROR_CHECK(ret_code);

    // Fill tx buffer for the first frame.
    NRF_LOG_INFO("ant_update_payload");
    ant_update_payload(i, 0, 0, 10, 0);

    // Open channel.
    NRF_LOG_INFO("sd_ant_channel_open");
    ret_code = sd_ant_channel_open(i);
    APP_ERROR_CHECK(ret_code);

    NRF_LOG_INFO("sd_ant_channel_radio_tx_power_set");
    ret_code = sd_ant_channel_radio_tx_power_set(i, RADIO_TX_POWER_LVL_5, 0);
    APP_ERROR_CHECK(ret_code);
  }
}

void ant_update_payload(uint8_t group, uint8_t control, uint8_t red, uint8_t green, uint8_t blue) {
  uint8_t channel;
  union payload message;
  message.combined = 0;

  if (group >= NUM_CHANNELS * GROUPS_PER_CHANNEL) {
    return;
  }

  channel = GROUP_TO_CHANNEL(group);

  // update data for group
  groups[group].control = control & 0x1F;  // 5 bits
  groups[group].red = red >> 3;            // 8 bit to 5 bit
  groups[group].green = green >> 2;
  groups[group].blue = blue >> 3;

  // build payload
  message.combined |= (uint64_t) GROUP_PACKED(groups[(channel*GROUPS_PER_CHANNEL) + 0]);      
  message.combined |= (uint64_t) GROUP_PACKED(groups[(channel*GROUPS_PER_CHANNEL) + 1]) << 21;
  message.combined |= (uint64_t) GROUP_PACKED(groups[(channel*GROUPS_PER_CHANNEL) + 2]) << 42;
  // Broadcast the data.
  ret_code_t ret_code =
      sd_ant_broadcast_message_tx(channel, ANT_STANDARD_DATA_PAYLOAD_SIZE, message.values);
  APP_ERROR_CHECK(ret_code);
}

void ant_init(void) {
  ret_code_t ret_code = nrf_sdh_enable_request();
  APP_ERROR_CHECK(ret_code);

  ASSERT(nrf_sdh_is_enabled());

  ret_code = nrf_sdh_ant_enable();
  APP_ERROR_CHECK(ret_code);

  NRF_SDH_ANT_OBSERVER(m_ant_observer, APP_ANT_OBSERVER_PRIO, ant_evt_handler, NULL);
}

void ant_start(void) {
  ant_tx_broadcast_setup();
}