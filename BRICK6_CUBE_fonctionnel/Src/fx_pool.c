#include "fx_pool.h"
#include "fx_dj_eq3_cmsis.h"
#include "fx_saturation.h"
#include "fx_granular.h"

#define FX_POOL_SIZE 3

static fx_slot_t g_slots[FX_POOL_SIZE];

static fx_dj_eq3_t g_eq;
static fx_saturation_t g_sat;
static uint8_t g_gran;

void fx_pool_init(void)
{
    g_slots[0].active = 1;
    g_slots[0].type = FX_EQ3;
    g_slots[0].state = &g_eq;

    g_slots[1].active = 1;
    g_slots[1].type = FX_SAT;
    g_slots[1].state = &g_sat;

    g_slots[2].active = 1;
    g_slots[2].type = FX_GRANULAR;
    g_slots[2].state = &g_gran;
}

fx_slot_t* fx_pool_get_slot(uint32_t index)
{
    if (index >= FX_POOL_SIZE) return 0;
    return &g_slots[index];
}
