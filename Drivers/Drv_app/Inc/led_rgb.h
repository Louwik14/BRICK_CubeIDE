#ifndef LED_RGB_H
#define LED_RGB_H

#include <stdint.h>

#include "led_framebuffer.h"

#define LED_COUNT LED_FB_COUNT

void led_init(void);
void led_set(led_id_t led, uint8_t r, uint8_t g, uint8_t b);
void led_fill(uint8_t r, uint8_t g, uint8_t b);
void led_clear(void);
void led_show(void);
void led_presentation_service(void);
uint8_t led_presentation_next_deadline(uint32_t now_ms, uint32_t *out_deadline_ms);
void led_presentation_service_deadline(uint32_t now_ms);

#endif
