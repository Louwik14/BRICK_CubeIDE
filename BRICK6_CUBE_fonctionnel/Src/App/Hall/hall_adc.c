#include "App/Hall/hall_adc.h"

#include "adc.h"
#include "main.h"
#include "tim.h"
#include "App/Hall/hall_engine.h"

#define HALL_MUX_COUNT 8U

static volatile uint16_t adc1_dma;
static volatile uint16_t adc2_dma;

static volatile uint16_t hall_raw[HALL_KEY_COUNT];
static volatile uint32_t hall_sample_count[HALL_KEY_COUNT];

static volatile uint8_t hall_mux_index;
static volatile uint8_t hall_discard_next;

/*
 * Table de remap physique MUX -> index logique hall.
 *
 * Mapping demandé :
 * hall 0 -> mux 4
 * hall 1 -> mux 6
 * hall 2 -> mux 7
 * hall 3 -> mux 5
 * hall 4 -> mux 3
 * hall 5 -> mux 0
 * hall 6 -> mux 1
 * hall 7 -> mux 2
 * hall 8 -> mux 12
 * hall 9 -> mux 14
 * hall 10 -> mux 15
 * hall 11 -> mux 13
 * hall 12 -> mux 11
 * hall 13 -> mux 8
 * hall 14 -> mux 9
 * hall 15 -> mux 10
 *
 * Donc inverse MUX -> hall :
 * mux 0  -> hall 5
 * mux 1  -> hall 6
 * mux 2  -> hall 7
 * mux 3  -> hall 4
 * mux 4  -> hall 0
 * mux 5  -> hall 3
 * mux 6  -> hall 1
 * mux 7  -> hall 2
 * mux 8  -> hall 13
 * mux 9  -> hall 14
 * mux 10 -> hall 15
 * mux 11 -> hall 12
 * mux 12 -> hall 8
 * mux 13 -> hall 11
 * mux 14 -> hall 9
 * mux 15 -> hall 10
 */
static const uint8_t hall_key_from_mux[HALL_KEY_COUNT] =
{
    5U,  6U,  7U,  4U,
    0U,  3U,  1U,  2U,
    13U, 14U, 15U, 12U,
    8U,  11U, 9U,  10U
};

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

static void hall_adc_publish_sample(uint8_t key, uint16_t raw)
{
    const uint32_t sample_count = hall_sample_count[key] + 1U;

    hall_raw[key] = raw;
    hall_sample_count[key] = sample_count;

    hall_engine_process_sample(key, raw, sample_count);
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
        const uint8_t mux_a = hall_mux_index;
        const uint8_t mux_b = (uint8_t)(mux_a + HALL_MUX_COUNT);
        const uint8_t key_a = hall_key_from_mux[mux_a];
        const uint8_t key_b = hall_key_from_mux[mux_b];

        hall_adc_publish_sample(key_a, v1);
        hall_adc_publish_sample(key_b, v2);

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

    hall_mux_select(hall_mux_index);

    for (uint8_t i = 0U; i < HALL_KEY_COUNT; i++)
    {
        hall_raw[i] = 0U;
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

    if (hadc->Instance != ADC1)
    {
        return;
    }

    hall_adc_process_pair();
}
