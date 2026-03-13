#include "ak4619.h"

#define AK4619_REG_COUNT      21U
#define AK4619_I2C_TIMEOUT_MS 100U

static const uint8_t ak4619_config[AK4619_REG_COUNT] =
{
    0x37, 0xAF, 0x1C, 0x00, 0x44,
    0x44, 0x30, 0x30, 0x30, 0x30,
    0x22, 0x55, 0x00, 0x06, 0x18,
    0x18, 0x18, 0x18, 0x04, 0x05,
    0x0A
};

void AK4619_Reset(AK4619_Handle *codec)
{
    if (codec == NULL)
    {
        return;
    }

    HAL_GPIO_WritePin(codec->reset_port, codec->reset_pin, GPIO_PIN_RESET);
    HAL_Delay(2);
    HAL_GPIO_WritePin(codec->reset_port, codec->reset_pin, GPIO_PIN_SET);
    HAL_Delay(10);
}

HAL_StatusTypeDef AK4619_WriteRegister(AK4619_Handle *codec, uint8_t reg, uint8_t value)
{
    if ((codec == NULL) || (codec->i2c == NULL))
    {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Write(codec->i2c,
                             (uint16_t)(codec->address << 1),
                             reg,
                             I2C_MEMADD_SIZE_8BIT,
                             &value,
                             1U,
                             AK4619_I2C_TIMEOUT_MS);
}

HAL_StatusTypeDef AK4619_ReadRegister(AK4619_Handle *codec, uint8_t reg, uint8_t *value)
{
    if ((codec == NULL) || (codec->i2c == NULL) || (value == NULL))
    {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Read(codec->i2c,
                            (uint16_t)(codec->address << 1),
                            reg,
                            I2C_MEMADD_SIZE_8BIT,
                            value,
                            1U,
                            AK4619_I2C_TIMEOUT_MS);
}

HAL_StatusTypeDef AK4619_Init(AK4619_Handle *codec)
{
    HAL_StatusTypeDef status;
    uint8_t reg;

    if (codec == NULL)
    {
        return HAL_ERROR;
    }

    AK4619_Reset(codec);

    for (reg = 0U; reg < AK4619_REG_COUNT; reg++)
    {
        status = AK4619_WriteRegister(codec, reg, ak4619_config[reg]);
        if (status != HAL_OK)
        {
            return status;
        }
        HAL_Delay(1);
    }

    return HAL_OK;
}
