#include "Core/track_tone_sound_state.h"

#include <stddef.h>

#include "Param/param_registry.h"
#include "Storage/memory_layout.h"
#include "Seq/seq_types.h"

SEQ_STATE_D2 static track_tone_sound_state_t g_track_tone_sound_state[SEQ_TRACK_COUNT];

#define TRACK_TONE_WAVE_DEFAULT_EDIT               0.0f
#define TRACK_TONE_WAVE_DEFAULT_FINE               0.5f
#define TRACK_TONE_WAVE_DEFAULT_COARSE             0.5f
#define TRACK_TONE_WAVE_DEFAULT_FM                 0.0f
#define TRACK_TONE_WAVE_DEFAULT_TIMBRE             0.5f
#define TRACK_TONE_WAVE_DEFAULT_MODULATION         0.5f
#define TRACK_TONE_WAVE_DEFAULT_COLOR              0.5f
#define TRACK_TONE_WAVE_DEFAULT_PHASE_RESET        0.0f
#define TRACK_TONE_CLIP_DEFAULT_SOURCE_BPM           120.0f
#define TRACK_TONE_CLIP_DEFAULT_SYNC_LENGTH          0.0f
#define TRACK_TONE_CLIP_DEFAULT_PITCH                0.0f
#define TRACK_TONE_CLIP_DEFAULT_PLAY_MODE            0.0f
#define TRACK_TONE_CLIP_DEFAULT_LOOP                 1.0f
#define TRACK_TONE_CLIP_DEFAULT_STRETCH_MODE         0.0f
#define TRACK_TONE_CLIP_DEFAULT_GRAIN_SIZE           4.0f
#define TRACK_TONE_CLIP_DEFAULT_HOP_SIZE             3.0f
#define TRACK_TONE_CLIP_DEFAULT_SEARCH_SIZE          4.0f

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
    state->clip.source_bpm = TRACK_TONE_CLIP_DEFAULT_SOURCE_BPM;
    state->clip.sync_length = TRACK_TONE_CLIP_DEFAULT_SYNC_LENGTH;
    state->clip.pitch = TRACK_TONE_CLIP_DEFAULT_PITCH;
    state->clip.play_mode = TRACK_TONE_CLIP_DEFAULT_PLAY_MODE;
    state->clip.loop = TRACK_TONE_CLIP_DEFAULT_LOOP;
    state->clip.stretch_mode = TRACK_TONE_CLIP_DEFAULT_STRETCH_MODE;
    state->clip.grain_size = TRACK_TONE_CLIP_DEFAULT_GRAIN_SIZE;
    state->clip.hop_size = TRACK_TONE_CLIP_DEFAULT_HOP_SIZE;
    state->clip.search_size = TRACK_TONE_CLIP_DEFAULT_SEARCH_SIZE;
    state->multi.loop = param_registry[PARAM_SAMPLER_MULTI_LOOP].default_value;
    state->looper.arm = param_registry[PARAM_LOOPER_ARM].default_value;
    state->looper.len = param_registry[PARAM_LOOPER_LEN].default_value;
    state->looper.play = param_registry[PARAM_LOOPER_PLAY].default_value;
    state->looper.xfade = param_registry[PARAM_LOOPER_XFADE].default_value;
    state->looper.stretch = param_registry[PARAM_LOOPER_STRETCH].default_value;
    state->looper.pitch = param_registry[PARAM_LOOPER_PITCH].default_value;
    state->looper.grain = param_registry[PARAM_LOOPER_GRAIN].default_value;
    for (uint8_t slot = 0U; slot < 4U; ++slot)
    {
        state->master_fx.type[slot] = param_registry[(param_id_t)(PARAM_MASTER_FX1_TYPE + (slot * 4U))].default_value;
        state->master_fx.level[slot] = param_registry[(param_id_t)(PARAM_MASTER_FX1_LEVEL + (slot * 4U))].default_value;
        state->master_fx.macro_a[slot] = param_registry[(param_id_t)(PARAM_MASTER_FX1_A + (slot * 4U))].default_value;
        state->master_fx.macro_b[slot] = param_registry[(param_id_t)(PARAM_MASTER_FX1_B + (slot * 4U))].default_value;
    }
    state->wave.edit = TRACK_TONE_WAVE_DEFAULT_EDIT;
    state->wave.fine = TRACK_TONE_WAVE_DEFAULT_FINE;
    state->wave.coarse = TRACK_TONE_WAVE_DEFAULT_COARSE;
    state->wave.fm = TRACK_TONE_WAVE_DEFAULT_FM;
    state->wave.timbre = TRACK_TONE_WAVE_DEFAULT_TIMBRE;
    state->wave.modulation = TRACK_TONE_WAVE_DEFAULT_MODULATION;
    state->wave.color = TRACK_TONE_WAVE_DEFAULT_COLOR;
    state->wave.phase_reset = TRACK_TONE_WAVE_DEFAULT_PHASE_RESET;
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
