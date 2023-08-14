/* Copyright (c) 2023  Hunter Whyte */
#include <stdint.h>

#include "app_error.h"
#include "nrf_log.h"
#include "nrf_sdh_ant.h"

#include "ant_channel_config.h"
#include "ant_error.h"
#include "ant_interface.h"
#include "ant_parameters.h"

#include "bracelet.h"
#include "bracelet_ant.h"
#include "common.h"

static uint8_t open_group;

/* ######################### EVENT HANDLERS ######################### */
void ant_evt_handler(ant_evt_t* p_ant_evt, void* p_context) {
  uint8_t index;
  union payload message_payload;
  uint32_t data;
  switch (p_ant_evt->event) {
    case EVENT_RX:
      /* parse data from received message */
      index = GROUP_TO_INDEX(open_group);
      for (int i = 0; i < ANT_STANDARD_DATA_PAYLOAD_SIZE; i++) {
        message_payload.values[i] = p_ant_evt->message.ANT_MESSAGE_aucPayload[i];
      }
      if (index == 0)
        data = GROUP_A_DATA(message_payload.combined);
      else if (index == 1)
        data = GROUP_B_DATA(message_payload.combined);
      else
        data = GROUP_C_DATA(message_payload.combined);

      ant_data_handler(GROUP_CONTROL(data), (GROUP_RED(data) << 3), (GROUP_GREEN(data) << 2),
                       (GROUP_BLUE(data) << 3));
      break;
    case EVENT_RX_FAIL:
      NRF_LOG_INFO("ant: rx fail event");
      break;
    case EVENT_RX_FAIL_GO_TO_SEARCH:
      NRF_LOG_INFO("ant: dropped to search event");
      ant_disconnect_handler();
      break;
    case EVENT_RX_SEARCH_TIMEOUT:
      NRF_LOG_INFO("ant: search timed out event");
      break;
    case EVENT_CHANNEL_CLOSED:
      NRF_LOG_INFO("ant: channel closed event");
      break;
    default:
      break;
  }
}

/* ######################### ANT CONTROL ######################### */
void ant_set_group(uint8_t group) {
  uint8_t old_channel, new_channel;
  ret_code_t ret_code;

  if (open_group == group) {
    NRF_LOG_INFO("group %d already open", group);
    return;
  }

  old_channel = GROUP_TO_CHANNEL(open_group);
  new_channel = GROUP_TO_CHANNEL(group);

  NRF_LOG_INFO("old group %d new group %d", open_group, group);

  if (old_channel != new_channel) {
    NRF_LOG_INFO("old channel %d new channel %d", old_channel, new_channel);
    ret_code = sd_ant_channel_close(old_channel);
    NRF_LOG_INFO("sd_ant_channel_close %d", ret_code);
    APP_ERROR_CHECK(ret_code);

    ret_code = sd_ant_channel_open(new_channel);
    NRF_LOG_INFO("sd_ant_channel_open %d", ret_code);
    APP_ERROR_CHECK(ret_code);
  }
  open_group = group;
}

/* ######################### INITIALIZATION ######################### */
void ant_rx_broadcast_setup(uint8_t group) {
  ret_code_t ret_code;
  for (int i = 0; i < NUM_CHANNELS; i++) {
    ant_channel_config_t broadcast_channel_config = {
        .channel_number = i,
        .channel_type = CHANNEL_TYPE_SLAVE_RX_ONLY,
        .ext_assign = 0x00,
        .rf_freq = RF_FREQ + i,
        .transmission_type = CHAN_ID_TRANS_TYPE,
        .device_type = CHAN_ID_DEV_TYPE,
        .device_number = CHAN_ID_DEV_NUM + i,
        .channel_period = CHAN_PERIOD,
        .network_number = ANT_NETWORK_NUM,
    };

    ret_code = ant_channel_init(&broadcast_channel_config);
    APP_ERROR_CHECK(ret_code);

    /* When applied to an assigned slave channel, ucTimeout is in 2.5 second increments */
    /* debug mode set channel search to infinite timeout */
    ret_code = sd_ant_channel_search_timeout_set(i, 255);
    APP_ERROR_CHECK(ret_code);
  }

  open_group = group;
  ret_code = sd_ant_channel_open(GROUP_TO_CHANNEL(open_group));
  APP_ERROR_CHECK(ret_code);

  NRF_LOG_INFO("ant channel setup finished");
}