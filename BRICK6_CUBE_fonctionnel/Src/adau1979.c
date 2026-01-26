#include "adau1979.h"
#include "i2c.h"
#include "main.h"
#include "usart.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ADAU1979 register map (subset used). */
enum
{
  ADAU1979_REG_M_POWER = 0x00,
  ADAU1979_REG_PLL_CONTROL = 0x01,
  ADAU1979_REG_BLOCK_POWER_SAI = 0x04,
  ADAU1979_REG_SAI_CTRL0 = 0x05,
  ADAU1979_REG_SAI_CTRL1 = 0x06,
  ADAU1979_REG_SAI_CMAP12 = 0x07,
  ADAU1979_REG_SAI_CMAP34 = 0x08,
  ADAU1979_REG_SAI_OVERTEMP = 0x09,
  ADAU1979_REG_POSTADC_GAIN1 = 0x0A,
  ADAU1979_REG_POSTADC_GAIN2 = 0x0B,
  ADAU1979_REG_POSTADC_GAIN3 = 0x0C,
  ADAU1979_REG_POSTADC_GAIN4 = 0x0D,
  ADAU1979_REG_MISC_CONTROL = 0x0E,
  ADAU1979_REG_ADC_CLIP = 0x19
};

/* ADAU1979 post-ADC gain limits and step size (datasheet Table 25-28). */
enum
{
  ADAU1979_INPUT_GAIN_MIN_REG = 0xFE,
  ADAU1979_INPUT_GAIN_MUTE_REG = 0xFF
};

static const float ADAU1979_INPUT_GAIN_MAX_DB = 60.0f;
static const float ADAU1979_INPUT_GAIN_MIN_DB = -35.625f;
static const float ADAU1979_INPUT_GAIN_STEP_DB = 0.375f;

/* M_POWER bits. */
enum
{
  ADAU1979_M_POWER_SOFT_RESET = 0x80,
  ADAU1979_M_POWER_POWER_UP = 0x01
};

/* PLL_CONTROL bits (256 x FS, PLL enabled, default divider values). */
enum
{
  ADAU1979_PLL_CONTROL_256FS = 0x41,
  ADAU1979_PLL_CONTROL_MASK = 0x7F
};

/* BLOCK_POWER_SAI bits (enable all ADC channels and SAI block). */
enum
{
  ADAU1979_BLOCK_POWER_ENABLE = 0x3F,
  ADAU1979_BLOCK_POWER_MASK = 0x3F
};

/* SAI_CTRL0 format bits. */
enum
{
  ADAU1979_SAI_CTRL0_LJ = 0x40,
  ADAU1979_SAI_CTRL0_TDM8 = 0x18,
  ADAU1979_SAI_CTRL0_FS_48K = 0x02,
  ADAU1979_SAI_CTRL0_MASK = 0xFF
};

/* SAI_CTRL1: 32-bit slots, 24-bit left-justified data. */
enum
{
  ADAU1979_SAI_CTRL1_32BIT_24DATA = 0x00,
  ADAU1979_SAI_CTRL1_MASK = 0xFF
};

/* SAI_OVERTEMP: drive all output pins. */
enum
{
  ADAU1979_SAI_DRIVE_ALL = 0xF0,
  ADAU1979_SAI_DRIVE_MASK = 0xF8
};

enum
{
  ADAU1979_MISC_CONTROL_MMUTE = 0x10,
  ADAU1979_MISC_CONTROL_DC_CAL = 0x01
};

static void adau1979_uart_logf(const char *fmt, ...)
{
  char buffer[160];
  va_list args;
  int length = 0;

  va_start(args, fmt);
  length = vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  if (length < 0)
  {
    return;
  }
  if ((size_t)length >= sizeof(buffer))
  {
    length = (int)sizeof(buffer) - 1;
  }

  (void)HAL_UART_Transmit(&huart1, (uint8_t *)buffer, (uint16_t)length, 100U);
}

