#ifndef SIM_STM32H7XX_HAL_H
#define SIM_STM32H7XX_HAL_H

#include <stdint.h>

typedef struct { uint32_t unused; } SPI_HandleTypeDef;
typedef struct { uint32_t unused; } HAL_SD_CardInfoTypeDef;

uint32_t HAL_GetTick(void);

#endif
