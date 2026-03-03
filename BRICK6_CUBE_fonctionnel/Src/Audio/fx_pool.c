#include "fx_pool.h"

#include "fx_dj_eq3_cmsis.h"
#include "fx_granular.h"
#include "fx_saturation.h"
#include "fx_daisy_comp.h"
#include "memory_layout.h"
#include "stm32h7xx.h"

#define FX_POOL_SIZE 3u

static fx_slot_t g_slots[FX_POOL_SIZE];

static fx_dj_eq3_t g_eq;
static fx_saturation_t g_sat;
static fx_daisy_comp_t g_daisy_comp;

AUDIO_WARM ALIGN32 static float grain_buffer_l[48000];
AUDIO_WARM ALIGN32 static float grain_buffer_r[48000];
AUDIO_WARM ALIGN32 static uint8_t g_granular_state_storage[2048];

static uint8_t g_granular_in_use;

void fx_pool_init(void)
{
    for (uint32_t i = 0u; i < FX_POOL_SIZE; ++i)
    {
        g_slots[i].active = 0u;
        g_slots[i].type = FX_NONE;
        g_slots[i].state = NULL;
    }

    g_granular_in_use = 0u;
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
            if ((state_size > sizeof(g_granular_state_storage)) || (g_granular_in_use != 0u))
                return 0;

            fx_granular_state_t* state = (fx_granular_state_t*)g_granular_state_storage;

            fx_granular_init(state,
                             48000.0f,
                             grain_buffer_l,
                             grain_buffer_r,
                             48000u);

            g_granular_in_use = 1u;
            slot->state = state;
            break;
        }

        case FX_DAISY_COMP:
            slot->state = &g_daisy_comp;
            break;

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
            slot->state = NULL;
            slot->type = FX_NONE;
            g_granular_in_use = 0u;

            __DMB();
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
