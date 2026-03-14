#include "buttons_hw.h"
#include "main.h"

#define BUTTONS_HW_REG_COUNT 3U
#define BUTTONS_HW_BITS_PER_REG 8U
#define BUTTONS_HW_TOTAL_BITS (BUTTONS_HW_REG_COUNT * BUTTONS_HW_BITS_PER_REG)
#define BUTTONS_HW_MASK ((1UL << BUTTONS_HW_TOTAL_BITS) - 1UL)

static uint8_t buttons_hw_state[BTN_COUNT];

/*
 * Physical-to-logical button mapping.
 *
 * Index  : physical bit index read from cascaded SN74HC165 chain.
 * Value  : logical button_id_t used everywhere else in the firmware.
 *
 * Measured hardware mapping:
 *   0: transpose_down,  1: transpose_up,
 *   2: page2,           3: page1,
 *   4: settings,        5: paste,
 *   6: copy,            7: shift,
 *   9: play,           10: rec,
 *  11: page3,          12: page4,
 *  16: param7,         17: param8,
 *  18: param4,         19: param3,
 *  20: param2,         21: param1,
 *  22: param5,         23: param6.
 *
 * Unused / not assigned physical inputs are routed to BTN_UNUSED_* slots.
 */
static const uint8_t button_physical_to_logical[BTN_COUNT] = {
    [0]  = BTN_TRANSPOSE_DOWN,
    [1]  = BTN_TRANSPOSE_UP,
    [2]  = BTN_UNUSED_2, /* page2 */
    [3]  = BTN_UNUSED_1, /* page1 */
    [4]  = BTN_SETTINGS,
    [5]  = BTN_PASTE,
    [6]  = BTN_COPY,
    [7]  = BTN_SHIFT,
    [8]  = BTN_UNUSED_5,
    [9]  = BTN_PLAY,
    [10] = BTN_REC,
    [11] = BTN_UNUSED_3, /* page3 */
    [12] = BTN_UNUSED_4, /* page4 */
    [13] = BTN_UNUSED_6,
    [14] = BTN_UNUSED_7,
    [15] = BTN_UNUSED_8,
    [16] = BTN_PARAM_7,
    [17] = BTN_PARAM_8,
    [18] = BTN_PARAM_4,
    [19] = BTN_PARAM_3,
    [20] = BTN_PARAM_2,
    [21] = BTN_PARAM_1,
    [22] = BTN_PARAM_5,
    [23] = BTN_PARAM_6,
};

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
        buttons_hw_state[i] = 0U;
    }

    for(uint32_t physical_idx = 0; physical_idx < BTN_COUNT; physical_idx++)
    {
        const uint8_t logical_idx = button_physical_to_logical[physical_idx];
        buttons_hw_state[logical_idx] = (uint8_t)((pressed_mask >> physical_idx) & 1U);
    }
}

/* --------- API --------- */

uint8_t buttons_hw_get(button_id_t btn)
{
    if(btn >= BTN_COUNT)
        return 0;

    return buttons_hw_state[btn];
}
