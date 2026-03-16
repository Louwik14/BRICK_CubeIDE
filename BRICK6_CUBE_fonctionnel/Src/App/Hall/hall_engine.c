#include "App/Hall/hall_engine.h"
#include "App/Hall/hall_filter.h"

#define HALL_DEADZONE_PERCENT 6U
#define HALL_PRESS_PERCENT    12U
#define HALL_RELEASE_PERCENT  8U

static uint16_t hall_min[HALL_KEY_COUNT];
static uint16_t hall_max[HALL_KEY_COUNT];

static uint16_t hall_value[HALL_KEY_COUNT];
static uint8_t hall_pressed[HALL_KEY_COUNT];

void hall_engine_init(void)
{
    for(uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        hall_min[i] = 0xFFFFU;
        hall_max[i] = 0U;
        hall_value[i] = 0U;
        hall_pressed[i] = 0U;
    }
}

void hall_engine_process(void)
{
    for(uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        uint16_t v = hall_filter_get(i);

        /* calibration min/max */
        if(v < hall_min[i])
            hall_min[i] = v;

        if(v > hall_max[i])
            hall_max[i] = v;

        uint16_t range = hall_max[i] - hall_min[i];

        if(range < 10U)
            continue;

        uint16_t pos = (uint32_t)(v - hall_min[i]) * 100U / range;

        /* deadzone repos */
        if(pos < HALL_DEADZONE_PERCENT)
            pos = 0U;

        hall_value[i] = pos;

        /* hysteresis press/release */
        if(!hall_pressed[i])
        {
            if(pos > HALL_PRESS_PERCENT)
                hall_pressed[i] = 1U;
        }
        else
        {
            if(pos < HALL_RELEASE_PERCENT)
                hall_pressed[i] = 0U;
        }
    }
}

uint16_t hall_engine_get_value(uint8_t key)
{
    if(key >= HALL_KEY_COUNT)
        return 0U;

    return hall_value[key];
}

uint8_t hall_engine_is_pressed(uint8_t key)
{
    if(key >= HALL_KEY_COUNT)
        return 0U;

    return hall_pressed[key];
}

uint16_t hall_engine_get_min(uint8_t key)
{
    if(key >= HALL_KEY_COUNT)
        return 0U;

    return hall_min[key];
}

uint16_t hall_engine_get_max(uint8_t key)
{
    if(key >= HALL_KEY_COUNT)
        return 0U;

    return hall_max[key];
}
