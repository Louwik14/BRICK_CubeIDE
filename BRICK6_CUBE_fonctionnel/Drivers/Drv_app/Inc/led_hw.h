#ifndef LED_HW_H
#define LED_HW_H

#include <stdbool.h>
#include <stdint.h>

#define LED_HW_COUNT 25U

void led_hw_init(void);
void led_hw_send(const uint8_t *rgb, uint32_t count);
bool led_hw_busy(void);

#endif
