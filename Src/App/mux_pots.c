#include "App/mux_pots.h"

#include "Board/board_controls.h"

#define MUX_POTS_COUNT        6U
#define MUX_POTS_SETTLE_MS    1U
#define MUX_POTS_DEADBAND     768U

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
  board_controls_mux_pot_select(channel);
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
      settle_started_ms = board_controls_millis();
      scan_state = MUX_POTS_STATE_SETTLE;
      break;

    case MUX_POTS_STATE_SETTLE:
      if ((board_controls_millis() - settle_started_ms) >= MUX_POTS_SETTLE_MS)
      {
        if (board_controls_pot_adc_start() != 0U)
        {
          scan_state = MUX_POTS_STATE_DUMMY_CONVERT;
        }
      }
      break;

    case MUX_POTS_STATE_DUMMY_CONVERT:
      if (board_controls_pot_adc_poll() != 0U)
      {
        (void)board_controls_pot_adc_read_raw();
        board_controls_pot_adc_stop();

        if (board_controls_pot_adc_start() != 0U)
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
      if (board_controls_pot_adc_poll() != 0U)
      {
        uint16_t raw = (uint16_t)(65535U - board_controls_pot_adc_read_raw());

        if (((raw > pot_values[current_channel]) &&
             ((raw - pot_values[current_channel]) >= MUX_POTS_DEADBAND)) ||
            ((pot_values[current_channel] > raw) &&
             ((pot_values[current_channel] - raw) >= MUX_POTS_DEADBAND)) ||
            ((pot_valid_mask & (uint8_t)(1U << current_channel)) == 0U))
        {
          pot_values[current_channel] = raw;
        }

        pot_valid_mask |= (uint8_t)(1U << current_channel);
        board_controls_pot_adc_stop();

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
