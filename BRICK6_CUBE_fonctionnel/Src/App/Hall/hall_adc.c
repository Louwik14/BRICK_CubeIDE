#include "App/Hall/hall_adc.h"

#include "adc.h"
#include "main.h"
#include "tim.h"

#define HALL_MUX_COUNT 8U

static volatile uint16_t adc1_dma;
static volatile uint16_t adc2_dma;

static volatile uint16_t hall_raw[HALL_KEY_COUNT];
static volatile uint8_t hall_raw_fresh[HALL_KEY_COUNT];
static volatile uint32_t hall_sample_count[HALL_KEY_COUNT];

static volatile uint8_t hall_mux_index;
static volatile uint8_t hall_discard_next;

static volatile uint8_t adc1_ready;
static volatile uint8_t adc2_ready;

static void hall_mux_select(uint8_t index)
{
    const uint8_t mux = index & 0x07U;

    HAL_GPIO_WritePin(MUX_HALL_S0_GPIO_Port, MUX_HALL_S0_Pin,
                      (mux & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    HAL_GPIO_WritePin(MUX_HALL_S1_GPIO_Port, MUX_HALL_S1_Pin,
                      (mux & 0x02U) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    HAL_GPIO_WritePin(MUX_HALL_S2_GPIO_Port, MUX_HALL_S2_Pin,
                      (mux & 0x04U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void hall_adc_process_pair(void)
{
    const uint16_t v1 = adc1_dma;
    const uint16_t v2 = adc2_dma;

    if (hall_discard_next != 0U)
    {
        hall_discard_next = 0U;
        return;
    }

    {
        const uint8_t mux = hall_mux_index;

        hall_raw[mux] = v1;
        hall_raw[mux + 8U] = v2;
        hall_raw_fresh[mux] = 1U;
        hall_raw_fresh[mux + 8U] = 1U;
        hall_sample_count[mux]++;
        hall_sample_count[mux + 8U]++;

        hall_mux_index = (uint8_t)((hall_mux_index + 1U) & 0x07U);
        hall_mux_select(hall_mux_index);

        hall_discard_next = 1U;
    }
}

void hall_adc_init(void)
{
    hall_mux_index = 0U;
    hall_discard_next = 1U;

    adc1_dma = 0U;
    adc2_dma = 0U;

    adc1_ready = 0U;
    adc2_ready = 0U;

    hall_mux_select(hall_mux_index);

    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        hall_raw[i] = 0U;
        hall_raw_fresh[i] = 0U;
        hall_sample_count[i] = 0U;
    }

    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)&adc1_dma, 1U) != HAL_OK)
    {
        return;
    }

    if (HAL_ADC_Start_DMA(&hadc2, (uint32_t *)&adc2_dma, 1U) != HAL_OK)
    {
        return;
    }

    if (HAL_TIM_Base_Start(&htim6) != HAL_OK)
    {
        return;
    }
}

uint16_t hall_adc_get_raw(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_raw[key];
}

uint8_t hall_adc_get_mux_index(void)
{
    return hall_mux_index;
}

uint8_t hall_adc_is_fresh(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_raw_fresh[key];
}

void hall_adc_clear_fresh(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return;
    }

    hall_raw_fresh[key] = 0U;
}

uint32_t hall_adc_get_sample_count(uint8_t key)
{
    if (key >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    return hall_sample_count[key];
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == NULL)
    {
        return;
    }

    if (hadc->Instance == ADC1)
    {
        adc1_ready = 1U;
    }
    else if (hadc->Instance == ADC2)
    {
        adc2_ready = 1U;
    }
    else
    {
        return;
    }

    if ((adc1_ready != 0U) && (adc2_ready != 0U))
    {
        adc1_ready = 0U;
        adc2_ready = 0U;
        hall_adc_process_pair();
    }
}
