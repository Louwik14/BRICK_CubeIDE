#include "led_framebuffer.h"

#include <string.h>

static uint8_t led_fb_rgb[LED_FB_COUNT * 3U];

void led_fb_init(void)
{
    led_hw_init();
    led_fb_clear();
}

void led_fb_clear(void)
{
    memset(led_fb_rgb, 0, sizeof(led_fb_rgb));
}

void led_fb_set(uint32_t id, uint8_t r, uint8_t g, uint8_t b)
{
    if (id >= LED_FB_COUNT)
    {
        return;
    }

    led_fb_rgb[(id * 3U) + 0U] = r;
    led_fb_rgb[(id * 3U) + 1U] = g;
    led_fb_rgb[(id * 3U) + 2U] = b;
}

void led_fb_fill(uint8_t r, uint8_t g, uint8_t b)
{
    for (uint32_t i = 0U; i < LED_FB_COUNT; i++)
    {
        led_fb_set(i, r, g, b);
    }
}

void led_fb_commit(void)
{
    led_hw_send(led_fb_rgb, LED_FB_COUNT);
}