static HAL_StatusTypeDef adau1979_write_reg(uint8_t addr, uint8_t reg, uint8_t value)
{
  return HAL_I2C_Mem_Write(&hi2c1,
                           (uint16_t)(addr << 1),
                           reg,
                           I2C_MEMADD_SIZE_8BIT,
                           &value,
                           1U,
                           100U);
}

static HAL_StatusTypeDef adau1979_read_reg(uint8_t addr, uint8_t reg, uint8_t *value)
{
  return HAL_I2C_Mem_Read(&hi2c1,
                          (uint16_t)(addr << 1),
                          reg,
                          I2C_MEMADD_SIZE_8BIT,
                          value,
                          1U,
                          100U);
}

static bool adau1979_write_reg_log(uint8_t addr, uint8_t reg, uint8_t value)
{
  bool ok = adau1979_write_reg(addr, reg, value) == HAL_OK;

  adau1979_uart_logf("ADAU1979[0x%02X] REG 0x%02X = 0x%02X (write %s)\r\n",
                     addr,
                     reg,
                     value,
                     ok ? "OK" : "FAIL");
  return ok;
}

static bool adau1979_write_verify_log(uint8_t addr, uint8_t reg, uint8_t value, uint8_t mask)
{
  uint8_t readback = 0U;
  bool ok = false;

  if (adau1979_write_reg(addr, reg, value) != HAL_OK)
  {
    adau1979_uart_logf("ADAU1979[0x%02X] REG 0x%02X = 0x%02X (write FAIL)\r\n",
                       addr,
                       reg,
                       value);
    return false;
  }

  if (adau1979_read_reg(addr, reg, &readback) != HAL_OK)
  {
    adau1979_uart_logf("ADAU1979[0x%02X] REG 0x%02X = 0x%02X (read FAIL)\r\n",
                       addr,
                       reg,
                       value);
    return false;
  }

  ok = (uint8_t)(readback & mask) == (uint8_t)(value & mask);
  adau1979_uart_logf("ADAU1979[0x%02X] REG 0x%02X = 0x%02X (read 0x%02X, mask 0x%02X) %s\r\n",
                     addr,
                     reg,
                     value,
                     readback,
                     mask,
                     ok ? "OK" : "MISMATCH");

  return ok;
}

static bool adau1979_is_present(uint8_t addr)
{
  return HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 3U, 100U) == HAL_OK;
}

static uint8_t adau1979_gain_db_to_reg(float gain_db)
{
  float clamped_gain = gain_db;
  float steps = 0.0f;
  uint8_t reg_value = 0U;

  if (clamped_gain > ADAU1979_INPUT_GAIN_MAX_DB)
  {
    clamped_gain = ADAU1979_INPUT_GAIN_MAX_DB;
  }
  else if (clamped_gain < ADAU1979_INPUT_GAIN_MIN_DB)
  {
    clamped_gain = ADAU1979_INPUT_GAIN_MIN_DB;
  }

  /* Datasheet mapping:
   * 0x00 = +60 dB, 0xA0 = 0 dB, 1 LSB = -0.375 dB, 0xFF = Mute.
   * Convert dB to register with rounding to the nearest 0.375 dB step.
   */
  steps = (ADAU1979_INPUT_GAIN_MAX_DB - clamped_gain) / ADAU1979_INPUT_GAIN_STEP_DB;
  reg_value = (uint8_t)(steps + 0.5f);

  if (reg_value >= ADAU1979_INPUT_GAIN_MUTE_REG)
  {
    reg_value = ADAU1979_INPUT_GAIN_MIN_REG;
  }

  return reg_value;
}

static bool adau1979_set_input_gain_reg(uint8_t addr, adau1979_channel_t ch, uint8_t reg_value)
{
  uint8_t reg = 0U;

  switch (ch)
  {
    case ADAU1979_CH1:
      reg = ADAU1979_REG_POSTADC_GAIN1;
      break;
    case ADAU1979_CH2:
      reg = ADAU1979_REG_POSTADC_GAIN2;
      break;
    case ADAU1979_CH3:
      reg = ADAU1979_REG_POSTADC_GAIN3;
      break;
    case ADAU1979_CH4:
      reg = ADAU1979_REG_POSTADC_GAIN4;
      break;
    default:
      return false;
  }

  return adau1979_write_verify_log(addr, reg, reg_value, 0xFF);
}

