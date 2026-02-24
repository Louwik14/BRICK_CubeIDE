#include "control_router.h"
#include "param_store.h"
#include "fx_granular.h"

// accès direct temporaire (compat mode)
extern void audio_float_set_saturation_mix_ui(uint8_t);
extern void audio_float_set_saturation_drive_ui(uint8_t);

void control_router_set_param(control_param_id_t id, float v)
{
    // 1. write staging (future architecture)
    param_store_set_staging((param_id_t)id, v);

    // 2. compat mode (comportement actuel inchangé)
    switch (id)
    {
        case CTRL_PARAM_GRAN_DENSITY:
            fx_granular_set_density(v);
            break;

        case CTRL_PARAM_GRAN_PITCH:
            fx_granular_set_pitch(v);
            break;

        case CTRL_PARAM_GRAN_MIX:
            fx_granular_set_mix(v);
            break;

        case CTRL_PARAM_GRAN_FREEZE:
            fx_granular_set_freeze(v > 0.5f);
            break;

        case CTRL_PARAM_GRAN_SPREAD:
            fx_granular_set_spread(v);
            break;

        case CTRL_PARAM_GRAN_STEREO:
            fx_granular_set_stereo_offset(v);
            break;

        default:
            break;
    }

    // 3. tentative commit (bornée par bloc)
    (void)param_store_commit_if_block_advanced();
}
