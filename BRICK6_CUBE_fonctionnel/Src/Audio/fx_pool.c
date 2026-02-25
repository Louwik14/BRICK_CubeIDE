#include "fx_pool.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "fx_dj_eq3_cmsis.h"
#include "fx_granular.h"
#include "fx_saturation.h"

#define FX_POOL_SIZE 3u
#define FX_FAST_POOL_SIZE (448u * 1024u)
#define FX_SLOW_POOL_SIZE (1024u * 1024u)
#define FX_ALLOC_ALIGN 32u

static fx_slot_t g_slots[FX_POOL_SIZE];

static fx_dj_eq3_t g_eq;
static fx_saturation_t g_sat;

AUDIO_WARM static uint8_t g_fast_pool[FX_FAST_POOL_SIZE] __attribute__((aligned(FX_ALLOC_ALIGN)));
AUDIO_COLD_SDRAM static uint8_t g_slow_pool[FX_SLOW_POOL_SIZE] __attribute__((aligned(FX_ALLOC_ALIGN)));

static size_t g_fast_offset = 0u;
static size_t g_slow_offset = 0u;

static size_t fx_align_up(size_t value, size_t align)
{
    return (value + (align - 1u)) & ~(align - 1u);
}

static void fx_pool_reset_allocators(void)
{
    g_fast_offset = 0u;
    g_slow_offset = 0u;
    (void)memset(g_fast_pool, 0, sizeof(g_fast_pool));
    (void)memset(g_slow_pool, 0, sizeof(g_slow_pool));
}

void* fx_alloc_fast(size_t size)
{
    const size_t aligned = fx_align_up(size, FX_ALLOC_ALIGN);
    const size_t offset = fx_align_up(g_fast_offset, FX_ALLOC_ALIGN);

    if ((offset + aligned) > sizeof(g_fast_pool))
        return NULL;

    g_fast_offset = offset + aligned;
    return &g_fast_pool[offset];
}

void* fx_alloc_slow(size_t size)
{
    const size_t aligned = fx_align_up(size, FX_ALLOC_ALIGN);
    const size_t offset = fx_align_up(g_slow_offset, FX_ALLOC_ALIGN);

    if ((offset + aligned) > sizeof(g_slow_pool))
        return NULL;

    g_slow_offset = offset + aligned;
    return &g_slow_pool[offset];
}

void fx_pool_init(void)
{
    fx_pool_reset_allocators();

    for (uint32_t i = 0u; i < FX_POOL_SIZE; ++i)
    {
        g_slots[i].active = 0u;
        g_slots[i].type = FX_NONE;
        g_slots[i].state = NULL;
    }
}

int fx_pool_activate_slot(uint32_t index, fx_type_t type)
{
    void* mem = NULL;

    if (index >= FX_POOL_SIZE)
        return 0;

    switch (type)
    {
        case FX_EQ3:
            mem = &g_eq;
            break;

        case FX_SAT:
            mem = &g_sat;
            break;

        case FX_GRANULAR:
        {
            fx_granular_state_t* state = (fx_granular_state_t*)fx_alloc_fast(fx_granular_state_size());
            float* buffer_l = (float*)fx_alloc_fast(fx_granular_buffer_size());
            float* buffer_r = (float*)fx_alloc_fast(fx_granular_buffer_size());

            if (!state || !buffer_l || !buffer_r)
                return 0;

            fx_granular_init(state, 48000.0f, buffer_l, buffer_r,
                             (uint32_t)(fx_granular_buffer_size() / sizeof(float)));
            mem = state;
            break;
        }

        default:
            return 0;
    }

    g_slots[index].active = 1u;
    g_slots[index].type = (uint8_t)type;
    g_slots[index].state = mem;
    return 1;
}

void fx_pool_deactivate_slot(uint32_t index)
{
    if (index >= FX_POOL_SIZE)
        return;

    g_slots[index].active = 0u;
    g_slots[index].type = FX_NONE;
    g_slots[index].state = NULL;
}

fx_slot_t* fx_pool_get_slot(uint32_t index)
{
    if (index >= FX_POOL_SIZE)
        return 0;

    return &g_slots[index];
}
