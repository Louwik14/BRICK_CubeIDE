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
    {GPIOB, GPIO_PIN_6, GPIOD, GPIO_PIN_12},
    {GPIOH, GPIO_PIN_8, GPIOH, GPIO_PIN_12},
    {GPIOH, GPIO_PIN_10, GPIOH, GPIO_PIN_11},
    {GPIOA, GPIO_PIN_2, GPIOA, GPIO_PIN_1},
};

static const button_id_t g_button_physical_to_logical[] = {
    [0]  = BTN_TRANSPOSE_DOWN,
    [1]  = BTN_TRANSPOSE_UP,
    [2]  = BTN_PAGE_2,
    [3]  = BTN_PAGE_1,
    [4]  = BTN_SETTINGS,
    [5]  = BTN_COPY,
    [6]  = BTN_PASTE,
    [7]  = BTN_SHIFT,
    [8]  = BTN_UNUSED_5,
    [9]  = BTN_PLAY,
    [10] = BTN_REC,
    [11] = BTN_PAGE_3,
    [12] = BTN_PAGE_4,
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

void board_controls_buttons_latch_low(void) { SR_CS_GPIO_Port->BSRR = ((uint32_t)SR_CS_Pin << 16U); }
void board_controls_buttons_latch_high(void) { SR_CS_GPIO_Port->BSRR = SR_CS_Pin; }
void board_controls_buttons_clock_low(void) { SR_SCK_GPIO_Port->BSRR = ((uint32_t)SR_SCK_Pin << 16U); }
void board_controls_buttons_clock_high(void) { SR_SCK_GPIO_Port->BSRR = SR_SCK_Pin; }
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
    HAL_GPIO_WritePin(MUX_POT_S0_GPIO_Port, MUX_POT_S0_Pin, (channel & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MUX_POT_S1_GPIO_Port, MUX_POT_S1_Pin, (channel & 0x02U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MUX_POT_S2_GPIO_Port, MUX_POT_S2_Pin, (channel & 0x04U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

uint32_t board_controls_millis(void) { return HAL_GetTick(); }
uint8_t board_controls_pot_adc_start(void) { return (HAL_ADC_Start(&hadc3) == HAL_OK) ? 1U : 0U; }
uint8_t board_controls_pot_adc_poll(void) { return (HAL_ADC_PollForConversion(&hadc3, 0U) == HAL_OK) ? 1U : 0U; }
uint16_t board_controls_pot_adc_read_raw(void) { return (uint16_t)HAL_ADC_GetValue(&hadc3); }
void board_controls_pot_adc_stop(void) { (void)HAL_ADC_Stop(&hadc3); }
