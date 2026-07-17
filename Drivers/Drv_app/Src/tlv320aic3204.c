#include "tlv320aic3204.h"
#include "i2c.h"
#include "main.h"
#include <stddef.h>

#define TLV_I2C_TIMEOUT_MS 100U

enum
{
  TLV_PAGE_SELECT = 0,
  TLV_RESET = 1,
  TLV_CLK_MUX = 4,
  TLV_NDAC = 11,
  TLV_MDAC = 12,
  TLV_DOSR_MSB = 13,
  TLV_DOSR_LSB = 14,
  TLV_NADC = 18,
  TLV_MADC = 19,
  TLV_AOSR = 20,
  TLV_AUDIO_IF_1 = 27,
  TLV_DAC_PROCESSING_BLOCK = 60,
  TLV_DAC_DATAPATH = 63,
  TLV_DAC_VOLUME_CTRL = 64,
  TLV_LEFT_DAC_VOL = 65,
  TLV_RIGHT_DAC_VOL = 66,
  TLV_ADC_POWER = 81,
  TLV_ADC_FINE_GAIN = 82,
  TLV_LEFT_AGC = 86,
  TLV_RIGHT_AGC = 94
};

enum
{
  TLV_P1_POWER_CONFIG = 1,
  TLV_P1_LDO_CTRL = 2,
  TLV_P1_OUTPUT_POWER = 9,
  TLV_P1_CM_CTRL = 10,
  TLV_P1_HPL_ROUTE = 12,
  TLV_P1_HPR_ROUTE = 13,
  TLV_P1_LOL_ROUTE = 14,
  TLV_P1_LOR_ROUTE = 15,
  TLV_P1_HPL_GAIN = 16,
  TLV_P1_HPR_GAIN = 17,
  TLV_P1_LOL_GAIN = 18,
  TLV_P1_LOR_GAIN = 19,
  TLV_P1_HEADPHONE_STARTUP = 20,
  TLV_P1_OUTPUT_COMMON_MODE = 40,
  TLV_P1_MICBIAS = 51,
  TLV_P1_LEFT_P_ROUTE = 52,
  TLV_P1_RIGHT_P_ROUTE = 54,
  TLV_P1_LEFT_M_ROUTE = 55,
  TLV_P1_RIGHT_M_ROUTE = 57,
  TLV_P1_FLOATING_INPUT = 58,
  TLV_P1_LEFT_PGA_GAIN = 59,
  TLV_P1_RIGHT_PGA_GAIN = 60,
  TLV_P1_ADC_POWER_TUNE = 61,
  TLV_P1_ANALOG_INPUT_QUICK_CHARGE = 71,
  TLV_P1_REFERENCE_POWER_UP = 123
};

static tlv320aic3204_status_t tlv_hal_to_status(HAL_StatusTypeDef status)
{
  return (status == HAL_OK) ? TLV320AIC3204_STATUS_OK : TLV320AIC3204_STATUS_I2C_ERROR;
}

tlv320aic3204_status_t TLV320AIC3204_SelectPage(I2C_HandleTypeDef *i2c,
                                                uint8_t address_7bit,
                                                uint8_t page)
{
  if ((i2c == NULL) || (address_7bit == 0U))
  {
    return TLV320AIC3204_STATUS_CONFIG_ERROR;
  }

  return tlv_hal_to_status(HAL_I2C_Mem_Write(i2c,
                                             (uint16_t)(address_7bit << 1),
                                             TLV_PAGE_SELECT,
                                             I2C_MEMADD_SIZE_8BIT,
                                             &page,
                                             1U,
                                             TLV_I2C_TIMEOUT_MS));
}

