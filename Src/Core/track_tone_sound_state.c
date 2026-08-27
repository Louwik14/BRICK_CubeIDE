#include "Core/track_tone_sound_state.h"
#include "Core/project_control.h"

#include <stddef.h>

#include "Audio/md_model.h"
#include "Param/param_registry.h"
#include "Storage/memory_layout.h"
#include "Seq/seq_types.h"
#include "Sampler/sample_global_pool.h"

SEQ_STATE_D2 static track_tone_sound_state_t g_track_tone_sound_state[SEQ_LANE_CAPACITY];

#define TRACK_TONE_PRISM_DEFAULT_EDIT               0.0f
#define TRACK_TONE_PRISM_DEFAULT_PITCH_MOD                 0.0f
#define TRACK_TONE_PRISM_DEFAULT_TIMBRE             0.5f
#define TRACK_TONE_PRISM_DEFAULT_MODULATION         0.5f
#define TRACK_TONE_PRISM_DEFAULT_COLOR              0.5f
#define TRACK_TONE_PRISM_DEFAULT_PHASE_RESET        0.0f
#define TRACK_TONE_CLIP_DEFAULT_SOURCE_BPM           120.0f
#define TRACK_TONE_CLIP_DEFAULT_SYNC_LENGTH          0.0f
#define TRACK_TONE_CLIP_DEFAULT_PITCH                0.0f
#define TRACK_TONE_CLIP_DEFAULT_PLAY_MODE            0.0f
#define TRACK_TONE_CLIP_DEFAULT_LOOP                 1.0f
#define TRACK_TONE_CLIP_DEFAULT_STRETCH_MODE         0.0f
#define TRACK_TONE_CLIP_DEFAULT_GRAIN_SIZE           4.0f
#define TRACK_TONE_CLIP_DEFAULT_HOP_SIZE             3.0f
#define TRACK_TONE_CLIP_DEFAULT_SEARCH_SIZE          4.0f
#define TRACK_TONE_FM_DEFAULT_RATIO                 0.0f
#define TRACK_TONE_FM_DEFAULT_ALGORITHM             0.0f
#define TRACK_TONE_FM_DEFAULT_FEEDBACK              0.0f
#define TRACK_TONE_FM_DEFAULT_SYNC                  1.0f
#define TRACK_TONE_FM_DEFAULT_MACRO                 0.0f
#define TRACK_TONE_FM_DEFAULT_PLAY_VEL             1.0f
#define TRACK_TONE_FM_DEFAULT_PLAY_KEY             0.0f
#define TRACK_TONE_FM_DEFAULT_PITCH_ENV            0.0f
#define TRACK_TONE_FM_DEFAULT_PITCH_TIME           0.5f
#define TRACK_TONE_FM_DEFAULT_OPERATOR_SELECT      0.0f

static const uint8_t g_track_tone_fm_operator_levels[TRACK_TONE_FM_OPERATOR_COUNT] = {
    99U, 82U, 76U, 70U, 64U, 58U
};

