#include "fx_chain.h"
#include "fx_pool.h"
#include "fx_dj_eq3_cmsis.h"
#include "fx_saturation.h"
#include "fx_granular.h"

void fx_chain_process_track0(float* L, float* R, uint32_t frames)
{
    for (uint32_t i = 0; i < 3; i++)
    {
        fx_slot_t* s = fx_pool_get_slot(i);
        if (!s || !s->active) continue;

        switch (s->type)
        {
            case FX_EQ3:
                fx_dj_eq3_process_block((fx_dj_eq3_t*)s->state, L, R, frames);
                break;

            case FX_SAT:
                fx_saturation_process_block((fx_saturation_t*)s->state, L, R, frames);
                break;

            case FX_GRANULAR:
                fx_granular_process_block(L, R, L, R, frames);
                break;

            default:
                break;
        }
    }
}
