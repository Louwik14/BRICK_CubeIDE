#include "adau1979.h"
#include "i2c.h"
#include "main.h"

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

enum
{
  ADAU1979_SAI_CTRL0_LJ = 0x40,
  ADAU1979_SAI_CTRL0_TDM8 = 0x18,
  ADAU1979_SAI_CTRL0_FS_48K = 0x02,
  ADAU1979_SAI_CTRL0_TDM8_48K_LJ = (ADAU1979_SAI_CTRL0_LJ | ADAU1979_SAI_CTRL0_TDM8 | ADAU1979_SAI_CTRL0_FS_48K),

  ADAU1979_SAI_CTRL1_32BIT_24DATA = 0x00,
  ADAU1979_BLOCK_POWER_ENABLE = 0x3F,
  ADAU1979_SAI_DRIVE_ALL = 0xF0,
  ADAU1979_PLL_CONTROL_256FS = 0x41,
  ADAU1979_POWER_UP = 0x01,
  ADAU1979_SOFT_RESET = 0x80
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

  (void)adau1979_write_reg(addr, ADAU1979_REG_M_POWER, ADAU1979_SOFT_RESET);
  HAL_Delay(2);

  (void)adau1979_write_reg(addr, ADAU1979_REG_PLL_CONTROL, ADAU1979_PLL_CONTROL_256FS);
  (void)adau1979_write_reg(addr, ADAU1979_REG_BLOCK_POWER_SAI, ADAU1979_BLOCK_POWER_ENABLE);
  (void)adau1979_write_reg(addr, ADAU1979_REG_SAI_CTRL0, ADAU1979_SAI_CTRL0_TDM8_48K_LJ);
  (void)adau1979_write_reg(addr, ADAU1979_REG_SAI_CTRL1, ADAU1979_SAI_CTRL1_32BIT_24DATA);
  (void)adau1979_write_reg(addr, ADAU1979_REG_SAI_CMAP12, cmap12);
  (void)adau1979_write_reg(addr, ADAU1979_REG_SAI_CMAP34, cmap34);
  (void)adau1979_write_reg(addr, ADAU1979_REG_SAI_OVERTEMP, ADAU1979_SAI_DRIVE_ALL);

  HAL_Delay(10);
  (void)adau1979_write_reg(addr, ADAU1979_REG_M_POWER, ADAU1979_POWER_UP);
}

void adau1979_init_all(void)
{
  adau1979_init(ADAU1979_ADDR_0, 0U);
  adau1979_init(ADAU1979_ADDR_1, 4U);
}
