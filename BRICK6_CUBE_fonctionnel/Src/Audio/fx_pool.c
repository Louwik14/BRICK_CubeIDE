#include "fx_pool.h"

#include "audio_mem_pool.h"
#include "fx_dj_eq3_cmsis.h"
#include "fx_granular.h"
#include "fx_saturation.h"
#include "stm32h7xx.h"

#define FX_POOL_SIZE 3u

static fx_slot_t g_slots[FX_POOL_SIZE];

static fx_dj_eq3_t g_eq;
static fx_saturation_t g_sat;

static float* g_granular_buffer_l[FX_POOL_SIZE];
static float* g_granular_buffer_r[FX_POOL_SIZE];

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

    audio_mem_init();
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
            const size_t state_size = fx_granular_state_size();
            const size_t buffer_size = fx_granular_buffer_size();
            fx_granular_state_t* state =
                (fx_granular_state_t*)audio_mem_alloc_fast(state_size, 32u);
            float* buffer_l = (float*)audio_mem_alloc_fast(buffer_size, 32u);
            float* buffer_r = (float*)audio_mem_alloc_fast(buffer_size, 32u);

            if (!state || !buffer_l || !buffer_r)
            {
                audio_mem_free_fast(buffer_l);
                audio_mem_free_fast(buffer_r);
                audio_mem_free_fast(state);
                return 0;
            }

            fx_granular_init(state,
                             48000.0f,
                             buffer_l,
                             buffer_r,
                             (uint32_t)(buffer_size / sizeof(float)));

            g_granular_buffer_l[index] = buffer_l;
            g_granular_buffer_r[index] = buffer_r;
            slot->state = state;
            break;
        }

        default:
            return 0;
    }

    slot->type = (uint8_t)type;
    __DMB();
    slot->active = 1u;
    __DSB();
    return 1;
}

void fx_pool_deactivate_slot(uint32_t index)
{
    fx_slot_t* slot = NULL;

    if (index >= FX_POOL_SIZE)
        return;

    slot = &g_slots[index];

    slot->active = 0u;
    __DMB();

    switch ((fx_type_t)slot->type)
    {
        case FX_GRANULAR:
        {
            float* buffer_l = g_granular_buffer_l[index];
            float* buffer_r = g_granular_buffer_r[index];
            void* state = slot->state;

            g_granular_buffer_l[index] = NULL;
            g_granular_buffer_r[index] = NULL;
            slot->state = NULL;
            slot->type = FX_NONE;

            __DMB();

            audio_mem_free_fast(buffer_l);
            audio_mem_free_fast(buffer_r);
            audio_mem_free_fast(state);
            break;
        }

        default:
            slot->state = NULL;
            slot->type = FX_NONE;
            break;
    }

    __DSB();
}

fx_slot_t* fx_pool_get_slot(uint32_t index)
{
    if (index >= FX_POOL_SIZE)
        return 0;

    return &g_slots[index];
}
