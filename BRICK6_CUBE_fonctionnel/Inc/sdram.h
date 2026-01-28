#pragma once

#include "stm32h7xx_hal.h"
#include <stdint.h>

/* Base address of SDRAM */
#define SDRAM_BANK_ADDR   ((uint32_t)0xC0000000)

/* Timeouts and test sizes */
#define SDRAM_TIMEOUT     ((uint32_t)0xFFFF)
#define SDRAM_BUFFER_SIZE ((uint32_t)0x1000)

/* SDRAM mode register definitions */
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

/* Public API */
void SDRAM_Init(void);
void SDRAM_Test(void);

/* =========================================================
 * ⚠️ Important note about 32-bit access on x16 FMC (STM32H7)
 *
 * The FMC bus is configured in x16 mode. When the CPU performs
 * 32-bit accesses, the FMC swaps the two 16-bit halfwords.
 * This means *direct* 32-bit reads/writes in SDRAM are corrupted.
 *
 * Rules of thumb:
 *  - ✅ 16-bit buffers are safe for audio/streaming.
 *  - ✅ Use sdram_write32/sdram_read32 (or an adapted memcpy) when
 *    you need 32-bit data.
 *  - ✅ If needed later, add an abstraction layer to hide this.
 *
 * Do NOT use direct 32-bit pointers on SDRAM without the swap.
 * ========================================================= */

static inline uint32_t sdram_swap16(uint32_t value)
{
    return (value >> 16) | (value << 16);
}

static inline void sdram_write32(uint32_t index, uint32_t value)
{
    volatile uint32_t *mem = (uint32_t *)SDRAM_BANK_ADDR;
    mem[index] = sdram_swap16(value);   /* ✅ swap on write */
}

static inline uint32_t sdram_read32(uint32_t index)
{
    volatile uint32_t *mem = (uint32_t *)SDRAM_BANK_ADDR;
    uint32_t raw = mem[index];
    return sdram_swap16(raw);           /* ✅ swap on read */
}