tlv320aic3204_status_t TLV320AIC3204_WriteReg(I2C_HandleTypeDef *i2c,
                                              uint8_t address_7bit,
                                              uint8_t page,
                                              uint8_t reg,
                                              uint8_t value)
{
  tlv320aic3204_status_t status = TLV320AIC3204_SelectPage(i2c, address_7bit, page);
  if (status != TLV320AIC3204_STATUS_OK)
  {
    return status;
  }

  return tlv_hal_to_status(HAL_I2C_Mem_Write(i2c,
                                             (uint16_t)(address_7bit << 1),
                                             reg,
                                             I2C_MEMADD_SIZE_8BIT,
                                             &value,
                                             1U,
                                             TLV_I2C_TIMEOUT_MS));
}

tlv320aic3204_status_t TLV320AIC3204_ReadReg(I2C_HandleTypeDef *i2c,
                                             uint8_t address_7bit,
                                             uint8_t page,
                                             uint8_t reg,
                                             uint8_t *value)
{
  if (value == NULL)
  {
    return TLV320AIC3204_STATUS_CONFIG_ERROR;
  }

  tlv320aic3204_status_t status = TLV320AIC3204_SelectPage(i2c, address_7bit, page);
  if (status != TLV320AIC3204_STATUS_OK)
  {
    return status;
  }

  return tlv_hal_to_status(HAL_I2C_Mem_Read(i2c,
                                            (uint16_t)(address_7bit << 1),
                                            reg,
                                            I2C_MEMADD_SIZE_8BIT,
                                            value,
                                            1U,
                                            TLV_I2C_TIMEOUT_MS));
}

tlv320aic3204_status_t TLV320AIC3204_SoftwareReset(I2C_HandleTypeDef *i2c,
                                                   uint8_t address_7bit)
{
#if defined(CODEC_RESET_Pin) && defined(CODEC_RESET_GPIO_Port)
  HAL_GPIO_WritePin(CODEC_RESET_GPIO_Port, CODEC_RESET_Pin, GPIO_PIN_RESET);
  HAL_Delay(2U);
  HAL_GPIO_WritePin(CODEC_RESET_GPIO_Port, CODEC_RESET_Pin, GPIO_PIN_SET);
  HAL_Delay(10U);
#endif

  tlv320aic3204_status_t status =
      TLV320AIC3204_WriteReg(i2c, address_7bit, 0U, TLV_RESET, 0x01U);
  if (status == TLV320AIC3204_STATUS_OK)
  {
    HAL_Delay(2U);
  }
  return status;
}

static tlv320aic3204_status_t tlv_write_checked(I2C_HandleTypeDef *i2c,
                                                uint8_t address,
                                                uint8_t page,
                                                uint8_t reg,
                                                uint8_t value)
{
  tlv320aic3204_status_t status = TLV320AIC3204_WriteReg(i2c, address, page, reg, value);
  if (status != TLV320AIC3204_STATUS_OK)
  {
    return status;
  }

  uint8_t readback = 0U;
  status = TLV320AIC3204_ReadReg(i2c, address, page, reg, &readback);
  if (status != TLV320AIC3204_STATUS_OK)
  {
    return status;
  }

  return (readback == value) ? TLV320AIC3204_STATUS_OK : TLV320AIC3204_STATUS_VERIFY_ERROR;
}

static uint8_t tlv_word_length_bits(uint8_t word_bits)
{
  switch (word_bits)
  {
  case 16U: return 0x00U;
  case 20U: return 0x10U;
  case 24U: return 0x20U;
  case 32U: return 0x30U;
  default: return 0xFFU;
  }
}

