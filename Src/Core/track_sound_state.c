#include "Core/track_sound_state.h"

#include <stddef.h>

#include "Param/param_registry.h"
#include "Storage/memory_layout.h"
#include "Seq/seq_types.h"

SEQ_STATE_D2 static track_sound_state_t g_track_sound_state[SEQ_TRACK_COUNT];

static void track_sound_state_set_defaults(track_sound_state_t *state)
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
    state->input.hybrid_gate = param_registry[PARAM_HYBRID_GATE].default_value;
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
    state->mod_lfo[0].rate = param_registry[PARAM_LFO1_RATE].default_value;
    state->mod_lfo[0].shape = param_registry[PARAM_LFO1_SHAPE].default_value;
    state->mod_lfo[0].delay = param_registry[PARAM_LFO1_DELAY].default_value;
    state->mod_lfo[0].trig = param_registry[PARAM_LFO1_TRIG].default_value;
    state->mod_lfo[0].fade = param_registry[PARAM_LFO1_FADE].default_value;
    state->mod_lfo[0].phase_slew = param_registry[PARAM_LFO1_PHASE_SLEW].default_value;
    state->mod_lfo[1].rate = param_registry[PARAM_LFO2_RATE].default_value;
    state->mod_lfo[1].shape = param_registry[PARAM_LFO2_SHAPE].default_value;
    state->mod_lfo[1].delay = param_registry[PARAM_LFO2_DELAY].default_value;
    state->mod_lfo[1].trig = param_registry[PARAM_LFO2_TRIG].default_value;
    state->mod_lfo[1].fade = param_registry[PARAM_LFO2_FADE].default_value;
    state->mod_lfo[1].phase_slew = param_registry[PARAM_LFO2_PHASE_SLEW].default_value;
    state->mod_env3.attack = 0.0f;
    state->mod_env3.decay = 32.0f;
    state->mod_env3.sustain = 127.0f;
    state->mod_env3.release = 32.0f;
    mod_matrix_set_defaults(state->mod_matrix, &state->mod_matrix_selected_slot);
}

void track_sound_state_init(void)
{
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        track_sound_state_set_defaults(&g_track_sound_state[track]);
    }
    mod_matrix_rebuild_route_cache_all();
}

track_sound_state_t *track_sound_state_get(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return NULL;
    }

    return &g_track_sound_state[track];
}

const track_sound_state_t *track_sound_state_get_const(uint8_t track)
{
    return track_sound_state_get(track);
}
