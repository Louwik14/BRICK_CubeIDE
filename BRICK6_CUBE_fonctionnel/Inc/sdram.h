#pragma once

#include "stm32h7xx_hal.h"

#define SDRAM_BANK_ADDR                 ((uint32_t)0xC0000000)
#define SDRAM_TIMEOUT                   ((uint32_t)0xFFFF)

#define SDRAM_BUFFER_SIZE               ((uint32_t)0x1000)
#define SDRAM_WRITE_READ_ADDR           ((uint32_t)0x0800)

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
void SDRAM_Test(void);

static inline uint32_t sdram_swap16(uint32_t value)
{
  return (value << 16) | (value >> 16);
}

static inline void sdram_write32(uint32_t index, uint32_t value)
{
  *(__IO uint32_t *)(SDRAM_BANK_ADDR + SDRAM_WRITE_READ_ADDR + 4U * index) = value;
}

static inline uint32_t sdram_read32(uint32_t index)
{
  uint32_t raw = *(__IO uint32_t *)(SDRAM_BANK_ADDR + SDRAM_WRITE_READ_ADDR + 4U * index);
  return sdram_swap16(raw);
}
