#include "App/mux_pots.h"

#include "adc.h"
#include "main.h"

#define MUX_POTS_COUNT        6U
#define MUX_POTS_SETTLE_MS    1U

typedef enum
{
  MUX_POTS_STATE_SELECT = 0,
  MUX_POTS_STATE_SETTLE,
  MUX_POTS_STATE_DUMMY_CONVERT,
  MUX_POTS_STATE_CONVERT
} mux_pots_state_t;

static uint16_t pot_values[MUX_POTS_COUNT];
static uint8_t pot_valid_mask;
static uint8_t current_channel;
static mux_pots_state_t scan_state;
static uint32_t settle_started_ms;

static void mux_pots_select_channel(uint8_t channel)
{
  HAL_GPIO_WritePin(MUX_POT_S0_GPIO_Port, MUX_POT_S0_Pin,
                    (channel & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(MUX_POT_S1_GPIO_Port, MUX_POT_S1_Pin,
                    (channel & 0x02U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(MUX_POT_S2_GPIO_Port, MUX_POT_S2_Pin,
                    (channel & 0x04U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void mux_pots_init(void)
{
  for (uint8_t i = 0U; i < MUX_POTS_COUNT; i++)
  {
    pot_values[i] = 0U;
  }

  current_channel = 0U;
  pot_valid_mask = 0U;
  scan_state = MUX_POTS_STATE_SELECT;
  settle_started_ms = 0U;

  mux_pots_select_channel(current_channel);
}

void mux_pots_scan(void)
{
  switch (scan_state)
  {
    case MUX_POTS_STATE_SELECT:
      mux_pots_select_channel(current_channel);
      settle_started_ms = HAL_GetTick();
      scan_state = MUX_POTS_STATE_SETTLE;
      break;

    case MUX_POTS_STATE_SETTLE:
      if ((HAL_GetTick() - settle_started_ms) >= MUX_POTS_SETTLE_MS)
      {
        if (HAL_ADC_Start(&hadc3) == HAL_OK)
        {
          scan_state = MUX_POTS_STATE_DUMMY_CONVERT;
        }
      }
      break;

    case MUX_POTS_STATE_DUMMY_CONVERT:
      if (HAL_ADC_PollForConversion(&hadc3, 0U) == HAL_OK)
      {
        (void)HAL_ADC_GetValue(&hadc3);
        (void)HAL_ADC_Stop(&hadc3);

        if (HAL_ADC_Start(&hadc3) == HAL_OK)
        {
          scan_state = MUX_POTS_STATE_CONVERT;
        }
        else
        {
          scan_state = MUX_POTS_STATE_SELECT;
        }
      }
      break;

    case MUX_POTS_STATE_CONVERT:
      if (HAL_ADC_PollForConversion(&hadc3, 0U) == HAL_OK)
      {
        pot_values[current_channel] = (uint16_t)HAL_ADC_GetValue(&hadc3);
        pot_valid_mask |= (uint8_t)(1U << current_channel);
        (void)HAL_ADC_Stop(&hadc3);

        current_channel++;
        if (current_channel >= MUX_POTS_COUNT)
        {
          current_channel = 0U;
        }
        scan_state = MUX_POTS_STATE_SELECT;
      }
      break;

    default:
      scan_state = MUX_POTS_STATE_SELECT;
      break;
  }
}

uint16_t mux_pots_get(uint8_t pot)
{
  if (pot >= MUX_POTS_COUNT)
  {
    return 0U;
  }

  return pot_values[pot];
}

uint8_t mux_pots_is_valid(uint8_t pot)
{
  if (pot >= MUX_POTS_COUNT)
  {
    return 0U;
  }

  return ((pot_valid_mask & (uint8_t)(1U << pot)) != 0U) ? 1U : 0U;
}
