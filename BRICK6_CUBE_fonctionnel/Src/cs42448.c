#include "cs42448.h"
#include "i2c.h"
#include <stdbool.h>

/* CS42448 register map (subset used). */
enum
{
  CS42448_REG_CHIP_ID = 0x01,
  CS42448_REG_POWER_CTRL = 0x02,
  CS42448_REG_FUNCTIONAL_MODE = 0x03,
  CS42448_REG_INTERFACE_FORMAT = 0x04,
  CS42448_REG_ADC_CTRL = 0x05,
  CS42448_REG_TRANSITION_CTRL = 0x06,
  CS42448_REG_DAC_MUTE = 0x07,
  CS42448_REG_DAC_VOL_BASE = 0x08,
  CS42448_REG_DAC_VOL_LAST = 0x0F,
  CS42448_REG_ADC_VOL_BASE = 0x11,
  CS42448_REG_ADC_VOL_LAST = 0x16
};

enum
{
  CS42448_POWER_PDN = 0x01
};

enum
{
  CS42448_FUNCTIONAL_SLAVE_AUTO = 0xF0,
  CS42448_FUNCTIONAL_MCLK_256FS = 0x00
};

enum
{
  CS42448_INTERFACE_TDM_24BIT = 0x36,
  CS42448_INTERFACE_FREEZE = 0x80
};

static HAL_StatusTypeDef cs42448_write_reg(uint8_t addr, uint8_t reg, uint8_t value)
{
  return HAL_I2C_Mem_Write(&hi2c1,
                           (uint16_t)(addr << 1),
                           reg,
                           I2C_MEMADD_SIZE_8BIT,
                           &value,
                           1U,
                           100U);
}

static bool cs42448_is_present(uint8_t addr)
{
  return HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 3U, 100U) == HAL_OK;
}

bool CS42448_Init(uint8_t addr)
{
  if (!cs42448_is_present(addr))
  {
    return false;
  }

  /* Hold device in power-down while configuring control registers. */
  if (cs42448_write_reg(addr, CS42448_REG_POWER_CTRL, CS42448_POWER_PDN) != HAL_OK)
  {
    return false;
  }
  HAL_Delay(2U);

  /* TDM, slave, auto-detect sample rates, 256Fs clocking. */
  if (cs42448_write_reg(addr,
                        CS42448_REG_FUNCTIONAL_MODE,
                        (uint8_t)(CS42448_FUNCTIONAL_SLAVE_AUTO | CS42448_FUNCTIONAL_MCLK_256FS)) != HAL_OK)
  {
    return false;
  }

  /* Freeze interface-related changes, set both ADC and DAC to TDM 24-bit. */
  if (cs42448_write_reg(addr,
                        CS42448_REG_INTERFACE_FORMAT,
                        (uint8_t)(CS42448_INTERFACE_TDM_24BIT | CS42448_INTERFACE_FREEZE)) != HAL_OK)
  {
    return false;
  }

  /* Default ADC control: normal inputs, no de-emphasis, HPF running. */
  if (cs42448_write_reg(addr, CS42448_REG_ADC_CTRL, 0x00) != HAL_OK)
  {
    return false;
  }

  /* Default transition control. */
  if (cs42448_write_reg(addr, CS42448_REG_TRANSITION_CTRL, 0x00) != HAL_OK)
  {
    return false;
  }

  /* Unmute all DAC channels. */
  if (cs42448_write_reg(addr, CS42448_REG_DAC_MUTE, 0x00) != HAL_OK)
  {
    return false;
  }

  /* Set DAC volumes to 0 dB. */
  for (uint8_t reg = CS42448_REG_DAC_VOL_BASE; reg <= CS42448_REG_DAC_VOL_LAST; ++reg)
  {
    if (cs42448_write_reg(addr, reg, 0x00) != HAL_OK)
    {
      return false;
    }
  }

  /* Set ADC volumes to 0 dB. */
  for (uint8_t reg = CS42448_REG_ADC_VOL_BASE; reg <= CS42448_REG_ADC_VOL_LAST; ++reg)
  {
    if (cs42448_write_reg(addr, reg, 0x00) != HAL_OK)
    {
      return false;
    }
  }

  /* Release FREEZE while keeping TDM mode. */
  if (cs42448_write_reg(addr, CS42448_REG_INTERFACE_FORMAT, CS42448_INTERFACE_TDM_24BIT) != HAL_OK)
  {
    return false;
  }

  /* Power up all ADC/DAC blocks. */
  if (cs42448_write_reg(addr, CS42448_REG_POWER_CTRL, 0x00) != HAL_OK)
  {
    return false;
  }

  HAL_Delay(10U);
  return true;
}