void adau1979_init(uint8_t addr, uint8_t first_slot)
{
  /* Slot mapping values follow the datasheet slot numbering:
   * code 0 -> slot 1, code 7 -> slot 8 in TDM8 mode.
   */
  uint8_t slot0 = (uint8_t)(first_slot + 0U);
  uint8_t slot1 = (uint8_t)(first_slot + 1U);
  uint8_t slot2 = (uint8_t)(first_slot + 2U);
  uint8_t slot3 = (uint8_t)(first_slot + 3U);
  uint8_t cmap12 = (uint8_t)((slot1 << 4) | slot0);
  uint8_t cmap34 = (uint8_t)((slot3 << 4) | slot2);

  if (!adau1979_is_present(addr))
  {
    return;
  }

  /* Reset the ADC core. */
  (void)adau1979_write_reg_log(addr, ADAU1979_REG_M_POWER, ADAU1979_M_POWER_SOFT_RESET);
  HAL_Delay(2);

  /* Configure PLL for 256 x FS in slave mode (BCLK/LRCLK provided externally). */
  (void)adau1979_write_verify_log(addr,
                                  ADAU1979_REG_PLL_CONTROL,
                                  ADAU1979_PLL_CONTROL_256FS,
                                  ADAU1979_PLL_CONTROL_MASK);

  /* Enable ADC channels and SAI block. */
  (void)adau1979_write_verify_log(addr,
                                  ADAU1979_REG_BLOCK_POWER_SAI,
                                  ADAU1979_BLOCK_POWER_ENABLE,
                                  ADAU1979_BLOCK_POWER_MASK);

  /* SAI: TDM8, 48 kHz, left-justified 24-bit data in 32-bit slots. */
  (void)adau1979_write_verify_log(addr,
                                  ADAU1979_REG_SAI_CTRL0,
                                  (uint8_t)(ADAU1979_SAI_CTRL0_LJ | ADAU1979_SAI_CTRL0_TDM8 | ADAU1979_SAI_CTRL0_FS_48K),
                                  ADAU1979_SAI_CTRL0_MASK);
  (void)adau1979_write_verify_log(addr,
                                  ADAU1979_REG_SAI_CTRL1,
                                  ADAU1979_SAI_CTRL1_32BIT_24DATA,
                                  ADAU1979_SAI_CTRL1_MASK);

  /* Channel-to-slot map: slots are 0..7 for TDM8 (slot code 0 is first slot). */
  (void)adau1979_write_verify_log(addr,
                                  ADAU1979_REG_SAI_CMAP12,
                                  cmap12,
                                  0xFF);
  (void)adau1979_write_verify_log(addr,
                                  ADAU1979_REG_SAI_CMAP34,
                                  cmap34,
                                  0xFF);

  /* Drive all SAI output pins, disable overtemp shutdown output tri-state. */
  (void)adau1979_write_verify_log(addr,
                                  ADAU1979_REG_SAI_OVERTEMP,
                                  ADAU1979_SAI_DRIVE_ALL,
                                  ADAU1979_SAI_DRIVE_MASK);

  HAL_Delay(10);

  /* Power up the ADC. */
  (void)adau1979_write_verify_log(addr, ADAU1979_REG_M_POWER, ADAU1979_M_POWER_POWER_UP, ADAU1979_M_POWER_POWER_UP);
}

void adau1979_init_all(void)
{
  adau1979_init(ADAU1979_ADDR_0, 0U);
  adau1979_init(ADAU1979_ADDR_1, 4U);
}

bool adau1979_set_input_gain_db(uint8_t addr, adau1979_channel_t ch, float gain_db)
{
  uint8_t reg_value = adau1979_gain_db_to_reg(gain_db);

  return adau1979_set_input_gain_reg(addr, ch, reg_value);
}

