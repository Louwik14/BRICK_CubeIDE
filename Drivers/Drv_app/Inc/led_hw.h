#ifndef LED_HW_H
#define LED_HW_H

#include <stdbool.h>
#include <stdint.h>

#include "Board/board_led_transport.h"

#define LED_HW_COUNT BOARD_LED_TRANSPORT_COUNT

void led_hw_init(void);
void led_hw_send(const uint8_t *rgb, uint32_t count);
bool led_hw_busy(void);

#endif
