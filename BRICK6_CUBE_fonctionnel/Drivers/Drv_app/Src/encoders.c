#include "encoders.h"

#include "encoders_hw.h"

#define ENCODER_MAX_STEP_PER_TICK 8

static int16_t enc_accumulated_delta[ENC_COUNT];

static int16_t encoder_clamp_step(int16_t value)
{
    if (value > ENCODER_MAX_STEP_PER_TICK)
    {
        return ENCODER_MAX_STEP_PER_TICK;
    }

    if (value < -ENCODER_MAX_STEP_PER_TICK)
    {
        return -ENCODER_MAX_STEP_PER_TICK;
    }

    return value;
}

static int16_t encoder_accumulate_saturating(int16_t current, int16_t delta)
{
    int32_t sum = (int32_t)current + (int32_t)delta;

    if (sum > 32767)
    {
        sum = 32767;
    }
    else if (sum < -32768)
    {
        sum = -32768;
    }

    return (int16_t)sum;
}

void encoders_init(void)
{
    for (uint8_t i = 0U; i < (uint8_t)ENC_COUNT; i++)
    {
        enc_accumulated_delta[i] = 0;
    }

    encoders_hw_init();
}

void encoders_update(uint32_t dt_ms)
{
    (void)dt_ms;

    encoders_hw_read();

    for (uint8_t i = 0U; i < (uint8_t)ENC_COUNT; i++)
    {
        int16_t delta = (int16_t)encoders_hw_get_delta(i);
        delta = encoder_clamp_step(delta);

        if (delta == 0)
        {
            continue;
        }

        enc_accumulated_delta[i] = encoder_accumulate_saturating(enc_accumulated_delta[i], delta);
    }
}

int16_t encoder_get_delta(uint8_t encoder)
{
    if (encoder >= (uint8_t)ENC_COUNT)
    {
        return 0;
    }

    return enc_accumulated_delta[encoder];
}

int16_t encoder_consume_delta(uint8_t encoder)
{
    if (encoder >= (uint8_t)ENC_COUNT)
    {
        return 0;
    }

    const int16_t delta = enc_accumulated_delta[encoder];
    enc_accumulated_delta[encoder] = 0;
    return delta;
}
