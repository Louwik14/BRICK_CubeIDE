#include "encoders_hw.h"

#include "stm32h7xx.h"

typedef struct
{
    GPIO_TypeDef *port_a;
    uint32_t pin_a;
    GPIO_TypeDef *port_b;
    uint32_t pin_b;
} encoder_hw_pin_t;

static const encoder_hw_pin_t enc_hw_pins[ENC_COUNT] = {
    {GPIOB, GPIO_PIN_6, GPIOD, GPIO_PIN_12},
    {GPIOH, GPIO_PIN_8, GPIOH, GPIO_PIN_12},
    {GPIOH, GPIO_PIN_10, GPIOH, GPIO_PIN_11},
    {GPIOA, GPIO_PIN_2, GPIOA, GPIO_PIN_1},
};

static uint8_t enc_prev_state[ENC_COUNT];
static int8_t enc_raw_delta[ENC_COUNT];

static const int8_t quad_table[16] = {
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0,
};

static inline uint8_t enc_read_pin(GPIO_TypeDef *port, uint32_t pin)
{
    return ((port->IDR & pin) != 0U) ? 1U : 0U;
}

static uint8_t enc_read_state(uint8_t encoder)
{
    const encoder_hw_pin_t *h = &enc_hw_pins[encoder];
    const uint8_t a = enc_read_pin(h->port_a, h->pin_a);
    const uint8_t b = enc_read_pin(h->port_b, h->pin_b);
    return (uint8_t)((a << 1) | b);
}

void encoders_hw_init(void)
{
    for (uint8_t i = 0U; i < (uint8_t)ENC_COUNT; i++)
    {
        enc_prev_state[i] = enc_read_state(i);
        enc_raw_delta[i] = 0;
    }
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

        enc_raw_delta[i] = (int8_t)(enc_raw_delta[i] + step);
    }
}

int8_t encoders_hw_get_delta(uint8_t encoder)
{
    if (encoder >= (uint8_t)ENC_COUNT)
    {
        return 0;
    }

    const int8_t delta = enc_raw_delta[encoder];
    enc_raw_delta[encoder] = 0;
    return delta;
}
