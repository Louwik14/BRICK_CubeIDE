#ifndef TEST_STM32H7XX_HAL_H
#define TEST_STM32H7XX_HAL_H

#include <stdint.h>

typedef struct
{
    uint32_t unused;
} SD_HandleTypeDef;

typedef enum
{
    HAL_OK = 0,
    HAL_ERROR = 1,
    HAL_BUSY = 2,
    HAL_TIMEOUT = 3
} HAL_StatusTypeDef;

uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_SD_Abort_IT(SD_HandleTypeDef *hsd);
uint32_t test_get_ipsr(void);

#define __get_IPSR() test_get_ipsr()

#endif
