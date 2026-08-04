#ifndef CS42448_H
#define CS42448_H

#include <stdint.h>

#define CS42448_I2C_ADDR 0x48U

typedef enum
{
    CS42448_STATUS_OK = 0,
    CS42448_STATUS_NOT_FOUND,
    CS42448_STATUS_I2C,
    CS42448_STATUS_VERIFY,
    CS42448_STATUS_CLOCK
} cs42448_status_t;

cs42448_status_t CS42448_Init(uint8_t addr);
cs42448_status_t CS42448_ReadReg(uint8_t addr, uint8_t reg, uint8_t *value);

#endif /* CS42448_H */
