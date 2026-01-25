#include "adau1979.h"
#include "i2c.h"
#include "main.h"
#include <stdbool.h>

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
  ADAU1979_REG_SAI_OVERTEMP = 0x09
};

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
  ADAU1979_SAI_CTRL0_MASK = 0x7A
};

/* SAI_CTRL1: 32-bit slots, 24-bit left-justified data. */
enum
{
  ADAU1979_SAI_CTRL1_32BIT_24DATA = 0x00,
  ADAU1979_SAI_CTRL1_MASK = 0x00
};

/* SAI_OVERTEMP: drive all output pins. */
enum
{
  ADAU1979_SAI_DRIVE_ALL = 0xF0,
  ADAU1979_SAI_DRIVE_MASK = 0xF0
};

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

static bool adau1979_write_verify(uint8_t addr, uint8_t reg, uint8_t value, uint8_t mask)
{
  uint8_t readback = 0;

  if (adau1979_write_reg(addr, reg, value) != HAL_OK)
  {
    return false;
  }

  if (adau1979_read_reg(addr, reg, &readback) != HAL_OK)
  {
    return false;
  }

  return (uint8_t)(readback & mask) == (uint8_t)(value & mask);
}

static bool adau1979_is_present(uint8_t addr)
{
  return HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 3U, 100U) == HAL_OK;
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
  (void)adau1979_write_reg(addr, ADAU1979_REG_M_POWER, ADAU1979_M_POWER_SOFT_RESET);
  HAL_Delay(2);

  /* Configure PLL for 256 x FS in slave mode (BCLK/LRCLK provided externally). */
  (void)adau1979_write_verify(addr,
                              ADAU1979_REG_PLL_CONTROL,
                              ADAU1979_PLL_CONTROL_256FS,
                              ADAU1979_PLL_CONTROL_MASK);

  /* Enable ADC channels and SAI block. */
  (void)adau1979_write_verify(addr,
                              ADAU1979_REG_BLOCK_POWER_SAI,
                              ADAU1979_BLOCK_POWER_ENABLE,
                              ADAU1979_BLOCK_POWER_MASK);

  /* SAI: TDM8, 48 kHz, left-justified 24-bit data in 32-bit slots. */
  (void)adau1979_write_verify(addr,
                              ADAU1979_REG_SAI_CTRL0,
                              (uint8_t)(ADAU1979_SAI_CTRL0_LJ | ADAU1979_SAI_CTRL0_TDM8 | ADAU1979_SAI_CTRL0_FS_48K),
                              ADAU1979_SAI_CTRL0_MASK);
  (void)adau1979_write_verify(addr,
                              ADAU1979_REG_SAI_CTRL1,
                              ADAU1979_SAI_CTRL1_32BIT_24DATA,
                              ADAU1979_SAI_CTRL1_MASK);

  /* Channel-to-slot map: slots are 0..7 for TDM8 (slot code 0 is first slot). */
  (void)adau1979_write_verify(addr,
                              ADAU1979_REG_SAI_CMAP12,
                              cmap12,
                              0xFF);
  (void)adau1979_write_verify(addr,
                              ADAU1979_REG_SAI_CMAP34,
                              cmap34,
                              0xFF);

  /* Drive all SAI output pins, disable overtemp shutdown output tri-state. */
  (void)adau1979_write_verify(addr,
                              ADAU1979_REG_SAI_OVERTEMP,
                              ADAU1979_SAI_DRIVE_ALL,
                              ADAU1979_SAI_DRIVE_MASK);

  HAL_Delay(10);

  /* Power up the ADC. */
  (void)adau1979_write_verify(addr, ADAU1979_REG_M_POWER, ADAU1979_M_POWER_POWER_UP, ADAU1979_M_POWER_POWER_UP);
}

void adau1979_init_all(void)
{
  adau1979_init(ADAU1979_ADDR_0, 0U);
  adau1979_init(ADAU1979_ADDR_1, 4U);
}
