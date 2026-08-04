#include "tlv320aic3204.h"
#include "i2c.h"
#include "main.h"
#include <stddef.h>
#include <string.h>

#define TLV_I2C_TIMEOUT_MS 100U
#define TLV_DEVICE_READY_TIMEOUT_MS 100U
#define TLV_BLOCK_READY_TIMEOUT_MS 100U
#define TLV_CODEC_STARTUP_WAIT_MS 5U

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
  TLV_ADC_FLAG = 36,
  TLV_DAC_FLAG_1 = 37,
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
  TLV_P1_HPL_GAIN = 16,
  TLV_P1_HPR_GAIN = 17,
  TLV_P1_HEADPHONE_STARTUP = 20,
  TLV_P1_MICBIAS = 51,
  TLV_P1_LEFT_P_ROUTE = 52,
  TLV_P1_LEFT_M_ROUTE = 54,
  TLV_P1_RIGHT_P_ROUTE = 55,
  TLV_P1_RIGHT_M_ROUTE = 57,
  TLV_P1_FLOATING_INPUT = 58,
  TLV_P1_LEFT_PGA_GAIN = 59,
  TLV_P1_RIGHT_PGA_GAIN = 60,
  TLV_P1_ADC_POWER_TUNE = 61,
  TLV_P1_ANALOG_INPUT_QUICK_CHARGE = 71,
  TLV_P1_REFERENCE_POWER_UP = 123
};

static tlv320aic3204_diag_t g_tlv_diag;

static void tlv_set_stage(tlv320aic3204_stage_t stage)
{
  g_tlv_diag.stage = stage;
}

static tlv320aic3204_status_t tlv_record_status(tlv320aic3204_status_t status)
{
  g_tlv_diag.status = status;
  return status;
}

static tlv320aic3204_status_t tlv_hal_to_status(HAL_StatusTypeDef status)
{
  return (status == HAL_OK) ? TLV320AIC3204_STATUS_OK : TLV320AIC3204_STATUS_I2C_ERROR;
}

static void tlv_record_i2c_error(void)
{
  g_tlv_diag.i2c_errors++;
}

tlv320aic3204_status_t TLV320AIC3204_SelectPage(I2C_HandleTypeDef *i2c,
                                                uint8_t address_7bit,
                                                uint8_t page)
{
  if ((i2c == NULL) || (address_7bit == 0U))
  {
    return TLV320AIC3204_STATUS_CONFIG_ERROR;
  }

  const HAL_StatusTypeDef hal_status = HAL_I2C_Mem_Write(i2c,
                                             (uint16_t)(address_7bit << 1),
                                             TLV_PAGE_SELECT,
                                             I2C_MEMADD_SIZE_8BIT,
                                             &page,
                                             1U,
                                             TLV_I2C_TIMEOUT_MS);
  if (hal_status != HAL_OK) tlv_record_i2c_error();
  return tlv_hal_to_status(hal_status);
}

tlv320aic3204_status_t TLV320AIC3204_WriteReg(I2C_HandleTypeDef *i2c,
                                              uint8_t address_7bit,
                                              uint8_t page,
                                              uint8_t reg,
                                              uint8_t value)
{
  g_tlv_diag.page = page;
  g_tlv_diag.reg = reg;
  g_tlv_diag.expected = value;
  g_tlv_diag.mask = 0xFFU;

  tlv320aic3204_status_t status = TLV320AIC3204_SelectPage(i2c, address_7bit, page);
  if (status != TLV320AIC3204_STATUS_OK)
  {
    g_tlv_diag.write_failures++;
    return tlv_record_status(status);
  }

  const HAL_StatusTypeDef hal_status = HAL_I2C_Mem_Write(i2c,
                                               (uint16_t)(address_7bit << 1),
                                               reg,
                                               I2C_MEMADD_SIZE_8BIT,
                                               &value,
                                               1U,
                                               TLV_I2C_TIMEOUT_MS);
  if (hal_status != HAL_OK)
  {
    tlv_record_i2c_error();
    g_tlv_diag.write_failures++;
  }
  status = tlv_hal_to_status(hal_status);
  return tlv_record_status(status);
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
    g_tlv_diag.page = page;
    g_tlv_diag.reg = reg;
    return tlv_record_status(status);
  }

  const HAL_StatusTypeDef hal_status = HAL_I2C_Mem_Read(i2c,
                                              (uint16_t)(address_7bit << 1),
                                              reg,
                                              I2C_MEMADD_SIZE_8BIT,
                                              value,
                                              1U,
                                              TLV_I2C_TIMEOUT_MS);
  if (hal_status != HAL_OK) tlv_record_i2c_error();
  status = tlv_hal_to_status(hal_status);
  g_tlv_diag.page = page;
  g_tlv_diag.reg = reg;
  if (status == TLV320AIC3204_STATUS_OK)
  {
    g_tlv_diag.actual = *value;
  }
  return tlv_record_status(status);
}

