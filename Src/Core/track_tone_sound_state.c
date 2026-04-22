#include "Core/track_tone_sound_state.h"

#include <stddef.h>

#include "Param/param_registry.h"
#include "Storage/memory_layout.h"
#include "Seq/seq_types.h"

SEQ_STATE_D2 static track_tone_sound_state_t g_track_tone_sound_state[SEQ_TRACK_COUNT];

static void track_tone_sound_state_set_defaults(track_tone_sound_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    state->sample = param_registry[PARAM_SAMPLER_SAMPLE].default_value;
    state->gain = param_registry[PARAM_SAMPLER_GAIN].default_value;
    state->start = param_registry[PARAM_SAMPLER_START].default_value;
    state->end = param_registry[PARAM_SAMPLER_END].default_value;
    state->mode = param_registry[PARAM_SAMPLER_MODE].default_value;
    state->tune = param_registry[PARAM_SAMPLER_TUNE].default_value;
    state->fade_in = param_registry[PARAM_SAMPLER_FADE_IN].default_value;
    state->fade_out = param_registry[PARAM_SAMPLER_FADE_OUT].default_value;
    state->slice_count = param_registry[PARAM_SAMPLER_SLICE_COUNT].default_value;
    state->midi_program = param_registry[PARAM_MIDI_PROGRAM].default_value;
    state->midi_cc[0] = param_registry[PARAM_MIDI_CC1_1].default_value;
    state->midi_cc[1] = param_registry[PARAM_MIDI_CC1_2].default_value;
    state->midi_cc[2] = param_registry[PARAM_MIDI_CC1_3].default_value;
    state->midi_cc[3] = param_registry[PARAM_MIDI_CC1_4].default_value;
    state->midi_cc[4] = param_registry[PARAM_MIDI_CC2_1].default_value;
    state->midi_cc[5] = param_registry[PARAM_MIDI_CC2_2].default_value;
    state->midi_cc[6] = param_registry[PARAM_MIDI_CC2_3].default_value;
    state->midi_cc[7] = param_registry[PARAM_MIDI_CC2_4].default_value;
    state->midi_cc[8] = param_registry[PARAM_MIDI_CC3_1].default_value;
    state->midi_cc[9] = param_registry[PARAM_MIDI_CC3_2].default_value;
    state->midi_cc[10] = param_registry[PARAM_MIDI_CC3_3].default_value;
    state->midi_cc[11] = param_registry[PARAM_MIDI_CC3_4].default_value;
    state->trx_bd.pitch = param_registry[PARAM_DRUM_TRX_BD_PITCH].default_value;
    state->trx_bd.decay = param_registry[PARAM_DRUM_TRX_BD_DECAY].default_value;
    state->trx_bd.pitch_sweep = param_registry[PARAM_DRUM_TRX_BD_PITCH_SWEEP].default_value;
    state->trx_bd.sweep_decay = param_registry[PARAM_DRUM_TRX_BD_SWEEP_DECAY].default_value;
    state->trx_bd.attack = param_registry[PARAM_DRUM_TRX_BD_ATTACK].default_value;
    state->trx_bd.noise = param_registry[PARAM_DRUM_TRX_BD_NOISE].default_value;
    state->trx_bd.harmonics = param_registry[PARAM_DRUM_TRX_BD_HARMONICS].default_value;
    state->trx_bd.drive = param_registry[PARAM_DRUM_TRX_BD_DRIVE].default_value;
    state->trx_claves.pitch = param_registry[PARAM_DRUM_TRX_CLAVES_PITCH].default_value;
    state->trx_claves.interval = param_registry[PARAM_DRUM_TRX_CLAVES_INTERVAL].default_value;
    state->trx_claves.decay = param_registry[PARAM_DRUM_TRX_CLAVES_DECAY].default_value;
    state->trx_claves.balance = param_registry[PARAM_DRUM_TRX_CLAVES_BALANCE].default_value;
    state->trx_claves.drive = param_registry[PARAM_DRUM_TRX_CLAVES_DRIVE].default_value;
    state->trx_hihat.decay = param_registry[PARAM_DRUM_TRX_HIHAT_DECAY].default_value;
    state->trx_hihat.metal = param_registry[PARAM_DRUM_TRX_HIHAT_METAL].default_value;
    state->trx_hihat.hp_tone = param_registry[PARAM_DRUM_TRX_HIHAT_HP_TONE].default_value;
    state->trx_hihat.lp_tone = param_registry[PARAM_DRUM_TRX_HIHAT_LP_TONE].default_value;
    state->trx_hihat.gap = param_registry[PARAM_DRUM_TRX_HIHAT_GAP].default_value;
    state->trx_hihat.peak = param_registry[PARAM_DRUM_TRX_HIHAT_PEAK].default_value;
    state->fm_kick.pitch = param_registry[PARAM_DRUM_FM_KICK_PITCH].default_value;
    state->fm_kick.decay = param_registry[PARAM_DRUM_FM_KICK_DECAY].default_value;
    state->fm_kick.fm_amount = param_registry[PARAM_DRUM_FM_KICK_FM_AMOUNT].default_value;
    state->fm_kick.pitch_sweep = param_registry[PARAM_DRUM_FM_KICK_PITCH_SWEEP].default_value;
    state->fm_kick.feedback = param_registry[PARAM_DRUM_FM_KICK_FEEDBACK].default_value;
    state->fm_kick.mod_freq = param_registry[PARAM_DRUM_FM_KICK_MOD_FREQ].default_value;
    state->fm_kick.mod_decay = param_registry[PARAM_DRUM_FM_KICK_MOD_DECAY].default_value;
    state->fm_kick.sweep_decay = param_registry[PARAM_DRUM_FM_KICK_SWEEP_DECAY].default_value;
    state->fm_kick.ratio_mode = param_registry[PARAM_DRUM_FM_KICK_RATIO_MODE].default_value;
    state->fm_kick.ratio_index = param_registry[PARAM_DRUM_FM_KICK_RATIO_INDEX].default_value;
    state->fm_kick.mod_env_sync = param_registry[PARAM_DRUM_FM_KICK_MOD_ENV_SYNC].default_value;
}

void track_tone_sound_state_init(void)
{
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        track_tone_sound_state_set_defaults(&g_track_tone_sound_state[track]);
    }
}

track_tone_sound_state_t *track_tone_sound_state_get(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return NULL;
    }

    return &g_track_tone_sound_state[track];
}

const track_tone_sound_state_t *track_tone_sound_state_get_const(uint8_t track)
{
    return track_tone_sound_state_get(track);
}
