#ifndef AK4619_H
#define AK4619_H

#include "stm32h7xx_hal.h"

typedef struct
{
    I2C_HandleTypeDef *i2c;
    uint8_t address;
    GPIO_TypeDef *reset_port;
    uint16_t reset_pin;
} AK4619_Handle;

void AK4619_Reset(AK4619_Handle *codec);
HAL_StatusTypeDef AK4619_WriteRegister(AK4619_Handle *codec, uint8_t reg, uint8_t value);
HAL_StatusTypeDef AK4619_ReadRegister(AK4619_Handle *codec, uint8_t reg, uint8_t *value);
HAL_StatusTypeDef AK4619_Init(AK4619_Handle *codec);

#endif /* AK4619_H */
