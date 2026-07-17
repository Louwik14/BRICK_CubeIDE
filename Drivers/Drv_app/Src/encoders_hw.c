#include "encoders_hw.h"

#include "Board/board_controls.h"

#include "cmsis_gcc.h"

#include <limits.h>

static uint8_t enc_prev_state[ENC_COUNT];
static volatile int16_t enc_raw_delta[ENC_COUNT];

static const int8_t quad_table[16] = {
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0,
};

static uint8_t enc_read_state(uint8_t encoder)
{
    return board_controls_encoder_state(encoder);
}

void encoders_hw_init(void)
{
    for (uint8_t i = 0U; i < (uint8_t)ENC_COUNT; i++)
    {
        enc_prev_state[i] = enc_read_state(i);
        enc_raw_delta[i] = 0;
    }
}

void encoders_fast_poll_init(void)
{
    board_controls_start_encoder_fast_poll_timer();
}

void encoders_fast_poll_irq(void)
{
    encoders_hw_read();
}

void encoders_hw_read(void)
{
    for (uint8_t i = 0U; i < (uint8_t)ENC_COUNT; i++)
    {
        const uint8_t prev = enc_prev_state[i];
        const uint8_t now = enc_read_state(i);

        if (now == prev)
        {
            continue;
        }

        const uint8_t idx = (uint8_t)((prev << 2) | now);
        const int8_t step = quad_table[idx];

        enc_prev_state[i] = now;

        if (step == 0)
        {
            continue;
        }

        const int32_t sum = (int32_t)enc_raw_delta[i] + (int32_t)step;
        if (sum > (int32_t)INT16_MAX)
        {
            enc_raw_delta[i] = INT16_MAX;
        }
        else if (sum < (int32_t)INT16_MIN)
        {
            enc_raw_delta[i] = INT16_MIN;
        }
        else
        {
            enc_raw_delta[i] = (int16_t)sum;
        }
    }
}

int16_t encoders_hw_get_delta(uint8_t encoder)
{
    if (encoder >= (uint8_t)ENC_COUNT)
    {
        return 0;
    }

    int16_t delta;

    __disable_irq();
    delta = enc_raw_delta[encoder];
    enc_raw_delta[encoder] = 0;
    __enable_irq();

    return delta;
}
