#include "App/mux_pots.h"

#include "adc.h"

#define MUX_POTS_COUNT     2U
#define MUX_POTS_DEADBAND  768U

static uint16_t pot_values[MUX_POTS_COUNT];
static uint8_t pot_valid_mask;
static uint8_t current_channel;
static uint8_t scan_active;

static void mux_pots_store(uint8_t pot, uint16_t raw)
{
  if (((raw > pot_values[pot]) &&
       ((raw - pot_values[pot]) >= MUX_POTS_DEADBAND)) ||
      ((pot_values[pot] > raw) &&
       ((pot_values[pot] - raw) >= MUX_POTS_DEADBAND)) ||
      ((pot_valid_mask & (uint8_t)(1U << pot)) == 0U))
  {
    pot_values[pot] = raw;
  }

  pot_valid_mask |= (uint8_t)(1U << pot);
}

void mux_pots_init(void)
{
  for (uint8_t i = 0U; i < MUX_POTS_COUNT; i++)
  {
    pot_values[i] = 0U;
  }

  current_channel = 0U;
  pot_valid_mask = 0U;
  scan_active = 0U;
}

void mux_pots_scan(void)
{
  if (scan_active == 0U)
  {
    if (HAL_ADC_Start(&hadc3) == HAL_OK)
    {
      current_channel = 0U;
      scan_active = 1U;
    }
    return;
  }

  if (HAL_ADC_PollForConversion(&hadc3, 0U) != HAL_OK)
  {
    return;
  }

  const uint16_t raw = (uint16_t)(65535U - HAL_ADC_GetValue(&hadc3));
  mux_pots_store(current_channel, raw);

  current_channel++;
  if (current_channel >= MUX_POTS_COUNT)
  {
    (void)HAL_ADC_Stop(&hadc3);
    current_channel = 0U;
    scan_active = 0U;
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
