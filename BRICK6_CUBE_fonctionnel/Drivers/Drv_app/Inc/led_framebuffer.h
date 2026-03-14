#ifndef LED_FRAMEBUFFER_H
#define LED_FRAMEBUFFER_H

#include <stdint.h>

#include "led_hw.h"

#define LED_FB_COUNT LED_HW_COUNT

void led_fb_init(void);
void led_fb_clear(void);
void led_fb_set(uint32_t id, uint8_t r, uint8_t g, uint8_t b);
void led_fb_fill(uint8_t r, uint8_t g, uint8_t b);
void led_fb_commit(void);

#endif
