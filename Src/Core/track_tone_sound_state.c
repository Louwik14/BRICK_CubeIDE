#include "Core/track_tone_sound_state.h"

#include <stddef.h>

#include "Param/param_registry.h"
#include "Storage/memory_layout.h"
#include "Seq/seq_types.h"

SEQ_STATE_D2 static track_tone_sound_state_t g_track_tone_sound_state[SEQ_TRACK_COUNT];

#define TRACK_TONE_PLAITS_DEFAULT_MODEL              0.0f
#define TRACK_TONE_PLAITS_DEFAULT_COARSE_FREQUENCY   0.5f
#define TRACK_TONE_PLAITS_DEFAULT_HARMONICS          0.5f
#define TRACK_TONE_PLAITS_DEFAULT_TIMBRE             0.5f
#define TRACK_TONE_PLAITS_DEFAULT_MORPH              0.5f
#define TRACK_TONE_PLAITS_DEFAULT_LPG_RESPONSE       0.0f
#define TRACK_TONE_PLAITS_DEFAULT_DECAY              0.5f
#define TRACK_TONE_PLAITS_DEFAULT_FREQUENCY_RANGE    0.5f
#define TRACK_TONE_BUFFER_DEFAULT_STRETCH_MODE       0.0f
#define TRACK_TONE_BUFFER_DEFAULT_SYNC_LEN           0.0f
#define TRACK_TONE_BUFFER_DEFAULT_GRAIN_SIZE         1.0f
#define TRACK_TONE_BUFFER_DEFAULT_HOP_SIZE           1.0f
#define TRACK_TONE_BUFFER_DEFAULT_SOURCE_BPM         120.0f
#define TRACK_TONE_BUFFER_DEFAULT_RATIO_Q16          65536.0f
#define TRACK_TONE_BUFFER_DEFAULT_TRANSIENT_SENS     64.0f
#define TRACK_TONE_BUFFER_DEFAULT_PRESERVE_PITCH     1.0f

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
    state->buffer.stretch_mode = TRACK_TONE_BUFFER_DEFAULT_STRETCH_MODE;
    state->buffer.sync_len = TRACK_TONE_BUFFER_DEFAULT_SYNC_LEN;
    state->buffer.grain_size = TRACK_TONE_BUFFER_DEFAULT_GRAIN_SIZE;
    state->buffer.hop_size = TRACK_TONE_BUFFER_DEFAULT_HOP_SIZE;
    state->buffer.source_bpm = TRACK_TONE_BUFFER_DEFAULT_SOURCE_BPM;
    state->buffer.ratio_q16 = TRACK_TONE_BUFFER_DEFAULT_RATIO_Q16;
    state->buffer.transient_sensitivity = TRACK_TONE_BUFFER_DEFAULT_TRANSIENT_SENS;
    state->buffer.preserve_pitch = TRACK_TONE_BUFFER_DEFAULT_PRESERVE_PITCH;
    state->plaits.model = TRACK_TONE_PLAITS_DEFAULT_MODEL;
    state->plaits.coarse_frequency = TRACK_TONE_PLAITS_DEFAULT_COARSE_FREQUENCY;
    state->plaits.harmonics = TRACK_TONE_PLAITS_DEFAULT_HARMONICS;
    state->plaits.timbre = TRACK_TONE_PLAITS_DEFAULT_TIMBRE;
    state->plaits.morph = TRACK_TONE_PLAITS_DEFAULT_MORPH;
    state->plaits.lpg_response = TRACK_TONE_PLAITS_DEFAULT_LPG_RESPONSE;
    state->plaits.decay = TRACK_TONE_PLAITS_DEFAULT_DECAY;
    state->plaits.frequency_range = TRACK_TONE_PLAITS_DEFAULT_FREQUENCY_RANGE;
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
    state->fm_snare.pitch = param_registry[PARAM_DRUM_FM_SNARE_PITCH].default_value;
    state->fm_snare.decay = param_registry[PARAM_DRUM_FM_SNARE_DECAY].default_value;
    state->fm_snare.fm_amount = param_registry[PARAM_DRUM_FM_SNARE_FM_AMOUNT].default_value;
    state->fm_snare.noise = param_registry[PARAM_DRUM_FM_SNARE_NOISE].default_value;
    state->fm_snare.hp_tone = param_registry[PARAM_DRUM_FM_SNARE_HP_TONE].default_value;
    state->fm_snare.mod_freq = param_registry[PARAM_DRUM_FM_SNARE_MOD_FREQ].default_value;
    state->fm_snare.mod_decay = param_registry[PARAM_DRUM_FM_SNARE_MOD_DECAY].default_value;
    state->fm_snare.noise_decay = param_registry[PARAM_DRUM_FM_SNARE_NOISE_DECAY].default_value;
    state->fm_tom.pitch = param_registry[PARAM_DRUM_FM_TOM_PITCH].default_value;
    state->fm_tom.decay = param_registry[PARAM_DRUM_FM_TOM_DECAY].default_value;
    state->fm_tom.pitch_sweep = param_registry[PARAM_DRUM_FM_TOM_PITCH_SWEEP].default_value;
    state->fm_tom.fm_amount = param_registry[PARAM_DRUM_FM_TOM_FM_AMOUNT].default_value;
    state->fm_tom.mod_freq = param_registry[PARAM_DRUM_FM_TOM_MOD_FREQ].default_value;
    state->fm_tom.mod_decay = param_registry[PARAM_DRUM_FM_TOM_MOD_DECAY].default_value;
    state->fm_tom.sweep_decay = param_registry[PARAM_DRUM_FM_TOM_SWEEP_DECAY].default_value;
    state->fm_tom.start_phase = param_registry[PARAM_DRUM_FM_TOM_START_PHASE].default_value;
    state->fm_rimshot.rim_pitch = param_registry[PARAM_DRUM_FM_RIMSHOT_RIM_PITCH].default_value;
    state->fm_rimshot.rim_decay = param_registry[PARAM_DRUM_FM_RIMSHOT_RIM_DECAY].default_value;
    state->fm_rimshot.body_mix = param_registry[PARAM_DRUM_FM_RIMSHOT_BODY_MIX].default_value;
    state->fm_rimshot.hp_tone = param_registry[PARAM_DRUM_FM_RIMSHOT_HP_TONE].default_value;
    state->fm_rimshot.rim_fm_amount = param_registry[PARAM_DRUM_FM_RIMSHOT_RIM_FM_AMOUNT].default_value;
    state->fm_rimshot.body_pitch = param_registry[PARAM_DRUM_FM_RIMSHOT_BODY_PITCH].default_value;
    state->fm_rimshot.body_decay = param_registry[PARAM_DRUM_FM_RIMSHOT_BODY_DECAY].default_value;
    state->fm_rimshot.body_fm_amount = param_registry[PARAM_DRUM_FM_RIMSHOT_BODY_FM_AMOUNT].default_value;
    state->fm_rimshot.mod_decay = param_registry[PARAM_DRUM_FM_RIMSHOT_MOD_DECAY].default_value;
    state->fm_clap.clap_count = param_registry[PARAM_DRUM_FM_CLAP_CLAP_COUNT].default_value;
    state->fm_clap.clap_spacing = param_registry[PARAM_DRUM_FM_CLAP_CLAP_SPACING].default_value;
    state->fm_clap.tail_decay = param_registry[PARAM_DRUM_FM_CLAP_TAIL_DECAY].default_value;
    state->fm_clap.hp_tone = param_registry[PARAM_DRUM_FM_CLAP_HP_TONE].default_value;
    state->fm_clap.feedback = param_registry[PARAM_DRUM_FM_CLAP_FEEDBACK].default_value;
    state->fm_clap.fm_amount = param_registry[PARAM_DRUM_FM_CLAP_FM_AMOUNT].default_value;
    state->fm_clap.base_freq = param_registry[PARAM_DRUM_FM_CLAP_BASE_FREQ].default_value;
    state->fm_clap.mod_freq = param_registry[PARAM_DRUM_FM_CLAP_MOD_FREQ].default_value;
    state->fm_clap.mod_decay = param_registry[PARAM_DRUM_FM_CLAP_MOD_DECAY].default_value;
    state->fm_clap.clap_decay = param_registry[PARAM_DRUM_FM_CLAP_CLAP_DECAY].default_value;
    state->fm_cowbell.pitch = param_registry[PARAM_DRUM_FM_COWBELL_PITCH].default_value;
    state->fm_cowbell.decay_short = param_registry[PARAM_DRUM_FM_COWBELL_DECAY_SHORT].default_value;
    state->fm_cowbell.decay_long = param_registry[PARAM_DRUM_FM_COWBELL_DECAY_LONG].default_value;
    state->fm_cowbell.fm_amount = param_registry[PARAM_DRUM_FM_COWBELL_FM_AMOUNT].default_value;
    state->fm_cowbell.feedback = param_registry[PARAM_DRUM_FM_COWBELL_FEEDBACK].default_value;
    state->fm_cowbell.env_mix = param_registry[PARAM_DRUM_FM_COWBELL_ENV_MIX].default_value;
    state->fm_cowbell.mod_decay = param_registry[PARAM_DRUM_FM_COWBELL_MOD_DECAY].default_value;
    state->fm_cowbell.mod_freq = param_registry[PARAM_DRUM_FM_COWBELL_MOD_FREQ].default_value;
    state->fm_cymbal.decay = param_registry[PARAM_DRUM_FM_CYMBAL_DECAY].default_value;
    state->fm_cymbal.sustain = param_registry[PARAM_DRUM_FM_CYMBAL_SUSTAIN].default_value;
    state->fm_cymbal.fm_amount = param_registry[PARAM_DRUM_FM_CYMBAL_FM_AMOUNT].default_value;
    state->fm_cymbal.hp_tone = param_registry[PARAM_DRUM_FM_CYMBAL_HP_TONE].default_value;
    state->fm_cymbal.feedback = param_registry[PARAM_DRUM_FM_CYMBAL_FEEDBACK].default_value;
    state->fm_cymbal.base_carrier = param_registry[PARAM_DRUM_FM_CYMBAL_BASE_CARRIER].default_value;
    state->fm_cymbal.base_mod = param_registry[PARAM_DRUM_FM_CYMBAL_BASE_MOD].default_value;
    state->fm_cymbal.mod_decay = param_registry[PARAM_DRUM_FM_CYMBAL_MOD_DECAY].default_value;
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