tlv320aic3204_status_t TLV320AIC3204_Init(const tlv320aic3204_config_t *config)
{
  if ((config == NULL) ||
      (config->i2c == NULL) ||
      (config->address_7bit == 0U) ||
      (config->mclk_hz != 12288000UL) ||
      (config->left_p_route != 0x80U) ||
      (config->left_m_route != 0x40U) ||
      (config->right_p_route != 0x80U) ||
      (config->right_m_route != 0x40U))
  {
    return TLV320AIC3204_STATUS_CONFIG_ERROR;
  }

  const uint8_t word_length = tlv_word_length_bits(config->word_bits);
  if (word_length == 0xFFU)
  {
    return TLV320AIC3204_STATUS_CONFIG_ERROR;
  }

  if (HAL_I2C_IsDeviceReady(config->i2c,
                            (uint16_t)(config->address_7bit << 1),
                            3U,
                            TLV_I2C_TIMEOUT_MS) != HAL_OK)
  {
    return TLV320AIC3204_STATUS_I2C_ERROR;
  }

  tlv320aic3204_status_t status =
      TLV320AIC3204_SoftwareReset(config->i2c, config->address_7bit);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }

  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_REFERENCE_POWER_UP, 0x05U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_ANALOG_INPUT_QUICK_CHARGE, 0x32U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  HAL_Delay(40U);

  status = tlv_write_checked(config->i2c, config->address_7bit, 0U, TLV_CLK_MUX, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_checked(config->i2c, config->address_7bit, 0U, TLV_NDAC, 0x81U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_checked(config->i2c, config->address_7bit, 0U, TLV_MDAC, 0x82U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_checked(config->i2c, config->address_7bit, 0U, TLV_DOSR_MSB, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_checked(config->i2c, config->address_7bit, 0U, TLV_DOSR_LSB, 0x80U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_checked(config->i2c, config->address_7bit, 0U, TLV_NADC, 0x81U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_checked(config->i2c, config->address_7bit, 0U, TLV_MADC, 0x82U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_checked(config->i2c, config->address_7bit, 0U, TLV_AOSR, 0x80U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }

  status = tlv_write_checked(config->i2c, config->address_7bit, 0U, TLV_AUDIO_IF_1, word_length);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 0U, TLV_DAC_PROCESSING_BLOCK, 0x01U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }

  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_MICBIAS, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_FLOATING_INPUT, 0x3FU);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_LEFT_P_ROUTE, config->left_p_route);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_LEFT_M_ROUTE, config->left_m_route);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_RIGHT_P_ROUTE, config->right_p_route);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_RIGHT_M_ROUTE, config->right_m_route);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_LEFT_PGA_GAIN, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_RIGHT_PGA_GAIN, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_ADC_POWER_TUNE, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }

  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_POWER_CONFIG, 0x08U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_LDO_CTRL, 0x01U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_CM_CTRL, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }

  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 0U, TLV_ADC_POWER, 0xC0U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 0U, TLV_ADC_FINE_GAIN, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 0U, TLV_LEFT_AGC, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 0U, TLV_RIGHT_AGC, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }

  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_HPL_ROUTE, 0x08U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_HPR_ROUTE, 0x08U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_LOL_ROUTE, 0x08U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_LOR_ROUTE, 0x08U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_OUTPUT_COMMON_MODE, 0x06U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_HEADPHONE_STARTUP, 0x25U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_HPL_GAIN, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_HPR_GAIN, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_LOL_GAIN, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_LOR_GAIN, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 1U, TLV_P1_OUTPUT_POWER, 0x3CU);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }

  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 0U, TLV_DAC_DATAPATH, 0xD4U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 0U, TLV_DAC_VOLUME_CTRL, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 0U, TLV_LEFT_DAC_VOL, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = TLV320AIC3204_WriteReg(config->i2c, config->address_7bit, 0U, TLV_RIGHT_DAC_VOL, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }

  HAL_Delay(10U);
  return TLV320AIC3204_STATUS_OK;
}

tlv320aic3204_status_t TLV320AIC3204_InitDefault(void)
{
  const tlv320aic3204_config_t config = {
      .i2c = &hi2c1,
      .address_7bit = TLV320AIC3204_I2C_ADDR_7BIT,
      .mclk_hz = TLV320AIC3204_MCLK_HZ,
      .word_bits = TLV320AIC3204_WORD_BITS,
      .left_p_route = TLV320AIC3204_LEFT_P_ROUTE,
      .left_m_route = TLV320AIC3204_LEFT_M_ROUTE,
      .right_p_route = TLV320AIC3204_RIGHT_P_ROUTE,
      .right_m_route = TLV320AIC3204_RIGHT_M_ROUTE,
  };

  return TLV320AIC3204_Init(&config);
}
