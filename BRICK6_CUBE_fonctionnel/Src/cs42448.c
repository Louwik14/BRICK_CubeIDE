#include "cs42448.h"

enum
{
  CS42448_REG_POWER_CONTROL = 0x02U,
  CS42448_REG_FUNCTIONAL_MODE = 0x03U,
  CS42448_REG_INTERFACE_FORMATS = 0x04U,
  CS42448_REG_ADC_CONTROL = 0x05U,
  CS42448_REG_TRANSITION_CONTROL = 0x06U,
  CS42448_REG_CHANNEL_MUTE = 0x07U
};

static HAL_StatusTypeDef cs42448_write_reg(I2C_HandleTypeDef *hi2c,
                                           uint8_t i2c_addr,
                                           uint8_t reg,
                                           uint8_t value)
{
  return HAL_I2C_Mem_Write(hi2c,
                           (uint16_t)(i2c_addr << 1),
                           reg,
                           I2C_MEMADD_SIZE_8BIT,
                           &value,
                           1U,
                           100U);
}

HAL_StatusTypeDef CS42448_Init(I2C_HandleTypeDef *hi2c, uint8_t i2c_addr)
{
  if (hi2c == NULL)
  {
    return HAL_ERROR;
  }

  HAL_StatusTypeDef status = HAL_OK;

  HAL_Delay(2);

  status = cs42448_write_reg(hi2c, i2c_addr, CS42448_REG_POWER_CONTROL, 0x01U);
  if (status != HAL_OK)
  {
    return status;
  }

  HAL_Delay(2);

  status = cs42448_write_reg(hi2c, i2c_addr, CS42448_REG_FUNCTIONAL_MODE, 0xF0U);
  if (status != HAL_OK)
  {
    return status;
  }

  status = cs42448_write_reg(hi2c, i2c_addr, CS42448_REG_INTERFACE_FORMATS, 0x36U);
  if (status != HAL_OK)
  {
    return status;
  }

  status = cs42448_write_reg(hi2c, i2c_addr, CS42448_REG_ADC_CONTROL, 0x00U);
  if (status != HAL_OK)
  {
    return status;
  }

  status = cs42448_write_reg(hi2c, i2c_addr, CS42448_REG_TRANSITION_CONTROL, 0x10U);
  if (status != HAL_OK)
  {
    return status;
  }

  status = cs42448_write_reg(hi2c, i2c_addr, CS42448_REG_CHANNEL_MUTE, 0x00U);
  if (status != HAL_OK)
  {
    return status;
  }

  return HAL_OK;
}
