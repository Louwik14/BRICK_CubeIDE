#ifndef LED_ANIM_H
#define LED_ANIM_H

#include <stdint.h>

void led_anim_init(void);
void led_anim_tick(uint32_t ms);
void led_anim_blink(uint32_t led, uint8_t r, uint8_t g, uint8_t b, uint32_t period);
void led_anim_stop(uint32_t led);
void led_anim_stop_all(void);

#endif
