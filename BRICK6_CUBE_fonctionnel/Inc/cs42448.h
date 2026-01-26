#ifndef CS42448_H
#define CS42448_H

#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CS42448_I2C_ADDR 0x48U

HAL_StatusTypeDef CS42448_Init(I2C_HandleTypeDef *hi2c, uint8_t i2c_addr);

#ifdef __cplusplus
}
#endif

#endif /* CS42448_H */
