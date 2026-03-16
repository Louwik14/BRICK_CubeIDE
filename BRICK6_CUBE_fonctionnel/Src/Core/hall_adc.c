#include "hall_adc.h"

#include "adc.h"
#include "main.h"
#include "tim.h"


#define HALL_MUX_COUNT 8U
#define HALL_KEY_COUNT 16U

static volatile uint16_t adc1_dma[HALL_MUX_COUNT];
static volatile uint16_t adc2_dma[HALL_MUX_COUNT];
static volatile uint8_t hall_mux_index;

void hall_mux_select(uint8_t index)
{
    const uint8_t mux = index & 0x07U;

    HAL_GPIO_WritePin(MUX_HALL_S0_GPIO_Port, MUX_HALL_S0_Pin,
                      (mux & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MUX_HALL_S1_GPIO_Port, MUX_HALL_S1_Pin,
                      (mux & 0x02U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MUX_HALL_S2_GPIO_Port, MUX_HALL_S2_Pin,
                      (mux & 0x04U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void hall_adc_init(void)
{
    hall_mux_index = 0U;
    hall_mux_select(hall_mux_index);

    for (uint8_t i = 0U; i < HALL_MUX_COUNT; i++)
    {
        adc1_dma[i] = 0U;
        adc2_dma[i] = 0U;
    }

    (void)HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc1_dma, HALL_MUX_COUNT);
    (void)HAL_ADC_Start_DMA(&hadc2, (uint32_t *)adc2_dma, HALL_MUX_COUNT);

    (void)HAL_TIM_Base_Start(&htim6);
    (void)HAL_TIM_Base_Start_IT(&htim7);
}

uint16_t hall_adc_get_raw(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    const uint8_t mux = hall_mux_index;

    if (key < HALL_MUX_COUNT)
    {
        return adc1_dma[mux];
    }

    return adc2_dma[mux];
}

uint8_t hall_adc_get_mux_index(void)
{
    return hall_mux_index;
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if ((htim != NULL) && (htim->Instance == TIM7))
    {
        hall_mux_index = (uint8_t)((hall_mux_index + 1U) & 0x07U);
        hall_mux_select(hall_mux_index);
    }
}
