#pragma once

#include <stdint.h>

void board_usb_device_init(void);
void board_usb_host_init(void);
void board_usb_host_process(void);
uint8_t board_usb_next_deadline_ms(uint32_t now_ms,
                                   uint32_t *deadline_ms);
