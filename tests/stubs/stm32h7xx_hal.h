#ifndef STM32H7XX_HAL_H
#define STM32H7XX_HAL_H

#include <stdint.h>

typedef struct
{
    uint32_t LogBlockNbr;
    uint32_t LogBlockSize;
} HAL_SD_CardInfoTypeDef;

uint32_t HAL_GetTick(void);

static inline uint32_t __get_PRIMASK(void) { return 0U; }
static inline void __disable_irq(void) {}
static inline void __enable_irq(void) {}
static inline void __set_PRIMASK(uint32_t value) { (void)value; }
static inline void __DMB(void) {}

#endif
