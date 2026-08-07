#include "Core/track_sound_state.h"

#include <stddef.h>

#include "Param/param_registry.h"
#include "Storage/memory_layout.h"
#include "Seq/seq_types.h"

SEQ_STATE_D2 static track_sound_state_t g_track_sound_state[SEQ_LANE_CAPACITY];

static const param_id_t g_track_sound_lfo_default_params[MOD_LFO_COUNT_PER_TRACK][MOD_LFO_PARAM_COUNT] = {
    { PARAM_LFO1_RATE, PARAM_LFO1_SHAPE, PARAM_LFO1_TRIG, PARAM_LFO1_PHASE },
    { PARAM_LFO2_RATE, PARAM_LFO2_SHAPE, PARAM_LFO2_TRIG, PARAM_LFO2_PHASE },
    { PARAM_LFO3_RATE, PARAM_LFO3_SHAPE, PARAM_LFO3_TRIG, PARAM_LFO3_PHASE },
};

void track_sound_state_make_default(track_sound_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    state->mix_level = param_registry[PARAM_MIX_LEVEL].default_value;
    state->mix_pan = param_registry[PARAM_MIX_PAN].default_value;
    state->mix_send1 = param_registry[PARAM_MIX_SEND1].default_value;
    state->mix_send2 = param_registry[PARAM_MIX_SEND2].default_value;
    state->mix_mute = param_registry[PARAM_MIX_MUTE].default_value;
    state->type = param_registry[PARAM_FILTER_TYPE].default_value;
    state->cutoff = param_registry[PARAM_FILTER_CUTOFF].default_value;
    state->resonance = param_registry[PARAM_FILTER_RESONANCE].default_value;
    state->eg_amount = param_registry[PARAM_FILTER_EG_AMT].default_value;
    state->attack = param_registry[PARAM_FILTER_ATTACK].default_value;
    state->decay = param_registry[PARAM_FILTER_DECAY].default_value;
    state->sustain = param_registry[PARAM_FILTER_SUSTAIN].default_value;
    state->release = param_registry[PARAM_FILTER_RELEASE].default_value;
    state->keytrack = param_registry[PARAM_FILTER_KEYTRK].default_value;
    state->env_reset = param_registry[PARAM_FILTER_ENVRST].default_value;
    state->env_delay = param_registry[PARAM_FILTER_ENVDLY].default_value;
    state->eq_low = param_registry[PARAM_FILTER_EQ_LOW].default_value;
    state->eq_mid = param_registry[PARAM_FILTER_EQ_MID].default_value;
    state->eq_high = param_registry[PARAM_FILTER_EQ_HIGH].default_value;
    state->vca_attack = param_registry[PARAM_VCA_ATTACK].default_value;
    state->vca_decay = param_registry[PARAM_VCA_DECAY].default_value;
    state->vca_sustain = param_registry[PARAM_VCA_SUSTAIN].default_value;
    state->vca_release = param_registry[PARAM_VCA_RELEASE].default_value;
    state->vca_env_type = param_registry[PARAM_VCA_ENV_TYPE].default_value;
    state->env_retrig_filter = param_registry[PARAM_ENV_RETRIG_FILTER].default_value;
    state->env_retrig_vca = param_registry[PARAM_ENV_RETRIG_VCA].default_value;
    state->env_retrig_mod = param_registry[PARAM_ENV_RETRIG_MOD].default_value;
    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
    {
        state->mod_lfo[lfo].rate = param_registry[g_track_sound_lfo_default_params[lfo][MOD_LFO_PARAM_RATE]].default_value;
        state->mod_lfo[lfo].shape = param_registry[g_track_sound_lfo_default_params[lfo][MOD_LFO_PARAM_SHAPE]].default_value;
        state->mod_lfo[lfo].trig = param_registry[g_track_sound_lfo_default_params[lfo][MOD_LFO_PARAM_TRIG]].default_value;
        state->mod_lfo[lfo].phase = param_registry[g_track_sound_lfo_default_params[lfo][MOD_LFO_PARAM_PHASE]].default_value;
    }
    state->mod_multi[0].source_a = (uint8_t)param_registry[PARAM_MOD_MULTI_1_A].default_value;
    state->mod_multi[0].source_b = (uint8_t)param_registry[PARAM_MOD_MULTI_1_B].default_value;
    state->mod_multi[1].source_a = (uint8_t)param_registry[PARAM_MOD_MULTI_2_A].default_value;
    state->mod_multi[1].source_b = (uint8_t)param_registry[PARAM_MOD_MULTI_2_B].default_value;
    state->mod_slew[0].source = (uint8_t)param_registry[PARAM_MOD_SLEW_1_SOURCE].default_value;
    state->mod_slew[0].amount = param_registry[PARAM_MOD_SLEW_1_AMOUNT].default_value;
    state->mod_slew[1].source = (uint8_t)param_registry[PARAM_MOD_SLEW_2_SOURCE].default_value;
    state->mod_slew[1].amount = param_registry[PARAM_MOD_SLEW_2_AMOUNT].default_value;
    state->mod_env3.attack = 0.0f;
    state->mod_env3.decay = 32.0f;
    state->mod_env3.sustain = 127.0f;
    state->mod_env3.release = 32.0f;
    mod_matrix_set_defaults(state->mod_matrix, &state->mod_matrix_selected_slot);
}

void track_sound_state_init(void)
{
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
    {
        track_sound_state_make_default(&g_track_sound_state[track]);
    }
    mod_matrix_rebuild_route_cache_all();
}

track_sound_state_t *track_sound_state_get(uint8_t track)
{
    if (track >= SEQ_LANE_CAPACITY)
    {
        return NULL;
    }

    return &g_track_sound_state[track];
}

const track_sound_state_t *track_sound_state_get_const(uint8_t track)
{
    return track_sound_state_get(track);
}
