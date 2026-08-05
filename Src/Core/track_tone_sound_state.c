#include "Core/track_tone_sound_state.h"

#include <stddef.h>

#include "Audio/md_model.h"
#include "Param/param_registry.h"
#include "Storage/memory_layout.h"
#include "Seq/seq_types.h"

SEQ_STATE_D2 static track_tone_sound_state_t g_track_tone_sound_state[SEQ_TRACK_COUNT];

#define TRACK_TONE_PRISM_DEFAULT_EDIT               0.0f
#define TRACK_TONE_PRISM_DEFAULT_FINE               0.5f
#define TRACK_TONE_PRISM_DEFAULT_COARSE             0.5f
#define TRACK_TONE_PRISM_DEFAULT_FM                 0.0f
#define TRACK_TONE_PRISM_DEFAULT_TIMBRE             0.5f
#define TRACK_TONE_PRISM_DEFAULT_MODULATION         0.5f
#define TRACK_TONE_PRISM_DEFAULT_COLOR              0.5f
#define TRACK_TONE_PRISM_DEFAULT_PHASE_RESET        0.0f
#define TRACK_TONE_PRISM_DEFAULT_OSC1_LEVEL         1.0f
#define TRACK_TONE_PRISM_DEFAULT_OSC2_LEVEL         0.0f
#define TRACK_TONE_CLIP_DEFAULT_SOURCE_BPM           120.0f
#define TRACK_TONE_CLIP_DEFAULT_SYNC_LENGTH          0.0f
#define TRACK_TONE_CLIP_DEFAULT_PITCH                0.0f
#define TRACK_TONE_CLIP_DEFAULT_PLAY_MODE            0.0f
#define TRACK_TONE_CLIP_DEFAULT_LOOP                 1.0f
#define TRACK_TONE_CLIP_DEFAULT_STRETCH_MODE         0.0f
#define TRACK_TONE_CLIP_DEFAULT_GRAIN_SIZE           4.0f
#define TRACK_TONE_CLIP_DEFAULT_HOP_SIZE             3.0f
#define TRACK_TONE_CLIP_DEFAULT_SEARCH_SIZE          4.0f