tlv320aic3204_status_t TLV320AIC3204_ReadRegCurrentPage(I2C_HandleTypeDef *i2c,
                                                        uint8_t address_7bit,
                                                        uint8_t reg,
                                                        uint8_t *value)
{
  if ((i2c == NULL) || (address_7bit == 0U) || (value == NULL))
  {
    return TLV320AIC3204_STATUS_CONFIG_ERROR;
  }

  const HAL_StatusTypeDef hal_status = HAL_I2C_Mem_Read(
      i2c,
      (uint16_t)(address_7bit << 1),
      reg,
      I2C_MEMADD_SIZE_8BIT,
      value,
      1U,
      TLV_I2C_TIMEOUT_MS);
  if (hal_status != HAL_OK) tlv_record_i2c_error();
  const tlv320aic3204_status_t status = tlv_hal_to_status(hal_status);
  g_tlv_diag.reg = reg;
  if (status == TLV320AIC3204_STATUS_OK)
  {
    g_tlv_diag.actual = *value;
  }
  return tlv_record_status(status);
}

tlv320aic3204_status_t TLV320AIC3204_SoftwareReset(I2C_HandleTypeDef *i2c,
                                                   uint8_t address_7bit)
{
  tlv_set_stage(TLV320AIC3204_STAGE_RESET);
#if defined(CODEC_RESET_Pin) && defined(CODEC_RESET_GPIO_Port)
  g_tlv_diag.reset_pin_used = 1U;
  g_tlv_diag.reset_low_duration_ms = 2U;
  g_tlv_diag.reset_wait_ms = 12U;
  HAL_GPIO_WritePin(CODEC_RESET_GPIO_Port, CODEC_RESET_Pin, GPIO_PIN_RESET);
  HAL_Delay(2U);
  HAL_GPIO_WritePin(CODEC_RESET_GPIO_Port, CODEC_RESET_Pin, GPIO_PIN_SET);
  HAL_Delay(10U);
#else
  g_tlv_diag.reset_pin_used = 0U;
  g_tlv_diag.reset_low_duration_ms = 0U;
  g_tlv_diag.reset_wait_ms = 2U;
#endif

  tlv320aic3204_status_t status =
      TLV320AIC3204_WriteReg(i2c, address_7bit, 0U, TLV_RESET, 0x01U);
  if (status == TLV320AIC3204_STATUS_OK)
  {
    HAL_Delay(TLV_CODEC_STARTUP_WAIT_MS);
    uint8_t reset_value = 0xFFU;
    status = TLV320AIC3204_ReadReg(i2c, address_7bit, 0U, TLV_RESET, &reset_value);
    g_tlv_diag.expected = 0x00U;
    g_tlv_diag.mask = 0x01U;
    if ((status == TLV320AIC3204_STATUS_OK) && ((reset_value & 0x01U) != 0U))
    {
      g_tlv_diag.readback_errors++;
      status = TLV320AIC3204_STATUS_VERIFY_ERROR;
    }
  }
  if (status == TLV320AIC3204_STATUS_OK)
  {
    g_tlv_diag.reset_ok = 1U;
  }
  return tlv_record_status(status);
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

  if (readback != value)
  {
    g_tlv_diag.readback_errors++;
    return tlv_record_status(TLV320AIC3204_STATUS_VERIFY_ERROR);
  }
  return TLV320AIC3204_STATUS_OK;
}

