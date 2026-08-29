#pragma once

#include <stddef.h>

#include "stm32h7xx.h"
#include "Platform/cache_maintenance.h"

/* Logical publication is identical on H743 and H747.  H743 has one cache, so
 * invalidating from an IRQ could discard the interrupted producer's dirty
 * line.  A split-core H747 image defines BRICK6_H747_DUAL_CORE and performs
 * maintenance in each core's private cache. */
static inline void intercore_cache_publish(const void *address, size_t bytes)
{
#if defined(BRICK6_H747_DUAL_CORE)
    dcache_clean_by_addr_aligned(address, bytes);
#else
    (void)address;
    (void)bytes;
#endif
    __DMB();
}

static inline void intercore_cache_consume(const void *address, size_t bytes)
{
#if defined(BRICK6_H747_DUAL_CORE)
    dcache_invalidate_by_addr_aligned(address, bytes);
#else
    (void)address;
    (void)bytes;
#endif
    __DMB();
}
