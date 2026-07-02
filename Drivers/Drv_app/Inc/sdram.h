#pragma once

#include "stm32h7xx_hal.h"
#include <stdint.h>

/* Base address and logical size of the FMC SDRAM bank. */
#define SDRAM_BANK_ADDR         ((uint32_t)0xC0000000)
#define SDRAM_BUS_WIDTH_BITS    (32U)
#define SDRAM_SIZE_BYTES        (64U * 1024U * 1024U)
#define SDRAM_WORD_COUNT        (SDRAM_SIZE_BYTES / sizeof(uint32_t))

/* Place uninitialized, non real-time buffers in SDRAM. */
#define SDRAM_BSS __attribute__((section(".sdram")))

/* Timeouts and test sizes. */
#define SDRAM_TIMEOUT           ((uint32_t)0xFFFF)
#define SDRAM_BUFFER_SIZE       ((uint32_t)0x1000)
#define SDRAM_TEST_OK           ((int32_t)0)
#define SDRAM_TEST_FAIL         ((int32_t)-1)

/* SDRAM mode register definitions. */
#define SDRAM_MODEREG_BURST_LENGTH_1             ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_LENGTH_2             ((uint16_t)0x0001)
#define SDRAM_MODEREG_BURST_LENGTH_4             ((uint16_t)0x0002)
#define SDRAM_MODEREG_BURST_LENGTH_8             ((uint16_t)0x0004)
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL      ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_TYPE_INTERLEAVED     ((uint16_t)0x0008)
#define SDRAM_MODEREG_CAS_LATENCY_2              ((uint16_t)0x0020)
#define SDRAM_MODEREG_CAS_LATENCY_3              ((uint16_t)0x0030)
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD    ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_PROGRAMMED ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE     ((uint16_t)0x0200)

void SDRAM_Init(void);
int32_t SDRAM_Test(void);
int32_t SDRAM_Test_Quick(void);

static inline void sdram_write32(uint32_t index, uint32_t value)
{
    volatile uint32_t *mem = (uint32_t *)SDRAM_BANK_ADDR;
    mem[index] = value;
}

static inline uint32_t sdram_read32(uint32_t index)
{
    volatile uint32_t *mem = (uint32_t *)SDRAM_BANK_ADDR;
    return mem[index];
}
