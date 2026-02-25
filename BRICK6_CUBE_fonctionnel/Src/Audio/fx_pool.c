#include "fx_pool.h"
#include "fx_dj_eq3_cmsis.h"
#include "fx_saturation.h"
#include "fx_granular.h"
#include "sdram_alloc.h"

#define FX_POOL_SIZE 3U

static fx_slot_t g_slots[FX_POOL_SIZE];

/* États légers statiques */
static fx_dj_eq3_t g_eq;
static fx_saturation_t g_sat;

/* Allocation heavy-state hors IRQ (SDRAM allocator dédié) */
void* fx_alloc(size_t size)
{
    return SDRAM_Alloc((uint32_t)size, 32U);
}

void fx_free(void* ptr)
{
    (void)ptr;
    /* Bump allocator: free individuel non supporté pour l'instant. */
}

void fx_pool_init(void)
{
    for(uint32_t i = 0U; i < FX_POOL_SIZE; i++)
    {
        g_slots[i].active = 0U;
        g_slots[i].type = FX_NONE;
        g_slots[i].state = 0;
    }
}

fx_slot_t* fx_pool_get_slot(uint32_t index)
{
    if(index >= FX_POOL_SIZE)
        return 0;
    return &g_slots[index];
}

void fx_pool_activate_slot(uint32_t slot, fx_type_t type)
{
    if(slot >= FX_POOL_SIZE)
        return;

    fx_pool_deactivate_slot(slot);

    switch(type)
    {
        case FX_EQ3:
            fx_dj_eq3_init(&g_eq, 48000.0f, 200.0f, 1000.0f, 1.0f, 6000.0f);
            g_slots[slot].state = &g_eq;
            g_slots[slot].type = FX_EQ3;
            g_slots[slot].active = 1U;
            break;

        case FX_SAT:
            fx_saturation_init(&g_sat);
            g_slots[slot].state = &g_sat;
            g_slots[slot].type = FX_SAT;
            g_slots[slot].active = 1U;
            break;

        case FX_GRANULAR:
        {
            void *mem = fx_alloc(fx_granular_state_size());
            if(mem == 0)
                return;

            fx_granular_init_state(mem, 48000.0f);
            g_slots[slot].state = mem;
            g_slots[slot].type = FX_GRANULAR;
            g_slots[slot].active = 1U;
            break;
        }

        default:
            break;
    }
}

void fx_pool_deactivate_slot(uint32_t slot)
{
    if(slot >= FX_POOL_SIZE)
        return;

    fx_slot_t *s = &g_slots[slot];

    s->active = 0U;

    if(s->type == FX_GRANULAR && s->state != 0)
        fx_free(s->state);

    s->state = 0;
    s->type = FX_NONE;
}
