#pragma once

#include <stddef.h>
#include <stdint.h>

#include "sdram.h"

/*
 * Simple linear SDRAM allocator for audio buffers.
 * - No free().
 * - Allocates from a fixed SDRAM region.
 * - Default alignment is 32-bit (4 bytes).
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Configurable allocator region (override in build if needed). */
#ifndef SDRAM_ALLOC_BASE
#define SDRAM_ALLOC_BASE (SDRAM_BANK_ADDR)
#endif

#ifndef SDRAM_ALLOC_SIZE
#define SDRAM_ALLOC_SIZE (32U * 1024U * 1024U)
#endif

void sdram_alloc_init(void);
void *sdram_alloc(size_t size, size_t align);

#ifdef __cplusplus
}
#endif
