#include "fx_pool.h"

#include <stdlib.h>

#include "fx_dj_eq3_cmsis.h"
#include "fx_granular.h"
#include "fx_saturation.h"

#define FX_POOL_SIZE 3u

static fx_slot_t g_slots[FX_POOL_SIZE];

static fx_dj_eq3_t g_eq;
static fx_saturation_t g_sat;

static float* g_granular_buffer_l[FX_POOL_SIZE];
static float* g_granular_buffer_r[FX_POOL_SIZE];

void* fx_alloc(size_t size)
{
    return malloc(size);
}

void fx_free(void* ptr)
{
    if (ptr)
        free(ptr);
}

void fx_pool_init(void)
{
    for (uint32_t i = 0u; i < FX_POOL_SIZE; ++i)
    {
        g_slots[i].active = 0u;
        g_slots[i].type = FX_NONE;
        g_slots[i].state = NULL;
        g_granular_buffer_l[i] = NULL;
        g_granular_buffer_r[i] = NULL;
    }
}

int fx_pool_activate_slot(uint32_t index, fx_type_t type)
{
    fx_slot_t* slot = NULL;

    if (index >= FX_POOL_SIZE)
        return 0;

    slot = &g_slots[index];
    fx_pool_deactivate_slot(index);

    switch (type)
    {
        case FX_EQ3:
            slot->state = &g_eq;
            break;

        case FX_SAT:
            slot->state = &g_sat;
            break;

        case FX_GRANULAR:
        {
            const size_t buffer_size = fx_granular_buffer_size();
            fx_granular_state_t* state = (fx_granular_state_t*)fx_alloc(fx_granular_state_size());
            float* buffer_l = (float*)fx_alloc(buffer_size);
            float* buffer_r = (float*)fx_alloc(buffer_size);

            if (!state || !buffer_l || !buffer_r)
            {
                fx_free(buffer_l);
                fx_free(buffer_r);
                fx_free(state);
                return 0;
            }

            fx_granular_init(state,
                             48000.0f,
                             buffer_l,
                             buffer_r,
                             (uint32_t)(buffer_size / sizeof(float)));

            slot->state = state;
            g_granular_buffer_l[index] = buffer_l;
            g_granular_buffer_r[index] = buffer_r;
            break;
        }

        default:
            return 0;
    }

    slot->type = (uint8_t)type;
    slot->active = 1u;
    return 1;
}

void fx_pool_deactivate_slot(uint32_t index)
{
    fx_slot_t* slot = NULL;

    if (index >= FX_POOL_SIZE)
        return;

    slot = &g_slots[index];

    switch ((fx_type_t)slot->type)
    {
        case FX_GRANULAR:
            fx_free(g_granular_buffer_l[index]);
            fx_free(g_granular_buffer_r[index]);
            fx_free(slot->state);
            g_granular_buffer_l[index] = NULL;
            g_granular_buffer_r[index] = NULL;
            break;

        default:
            break;
    }

    slot->state = NULL;
    slot->type = FX_NONE;
    slot->active = 0u;
}

fx_slot_t* fx_pool_get_slot(uint32_t index)
{
    if (index >= FX_POOL_SIZE)
        return 0;

    return &g_slots[index];
}
