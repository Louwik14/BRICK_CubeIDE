#include "Audio/fx_delay_shared_pool.h"

#include "Storage/memory_layout.h"

#include <string.h>

AUDIO_DELAY_SDRAM static float g_delay_shared_l[FX_DELAY_SHARED_DUAL_CAPACITY];
AUDIO_DELAY_SDRAM static float g_delay_shared_r[FX_DELAY_SHARED_DUAL_CAPACITY];

static fx_delay_shared_owner_t g_delay_shared_owner = FX_DELAY_SHARED_OWNER_NONE;

float *fx_delay_shared_pool_left(void)
{
    return g_delay_shared_l;
}

float *fx_delay_shared_pool_right(void)
{
    return g_delay_shared_r;
}

uint32_t fx_delay_shared_pool_capacity(fx_delay_shared_owner_t owner)
{
    switch(owner)
    {
        case FX_DELAY_SHARED_OWNER_CLASSIC:
            return FX_DELAY_SHARED_CLASSIC_CAPACITY;
        case FX_DELAY_SHARED_OWNER_DUAL:
            return FX_DELAY_SHARED_DUAL_CAPACITY;
        case FX_DELAY_SHARED_OWNER_NONE:
        default:
            return 0U;
    }
}

fx_delay_shared_owner_t fx_delay_shared_pool_owner(void)
{
    return g_delay_shared_owner;
}

void fx_delay_shared_pool_clear(fx_delay_shared_owner_t owner)
{
    const uint32_t capacity = fx_delay_shared_pool_capacity(owner);
    if(capacity == 0U)
        return;

    memset(g_delay_shared_l, 0, sizeof(float) * capacity);
    memset(g_delay_shared_r, 0, sizeof(float) * capacity);
}

void fx_delay_shared_pool_acquire(fx_delay_shared_owner_t owner, uint8_t clear)
{
    g_delay_shared_owner = owner;
    if(clear != 0U)
    {
        fx_delay_shared_pool_clear(owner);
    }
}
