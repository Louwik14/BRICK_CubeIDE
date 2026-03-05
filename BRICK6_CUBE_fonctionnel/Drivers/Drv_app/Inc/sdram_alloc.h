#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Simple linear allocator for SDRAM.
 *
 * - SDRAM_Alloc_Reset() rewinds the allocator to the base.
 * - SDRAM_Alloc() returns an aligned pointer or NULL if out of space.
 *
 * The allocator does not free individual blocks by design.
 */

#define SDRAM_ALLOC_DEFAULT_SIZE_BYTES (32U * 1024U * 1024U)

void SDRAM_Alloc_Init(uint32_t base_address, uint32_t size_bytes);
void SDRAM_Alloc_Reset(void);
void *SDRAM_Alloc(uint32_t size_bytes, uint32_t alignment);

#ifdef __cplusplus
}
#endif