bool adau1979_set_all_inputs_gain_db(uint8_t addr, float gain_db)
{
  uint8_t reg_value = adau1979_gain_db_to_reg(gain_db);

  if (!adau1979_set_input_gain_reg(addr, ADAU1979_CH1, reg_value))
  {
    return false;
  }
  if (!adau1979_set_input_gain_reg(addr, ADAU1979_CH2, reg_value))
  {
    return false;
  }
  if (!adau1979_set_input_gain_reg(addr, ADAU1979_CH3, reg_value))
  {
    return false;
  }
  if (!adau1979_set_input_gain_reg(addr, ADAU1979_CH4, reg_value))
  {
    return false;
  }

  return true;
}
bool adau1979_debug_read_reg(uint8_t addr, uint8_t reg, uint8_t *value)
{
  if (HAL_I2C_Mem_Read(&hi2c1,
                       (uint16_t)(addr << 1),
                       reg,
                       I2C_MEMADD_SIZE_8BIT,
                       value,
                       1,
                       100) == HAL_OK)
  {
    return true;
  }
  return false;
}
float adau1979_gain_reg_to_db(uint8_t reg)
{
  if (reg == 0xFF)
    return -999.0f; // mute

  // 0x00 = +60 dB, 1 LSB = -0.375 dB
  return 60.0f - ((float)reg * 0.375f);
}

void ADAU1979_DumpAllRegisters(uint8_t addr)
{
  static const uint8_t registers[] = {
    ADAU1979_REG_M_POWER,
    ADAU1979_REG_PLL_CONTROL,
    ADAU1979_REG_BLOCK_POWER_SAI,
    ADAU1979_REG_SAI_CTRL0,
    ADAU1979_REG_SAI_CTRL1,
    ADAU1979_REG_SAI_CMAP12,
    ADAU1979_REG_SAI_CMAP34,
    ADAU1979_REG_SAI_OVERTEMP,
    ADAU1979_REG_POSTADC_GAIN1,
    ADAU1979_REG_POSTADC_GAIN2,
    ADAU1979_REG_POSTADC_GAIN3,
    ADAU1979_REG_POSTADC_GAIN4,
    ADAU1979_REG_MISC_CONTROL,
    ADAU1979_REG_ADC_CLIP
  };
  uint8_t value = 0U;

  adau1979_uart_logf("ADAU1979[0x%02X] register dump start\r\n", addr);
  for (size_t i = 0; i < (sizeof(registers) / sizeof(registers[0])); ++i)
  {
    if (adau1979_read_reg(addr, registers[i], &value) == HAL_OK)
    {
      adau1979_uart_logf("ADAU1979[0x%02X] REG 0x%02X = 0x%02X\r\n", addr, registers[i], value);
    }
    else
    {
      adau1979_uart_logf("ADAU1979[0x%02X] REG 0x%02X read FAIL\r\n", addr, registers[i]);
    }
  }
  adau1979_uart_logf("ADAU1979[0x%02X] register dump end\r\n", addr);
}

void ADAU1979_DumpGains(uint8_t addr)
{
  uint8_t gains[4] = {0U, 0U, 0U, 0U};
  float gain_db[4] = {0.0f, 0.0f, 0.0f, 0.0f};

  (void)adau1979_read_reg(addr, ADAU1979_REG_POSTADC_GAIN1, &gains[0]);
  (void)adau1979_read_reg(addr, ADAU1979_REG_POSTADC_GAIN2, &gains[1]);
  (void)adau1979_read_reg(addr, ADAU1979_REG_POSTADC_GAIN3, &gains[2]);
  (void)adau1979_read_reg(addr, ADAU1979_REG_POSTADC_GAIN4, &gains[3]);

  for (size_t i = 0; i < 4; ++i)
  {
    gain_db[i] = adau1979_gain_reg_to_db(gains[i]);
  }

  adau1979_uart_logf("ADAU1979[0x%02X] gains raw=%02X %02X %02X %02X (dB %.3f %.3f %.3f %.3f)\r\n",
                     addr,
                     gains[0],
                     gains[1],
                     gains[2],
                     gains[3],
                     gain_db[0],
                     gain_db[1],
                     gain_db[2],
                     gain_db[3]);
}