void track_tone_sound_state_make_default(track_tone_sound_state_t *state)
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
    state->slice_count = param_registry[PARAM_SAMPLER_SLICE_COUNT].default_value;
    state->loop_start = param_registry[PARAM_SAMPLER_LOOP_START].default_value;
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
    state->prism.edit[0] = TRACK_TONE_PRISM_DEFAULT_EDIT;
    state->prism.fine[0] = TRACK_TONE_PRISM_DEFAULT_FINE;
    state->prism.coarse[0] = TRACK_TONE_PRISM_DEFAULT_COARSE;
    state->prism.fm[0] = TRACK_TONE_PRISM_DEFAULT_FM;
    state->prism.timbre[0] = TRACK_TONE_PRISM_DEFAULT_TIMBRE;
    state->prism.modulation[0] = TRACK_TONE_PRISM_DEFAULT_MODULATION;
    state->prism.color[0] = TRACK_TONE_PRISM_DEFAULT_COLOR;
    state->prism.phase_reset[0] = TRACK_TONE_PRISM_DEFAULT_PHASE_RESET;
    state->prism.level[0] = TRACK_TONE_PRISM_DEFAULT_OSC1_LEVEL;
    state->prism.edit[1] = TRACK_TONE_PRISM_DEFAULT_EDIT;
    state->prism.fine[1] = TRACK_TONE_PRISM_DEFAULT_FINE;
    state->prism.coarse[1] = TRACK_TONE_PRISM_DEFAULT_COARSE;
    state->prism.fm[1] = TRACK_TONE_PRISM_DEFAULT_FM;
    state->prism.timbre[1] = TRACK_TONE_PRISM_DEFAULT_TIMBRE;
    state->prism.modulation[1] = TRACK_TONE_PRISM_DEFAULT_MODULATION;
    state->prism.color[1] = TRACK_TONE_PRISM_DEFAULT_COLOR;
    state->prism.phase_reset[1] = TRACK_TONE_PRISM_DEFAULT_PHASE_RESET;
    state->prism.level[1] = TRACK_TONE_PRISM_DEFAULT_OSC2_LEVEL;
    state->stack.level[0] = param_registry[PARAM_STACK_OSC1_LEVEL].default_value;
    state->stack.level[1] = param_registry[PARAM_STACK_OSC2_LEVEL].default_value;
    state->stack.level[2] = param_registry[PARAM_STACK_OSC3_LEVEL].default_value;
    state->stack.model[0] = param_registry[PARAM_STACK_OSC1_MODEL].default_value;
    state->stack.model[1] = param_registry[PARAM_STACK_OSC2_MODEL].default_value;
    state->stack.model[2] = param_registry[PARAM_STACK_OSC3_MODEL].default_value;
    state->stack.tune[0] = param_registry[PARAM_STACK_OSC1_TUNE].default_value;
    state->stack.tune[1] = param_registry[PARAM_STACK_OSC2_TUNE].default_value;
    state->stack.tune[2] = param_registry[PARAM_STACK_OSC3_TUNE].default_value;
    state->stack.timbre[0] = param_registry[PARAM_STACK_OSC1_TIMBRE].default_value;
    state->stack.timbre[1] = param_registry[PARAM_STACK_OSC2_TIMBRE].default_value;
    state->stack.timbre[2] = param_registry[PARAM_STACK_OSC3_TIMBRE].default_value;
    state->stack.color[0] = param_registry[PARAM_STACK_OSC1_COLOR].default_value;
    state->stack.color[1] = param_registry[PARAM_STACK_OSC2_COLOR].default_value;
    state->stack.color[2] = param_registry[PARAM_STACK_OSC3_COLOR].default_value;
    state->stack.noise_level = param_registry[PARAM_STACK_NOISE_LEVEL].default_value;
    state->stack.osc_detune = param_registry[PARAM_STACK_OSC_DETUNE].default_value;
    state->stack.phase_reset = param_registry[PARAM_STACK_PHASE_RESET].default_value;
    state->wave.table[0] = param_registry[PARAM_WAVE_OSC1_TABLE].default_value;
    state->wave.pos[0] = param_registry[PARAM_WAVE_OSC1_POS].default_value;
    state->wave.start[0] = param_registry[PARAM_WAVE_OSC1_START].default_value;
    state->wave.end[0] = param_registry[PARAM_WAVE_OSC1_END].default_value;
    state->wave.level[0] = param_registry[PARAM_WAVE_OSC1_LEVEL].default_value;
    state->wave.tune[0] = param_registry[PARAM_WAVE_OSC1_TUNE].default_value;
    state->wave.phase[0] = param_registry[PARAM_WAVE_OSC1_PHASE].default_value;
    state->wave.flip[0] = param_registry[PARAM_WAVE_OSC1_FLIP].default_value;
    state->wave.table[1] = param_registry[PARAM_WAVE_OSC2_TABLE].default_value;
    state->wave.pos[1] = param_registry[PARAM_WAVE_OSC2_POS].default_value;
    state->wave.start[1] = param_registry[PARAM_WAVE_OSC2_START].default_value;
    state->wave.end[1] = param_registry[PARAM_WAVE_OSC2_END].default_value;
    state->wave.level[1] = param_registry[PARAM_WAVE_OSC2_LEVEL].default_value;
    state->wave.tune[1] = param_registry[PARAM_WAVE_OSC2_TUNE].default_value;
    state->wave.phase[1] = param_registry[PARAM_WAVE_OSC2_PHASE].default_value;
    state->wave.flip[1] = param_registry[PARAM_WAVE_OSC2_FLIP].default_value;
    state->wave.frame_interp = param_registry[PARAM_WAVE_FRAME_INTERP].default_value;
    state->wave.sample_interp = param_registry[PARAM_WAVE_SAMPLE_INTERP].default_value;
    state->wave.pos_update = param_registry[PARAM_WAVE_POS_UPDATE].default_value;
    state->wave.pos_smooth = param_registry[PARAM_WAVE_POS_SMOOTH].default_value;
    state->deluge.model = param_registry[PARAM_DELUGE_MODEL].default_value;
    state->deluge.level = param_registry[PARAM_DELUGE_LEVEL].default_value;
    state->deluge.tune = param_registry[PARAM_DELUGE_TUNE].default_value;
    state->deluge.fine = param_registry[PARAM_DELUGE_FINE].default_value;
    state->deluge.width = param_registry[PARAM_DELUGE_WIDTH].default_value;
    state->deluge.phase = param_registry[PARAM_DELUGE_PHASE].default_value;
    state->deluge.retrig = param_registry[PARAM_DELUGE_RETRIG].default_value;
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
    state->md.model = md_model_validate(param_registry[PARAM_DRUM_MD_MODEL].default_value);
    {
        const md_model_profile_t *const profile = md_model_profile_get((uint8_t)state->md.model);
        for (uint8_t slot = 0U; slot < 8U; ++slot)
        {
            state->md.slot[slot] = profile->defaults[slot];
        }
    }
}

void track_tone_sound_state_init(void)
{
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        track_tone_sound_state_make_default(&g_track_tone_sound_state[track]);
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
