#ifndef CONTROLLER_USBD_H
#define CONTROLLER_USBD_H

void usbd_init(void);
void usbd_start(void);
void usbd_write(const void* pbuf, size_t length);
void usbd_process(void);

#endif  // CONTROLLER_USBD_H