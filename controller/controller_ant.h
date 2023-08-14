#ifndef CONTROLLER_ANT_H
#define CONTROLLER_ANT_H

void ant_init(void);
void ant_start(void);
void ant_update_payload(uint8_t group, uint8_t control, uint8_t red, uint8_t green, uint8_t blue);

#endif  // CONTROLLER_ANT_H