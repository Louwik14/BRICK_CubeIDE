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
  TLV320AIC3204_STATUS_I2C_ERROR,
  TLV320AIC3204_STATUS_VERIFY_ERROR,
  TLV320AIC3204_STATUS_READY_TIMEOUT
} tlv320aic3204_status_t;

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
tlv320aic3204_status_t TLV320AIC3204_SoftwareReset(I2C_HandleTypeDef *i2c,
                                                   uint8_t address_7bit);
tlv320aic3204_status_t TLV320AIC3204_Init(const tlv320aic3204_config_t *config);
tlv320aic3204_status_t TLV320AIC3204_InitDefault(void);

#endif /* TLV320AIC3204_H */
