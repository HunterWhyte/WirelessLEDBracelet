/* Copyright (c) 2023  Hunter Whyte */
#ifndef BRACELET_ANT_H
#define BRACELET_ANT_H

void ant_evt_handler(ant_evt_t* p_ant_evt, void* p_context);
void ant_rx_broadcast_setup(uint8_t group);
void ant_set_group(uint8_t group);

#endif  /* BRACELET_ANT_H */