void track_tone_sound_state_make_default(track_tone_sound_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    state->sample = param_registry[PARAM_SAMPLER_SAMPLE].default_value;
    state->gain = param_registry[PARAM_SAMPLER_GAIN].default_value;
    state->start = param_registry[PARAM_SAMPLER_START].default_value;
    state->length = param_registry[PARAM_SAMPLER_LENGTH].default_value;
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
    state->prism.model[0] = TRACK_TONE_PRISM_DEFAULT_EDIT;
    state->prism.pitch_mod[0] = TRACK_TONE_PRISM_DEFAULT_PITCH_MOD;
    state->prism.param1[0] = TRACK_TONE_PRISM_DEFAULT_TIMBRE;
    state->prism.amod[0] = TRACK_TONE_PRISM_DEFAULT_MODULATION;
    state->prism.param2[0] = TRACK_TONE_PRISM_DEFAULT_COLOR;
    state->prism.phase_reset = TRACK_TONE_PRISM_DEFAULT_PHASE_RESET;
    state->prism.model[1] = TRACK_TONE_PRISM_DEFAULT_EDIT;
    state->prism.pitch_mod[1] = TRACK_TONE_PRISM_DEFAULT_PITCH_MOD;
    state->prism.param1[1] = TRACK_TONE_PRISM_DEFAULT_TIMBRE;
    state->prism.amod[1] = TRACK_TONE_PRISM_DEFAULT_MODULATION;
    state->prism.param2[1] = TRACK_TONE_PRISM_DEFAULT_COLOR;
    state->prism.volume = param_registry[PARAM_PRISM_VOLUME].default_value;
    state->prism.balance = param_registry[PARAM_PRISM_BALANCE].default_value;
    state->prism.tune = param_registry[PARAM_PRISM_TUNE].default_value;
    state->prism.detune = param_registry[PARAM_PRISM_DETUNE].default_value;
    state->prism.drift = param_registry[PARAM_PRISM_DRIFT].default_value;
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
    uint16_t first_wavetable=0U;
    const uint16_t wavetable_count=project_control_list_wavetables(&first_wavetable,1U);
    const float default_table = (wavetable_count != 0U)
        ? (float)first_wavetable
        : param_registry[PARAM_WAVE_OSC1_TABLE].default_value;
    state->wave.table[0] = default_table;
    state->wave.pos[0] = param_registry[PARAM_WAVE_OSC1_POS].default_value;
    state->wave.start[0] = param_registry[PARAM_WAVE_OSC1_START].default_value;
    state->wave.len[0] = param_registry[PARAM_WAVE_OSC1_LEN].default_value;
    state->wave.table[1] = default_table;
    state->wave.pos[1] = param_registry[PARAM_WAVE_OSC2_POS].default_value;
    state->wave.start[1] = param_registry[PARAM_WAVE_OSC2_START].default_value;
    state->wave.len[1] = param_registry[PARAM_WAVE_OSC2_LEN].default_value;
    state->wave.volume = param_registry[PARAM_WAVE_VOLUME].default_value;
    state->wave.balance = param_registry[PARAM_WAVE_BALANCE].default_value;
    state->wave.tune = param_registry[PARAM_WAVE_TUNE].default_value;
    state->wave.detune = param_registry[PARAM_WAVE_DETUNE].default_value;
    state->fm.base.algorithm = (uint8_t)TRACK_TONE_FM_DEFAULT_ALGORITHM;
    state->fm.base.feedback = (uint8_t)TRACK_TONE_FM_DEFAULT_FEEDBACK;
    state->fm.base.key_sync = (uint8_t)TRACK_TONE_FM_DEFAULT_SYNC;
    state->fm.base.pitch_rates[0] = 0U;
    state->fm.base.pitch_rates[1] = 0U;
    state->fm.base.pitch_rates[2] = 0U;
    state->fm.base.pitch_rates[3] = 0U;
    state->fm.base.pitch_levels[0] = 49U;
    state->fm.base.pitch_levels[1] = 49U;
    state->fm.base.pitch_levels[2] = 49U;
    state->fm.base.pitch_levels[3] = 49U;
    state->fm.base.transpose = 24U;
    state->fm.macros.ratio = TRACK_TONE_FM_DEFAULT_RATIO;
    state->fm.macros.bright = TRACK_TONE_FM_DEFAULT_MACRO;
    state->fm.macros.body = TRACK_TONE_FM_DEFAULT_MACRO;
    state->fm.macros.detail = TRACK_TONE_FM_DEFAULT_MACRO;
    state->fm.macros.metal = TRACK_TONE_FM_DEFAULT_MACRO;
    state->fm.macros.env_attack = TRACK_TONE_FM_DEFAULT_MACRO;
    state->fm.macros.env_decay = TRACK_TONE_FM_DEFAULT_MACRO;
    state->fm.macros.env_sustain = TRACK_TONE_FM_DEFAULT_MACRO;
    state->fm.macros.env_release = TRACK_TONE_FM_DEFAULT_MACRO;
    state->fm.macros.play_vel = TRACK_TONE_FM_DEFAULT_PLAY_VEL;
    state->fm.macros.play_key = TRACK_TONE_FM_DEFAULT_PLAY_KEY;
    state->fm.macros.pitch_env = TRACK_TONE_FM_DEFAULT_PITCH_ENV;
    state->fm.macros.pitch_time = TRACK_TONE_FM_DEFAULT_PITCH_TIME;
    state->fm.operator_select = TRACK_TONE_FM_DEFAULT_OPERATOR_SELECT;
    for (uint8_t op = 0U; op < PARAM_FM_OPERATOR_COUNT; ++op)
    {
        track_tone_fm_operator_base_t *const base = &state->fm.base.operators[op];
        base->rates[0] = 99U;
        base->rates[1] = 92U;
        base->rates[2] = 80U;
        base->rates[3] = 72U;
        base->levels[0] = 99U;
        base->levels[1] = 92U;
        base->levels[2] = 80U;
        base->levels[3] = 0U;
        base->breakpoint = 39U;
        base->left_depth = 0U;
        base->right_depth = 0U;
        base->left_curve = 0U;
        base->right_curve = 3U;
        base->rate_scaling = 0U;
        base->output_level = g_track_tone_fm_operator_levels[op];
        base->mode = 0U;
        base->coarse = (uint8_t)(op + 1U);
        base->fine = 0U;
        base->detune = 0;
        base->velocity_sensitivity = 7U;
        base->enabled = 1U;
    }
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
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
    {
        track_tone_sound_state_make_default(&g_track_tone_sound_state[track]);
    }
}

track_tone_sound_state_t *track_tone_sound_state_get(uint8_t track)
{
    if (track >= SEQ_LANE_CAPACITY)
    {
        return NULL;
    }

    return &g_track_tone_sound_state[track];
}

const track_tone_sound_state_t *track_tone_sound_state_get_const(uint8_t track)
{
    return track_tone_sound_state_get(track);
}

uint8_t track_tone_sound_state_md_slot_count(uint8_t track)
{
    const track_tone_sound_state_t *const state =
        track_tone_sound_state_get_const(track);
    return (state != NULL)
        ? md_model_profile_get(md_model_validate(state->md.model))->slot_count
        : 0U;
}
