#ifndef LED_FRAMEBUFFER_H
#define LED_FRAMEBUFFER_H

#include <stdint.h>

#include "led_hw.h"
#include "led_ids.h"

#define LED_FB_COUNT LED_COUNT_TOTAL

void led_fb_init(void);
void led_fb_clear(void);
void led_fb_set(led_id_t led, uint8_t r, uint8_t g, uint8_t b);
void led_fb_fill(uint8_t r, uint8_t g, uint8_t b);
uint8_t led_fb_commit(void);

#endif
