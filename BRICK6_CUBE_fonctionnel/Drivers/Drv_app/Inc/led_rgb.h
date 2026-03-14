#ifndef LED_RGB_H
#define LED_RGB_H

#include <stdint.h>

#include "led_framebuffer.h"

#define LED_COUNT LED_FB_COUNT

void led_init(void);
void led_set(uint32_t id, uint8_t r, uint8_t g, uint8_t b);
void led_fill(uint8_t r, uint8_t g, uint8_t b);
void led_clear(void);
void led_show(void);

#endif
