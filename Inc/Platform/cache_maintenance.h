#ifndef STORAGE_CACHE_MAINTENANCE_H
#define STORAGE_CACHE_MAINTENANCE_H

#include <stddef.h>
#include <stdint.h>

/* Cortex-M7 D-cache line size on STM32H7. */
#define DCACHE_LINE_SIZE_BYTES 32U

static inline uintptr_t dcache_align_down_uintptr(uintptr_t addr)
{
    return addr & ~((uintptr_t)DCACHE_LINE_SIZE_BYTES - 1U);
}

static inline uintptr_t dcache_align_up_uintptr(uintptr_t addr)
{
    return (addr + ((uintptr_t)DCACHE_LINE_SIZE_BYTES - 1U))
           & ~((uintptr_t)DCACHE_LINE_SIZE_BYTES - 1U);
}

static inline void dcache_clean_by_addr_aligned(const void *addr, size_t size)
{
#if (__DCACHE_PRESENT == 1U)
    if ((addr == NULL) || (size == 0U))
    {
        return;
    }

    if ((SCB->CCR & SCB_CCR_DC_Msk) == 0U)
    {
        return;
    }

    {
        const uintptr_t start = dcache_align_down_uintptr((uintptr_t)addr);
        const uintptr_t end = dcache_align_up_uintptr((uintptr_t)addr + size);
        SCB_CleanDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
    }
#else
    (void)addr;
    (void)size;
#endif
}

static inline void dcache_invalidate_by_addr_aligned(const void *addr, size_t size)
{
#if (__DCACHE_PRESENT == 1U)
    if ((addr == NULL) || (size == 0U))
    {
        return;
    }

    if ((SCB->CCR & SCB_CCR_DC_Msk) == 0U)
    {
        return;
    }

    {
        const uintptr_t start = dcache_align_down_uintptr((uintptr_t)addr);
        const uintptr_t end = dcache_align_up_uintptr((uintptr_t)addr + size);
        SCB_InvalidateDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
    }
#else
    (void)addr;
    (void)size;
#endif
}

#endif /* STORAGE_CACHE_MAINTENANCE_H */
