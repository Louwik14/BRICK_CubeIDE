#include "App/Hall/hall_on_off.h"
#include "App/Hall/hall_adc.h"

#define PRESS_THRESHOLD   50000U
#define RELEASE_THRESHOLD 40000U

static uint8_t key_state[HALL_KEY_COUNT];
static uint8_t key_event[HALL_KEY_COUNT];

void hall_on_off_init(void)
{
    for(uint8_t i = 0; i < HALL_KEY_COUNT; i++)
    {
        key_state[i] = 0;
        key_event[i] = 0;
    }
}

void hall_on_off_process(void)
{
    for(uint8_t i = 0; i < HALL_KEY_COUNT; i++)
    {
        uint16_t v = hall_adc_get_raw(i);

        if(key_state[i] == 0)
        {
            if(v >= PRESS_THRESHOLD)
            {
                key_state[i] = 1;
                key_event[i] = 1;
            }
        }
        else
        {
            if(v <= RELEASE_THRESHOLD)
            {
                key_state[i] = 0;
            }
        }
    }
}

uint8_t hall_on_off_pressed(uint8_t key)
{
    if(key >= HALL_KEY_COUNT)
        return 0;

    return key_state[key];
}

uint8_t hall_on_off_event(uint8_t key)
{
    if(key >= HALL_KEY_COUNT)
        return 0;

    uint8_t e = key_event[key];
    key_event[key] = 0;

    return e;
}
