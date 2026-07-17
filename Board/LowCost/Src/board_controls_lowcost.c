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
