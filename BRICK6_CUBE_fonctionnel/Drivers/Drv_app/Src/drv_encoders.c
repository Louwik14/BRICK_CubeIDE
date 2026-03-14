#include "drv_encoders.h"
#include "stm32h7xx.h"

/* =========================
   === GPIO MAPPING ========
   ========================= */

typedef struct {
    GPIO_TypeDef *portA;
    uint32_t      pinA;
    GPIO_TypeDef *portB;
    uint32_t      pinB;
} encoder_hw_t;

/* >>> MAPPING AVEC GPIO_PIN_X <<< */
static const encoder_hw_t enc_hw[ENCODER_COUNT] = {
    { GPIOB, GPIO_PIN_6, GPIOD, GPIO_PIN_12 },
    { GPIOH, GPIO_PIN_8, GPIOH, GPIO_PIN_12 },
    { GPIOH, GPIO_PIN_10, GPIOH, GPIO_PIN_11 },
    { GPIOA, GPIO_PIN_2, GPIOA, GPIO_PIN_1 }
};

/* ========================= */

static int16_t enc_delta[ENCODER_COUNT];
static uint8_t enc_prev_state[ENCODER_COUNT];

/* Quadrature table */
static const int8_t quad_table[16] = {
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0
};

/* ========================= */

static inline uint8_t read_pin(GPIO_TypeDef *port, uint32_t pin)
{
    return (port->IDR & pin) ? 1U : 0U;
}

static uint8_t read_state(uint8_t i)
{
    const encoder_hw_t *h = &enc_hw[i];

    uint8_t a = read_pin(h->portA, h->pinA);
    uint8_t b = read_pin(h->portB, h->pinB);

    return (uint8_t)((a << 1) | b);
}

/* ========================= */

void drv_encoders_init(void)
{
    for (uint8_t i = 0; i < ENCODER_COUNT; i++)
    {
        enc_delta[i] = 0;
        enc_prev_state[i] = read_state(i);
    }
}

/* ========================= */

void drv_encoders_poll(void)
{
    for (uint8_t i = 0; i < ENCODER_COUNT; i++)
    {
        uint8_t prev = enc_prev_state[i];
        uint8_t now  = read_state(i);

        /* Ignore glitch (états identiques ou invalides) */
        if (now == prev)
            continue;

        uint8_t idx = (prev << 2) | now;
        int8_t step = quad_table[idx];

        /* Ignore transitions invalides (rebond / bruit) */
        if (step == 0)
        {
            enc_prev_state[i] = now;
            continue;
        }

        enc_delta[i] += step;
        enc_prev_state[i] = now;
    }
}

/* ========================= */

int16_t drv_encoder_get_delta(uint8_t id)
{
    if (id >= ENCODER_COUNT)
        return 0;

    int16_t d = enc_delta[id];
    enc_delta[id] = 0;
    return d;
}

/* ========================= */

void drv_encoder_reset(uint8_t id)
{
    if (id < ENCODER_COUNT)
        enc_delta[id] = 0;
}
