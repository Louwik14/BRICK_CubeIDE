#ifndef LED_LAYER_H
#define LED_LAYER_H

#include <stdint.h>

#include "led_framebuffer.h"

typedef enum
{
    LED_LAYER_BASE = 0,
    LED_LAYER_UI,
    LED_LAYER_SEQ,
    LED_LAYER_ANIM,
    LED_LAYER_COUNT
} led_layer_id_t;

void led_layer_init(void);
void led_layer_clear(uint32_t layer);
void led_layer_clear_all(void);
void led_layer_set(uint32_t layer, led_id_t led, uint8_t r, uint8_t g, uint8_t b);
void led_layer_fill(uint32_t layer, uint8_t r, uint8_t g, uint8_t b);
void led_layer_compose(void);
void led_layer_commit(void);

#endif
