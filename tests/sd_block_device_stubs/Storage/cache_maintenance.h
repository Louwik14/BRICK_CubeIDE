#ifndef TEST_CACHE_MAINTENANCE_H
#define TEST_CACHE_MAINTENANCE_H

#include <stddef.h>

#define DCACHE_LINE_SIZE_BYTES 32U

void test_cache_clean(const void *addr, size_t size);
void test_cache_invalidate(const void *addr, size_t size);

static inline void dcache_clean_by_addr_aligned(const void *addr, size_t size)
{
    test_cache_clean(addr, size);
}

static inline void dcache_invalidate_by_addr_aligned(const void *addr, size_t size)
{
    test_cache_invalidate(addr, size);
}

#endif
