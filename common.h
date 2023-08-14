// shared between bracelet and controller
#ifndef COMMON_H
#define COMMON_H

#define ANT_NETWORK_NUM 0
#define BROADCAST_CHANNEL_NUMBER 0
#define CHAN_ID_DEV_NUM 2
#define CHAN_ID_DEV_TYPE 2
#define CHAN_ID_TRANS_TYPE 1
#define CHAN_PERIOD 1024  // The period in /32768
#define RF_FREQ 66
#define NUM_CHANNELS 2
#define GROUPS_PER_CHANNEL 3

#define GROUP_TO_CHANNEL(group_id) (group_id / GROUPS_PER_CHANNEL)
#define GROUP_TO_INDEX(group_id) (group_id % GROUPS_PER_CHANNEL)

#define VALID_GROUP(g) (g <= (NUM_CHANNELS * GROUPS_PER_CHANNEL) && g > 0)
typedef struct group_data {
  uint32_t control;
  uint32_t red;
  uint32_t green;
  uint32_t blue;
} group_data_t;

union payload {
  uint8_t values[8];
  uint64_t combined;
};

#define GROUP_A_DATA(payload) (payload & 0x1fffff)
#define GROUP_B_DATA(payload) ((payload >> 21) & 0x1fffff)
#define GROUP_C_DATA(payload) ((payload >> 42) & 0x1fffff)

#define GROUP_CONTROL(data) ((uint8_t)((data & 0x1f0000) >> 16))
#define GROUP_RED(data) ((uint8_t)((data & 0xf800) >> 11))
#define GROUP_GREEN(data) ((uint8_t)((data & 0x7e0) >> 5))
#define GROUP_BLUE(data) ((uint8_t)(data & 0x1f))

#define GROUP_PACKED(group) \
  (group.blue | (group.green << 5) | (group.red << 11) | (group.control << 16))

#endif  // COMMON_H
