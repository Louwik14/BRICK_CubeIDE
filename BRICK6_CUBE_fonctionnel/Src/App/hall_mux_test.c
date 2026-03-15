#include "App/hall_mux_test.h"

#include "adc.h"
#include "main.h"
#include "stm32h7xx_hal.h"

#define HALL_MUX_CHANNEL_COUNT 8U
#define HALL_SETTLE_US         7U
#define HALL_ADC_TIMEOUT_US    10U

static uint16_t s_raw[HALL_MUX_TEST_KEY_COUNT];

static void hall_mux_select(uint8_t mux)
{
  HAL_GPIO_WritePin(MUX_HALL_S0_GPIO_Port, MUX_HALL_S0_Pin,
                    (mux & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(MUX_HALL_S1_GPIO_Port, MUX_HALL_S1_Pin,
                    (mux & 0x02U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(MUX_HALL_S2_GPIO_Port, MUX_HALL_S2_Pin,
                    (mux & 0x04U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void hall_wait_us(uint32_t us)
{
  uint32_t start = TIM5->CNT;
  while ((uint32_t)(TIM5->CNT - start) < us)
  {
    __NOP();
  }
}

static uint8_t hall_adc_sample_pair(uint16_t *adc1_out, uint16_t *adc2_out)
{
  if (HAL_ADC_Start(&hadc1) != HAL_OK)
  {
    return 0U;
  }

  if (HAL_ADC_Start(&hadc2) != HAL_OK)
  {
    (void)HAL_ADC_Stop(&hadc1);
    return 0U;
  }

  if (HAL_ADC_PollForConversion(&hadc1, HALL_ADC_TIMEOUT_US) != HAL_OK)
  {
    (void)HAL_ADC_Stop(&hadc1);
    (void)HAL_ADC_Stop(&hadc2);
    return 0U;
  }

  if (HAL_ADC_PollForConversion(&hadc2, HALL_ADC_TIMEOUT_US) != HAL_OK)
  {
    (void)HAL_ADC_Stop(&hadc1);
    (void)HAL_ADC_Stop(&hadc2);
    return 0U;
  }

  *adc1_out = (uint16_t)HAL_ADC_GetValue(&hadc1);
  *adc2_out = (uint16_t)HAL_ADC_GetValue(&hadc2);

  (void)HAL_ADC_Stop(&hadc1);
  (void)HAL_ADC_Stop(&hadc2);

  return 1U;
}

void hall_mux_test_init(void)
{
  for (uint8_t i = 0U; i < HALL_MUX_TEST_KEY_COUNT; i++)
  {
    s_raw[i] = 0U;
  }

  hall_mux_select(0U);
}

void hall_mux_test_poll(void)
{
  for (uint8_t mux = 0U; mux < HALL_MUX_CHANNEL_COUNT; mux++)
  {
    uint16_t adc1 = 0U;
    uint16_t adc2 = 0U;

    hall_mux_select(mux);
    hall_wait_us(HALL_SETTLE_US);

    if (hall_adc_sample_pair(&adc1, &adc2) == 0U)
    {
      continue;
    }

    s_raw[mux] = adc1;
    s_raw[(uint8_t)(mux + HALL_MUX_CHANNEL_COUNT)] = adc2;
  }
}

uint16_t hall_mux_test_get_raw(uint8_t key)
{
  if (key >= HALL_MUX_TEST_KEY_COUNT)
  {
    return 0U;
  }

  return s_raw[key];
}
