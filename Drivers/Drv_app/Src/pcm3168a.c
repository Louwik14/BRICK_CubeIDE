/**
 * @file pcm3168a.c
 * @brief Initialisation minimale des codecs PCM3168A via I2C.
 */

#include "pcm3168a.h"
#include "i2c.h"

enum
{
  PCM3168A_REG_RESET_SAMPLING = 0x40,
  PCM3168A_REG_DAC_CONTROL_1 = 0x41,
  PCM3168A_REG_DAC_CONTROL_2 = 0x42,
  PCM3168A_REG_DAC_SOFT_MUTE = 0x44,
  PCM3168A_REG_ADC_SAMPLING = 0x50,
  PCM3168A_REG_ADC_CONTROL_1 = 0x51,
  PCM3168A_REG_ADC_CONTROL_2 = 0x52,
  PCM3168A_REG_ADC_INPUT_CONFIG = 0x53,
  PCM3168A_REG_ADC_SOFT_MUTE = 0x55
};

enum
{
  PCM3168A_RESET_SAMPLING_NORMAL_AUTO = 0xC0,
  PCM3168A_DAC_CONTROL_1_SLAVE_TDM24_I2S = 0x06,
  PCM3168A_DAC_CONTROL_2_NORMAL_SHARP = 0x00,
  PCM3168A_DAC_SOFT_MUTE_DISABLE_ALL = 0x00,
  PCM3168A_ADC_SAMPLING_AUTO = 0x00,
  PCM3168A_ADC_CONTROL_1_SLAVE_TDM24_I2S = 0x06,
  PCM3168A_ADC_CONTROL_2_NORMAL_HPF = 0x00,
  PCM3168A_ADC_INPUT_CONFIG_DIFFERENTIAL = 0x00,
  PCM3168A_ADC_SOFT_MUTE_DISABLE_ALL = 0x00
};

static HAL_StatusTypeDef pcm3168a_write_reg(uint8_t addr_7bit, uint8_t reg, uint8_t value)
{
  return HAL_I2C_Mem_Write(&hi2c2,
                           (uint16_t)(addr_7bit << 1),
                           reg,
                           I2C_MEMADD_SIZE_8BIT,
                           &value,
                           1U,
                           100U);
}

static bool pcm3168a_is_present(uint8_t addr_7bit)
{
  return HAL_I2C_IsDeviceReady(&hi2c2,
                               (uint16_t)(addr_7bit << 1),
                               3U,
                               100U) == HAL_OK;
}

bool PCM3168A_Init(uint8_t addr_7bit)
{
  static const struct
  {
    uint8_t reg;
    uint8_t value;
  } init_seq[] = {
    { PCM3168A_REG_RESET_SAMPLING, PCM3168A_RESET_SAMPLING_NORMAL_AUTO },
    { PCM3168A_REG_DAC_CONTROL_1, PCM3168A_DAC_CONTROL_1_SLAVE_TDM24_I2S },
    { PCM3168A_REG_DAC_CONTROL_2, PCM3168A_DAC_CONTROL_2_NORMAL_SHARP },
    { PCM3168A_REG_DAC_SOFT_MUTE, PCM3168A_DAC_SOFT_MUTE_DISABLE_ALL },
    { PCM3168A_REG_ADC_SAMPLING, PCM3168A_ADC_SAMPLING_AUTO },
    { PCM3168A_REG_ADC_CONTROL_1, PCM3168A_ADC_CONTROL_1_SLAVE_TDM24_I2S },
    { PCM3168A_REG_ADC_CONTROL_2, PCM3168A_ADC_CONTROL_2_NORMAL_HPF },
    { PCM3168A_REG_ADC_INPUT_CONFIG, PCM3168A_ADC_INPUT_CONFIG_DIFFERENTIAL },
    { PCM3168A_REG_ADC_SOFT_MUTE, PCM3168A_ADC_SOFT_MUTE_DISABLE_ALL },
  };

  if (!pcm3168a_is_present(addr_7bit))
  {
    return false;
  }

  for (uint32_t i = 0U; i < (uint32_t)(sizeof(init_seq) / sizeof(init_seq[0])); i++)
  {
    if (pcm3168a_write_reg(addr_7bit, init_seq[i].reg, init_seq[i].value) != HAL_OK)
    {
      return false;
    }
  }

  return true;
}
