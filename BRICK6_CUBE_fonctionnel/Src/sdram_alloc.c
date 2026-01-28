#include "sdram_alloc.h"

#include <stdbool.h>

static uintptr_t sdram_alloc_cursor = 0U;
static uintptr_t sdram_alloc_end = 0U;

static bool sdram_is_power_of_two(size_t value)
{
    return (value != 0U) && ((value & (value - 1U)) == 0U);
}

static uintptr_t sdram_align_up(uintptr_t value, size_t align)
{
    return (value + (uintptr_t)(align - 1U)) & ~(uintptr_t)(align - 1U);
}

void sdram_alloc_init(void)
{
    sdram_alloc_cursor = (uintptr_t)SDRAM_ALLOC_BASE;
    sdram_alloc_end = (uintptr_t)SDRAM_ALLOC_BASE + (uintptr_t)SDRAM_ALLOC_SIZE;

    sdram_alloc_cursor = sdram_align_up(sdram_alloc_cursor, 4U);
}

void *sdram_alloc(size_t size, size_t align)
{
    if (size == 0U)
    {
        return NULL;
    }

    if (align == 0U)
    {
        align = 4U;
    }

    if (!sdram_is_power_of_two(align))
    {
        return NULL;
    }

    if (sdram_alloc_cursor == 0U)
    {
        sdram_alloc_init();
    }

    uintptr_t aligned = sdram_align_up(sdram_alloc_cursor, align);
    uintptr_t next = aligned + (uintptr_t)size;

    if ((aligned < sdram_alloc_cursor) || (next < aligned) || (next > sdram_alloc_end))
    {
        return NULL;
    }

    sdram_alloc_cursor = next;
    return (void *)aligned;
}
