#include "Board/board_controls.h"

#include "adc.h"
#include "main.h"
#include "tim.h"

typedef struct
{
    GPIO_TypeDef *port_a;
    uint32_t pin_a;
    GPIO_TypeDef *port_b;
    uint32_t pin_b;
} board_encoder_pin_t;

static const board_encoder_pin_t g_encoder_pins[] = {
    {GPIOG, GPIO_PIN_14, GPIOG, GPIO_PIN_13},
    {GPIOG, GPIO_PIN_12, GPIOF, GPIO_PIN_10},
    {GPIOG, GPIO_PIN_10, GPIOG, GPIO_PIN_9},
    {GPIOF, GPIO_PIN_8, GPIOF, GPIO_PIN_6},
};

static const button_id_t g_button_physical_to_logical[] = {
    [0]  = BTN_TRANSPOSE_UP,
    [1]  = BTN_TRANSPOSE_DOWN,
    [2]  = BTN_PASTE,
    [3]  = BTN_COPY,
    [4]  = BTN_REC,
    [5]  = BTN_PLAY,
    [6]  = BTN_SHIFT,
    [7]  = BTN_PARAM_8,
    [8]  = BTN_STEP_8,
    [9]  = BTN_STEP_7,
    [10] = BTN_STEP_6,
    [11] = BTN_STEP_5,
    [12] = BTN_STEP_4,
    [13] = BTN_STEP_3,
    [14] = BTN_STEP_2,
    [15] = BTN_STEP_1,
    [16] = BTN_STEP_12,
    [17] = BTN_STEP_11,
    [18] = BTN_STEP_10,
    [19] = BTN_STEP_9,
    [20] = BTN_STEP_16,
    [21] = BTN_STEP_15,
    [22] = BTN_STEP_14,
    [23] = BTN_STEP_13,
    [24] = BTN_PAGE_4,
    [25] = BTN_PAGE_3,
    [26] = BTN_ENCODER_4_PUSH,
    [27] = BTN_ENCODER_3_PUSH,
    [28] = BTN_ENCODER_1_PUSH,
    [29] = BTN_ENCODER_2_PUSH,
    [30] = BTN_PAGE_2,
    [31] = BTN_PAGE_1,
};

#define STEP_MASK (((1ULL << BTN_STEP_1) | (1ULL << BTN_STEP_2) | (1ULL << BTN_STEP_3) | (1ULL << BTN_STEP_4) | \
                    (1ULL << BTN_STEP_5) | (1ULL << BTN_STEP_6) | (1ULL << BTN_STEP_7) | (1ULL << BTN_STEP_8) | \
                    (1ULL << BTN_STEP_9) | (1ULL << BTN_STEP_10) | (1ULL << BTN_STEP_11) | (1ULL << BTN_STEP_12) | \
                    (1ULL << BTN_STEP_13) | (1ULL << BTN_STEP_14) | (1ULL << BTN_STEP_15) | (1ULL << BTN_STEP_16)))
#define ENCODER_PUSH_MASK (((1ULL << BTN_ENCODER_1_PUSH) | (1ULL << BTN_ENCODER_2_PUSH) | \
                            (1ULL << BTN_ENCODER_3_PUSH) | (1ULL << BTN_ENCODER_4_PUSH)))
#define LOWCOST_STEP_MASK_PRESENT (((1ULL << BTN_STEP_8) | (1ULL << BTN_STEP_7) | \
                                    (1ULL << BTN_STEP_6) | (1ULL << BTN_STEP_5) | \
                                    (1ULL << BTN_STEP_4) | (1ULL << BTN_STEP_3) | \
                                    (1ULL << BTN_STEP_2) | (1ULL << BTN_STEP_1) | \
                                    (1ULL << BTN_STEP_12) | (1ULL << BTN_STEP_11) | \
                                    (1ULL << BTN_STEP_10) | (1ULL << BTN_STEP_9) | \
                                    (1ULL << BTN_STEP_16) | (1ULL << BTN_STEP_15) | \
                                    (1ULL << BTN_STEP_14) | (1ULL << BTN_STEP_13)))
