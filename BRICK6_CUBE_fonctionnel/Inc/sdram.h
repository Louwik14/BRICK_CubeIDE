#pragma once

#include "stm32h7xx_hal.h"
#include <stdint.h>

/* Base address of SDRAM */
#define SDRAM_BANK_ADDR   ((uint32_t)0xC0000000)

/* =========================================================================================
 * SDRAM access notes (STM32H7 FMC, x16 SDRAM bus)
 * -----------------------------------------------------------------------------------------
 * When the FMC is configured for a 16-bit SDRAM bus, 32-bit CPU accesses are internally
 * split into two 16-bit transfers. On STM32H7, the FMC returns the halfwords swapped:
 *
 *   Write 0xAABBCCDD (32-bit) -> Read back 0xCCDDAABB (32-bit)
 *
 * This is expected behavior for 32-bit accesses on x16 SDRAM and is NOT a wiring issue.
 *
 * Software workaround used in this project:
 *   - Writes are performed "raw" (no swap) using sdram_write32().
 *   - Reads are swapped in software using sdram_read32().
 *
 * Rules for safe SDRAM use in this project (audio buffers, delay lines, samples):
 *   - For 32-bit CPU reads, always use sdram_read32() (or swap manually).
 *   - For 32-bit CPU writes, use sdram_write32() (raw write).
 *   - 16-bit accesses are naturally ordered and safe.
 *   - For bulk transfers, prefer 16-bit access, DMA, or memcpy via 16-bit/byte paths.
 *   - If you must dereference a 32-bit pointer directly, you MUST swap on read.
 *
 * The allocator in sdram_alloc.* provides a simple linear allocator for audio buffers.
 * It returns 32-bit aligned pointers by default and assumes callers respect the rules
 * above when dereferencing SDRAM memory directly.
 * ========================================================================================= */

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
 * 32-bit access helpers for x16 FMC bus (STM32H7)
 * FMC swaps halfwords => we compensate in software
 * ========================================================= */

static inline uint32_t sdram_swap16(uint32_t value)
{
    return (value >> 16) | (value << 16);
}

static inline void sdram_write32(uint32_t index, uint32_t value)
{
    volatile uint32_t *mem = (uint32_t *)SDRAM_BANK_ADDR;
    mem[index] = value; /* Raw 32-bit write (FMC swaps halfwords internally) */
}

static inline uint32_t sdram_read32(uint32_t index)
{
    volatile uint32_t *mem = (uint32_t *)SDRAM_BANK_ADDR;
    uint32_t raw = mem[index];
    return sdram_swap16(raw); /* Swap halfwords to restore logical 32-bit value */
}
