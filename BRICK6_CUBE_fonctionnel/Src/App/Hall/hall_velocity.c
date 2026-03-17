#include "App/Hall/hall_velocity.h"
#include "App/Hall/hall_engine.h"
#include "stm32h7xx_hal.h"

#define HALL_VELOCITY_MIN_US  500U
#define HALL_VELOCITY_MAX_US  30000U
#define HALL_VELOCITY_MAX     127U

#define HALL_START_THRESHOLD  12U
#define HALL_END_THRESHOLD    40U

static uint32_t velocity_start_time[HALL_KEY_COUNT];
static uint8_t  velocity_armed[HALL_KEY_COUNT];
static uint8_t  velocity[HALL_KEY_COUNT];

static uint32_t hall_time_us(void)
{
    return HAL_GetTick() * 1000U;
}

static uint8_t clamp_uint8(int32_t v)
{
    if(v < 0) return 0;
    if(v > HALL_VELOCITY_MAX) return HALL_VELOCITY_MAX;
    return (uint8_t)v;
}

void hall_velocity_init(void)
{
    for(uint8_t i = 0; i < HALL_KEY_COUNT; i++)
    {
        velocity_start_time[i] = 0;
        velocity_armed[i] = 0;
        velocity[i] = 0;
    }
}

void hall_velocity_process(void)
{
    uint32_t now = hall_time_us();

    for(uint8_t i = 0; i < HALL_KEY_COUNT; i++)
    {
        uint16_t pos = hall_engine_get_value(i);

        /* armement */
        if(!velocity_armed[i])
        {
            if(pos < HALL_START_THRESHOLD)
            {
                velocity_armed[i] = 1U;
            }

            continue;
        }

        /* début mesure */
        if(velocity_armed[i] && velocity_start_time[i] == 0U)
        {
            if(pos > HALL_START_THRESHOLD)
            {
                velocity_start_time[i] = now;
            }

            continue;
        }

        /* fin mesure */
        if(pos > HALL_END_THRESHOLD)
        {
            uint32_t dt =
                now - velocity_start_time[i];

            if(dt < HALL_VELOCITY_MIN_US)
                dt = HALL_VELOCITY_MIN_US;

            if(dt > HALL_VELOCITY_MAX_US)
                dt = HALL_VELOCITY_MAX_US;

            uint32_t span =
                HALL_VELOCITY_MAX_US -
                HALL_VELOCITY_MIN_US;

            uint32_t scaled =
                (HALL_VELOCITY_MAX_US - dt) *
                HALL_VELOCITY_MAX /
                span;

            velocity[i] = clamp_uint8((int32_t)scaled);

            velocity_start_time[i] = 0;
            velocity_armed[i] = 0;
        }
    }
}

uint8_t hall_velocity_get(uint8_t key)
{
    if(key >= HALL_KEY_COUNT)
        return 0;

    return velocity[key];
}
