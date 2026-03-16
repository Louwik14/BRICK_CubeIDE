#include "App/Hall/hall_filter.h"
#include "App/Hall/hall_adc.h"

#define HALL_FILTER_SHIFT 2U

static uint16_t hall_filtered[HALL_KEY_COUNT];
static uint8_t hall_filter_primed = 0U;

void hall_filter_init(void)
{
    for(uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        hall_filtered[i] = 0U;
    }

    hall_filter_primed = 0U;
}

void hall_filter_process(void)
{
    if(hall_filter_primed == 0U)
    {
        for(uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
        {
            hall_filtered[i] = hall_adc_get_raw(i);
        }

        hall_filter_primed = 1U;
        return;
    }

    for(uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        const uint16_t raw = hall_adc_get_raw(i);
        int32_t filtered = (int32_t)hall_filtered[i];
        const int32_t delta = (int32_t)raw - filtered;

        filtered += (delta >> HALL_FILTER_SHIFT);

        if(filtered < 0)
            filtered = 0;

        if(filtered > 65535)
            filtered = 65535;

        hall_filtered[i] = (uint16_t)filtered;
    }
}

uint16_t hall_filter_get(uint8_t key)
{
    if(key >= HALL_KEY_COUNT)
        return 0U;

    return hall_filtered[key];
}
