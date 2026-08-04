#ifndef TLV320AIC3204_H
#define TLV320AIC3204_H

#include "stm32h7xx_hal.h"
#include <stdint.h>

#ifndef TLV320AIC3204_I2C_ADDR_7BIT
#define TLV320AIC3204_I2C_ADDR_7BIT 0x18U
#endif

#ifndef TLV320AIC3204_MCLK_HZ
#define TLV320AIC3204_MCLK_HZ 12288000UL
#endif

#ifndef TLV320AIC3204_WORD_BITS
#define TLV320AIC3204_WORD_BITS 24U
#endif

#ifndef TLV320AIC3204_LEFT_P_ROUTE
#define TLV320AIC3204_LEFT_P_ROUTE 0x80U
#endif

#ifndef TLV320AIC3204_LEFT_M_ROUTE
#define TLV320AIC3204_LEFT_M_ROUTE 0x40U
#endif

#ifndef TLV320AIC3204_RIGHT_P_ROUTE
#define TLV320AIC3204_RIGHT_P_ROUTE 0x80U
#endif

#ifndef TLV320AIC3204_RIGHT_M_ROUTE
#define TLV320AIC3204_RIGHT_M_ROUTE 0x40U
#endif

typedef enum
{
  TLV320AIC3204_STATUS_OK = 0,
  TLV320AIC3204_STATUS_CONFIG_ERROR,
  TLV320AIC3204_STATUS_NOT_FOUND,
  TLV320AIC3204_STATUS_I2C_ERROR,
  TLV320AIC3204_STATUS_VERIFY_ERROR,
  TLV320AIC3204_STATUS_READY_TIMEOUT
} tlv320aic3204_status_t;

typedef enum
{
  TLV320AIC3204_STAGE_NONE = 0,
  TLV320AIC3204_STAGE_DEVICE_ACK,
  TLV320AIC3204_STAGE_RESET,
  TLV320AIC3204_STAGE_CLOCK_TREE,
  TLV320AIC3204_STAGE_AUDIO_INTERFACE,
  TLV320AIC3204_STAGE_ANALOG_POWER,
  TLV320AIC3204_STAGE_OUTPUT_MUTE,
  TLV320AIC3204_STAGE_OUTPUT_ROUTE,
  TLV320AIC3204_STAGE_DAC_ROUTE,
  TLV320AIC3204_STAGE_DAC_VOLUME,
  TLV320AIC3204_STAGE_DAC_READY,
  TLV320AIC3204_STAGE_OUTPUT_POWER,
  TLV320AIC3204_STAGE_OUTPUT_READY,
  TLV320AIC3204_STAGE_ADC_READY,
  TLV320AIC3204_STAGE_OUTPUT_UNMUTE,
  TLV320AIC3204_STAGE_COMPLETE
} tlv320aic3204_stage_t;

typedef struct
{
  tlv320aic3204_status_t status;
  tlv320aic3204_stage_t stage;
  uint8_t page;
  uint8_t reg;
  uint8_t expected;
  uint8_t mask;
  uint8_t actual;
  uint8_t device_ack;
  uint8_t reset_ok;
  uint8_t clocks_ok;
  uint8_t interface_ok;
  uint8_t dac_powered;
  uint8_t dac_routed;
  uint8_t dac_unmuted;
  uint8_t output_routed;
  uint8_t output_powered;
  uint8_t output_unmuted;
  uint8_t volume_ok;
  uint8_t reset_pin_used;
  uint32_t reset_low_duration_ms;
  uint32_t reset_wait_ms;
  uint32_t i2c_errors;
  uint32_t write_failures;
  uint32_t readback_errors;
} tlv320aic3204_diag_t;

typedef struct
{
  I2C_HandleTypeDef *i2c;
  uint8_t address_7bit;
  uint32_t mclk_hz;
  uint8_t word_bits;
  uint8_t left_p_route;
  uint8_t left_m_route;
  uint8_t right_p_route;
  uint8_t right_m_route;
} tlv320aic3204_config_t;

tlv320aic3204_status_t TLV320AIC3204_SelectPage(I2C_HandleTypeDef *i2c,
                                                uint8_t address_7bit,
                                                uint8_t page);
tlv320aic3204_status_t TLV320AIC3204_WriteReg(I2C_HandleTypeDef *i2c,
                                              uint8_t address_7bit,
                                              uint8_t page,
                                              uint8_t reg,
                                              uint8_t value);
tlv320aic3204_status_t TLV320AIC3204_ReadReg(I2C_HandleTypeDef *i2c,
                                             uint8_t address_7bit,
                                             uint8_t page,
                                             uint8_t reg,
                                             uint8_t *value);
/* Reads the currently selected page without writing the page selector. */
tlv320aic3204_status_t TLV320AIC3204_ReadRegCurrentPage(I2C_HandleTypeDef *i2c,
                                                        uint8_t address_7bit,
                                                        uint8_t reg,
                                                        uint8_t *value);
tlv320aic3204_status_t TLV320AIC3204_SoftwareReset(I2C_HandleTypeDef *i2c,
                                                   uint8_t address_7bit);
tlv320aic3204_status_t TLV320AIC3204_Init(const tlv320aic3204_config_t *config);
/* Used after an isolated codec reset; verifies the extended register set. */
tlv320aic3204_status_t TLV320AIC3204_InitChecked(const tlv320aic3204_config_t *config);
tlv320aic3204_status_t TLV320AIC3204_InitDefault(void);
tlv320aic3204_status_t TLV320AIC3204_InitDefaultChecked(void);
void TLV320AIC3204_GetDiag(tlv320aic3204_diag_t *out_diag);

#endif /* TLV320AIC3204_H */
