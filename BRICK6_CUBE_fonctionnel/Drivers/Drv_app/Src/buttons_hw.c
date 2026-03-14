#include "buttons_hw.h"
#include "main.h"

#define BUTTONS_HW_REG_COUNT 3U
#define BUTTONS_HW_BITS_PER_REG 8U
#define BUTTONS_HW_TOTAL_BITS (BUTTONS_HW_REG_COUNT * BUTTONS_HW_BITS_PER_REG)
#define BUTTONS_HW_MASK ((1UL << BUTTONS_HW_TOTAL_BITS) - 1UL)

static uint8_t buttons_hw_state[BTN_COUNT];

/* --------- GPIO helpers --------- */

static inline void sr_pl_low(void)
{
    SR_CS_GPIO_Port->BSRR = ((uint32_t)SR_CS_Pin << 16U);
}

static inline void sr_pl_high(void)
{
    SR_CS_GPIO_Port->BSRR = SR_CS_Pin;
}

static inline void sr_sck_low(void)
{
    SR_SCK_GPIO_Port->BSRR = ((uint32_t)SR_SCK_Pin << 16U);
}

static inline void sr_sck_high(void)
{
    SR_SCK_GPIO_Port->BSRR = SR_SCK_Pin;
}

static inline uint32_t sr_data_read(void)
{
    return (SR_DATA_GPIO_Port->IDR & SR_DATA_Pin) ? 1U : 0U;
}

/* --------- Init --------- */

void buttons_hw_init(void)
{
    for(uint32_t i = 0; i < BTN_COUNT; i++)
        buttons_hw_state[i] = 0;

    sr_pl_high();
    sr_sck_low();
}

/* --------- Read shift registers --------- */

void buttons_hw_read(void)
{
    uint32_t raw = 0;

    /* Latch parallel inputs */
    sr_pl_low();
    __NOP();
    sr_pl_high();

    for(uint32_t i = 0; i < BUTTONS_HW_TOTAL_BITS; i++)
    {
        sr_sck_low();
        __NOP();

        raw <<= 1;
        raw |= sr_data_read();

        sr_sck_high();
        __NOP();
    }

    /* active LOW buttons */
    uint32_t pressed_mask = (~raw) & BUTTONS_HW_MASK;

    for(uint32_t i = 0; i < BTN_COUNT; i++)
    {
        buttons_hw_state[i] = (pressed_mask >> i) & 1U;
    }
}

/* --------- API --------- */

uint8_t buttons_hw_get(button_id_t btn)
{
    if(btn >= BTN_COUNT)
        return 0;

    return buttons_hw_state[btn];
}