bool ADAU1979_SelfTest(uint8_t addr)
{
  uint8_t misc_control = 0U;
  uint8_t gains[4] = {0U, 0U, 0U, 0U};
  bool ok = true;

  if (adau1979_read_reg(addr, ADAU1979_REG_MISC_CONTROL, &misc_control) != HAL_OK)
  {
    adau1979_uart_logf("ADAU1979[0x%02X] selftest: read MISC_CONTROL FAIL\r\n", addr);
    return false;
  }

  (void)adau1979_read_reg(addr, ADAU1979_REG_POSTADC_GAIN1, &gains[0]);
  (void)adau1979_read_reg(addr, ADAU1979_REG_POSTADC_GAIN2, &gains[1]);
  (void)adau1979_read_reg(addr, ADAU1979_REG_POSTADC_GAIN3, &gains[2]);
  (void)adau1979_read_reg(addr, ADAU1979_REG_POSTADC_GAIN4, &gains[3]);

  adau1979_uart_logf("ADAU1979[0x%02X] selftest: mute\r\n", addr);
  ok &= adau1979_write_verify_log(addr,
                                  ADAU1979_REG_MISC_CONTROL,
                                  (uint8_t)(misc_control | ADAU1979_MISC_CONTROL_MMUTE),
                                  ADAU1979_MISC_CONTROL_MMUTE);

  adau1979_uart_logf("ADAU1979[0x%02X] selftest: set +60 dB\r\n", addr);
  ok &= adau1979_write_verify_log(addr, ADAU1979_REG_POSTADC_GAIN1, 0x00U, 0xFF);
  ok &= adau1979_write_verify_log(addr, ADAU1979_REG_POSTADC_GAIN2, 0x00U, 0xFF);
  ok &= adau1979_write_verify_log(addr, ADAU1979_REG_POSTADC_GAIN3, 0x00U, 0xFF);
  ok &= adau1979_write_verify_log(addr, ADAU1979_REG_POSTADC_GAIN4, 0x00U, 0xFF);
  ADAU1979_DumpGains(addr);

  adau1979_uart_logf("ADAU1979[0x%02X] selftest: set -30 dB\r\n", addr);
  {
    uint8_t gain_minus_30 = adau1979_gain_db_to_reg(-30.0f);
    ok &= adau1979_write_verify_log(addr, ADAU1979_REG_POSTADC_GAIN1, gain_minus_30, 0xFF);
    ok &= adau1979_write_verify_log(addr, ADAU1979_REG_POSTADC_GAIN2, gain_minus_30, 0xFF);
    ok &= adau1979_write_verify_log(addr, ADAU1979_REG_POSTADC_GAIN3, gain_minus_30, 0xFF);
    ok &= adau1979_write_verify_log(addr, ADAU1979_REG_POSTADC_GAIN4, gain_minus_30, 0xFF);
  }
  ADAU1979_DumpGains(addr);

  adau1979_uart_logf("ADAU1979[0x%02X] selftest: restore settings\r\n", addr);
  ok &= adau1979_write_verify_log(addr, ADAU1979_REG_MISC_CONTROL, misc_control, 0xFF);
  ok &= adau1979_write_verify_log(addr, ADAU1979_REG_POSTADC_GAIN1, gains[0], 0xFF);
  ok &= adau1979_write_verify_log(addr, ADAU1979_REG_POSTADC_GAIN2, gains[1], 0xFF);
  ok &= adau1979_write_verify_log(addr, ADAU1979_REG_POSTADC_GAIN3, gains[2], 0xFF);
  ok &= adau1979_write_verify_log(addr, ADAU1979_REG_POSTADC_GAIN4, gains[3], 0xFF);

  return ok;
}
