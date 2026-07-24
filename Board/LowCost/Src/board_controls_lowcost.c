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
    {ENC1_A_GPIO_Port, ENC1_A_Pin, ENC1_B_GPIO_Port, ENC1_B_Pin},
    {ENC2_A_GPIO_Port, ENC2_A_Pin, ENC2_B_GPIO_Port, ENC2_B_Pin},
    {ENC3_A_GPIO_Port, ENC3_A_Pin, ENC3_B_GPIO_Port, ENC3_B_Pin},
    {ENC4_A_GPIO_Port, ENC4_A_Pin, ENC4_B_GPIO_Port, ENC4_B_Pin},
};

static const button_id_t g_button_physical_to_logical[] = {
    [0]  = BTN_PAGE_4,
    [1]  = BTN_PAGE_3,
    [2]  = BTN_ENCODER_4_PUSH,
    [3]  = BTN_ENCODER_3_PUSH,
    [4]  = BTN_ENCODER_1_PUSH,
    [5]  = BTN_ENCODER_2_PUSH,
    [6]  = BTN_PAGE_2,
    [7]  = BTN_PAGE_1,
    [8]  = BTN_STEP_12,
    [9]  = BTN_STEP_11,
    [10] = BTN_STEP_10,
    [11] = BTN_STEP_9,
    [12] = BTN_STEP_16,
    [13] = BTN_STEP_15,
    [14] = BTN_STEP_14,
    [15] = BTN_STEP_13,
    [16] = BTN_STEP_8,
    [17] = BTN_STEP_7,
    [18] = BTN_STEP_6,
    [19] = BTN_STEP_5,
    [20] = BTN_STEP_4,
    [21] = BTN_STEP_3,
    [22] = BTN_STEP_2,
    [23] = BTN_STEP_1,
    [24] = BTN_TRANSPOSE_UP,
    [25] = BTN_TRANSPOSE_DOWN,
    [26] = BTN_PASTE,
    [27] = BTN_COPY,
    [28] = BTN_REC,
    [29] = BTN_PLAY,
    [30] = BTN_SHIFT,
    [31] = BTN_TRACK,
};

/*
 * The scan order above remains the raw board order.  These IDs are the
 * values currently reported by the low-cost button-test page and are remapped
 * only after that raw lookup.
 */
static button_id_t board_controls_button_remap_observed_id(button_id_t observed_id)
{
    switch ((uint8_t)observed_id)
    {
        case 7U:  return BTN_TRACK;
        case 8U:  return BTN_PLAY;
        case 9U:  return BTN_REC;
        case 10U: return BTN_SHIFT;
        case 11U: return BTN_TRANSPOSE_UP;
        case 12U: return BTN_TRANSPOSE_DOWN;
        case 13U: return BTN_COPY;
        case 14U: return BTN_PASTE;

        case 16U: return BTN_PAGE_1;
        case 18U: return BTN_PAGE_3;
        case 19U: return BTN_PAGE_4;
        case 20U: return BTN_ENCODER_1_PUSH;
        case 21U: return BTN_PAGE_2;

        case 24U: return BTN_STEP_4;
        case 25U: return BTN_STEP_1;
        case 26U: return BTN_STEP_2;
        case 27U: return BTN_STEP_3;
        case 28U: return BTN_STEP_5;
        case 29U: return BTN_STEP_6;
        case 30U: return BTN_STEP_7;
        case 31U: return BTN_STEP_8;

        case 32U: return BTN_STEP_13;
        case 33U: return BTN_STEP_14;
        case 34U: return BTN_STEP_15;
        case 35U: return BTN_STEP_16;
        case 36U: return BTN_STEP_9;
        case 37U: return BTN_STEP_10;
        case 38U: return BTN_STEP_11;
        case 39U: return BTN_STEP_12;

        default:  return observed_id;
    }
}

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
    return board_controls_button_remap_observed_id(g_button_physical_to_logical[physical_idx]);
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
