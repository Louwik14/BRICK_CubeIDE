#ifndef CS42448_H
#define CS42448_H

#include <stdbool.h>
#include <stdint.h>

#define CS42448_I2C_ADDR 0x48U

bool CS42448_Init(uint8_t addr);

#endif /* CS42448_H */