#define LOWCOST_ENCODER_PUSH_MASK_PRESENT (((1ULL << BTN_ENCODER_4_PUSH) | (1ULL << BTN_ENCODER_3_PUSH) | \
                                            (1ULL << BTN_ENCODER_1_PUSH) | (1ULL << BTN_ENCODER_2_PUSH)))

_Static_assert((sizeof(g_button_physical_to_logical) / sizeof(g_button_physical_to_logical[0])) == 32U,
               "Low-cost SR mapping must define 32 physical positions");
_Static_assert(LOWCOST_STEP_MASK_PRESENT == STEP_MASK, "Low-cost SR mapping must contain each STEP exactly once");
_Static_assert(LOWCOST_ENCODER_PUSH_MASK_PRESENT == ENCODER_PUSH_MASK,
               "Low-cost SR mapping must contain each encoder push exactly once");

uint8_t board_controls_button_physical_count(void)
{
    return (uint8_t)(sizeof(g_button_physical_to_logical) / sizeof(g_button_physical_to_logical[0]));
}

button_id_t board_controls_button_logical_for_physical(uint8_t physical_idx)
{
    if (physical_idx >= board_controls_button_physical_count())
    {
        return BOARD_CONTROLS_BUTTON_INVALID;
    }
    return g_button_physical_to_logical[physical_idx];
}

void board_controls_buttons_latch_low(void) { CS_SR_GPIO_Port->BSRR = ((uint32_t)CS_SR_Pin << 16U); }
void board_controls_buttons_latch_high(void) { CS_SR_GPIO_Port->BSRR = CS_SR_Pin; }
void board_controls_buttons_clock_low(void) { SCK_SR_GPIO_Port->BSRR = ((uint32_t)SCK_SR_Pin << 16U); }
void board_controls_buttons_clock_high(void) { SCK_SR_GPIO_Port->BSRR = SCK_SR_Pin; }
uint8_t board_controls_buttons_data_read(void) { return (SR_DATA_GPIO_Port->IDR & SR_DATA_Pin) ? 1U : 0U; }
void board_controls_io_barrier(void) { __NOP(); }

uint8_t board_controls_encoder_state(uint8_t encoder)
{
    if (encoder >= (uint8_t)(sizeof(g_encoder_pins) / sizeof(g_encoder_pins[0]))) return 0U;
    const board_encoder_pin_t *pins = &g_encoder_pins[encoder];
    const uint8_t a = ((pins->port_a->IDR & pins->pin_a) != 0U) ? 1U : 0U;
    const uint8_t b = ((pins->port_b->IDR & pins->pin_b) != 0U) ? 1U : 0U;
    return (uint8_t)((a << 1) | b);
}

void board_controls_start_encoder_fast_poll_timer(void)
{
    if (HAL_TIM_Base_Start_IT(&htim7) != HAL_OK) Error_Handler();
}

void board_controls_mux_pot_select(uint8_t channel)
{
    HAL_GPIO_WritePin(MUX_HALL_S0_GPIO_Port, MUX_HALL_S0_Pin,
                      (channel & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MUX_HALL_S1_GPIO_Port, MUX_HALL_S1_Pin,
                      (channel & 0x02U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MUX_HALL_S2_GPIO_Port, MUX_HALL_S2_Pin,
                      (channel & 0x04U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

uint32_t board_controls_millis(void) { return HAL_GetTick(); }
uint8_t board_controls_pot_adc_start(void) { return (HAL_ADC_Start(&hadc2) == HAL_OK) ? 1U : 0U; }
uint8_t board_controls_pot_adc_poll(void) { return (HAL_ADC_PollForConversion(&hadc2, 0U) == HAL_OK) ? 1U : 0U; }
uint16_t board_controls_pot_adc_read_raw(void) { return (uint16_t)(65535U - HAL_ADC_GetValue(&hadc2)); }
void board_controls_pot_adc_stop(void) { (void)HAL_ADC_Stop(&hadc2); }
