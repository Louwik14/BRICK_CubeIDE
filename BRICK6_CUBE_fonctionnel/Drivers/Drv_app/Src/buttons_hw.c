#include "buttons_hw.h"

#include "main.h"

#define BUTTONS_HW_REG_COUNT 3U
#define BUTTONS_HW_BITS_PER_REG 8U
#define BUTTONS_HW_TOTAL_BITS (BUTTONS_HW_REG_COUNT * BUTTONS_HW_BITS_PER_REG)
#define BUTTONS_HW_MASK ((1UL << BUTTONS_HW_TOTAL_BITS) - 1UL)

static uint8_t buttons_hw_state[BTN_COUNT];

static inline void sr_pl_low(void)
{
    SR_CS_GPIO_Port->BSRR = ((uint32_t)SR_CS_Pin << 16U);
}

static inline void sr_pl_high(void)
{
    SR_CS_GPIO_Port->BSRR = (uint32_t)SR_CS_Pin;
}

static inline void sr_sck_low(void)
{
    SR_SCK_GPIO_Port->BSRR = ((uint32_t)SR_SCK_Pin << 16U);
}

static inline void sr_sck_high(void)
{
    SR_SCK_GPIO_Port->BSRR = (uint32_t)SR_SCK_Pin;
}

static inline uint32_t sr_data_read(void)
{
    return ((SR_DATA_GPIO_Port->IDR & SR_DATA_Pin) != 0U) ? 1U : 0U;
}

void buttons_hw_init(void)
{
    for (uint32_t i = 0U; i < (uint32_t)BTN_COUNT; i++)
    {
        buttons_hw_state[i] = 0U;
    }

    sr_pl_high();
    sr_sck_low();
}

void buttons_hw_read(void)
{
    uint32_t raw_shifted = 0U;

    sr_sck_low();

    sr_pl_low();
    __NOP();
    sr_pl_high();

    for (uint32_t i = 0U; i < BUTTONS_HW_TOTAL_BITS; i++)
    {
        raw_shifted <<= 1U;
        raw_shifted |= sr_data_read();

        sr_sck_high();
        __NOP();
        sr_sck_low();
    }

    const uint32_t pressed_mask = (~raw_shifted) & BUTTONS_HW_MASK;

    for (uint32_t i = 0U; i < (uint32_t)BTN_COUNT; i++)
    {
        buttons_hw_state[i] = (uint8_t)((pressed_mask >> i) & 0x1U);
    }
}

uint8_t buttons_hw_get(button_id_t btn)
{
    if ((uint32_t)btn >= (uint32_t)BTN_COUNT)
    {
        return 0U;
    }

    return buttons_hw_state[(uint32_t)btn];
}
