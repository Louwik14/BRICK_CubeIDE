#include "led_rgb.h"

#include "led_anim.h"
#include "led_layer.h"

void led_init(void)
{
    led_fb_init();
    led_layer_init();
    led_anim_init();
}

void led_set(uint32_t id, uint8_t r, uint8_t g, uint8_t b)
{
    led_fb_set(id, r, g, b);
}

void led_fill(uint8_t r, uint8_t g, uint8_t b)
{
    led_fb_fill(r, g, b);
}

void led_clear(void)
{
    led_fb_clear();
}

void led_show(void)
{
    led_fb_commit();
}