static tlv320aic3204_status_t tlv_write_extended(I2C_HandleTypeDef *i2c,
                                                 uint8_t address,
                                                 uint8_t page,
                                                 uint8_t reg,
                                                 uint8_t value)
{
  return TLV320AIC3204_WriteReg(i2c, address, page, reg, value);
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

static tlv320aic3204_status_t tlv_wait_mask(I2C_HandleTypeDef *i2c,
                                            uint8_t address,
                                            uint8_t page,
                                            uint8_t reg,
                                            uint8_t mask,
                                            uint8_t expected,
                                            uint32_t timeout_ms)
{
  const uint32_t start = HAL_GetTick();
  do
  {
    uint8_t value = 0U;
    const tlv320aic3204_status_t status =
        TLV320AIC3204_ReadReg(i2c, address, page, reg, &value);
    g_tlv_diag.expected = expected;
    g_tlv_diag.mask = mask;
    if (status != TLV320AIC3204_STATUS_OK)
    {
      return status;
    }
    if ((value & mask) == expected)
    {
      return TLV320AIC3204_STATUS_OK;
    }
    HAL_Delay(1U);
  } while ((uint32_t)(HAL_GetTick() - start) < timeout_ms);

  return tlv_record_status(TLV320AIC3204_STATUS_READY_TIMEOUT);
}

static tlv320aic3204_status_t tlv_init(const tlv320aic3204_config_t *config)
{
  memset(&g_tlv_diag, 0, sizeof(g_tlv_diag));
  if ((config == NULL) ||
      (config->i2c == NULL) ||
      (config->address_7bit == 0U) ||
      (config->mclk_hz != 12288000UL) ||
      (config->left_p_route != 0x80U) ||
      (config->left_m_route != 0x40U) ||
      (config->right_p_route != 0x80U) ||
      (config->right_m_route != 0x40U))
  {
    return tlv_record_status(TLV320AIC3204_STATUS_CONFIG_ERROR);
  }

  const uint8_t word_length = tlv_word_length_bits(config->word_bits);
  if (word_length == 0xFFU)
  {
    return tlv_record_status(TLV320AIC3204_STATUS_CONFIG_ERROR);
  }

  tlv_set_stage(TLV320AIC3204_STAGE_DEVICE_ACK);
  const uint32_t ready_start = HAL_GetTick();
  while (HAL_I2C_IsDeviceReady(config->i2c,
                               (uint16_t)(config->address_7bit << 1),
                               1U,
                               2U) != HAL_OK)
  {
    tlv_record_i2c_error();
    if ((uint32_t)(HAL_GetTick() - ready_start) >= TLV_DEVICE_READY_TIMEOUT_MS)
    {
      return tlv_record_status(TLV320AIC3204_STATUS_NOT_FOUND);
    }
    HAL_Delay(1U);
  }
  g_tlv_diag.device_ack = 1U;

  tlv320aic3204_status_t status =
      TLV320AIC3204_SoftwareReset(config->i2c, config->address_7bit);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }

  tlv_set_stage(TLV320AIC3204_STAGE_ANALOG_POWER);
  status = tlv_write_extended(config->i2c, config->address_7bit, 1U, TLV_P1_REFERENCE_POWER_UP, 0x05U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_extended(config->i2c, config->address_7bit, 1U, TLV_P1_ANALOG_INPUT_QUICK_CHARGE, 0x32U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  HAL_Delay(40U);

  tlv_set_stage(TLV320AIC3204_STAGE_CLOCK_TREE);
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
  g_tlv_diag.clocks_ok = 1U;

  tlv_set_stage(TLV320AIC3204_STAGE_AUDIO_INTERFACE);
  status = tlv_write_checked(config->i2c, config->address_7bit, 0U, TLV_AUDIO_IF_1, word_length);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_checked(config->i2c, config->address_7bit, 0U, TLV_DAC_PROCESSING_BLOCK, 0x01U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  g_tlv_diag.interface_ok = 1U;

  tlv_set_stage(TLV320AIC3204_STAGE_ANALOG_POWER);
  status = tlv_write_extended(config->i2c, config->address_7bit, 1U, TLV_P1_MICBIAS, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_extended(config->i2c, config->address_7bit, 1U, TLV_P1_FLOATING_INPUT, 0x3FU);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_extended(config->i2c, config->address_7bit, 1U, TLV_P1_LEFT_P_ROUTE, config->left_p_route);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_extended(config->i2c, config->address_7bit, 1U, TLV_P1_LEFT_M_ROUTE, config->left_m_route);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_extended(config->i2c, config->address_7bit, 1U, TLV_P1_RIGHT_P_ROUTE, config->right_p_route);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_extended(config->i2c, config->address_7bit, 1U, TLV_P1_RIGHT_M_ROUTE, config->right_m_route);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_extended(config->i2c, config->address_7bit, 1U, TLV_P1_LEFT_PGA_GAIN, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_extended(config->i2c, config->address_7bit, 1U, TLV_P1_RIGHT_PGA_GAIN, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_extended(config->i2c, config->address_7bit, 1U, TLV_P1_ADC_POWER_TUNE, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }

  status = tlv_write_checked(config->i2c, config->address_7bit, 1U, TLV_P1_POWER_CONFIG, 0x08U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_checked(config->i2c, config->address_7bit, 1U, TLV_P1_LDO_CTRL, 0x01U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_checked(config->i2c, config->address_7bit, 1U, TLV_P1_CM_CTRL, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }

  status = tlv_write_extended(config->i2c, config->address_7bit, 0U, TLV_ADC_POWER, 0xC0U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_extended(config->i2c, config->address_7bit, 0U, TLV_ADC_FINE_GAIN, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_extended(config->i2c, config->address_7bit, 0U, TLV_LEFT_AGC, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_extended(config->i2c, config->address_7bit, 0U, TLV_RIGHT_AGC, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }

  /* Keep the analog drivers muted until clocks, DACs and routes are live. */
  tlv_set_stage(TLV320AIC3204_STAGE_OUTPUT_MUTE);
  status = tlv_write_checked(config->i2c, config->address_7bit, 1U, TLV_P1_HPL_GAIN, 0x40U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_checked(config->i2c, config->address_7bit, 1U, TLV_P1_HPR_GAIN, 0x40U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  tlv_set_stage(TLV320AIC3204_STAGE_OUTPUT_ROUTE);
  status = tlv_write_checked(config->i2c, config->address_7bit, 1U, TLV_P1_HPL_ROUTE, 0x08U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_checked(config->i2c, config->address_7bit, 1U, TLV_P1_HPR_ROUTE, 0x08U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  g_tlv_diag.output_routed = 1U;
  status = tlv_write_checked(config->i2c, config->address_7bit, 1U, TLV_P1_HEADPHONE_STARTUP, 0x25U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }

  tlv_set_stage(TLV320AIC3204_STAGE_DAC_ROUTE);
  status = tlv_write_checked(config->i2c, config->address_7bit, 0U, TLV_DAC_DATAPATH, 0xD4U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  g_tlv_diag.dac_routed = 1U;
  tlv_set_stage(TLV320AIC3204_STAGE_DAC_VOLUME);
  status = tlv_write_checked(config->i2c, config->address_7bit, 0U, TLV_DAC_VOLUME_CTRL, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_checked(config->i2c, config->address_7bit, 0U, TLV_LEFT_DAC_VOL, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_checked(config->i2c, config->address_7bit, 0U, TLV_RIGHT_DAC_VOL, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  g_tlv_diag.volume_ok = 1U;

  tlv_set_stage(TLV320AIC3204_STAGE_DAC_READY);
  status = tlv_wait_mask(config->i2c,
                         config->address_7bit,
                         0U,
                         TLV_DAC_FLAG_1,
                         0x88U,
                         0x88U,
                         TLV_BLOCK_READY_TIMEOUT_MS);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  g_tlv_diag.dac_powered = 1U;

  tlv_set_stage(TLV320AIC3204_STAGE_OUTPUT_POWER);
  status = tlv_write_checked(config->i2c,
                             config->address_7bit,
                             1U,
                             TLV_P1_OUTPUT_POWER,
                             0x30U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  tlv_set_stage(TLV320AIC3204_STAGE_OUTPUT_READY);
  status = tlv_wait_mask(config->i2c,
                         config->address_7bit,
                         0U,
                         TLV_DAC_FLAG_1,
                         0xAAU,
                         0xAAU,
                         TLV_BLOCK_READY_TIMEOUT_MS);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  g_tlv_diag.output_powered = 1U;
  tlv_set_stage(TLV320AIC3204_STAGE_ADC_READY);
  status = tlv_wait_mask(config->i2c,
                         config->address_7bit,
                         0U,
                         TLV_ADC_FLAG,
                         0x44U,
                         0x44U,
                         TLV_BLOCK_READY_TIMEOUT_MS);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }

  tlv_set_stage(TLV320AIC3204_STAGE_OUTPUT_UNMUTE);
  status = tlv_write_checked(config->i2c, config->address_7bit, 1U, TLV_P1_HPL_GAIN, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  status = tlv_write_checked(config->i2c, config->address_7bit, 1U, TLV_P1_HPR_GAIN, 0x00U);
  if (status != TLV320AIC3204_STATUS_OK) { return status; }
  g_tlv_diag.output_unmuted = 1U;
  g_tlv_diag.dac_unmuted = 1U;

  tlv_set_stage(TLV320AIC3204_STAGE_COMPLETE);
  g_tlv_diag.status = TLV320AIC3204_STATUS_OK;
  return TLV320AIC3204_STATUS_OK;
}

tlv320aic3204_status_t TLV320AIC3204_Init(const tlv320aic3204_config_t *config)
{
  return tlv_init(config);
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

void TLV320AIC3204_GetDiag(tlv320aic3204_diag_t *out_diag)
{
  if (out_diag != NULL)
  {
    *out_diag = g_tlv_diag;
  }
}
