#include "led_anim.h"

#include "led_layer.h"

#define LED_ANIM_MAX_SLOTS 16U

typedef struct
{
    uint8_t active;
    uint8_t on;
    uint32_t led;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint32_t period_ms;
    uint32_t elapsed_ms;
} led_anim_slot_t;

static led_anim_slot_t anim_slots[LED_ANIM_MAX_SLOTS];

static led_anim_slot_t *find_slot(uint32_t led)
{
    for (uint32_t i = 0U; i < LED_ANIM_MAX_SLOTS; i++)
    {
        if ((anim_slots[i].active != 0U) && (anim_slots[i].led == led))
        {
            return &anim_slots[i];
        }
    }

    for (uint32_t i = 0U; i < LED_ANIM_MAX_SLOTS; i++)
    {
        if (anim_slots[i].active == 0U)
        {
            return &anim_slots[i];
        }
    }

    return &anim_slots[0];
}

void led_anim_init(void)
{
    led_anim_stop_all();
}

void led_anim_stop_all(void)
{
    for (uint32_t i = 0U; i < LED_ANIM_MAX_SLOTS; i++)
    {
        anim_slots[i].active = 0U;
        anim_slots[i].on = 0U;
        anim_slots[i].elapsed_ms = 0U;
    }

    led_layer_clear(LED_LAYER_ANIM);
}

void led_anim_stop(uint32_t led)
{
    for (uint32_t i = 0U; i < LED_ANIM_MAX_SLOTS; i++)
    {
        if ((anim_slots[i].active != 0U) && (anim_slots[i].led == led))
        {
            anim_slots[i].active = 0U;
            anim_slots[i].on = 0U;
        }
    }

    led_layer_set(LED_LAYER_ANIM, led, 0U, 0U, 0U);
}

void led_anim_blink(uint32_t led, uint8_t r, uint8_t g, uint8_t b, uint32_t period)
{
    if ((period < 2U) || (led >= LED_FB_COUNT))
    {
        return;
    }

    led_anim_slot_t *slot = find_slot(led);

    slot->active = 1U;
    slot->on = 1U;
    slot->led = led;
    slot->r = r;
    slot->g = g;
    slot->b = b;
    slot->period_ms = period;
    slot->elapsed_ms = 0U;
}

void led_anim_tick(uint32_t ms)
{
    uint8_t changed = 0U;

    led_layer_clear(LED_LAYER_ANIM);

    for (uint32_t i = 0U; i < LED_ANIM_MAX_SLOTS; i++)
    {
        if (anim_slots[i].active == 0U)
        {
            continue;
        }

        anim_slots[i].elapsed_ms += ms;

        uint32_t half_period = anim_slots[i].period_ms / 2U;
        while (anim_slots[i].elapsed_ms >= half_period)
        {
            anim_slots[i].elapsed_ms -= half_period;
            anim_slots[i].on = (anim_slots[i].on == 0U) ? 1U : 0U;
            changed = 1U;
        }

        if (anim_slots[i].on != 0U)
        {
            led_layer_set(LED_LAYER_ANIM,
                          anim_slots[i].led,
                          anim_slots[i].r,
                          anim_slots[i].g,
                          anim_slots[i].b);
        }
    }

    if (changed != 0U)
    {
        led_layer_commit();
    }
}
