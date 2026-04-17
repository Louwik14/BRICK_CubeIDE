/**
 * @file param_registry.c
 * @brief Module applicatif param_registry.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à param_registry.
 * - Fournir les services internes utilisés par le firmware utilisateur.
 *
 * Architecture:
 * - Appelé par: modules applicatifs selon l'orchestration du firmware.
 * - Appelle: dépendances matérielles et/ou modules utilisateur associés.
 *
 * Contraintes temps réel:
 * - IRQ: selon les API appelées.
 * - Hard realtime: selon le chemin d'exécution.
 * - malloc: éviter en chemin critique.
 *
 * Notes:
 * - Documentation ajoutée sans modification de la logique d'exécution.
 */

#include "param_registry.h"

#include "audio_float.h"
#include "Audio/microdexed_synth.h"
#include "Audio/monob_synth.h"
#include "Audio/drum_synth.h"
#include "Core/brick6_master_buffer.h"
#include "Core/brick6_sampler_runtime.h"
#include "Keyboard/keyboard_runtime.h"
#include "midi.h"
#include "fx_daisy_comp.h"
#include "fx_granular.h"
#include "fx_pool.h"
#include "mixer.h"
#include "ui_core.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_model.h"
#include "Core/track_runtime.h"
#include "Mod/mod_lfo_v1.h"
#include "Sampler/sample_pool.h"
#include "Storage/memory_layout.h"
#include "Storage/undo_v1.h"
#include <math.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#ifndef SEQ_DEBUG_TRACK_BINDING
#define SEQ_DEBUG_TRACK_BINDING 0
#endif

#if SEQ_DEBUG_TRACK_BINDING
#define SEQ_BIND_LOG(...) printf(__VA_ARGS__)
#else
#define SEQ_BIND_LOG(...) do { } while (0)
#endif

static uint8_t seq_div_ui_to_runtime(float v);
static float seq_div_runtime_to_ui(uint8_t runtime_div);

/**
 * @brief Point d'entrée clamp_value.
 *
 * Rôle:
 * - Exécuter le traitement associé à clamp_value.
 *
 * @param v Paramètre d'entrée de l'API.
 * @param lo Paramètre d'entrée de l'API.
 * @param hi Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static float clamp_value(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

/**
 * @brief Point d'entrée control_float_to_slot.
 *
 * Rôle:
 * - Exécuter le traitement associé à control_float_to_slot.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static int8_t control_float_to_slot(float v)
{
    if (v < 0.0f)
        return -1;
    return (int8_t)v;
}

/**
 * @brief Point d'entrée control_float_to_ui127.
 *
 * Rôle:
 * - Exécuter le traitement associé à control_float_to_ui127.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static uint8_t control_float_to_ui127(float v)
{
    if (v <= 0.0f)
        return 0U;
    if (v >= 1.0f)
        return 127U;
    return (uint8_t)(v * 127.0f + 0.5f);
}

static float filter_ui127_clamp(float v)
{
    return clamp_value(v, 0.0f, 127.0f);
}

static float filter_ui127_to_unit(float v)
{
    return filter_ui127_clamp(v) * (1.0f / 127.0f);
}

static float filter_ui127_to_cutoff_hz(float v)
{
    const float t = filter_ui127_to_unit(v);
    const float min_hz = 20.0f;
    const float max_hz = 16000.0f;
    const float ratio = max_hz / min_hz;

    return min_hz * powf(ratio, t);
}

static float filter_ui127_to_resonance(float v)
{
    const float t = filter_ui127_to_unit(v);
    const float shaped = t * t;

    return shaped * 0.75f;
}

static float filter_ui127_to_eg_amount(float v)
{
    return filter_ui127_to_unit(v);
}

static float filter_ui127_to_time_s(float v, float min_s, float max_s)
{
    const float t = filter_ui127_to_unit(v);
    const float ratio = max_s / min_s;

    return min_s * powf(ratio, t);
}

static float filter_ui127_to_attack_s(float v)
{
    return filter_ui127_to_time_s(v, 0.001f, 5.0f);
}

static float filter_ui127_to_decay_s(float v)
{
    return filter_ui127_to_time_s(v, 0.001f, 5.0f);
}

static float filter_ui127_to_sustain(float v)
{
    return filter_ui127_to_unit(v);
}

static float filter_ui127_to_release_s(float v)
{
    return filter_ui127_to_time_s(v, 0.001f, 5.0f);
}

static float filter_ui127_to_keytrack(float v)
{
    return filter_ui127_to_unit(v);
}

static uint8_t filter_ui127_to_bool(float v)
{
    return (filter_ui127_clamp(v) >= 63.5f) ? 1U : 0U;
}

static float filter_ui127_to_env_delay_s(float v)
{
    return filter_ui127_to_time_s(v, 0.001f, 5.0f) - 0.001f;
}

static int8_t monob_range_index_to_octave(float v)
{
    static const int8_t octave_map[] = {-1, 0, 1, 2};
    uint8_t index = (uint8_t)(clamp_value(v, 0.0f, 3.0f) + 0.5f);
    return octave_map[index];
}

static int8_t monob_sub_octave_index_to_octave(float v)
{
    static const int8_t octave_map[] = {-1, -2, -3, -4};
    uint8_t index = (uint8_t)(clamp_value(v, 0.0f, 3.0f) + 0.5f);
    return octave_map[index];
}

static float filter_eq_ui127_to_db(float v)
{
    const float clamped = filter_ui127_clamp(v);

    if(clamped <= 64.0f)
    {
        return -80.0f + ((clamped / 64.0f) * 80.0f);
    }

    return ((clamped - 64.0f) / 63.0f) * 12.0f;
}

/*
 * Legacy MIX params (PARAM_MIX_TRACKx_*) are storage tombstones/load-only compat.
 * Normal MIX runtime writes use PARAM_MIX_* through param_registry_apply_track_value(track,...).
 */
#define PARAM_MIX_LEGACY_TRACK_COUNT MAX_TRACKS
#if PARAM_MIX_LEGACY_TRACK_COUNT != 4U
#error "Legacy PARAM_MIX_TRACKx mapping assumes 4 physical tracks"
#endif

uint8_t param_registry_is_legacy_physical_mix_param(param_id_t id)
{
    return ((id >= PARAM_MIX_TRACK0_GAIN)
            && (id <= PARAM_MIX_TRACK3_SEND1)
            && (id != PARAM_MIX_MUTE)) ? 1U : 0U;
}

static void apply_mix_send0_fx(float v) { mixer_set_send_fx_slot(0U, control_float_to_slot(v)); }
static void apply_mix_send1_fx(float v) { mixer_set_send_fx_slot(1U, control_float_to_slot(v)); }

static void apply_mix_reverb_wet(float v) { mixer_set_reverb_wet(clamp_value(v, 0.0f, 1.0f)); }
static void apply_mix_reverb_size(float v) { mixer_set_reverb_size(clamp_value(v, 0.0f, 1.0f)); }
static void apply_mix_reverb_decay(float v) { mixer_set_reverb_decay(clamp_value(v, 0.0f, 1.0f)); }
static void apply_mix_reverb_pred(float v) { mixer_set_reverb_pre_delay(clamp_value(v, 0.0f, 1.0f)); }
static void apply_mix_reverb_type(float v) { mixer_set_reverb_type((uint8_t)clamp_value(v, 0.0f, 1.0f)); }
static void apply_mix_reverb_surr(float v) { mixer_set_reverb_surround(clamp_value(v, 0.0f, 1.0f)); }
static void apply_tone_live_track(param_id_t id, float value)
{
    (void)param_registry_apply_track_value(id, ui_get_active_track(), value);
}

static uint8_t param_is_midi_cc_id(param_id_t id)
{
    return ((id >= PARAM_MIDI_CC1_1) && (id <= PARAM_MIDI_CC3_4)) ? 1U : 0U;
}

static uint8_t param_midi_cc_number_from_id(param_id_t id)
{
    if (param_is_midi_cc_id(id) == 0U)
    {
        return 0U;
    }

    return (uint8_t)(16U + (uint8_t)(id - PARAM_MIDI_CC1_1));
}

static uint8_t param_track_supports_midi_tone(const track_runtime_ctx_t *ctx)
{
    if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }

    if (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MIDI)
    {
        return 1U;
    }

    return ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_INPUT)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_HYBRID)) ? 1U : 0U;
}

static void apply_dx7_algorithm(float v) { apply_tone_live_track(PARAM_DX7_ALGORITHM, v); }
static void apply_dx7_feedback(float v) { apply_tone_live_track(PARAM_DX7_FEEDBACK, v); }
static void apply_dx7_transpose(float v) { apply_tone_live_track(PARAM_DX7_TRANSPOSE, v); }
static void apply_dx7_lfo_speed(float v) { apply_tone_live_track(PARAM_DX7_LFO_SPEED, v); }
static void apply_dx7_lfo_delay(float v) { apply_tone_live_track(PARAM_DX7_LFO_DELAY, v); }
static void apply_dx7_lfo_pitch_mod_depth(float v) { apply_tone_live_track(PARAM_DX7_LFO_PITCH_MOD_DEPTH, v); }
static void apply_dx7_lfo_amp_mod_depth(float v) { apply_tone_live_track(PARAM_DX7_LFO_AMP_MOD_DEPTH, v); }
static void apply_dx7_pitch_bend_range(float v) { apply_tone_live_track(PARAM_DX7_PITCH_BEND_RANGE, v); }
static void apply_dx7_portamento_time(float v) { apply_tone_live_track(PARAM_DX7_PORTAMENTO_TIME, v); }
static void apply_dx7_mono_mode(float v) { apply_tone_live_track(PARAM_DX7_MONO_MODE, v); }
static void apply_dx7_operator_mask(float v) { apply_tone_live_track(PARAM_DX7_OPERATOR_MASK, v); }
static void apply_dx7_operator_1_level(float v) { apply_tone_live_track(PARAM_DX7_OPERATOR_1_LEVEL, v); }
static void apply_dx7_operator_2_level(float v) { apply_tone_live_track(PARAM_DX7_OPERATOR_2_LEVEL, v); }
static void apply_dx7_operator_3_level(float v) { apply_tone_live_track(PARAM_DX7_OPERATOR_3_LEVEL, v); }
static void apply_dx7_operator_4_level(float v) { apply_tone_live_track(PARAM_DX7_OPERATOR_4_LEVEL, v); }
static void apply_midi_program(float v) { apply_tone_live_track(PARAM_MIDI_PROGRAM, v); }
static void apply_sampler_sample(float v) { apply_tone_live_track(PARAM_SAMPLER_SAMPLE, v); }
static void apply_sampler_gain(float v) { apply_tone_live_track(PARAM_SAMPLER_GAIN, v); }
static void apply_sampler_start(float v) { apply_tone_live_track(PARAM_SAMPLER_START, v); }
static void apply_sampler_end(float v) { apply_tone_live_track(PARAM_SAMPLER_END, v); }
static void apply_sampler_mode(float v) { apply_tone_live_track(PARAM_SAMPLER_MODE, v); }
static void apply_sampler_tune(float v) { apply_tone_live_track(PARAM_SAMPLER_TUNE, v); }
static void apply_sampler_fade_in(float v) { apply_tone_live_track(PARAM_SAMPLER_FADE_IN, v); }
static void apply_sampler_fade_out(float v) { apply_tone_live_track(PARAM_SAMPLER_FADE_OUT, v); }
static void apply_sampler_slice_count(float v) { apply_tone_live_track(PARAM_SAMPLER_SLICE_COUNT, v); }
static void apply_midi_cc1_1(float v) { apply_tone_live_track(PARAM_MIDI_CC1_1, v); }
static void apply_midi_cc1_2(float v) { apply_tone_live_track(PARAM_MIDI_CC1_2, v); }
static void apply_midi_cc1_3(float v) { apply_tone_live_track(PARAM_MIDI_CC1_3, v); }
static void apply_midi_cc1_4(float v) { apply_tone_live_track(PARAM_MIDI_CC1_4, v); }
static void apply_midi_cc2_1(float v) { apply_tone_live_track(PARAM_MIDI_CC2_1, v); }
static void apply_midi_cc2_2(float v) { apply_tone_live_track(PARAM_MIDI_CC2_2, v); }
static void apply_midi_cc2_3(float v) { apply_tone_live_track(PARAM_MIDI_CC2_3, v); }
static void apply_midi_cc2_4(float v) { apply_tone_live_track(PARAM_MIDI_CC2_4, v); }
static void apply_midi_cc3_1(float v) { apply_tone_live_track(PARAM_MIDI_CC3_1, v); }
static void apply_midi_cc3_2(float v) { apply_tone_live_track(PARAM_MIDI_CC3_2, v); }
static void apply_midi_cc3_3(float v) { apply_tone_live_track(PARAM_MIDI_CC3_3, v); }
static void apply_midi_cc3_4(float v) { apply_tone_live_track(PARAM_MIDI_CC3_4, v); }

static fx_granular_state_t *get_active_granular_state(void)
{
    for (uint32_t i = 0U;; ++i)
    {
        fx_slot_t *slot = fx_pool_get_slot(i);
        if (slot == NULL)
            break;

        if ((slot->active != 0U) && ((fx_type_t)slot->type == FX_GRANULAR) && (slot->state != NULL))
            return (fx_granular_state_t *)slot->state;
    }

    return NULL;
}

/**
 * @brief Point d'entrée apply_gran_density.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_gran_density.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_gran_density(float v)
{
    fx_granular_state_t *state = get_active_granular_state();
    if (state != NULL) fx_granular_set_density(state, v);
}

/**
 * @brief Point d'entrée apply_gran_pitch.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_gran_pitch.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_gran_pitch(float v)
{
    fx_granular_state_t *state = get_active_granular_state();
    if (state != NULL) fx_granular_set_pitch(state, v);
}

/**
 * @brief Point d'entrée apply_gran_mix.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_gran_mix.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_gran_mix(float v)
{
    fx_granular_state_t *state = get_active_granular_state();
    if (state != NULL) fx_granular_set_mix(state, v);
}

/**
 * @brief Point d'entrée apply_gran_freeze.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_gran_freeze.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_gran_freeze(float v)
{
    fx_granular_state_t *state = get_active_granular_state();
    if (state != NULL) fx_granular_set_freeze(state, (v >= 0.5f));
}

/**
 * @brief Point d'entrée apply_gran_spread.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_gran_spread.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_gran_spread(float v)
{
    fx_granular_state_t *state = get_active_granular_state();
    if (state != NULL) fx_granular_set_spread(state, v);
}

/**
 * @brief Point d'entrée apply_gran_stereo.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_gran_stereo.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_gran_stereo(float v)
{
    fx_granular_state_t *state = get_active_granular_state();
    if (state != NULL) fx_granular_set_stereo_offset(state, v);
}

static void apply_eq_low_db(float v) { audio_float_set_dj_eq_low_db(v); }
static void apply_eq_mid_db(float v) { audio_float_set_dj_eq_mid_db(v); }
static void apply_eq_high_db(float v) { audio_float_set_dj_eq_high_db(v); }

static void apply_sat_tone(float v) { audio_float_set_saturation_tone_ui(control_float_to_ui127(v)); }
static void apply_sat_bias(float v) { audio_float_set_saturation_bias_ui(control_float_to_ui127(v)); }
static void apply_sat_drive(float v) { audio_float_set_saturation_drive_ui(control_float_to_ui127(v)); }
static void apply_sat_mix(float v) { audio_float_set_saturation_mix_ui(control_float_to_ui127(v)); }

extern const param_desc_t param_registry[PARAM_COUNT];

#define FILTER_TRACK_STATE_COUNT SEQ_TRACK_COUNT
#define FILTER_RUNTIME_REBIND_NONE 0xFFU

typedef struct
{
    float type;
    float cutoff;
    float resonance;
    float eg_amount;
    float attack;
    float decay;
    float sustain;
    float release;
    float keytrack;
    float env_reset;
    float env_delay;
    float eq_low;
    float eq_mid;
    float eq_high;
    float drive;
    float decimator_bits;
    float decimator_rate;
    float decimator_rate2;
} filter_ui_state_t;

static filter_ui_state_t g_filter_ui_state[FILTER_TRACK_STATE_COUNT];
volatile uint32_t g_param_cfg_track_type_apply_stage = 0U;
static uint8_t g_param_registry_batch_depth = 0U;
SEQ_STATE_D2 static float g_param_runtime_track_values[SEQ_TRACK_COUNT][PARAM_COUNT];
SEQ_STATE_D2 static uint8_t g_param_runtime_track_valid[SEQ_TRACK_COUNT][PARAM_COUNT];

void param_registry_batch_begin(void)
{
    if (g_param_registry_batch_depth < 255U)
    {
        g_param_registry_batch_depth++;
    }
}

void param_registry_batch_end(void)
{
    if (g_param_registry_batch_depth > 0U)
    {
        g_param_registry_batch_depth--;
    }
}

static uint8_t param_runtime_cache_get(uint8_t track, param_id_t id, float *out_value)
{
    if ((track >= SEQ_TRACK_COUNT) || (id >= PARAM_COUNT) || (out_value == NULL))
    {
        return 0U;
    }

    if (g_param_runtime_track_valid[track][id] == 0U)
    {
        return 0U;
    }

    *out_value = g_param_runtime_track_values[track][id];
    return 1U;
}

static void param_runtime_cache_set(uint8_t track, param_id_t id, float value)
{
    if ((track >= SEQ_TRACK_COUNT) || (id >= PARAM_COUNT))
    {
        return;
    }

    g_param_runtime_track_values[track][id] = value;
    g_param_runtime_track_valid[track][id] = 1U;
}

static uint8_t param_lfo_map(param_id_t id, uint8_t *out_lfo_index, mod_lfo_param_t *out_lfo_param)
{
    if ((out_lfo_index == NULL) || (out_lfo_param == NULL))
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_LFO1_DEST:
            *out_lfo_index = 0U;
            *out_lfo_param = MOD_LFO_PARAM_DEST;
            return 1U;
        case PARAM_LFO1_RATE:
            *out_lfo_index = 0U;
            *out_lfo_param = MOD_LFO_PARAM_RATE;
            return 1U;
        case PARAM_LFO1_DEPTH:
            *out_lfo_index = 0U;
            *out_lfo_param = MOD_LFO_PARAM_DEPTH;
            return 1U;
        case PARAM_LFO1_SHAPE:
            *out_lfo_index = 0U;
            *out_lfo_param = MOD_LFO_PARAM_SHAPE;
            return 1U;
        case PARAM_LFO2_DEST:
            *out_lfo_index = 1U;
            *out_lfo_param = MOD_LFO_PARAM_DEST;
            return 1U;
        case PARAM_LFO2_RATE:
            *out_lfo_index = 1U;
            *out_lfo_param = MOD_LFO_PARAM_RATE;
            return 1U;
        case PARAM_LFO2_DEPTH:
            *out_lfo_index = 1U;
            *out_lfo_param = MOD_LFO_PARAM_DEPTH;
            return 1U;
        case PARAM_LFO2_SHAPE:
            *out_lfo_index = 1U;
            *out_lfo_param = MOD_LFO_PARAM_SHAPE;
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t param_is_filter_ui_param(param_id_t id)
{
    switch (id)
    {
        case PARAM_FILTER_TYPE:
        case PARAM_FILTER_CUTOFF:
        case PARAM_FILTER_RESONANCE:
        case PARAM_FILTER_EG_AMT:
        case PARAM_FILTER_ATTACK:
        case PARAM_FILTER_DECAY:
        case PARAM_FILTER_SUSTAIN:
        case PARAM_FILTER_RELEASE:
        case PARAM_FILTER_KEYTRK:
        case PARAM_FILTER_ENVRST:
        case PARAM_FILTER_ENVDLY:
        case PARAM_FILTER_EQ_LOW:
        case PARAM_FILTER_EQ_MID:
        case PARAM_FILTER_EQ_HIGH:
        case PARAM_FILTER_DRIVE:
        case PARAM_FILTER_DECIMATOR_BITS:
        case PARAM_FILTER_DECIMATOR_RATE:
        case PARAM_FILTER_DECIMATOR_RATE2:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t param_runtime_apply_tone_dx7(uint8_t instance_id, param_id_t id, float value)
{
    if (instance_id != 0U)
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_DX7_ALGORITHM: microdexed_synth_set_param(MICRODEXED_PARAM_ALGORITHM, value); return 1U;
        case PARAM_DX7_FEEDBACK: microdexed_synth_set_param(MICRODEXED_PARAM_FEEDBACK, value); return 1U;
        case PARAM_DX7_TRANSPOSE: microdexed_synth_set_param(MICRODEXED_PARAM_TRANSPOSE, value); return 1U;
        case PARAM_DX7_LFO_SPEED: microdexed_synth_set_param(MICRODEXED_PARAM_LFO_SPEED, value); return 1U;
        case PARAM_DX7_LFO_DELAY: microdexed_synth_set_param(MICRODEXED_PARAM_LFO_DELAY, value); return 1U;
        case PARAM_DX7_LFO_PITCH_MOD_DEPTH: microdexed_synth_set_param(MICRODEXED_PARAM_LFO_PITCH_MOD_DEPTH, value); return 1U;
        case PARAM_DX7_LFO_AMP_MOD_DEPTH: microdexed_synth_set_param(MICRODEXED_PARAM_LFO_AMP_MOD_DEPTH, value); return 1U;
        case PARAM_DX7_PITCH_BEND_RANGE: microdexed_synth_set_param(MICRODEXED_PARAM_PITCH_BEND_RANGE, value); return 1U;
        case PARAM_DX7_PORTAMENTO_TIME: microdexed_synth_set_param(MICRODEXED_PARAM_PORTAMENTO_TIME, value); return 1U;
        case PARAM_DX7_MONO_MODE: microdexed_synth_set_param(MICRODEXED_PARAM_MONO_MODE, value); return 1U;
        case PARAM_DX7_OPERATOR_MASK: microdexed_synth_set_param(MICRODEXED_PARAM_OPERATOR_MASK, value); return 1U;
        case PARAM_DX7_OPERATOR_1_LEVEL: microdexed_synth_set_param(MICRODEXED_PARAM_OPERATOR_1_LEVEL, value); return 1U;
        case PARAM_DX7_OPERATOR_2_LEVEL: microdexed_synth_set_param(MICRODEXED_PARAM_OPERATOR_2_LEVEL, value); return 1U;
        case PARAM_DX7_OPERATOR_3_LEVEL: microdexed_synth_set_param(MICRODEXED_PARAM_OPERATOR_3_LEVEL, value); return 1U;
        case PARAM_DX7_OPERATOR_4_LEVEL: microdexed_synth_set_param(MICRODEXED_PARAM_OPERATOR_4_LEVEL, value); return 1U;
        default: return 0U;
    }
}

static uint8_t param_runtime_apply_tone_monob(uint8_t instance_id, param_id_t id, float value)
{
    switch (id)
    {
        case PARAM_MONOB_OSC1_WAVE: monob_synth_set_osc_wave_for_instance(instance_id, 0U, (uint8_t)(clamp_value(value, 0.0f, 4.0f) + 0.5f)); return 1U;
        case PARAM_MONOB_OSC2_WAVE: monob_synth_set_osc_wave_for_instance(instance_id, 1U, (uint8_t)(clamp_value(value, 0.0f, 4.0f) + 0.5f)); return 1U;
        case PARAM_MONOB_OSC3_WAVE: monob_synth_set_osc_wave_for_instance(instance_id, 2U, (uint8_t)(clamp_value(value, 0.0f, 4.0f) + 0.5f)); return 1U;
        case PARAM_MONOB_SUB_WAVE: monob_synth_set_osc_wave_for_instance(instance_id, 3U, (uint8_t)(clamp_value(value, 0.0f, 4.0f) + 0.5f)); return 1U;
        case PARAM_MONOB_OSC1_RANGE: monob_synth_set_osc_range_for_instance(instance_id, 0U, monob_range_index_to_octave(value)); return 1U;
        case PARAM_MONOB_OSC2_RANGE: monob_synth_set_osc_range_for_instance(instance_id, 1U, monob_range_index_to_octave(value)); return 1U;
        case PARAM_MONOB_OSC3_RANGE: monob_synth_set_osc_range_for_instance(instance_id, 2U, monob_range_index_to_octave(value)); return 1U;
        case PARAM_MONOB_SUB_OCTAVE: monob_synth_set_sub_octave_for_instance(instance_id, monob_sub_octave_index_to_octave(value)); return 1U;
        case PARAM_MONOB_OSC1_DETUNE: monob_synth_set_osc_detune_for_instance(instance_id, 0U, clamp_value(value, -24.0f, 24.0f)); return 1U;
        case PARAM_MONOB_OSC2_DETUNE: monob_synth_set_osc_detune_for_instance(instance_id, 1U, clamp_value(value, -24.0f, 24.0f)); return 1U;
        case PARAM_MONOB_OSC3_DETUNE: monob_synth_set_osc_detune_for_instance(instance_id, 2U, clamp_value(value, -24.0f, 24.0f)); return 1U;
        case PARAM_MONOB_OSC1_MIX: monob_synth_set_osc_mix_for_instance(instance_id, 0U, clamp_value(value, 0.0f, 1.0f)); return 1U;
        case PARAM_MONOB_OSC2_MIX: monob_synth_set_osc_mix_for_instance(instance_id, 1U, clamp_value(value, 0.0f, 1.0f)); return 1U;
        case PARAM_MONOB_OSC3_MIX: monob_synth_set_osc_mix_for_instance(instance_id, 2U, clamp_value(value, 0.0f, 1.0f)); return 1U;
        case PARAM_MONOB_SUB_MIX: monob_synth_set_sub_mix_for_instance(instance_id, clamp_value(value, 0.0f, 1.0f)); return 1U;
        default: return 0U;
    }
}

static uint8_t param_runtime_apply_tone_sampler(uint8_t track, param_id_t id, float value)
{
    switch (id)
    {
        case PARAM_SAMPLER_SAMPLE:
            if (sample_pool_is_loaded((uint16_t)(clamp_value(value, 0.0f, 63.0f) + 0.5f)) == 0U)
            {
                brick6_sampler_runtime_stop(track);
                return 0U;
            }
            brick6_sampler_runtime_set_sample(track, (uint16_t)(clamp_value(value, 0.0f, 63.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_GAIN:
            brick6_sampler_runtime_set_gain(track, clamp_value(value, 0.0f, 2.0f));
            return 1U;
        case PARAM_SAMPLER_START:
            brick6_sampler_runtime_set_start(track, clamp_value(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_END:
            brick6_sampler_runtime_set_end(track, clamp_value(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_MODE:
            brick6_sampler_runtime_set_mode(track, (uint8_t)(clamp_value(value, 0.0f, 5.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_TUNE:
            brick6_sampler_runtime_set_tune(track, clamp_value(value, -24.0f, 24.0f));
            return 1U;
        case PARAM_SAMPLER_FADE_IN:
            brick6_sampler_runtime_set_fade_in(track, clamp_value(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_FADE_OUT:
            brick6_sampler_runtime_set_fade_out(track, clamp_value(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_SLICE_COUNT:
        {
            static const uint8_t counts[] = {2U, 4U, 8U, 16U, 32U, 64U};
            const uint8_t idx = (uint8_t)(clamp_value(value, 0.0f, 5.0f) + 0.5f);
            brick6_sampler_runtime_set_slice_count(track, counts[idx]);
            return 1U;
        }
        default:
            return 0U;
    }
}

static uint8_t param_runtime_apply_buffer_track(uint8_t track, param_id_t id, float value)
{
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_MASTER)
            || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_BUFFER))
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_BUFFER_REC_LEN:
            brick6_master_buffer_set_record_len((uint32_t)(clamp_value(value, 1.0f, 64.0f) + 0.5f));
            param_runtime_cache_set(track, id, value);
            return 1U;
        case PARAM_BUFFER_Q_REC:
            brick6_master_buffer_set_quantize_record((value >= 0.5f) ? 1U : 0U);
            param_runtime_cache_set(track, id, value);
            return 1U;
        case PARAM_BUFFER_Q_PLAY:
            brick6_master_buffer_set_quantize_play((value >= 0.5f) ? 1U : 0U);
            param_runtime_cache_set(track, id, value);
            return 1U;
        case PARAM_BUFFER_RATE:
            brick6_master_buffer_set_rate(value);
            param_runtime_cache_set(track, id, value);
            return 1U;
        case PARAM_BUFFER_FADE_IN:
            brick6_master_buffer_set_fade_in((uint32_t)(clamp_value(value, 0.0f, 127.0f) + 0.5f));
            param_runtime_cache_set(track, id, value);
            return 1U;
        case PARAM_BUFFER_FADE_OUT:
            brick6_master_buffer_set_fade_out((uint32_t)(clamp_value(value, 0.0f, 127.0f) + 0.5f));
            param_runtime_cache_set(track, id, value);
            return 1U;
        case PARAM_BUFFER_XFADE:
            brick6_master_buffer_set_xfade(clamp_value(value, 0.0f, 1.0f));
            param_runtime_cache_set(track, id, value);
            return 1U;

        default:
            return 0U;
    }
}

static uint8_t param_runtime_apply_mix_track(uint8_t track, param_id_t id, float value)
{
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL) || (track_runtime_is_audio_routable(track) == 0U))
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_MIX_LEVEL:
            mixer_set_track_gain(ctx->mix_track_id, clamp_value(value, 0.0f, 2.0f));
            return 1U;

        case PARAM_MIX_PAN:
            mixer_set_track_pan(ctx->mix_track_id, clamp_value(value, -1.0f, 1.0f));
            return 1U;

        case PARAM_MIX_SEND1:
            mixer_set_track_send_level(ctx->mix_track_id, 0U, clamp_value(value, 0.0f, 1.0f));
            return 1U;

        case PARAM_MIX_SEND2:
            mixer_set_track_send_level(ctx->mix_track_id, 1U, clamp_value(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_MIX_MUTE:
            mixer_set_track_mute(ctx->mix_track_id, (value >= 0.5f) ? 1U : 0U);
            return 1U;
        case PARAM_HYBRID_GATE:
            if ((ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_INPUT)
                    || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_HYBRID))
            {
                return 0U;
            }
            mixer_set_track_vca_enabled(ctx->mix_track_id, (value >= 0.5f) ? 1U : 0U);
            if (value < 0.5f)
            {
                mixer_track_vca_all_notes_off((uint32_t)ctx->mix_track_id);
            }
            return 1U;
        case PARAM_VCA_ATTACK:
            mixer_set_track_vca_attack(ctx->mix_track_id, filter_ui127_to_attack_s(value));
            return 1U;
        case PARAM_VCA_DECAY:
            mixer_set_track_vca_decay(ctx->mix_track_id, filter_ui127_to_decay_s(value));
            return 1U;
        case PARAM_VCA_SUSTAIN:
            mixer_set_track_vca_sustain(ctx->mix_track_id, filter_ui127_to_sustain(value));
            return 1U;
        case PARAM_VCA_RELEASE:
            mixer_set_track_vca_release(ctx->mix_track_id, filter_ui127_to_release_s(value));
            return 1U;

        default:
            return 0U;
    }
}

static uint8_t param_runtime_apply_track(uint8_t track, param_id_t id, float value)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_MIX))
    {
        return 0U;
    }

    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }

    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (param_track_supports_midi_tone(ctx) != 0U))
    {
        if (param_is_midi_cc_id(id) != 0U)
        {
            const uint8_t cc_number = param_midi_cc_number_from_id(id);
            const uint8_t cc_value = (uint8_t)(clamp_value(value, 0.0f, 127.0f) + 0.5f);
            const uint8_t channel_1_16 = ui_get_track_midi_channel(track);
            const uint8_t channel = (uint8_t)((channel_1_16 > 0U) ? (channel_1_16 - 1U) : 0U);
            midi_cc(MIDI_DEST_BOTH, channel, cc_number, cc_value);
            param_runtime_cache_set(track, id, value);
            return 1U;
        }

        if (id == PARAM_MIDI_PROGRAM)
        {
            return 0U;
        }
    }

    uint8_t applied = 0U;
    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
    {
        applied = param_runtime_apply_mix_track(track, id, value);
    }
    else if (rule.resource == TRACK_RUNTIME_RESOURCE_BUFFER)
    {
        applied = param_runtime_apply_buffer_track(track, id, value);
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DX7)
    {
        applied = param_runtime_apply_tone_dx7(ctx->instance_id, id, value);
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_MONOB)
    {
        applied = param_runtime_apply_tone_monob(ctx->instance_id, id, value);
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
    {
        applied = param_runtime_apply_tone_sampler(track, id, value);
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
    {
        applied = drum_synth_set_param_for_instance(ctx->instance_id, id, value);
    }

    if (applied != 0U)
    {
        param_runtime_cache_set(track, id, value);
    }

    return applied;
}

static uint8_t param_runtime_apply_colors_track(uint8_t track, param_id_t id, float value)
{
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }

    switch (ctx->engine)
    {
        case (uint8_t)TRACK_RUNTIME_ENGINE_MONOB:
            switch (id)
            {
                case PARAM_MONOB_FILTER_TYPE:
                    monob_synth_set_filter_type_for_instance(ctx->instance_id, (uint8_t)(clamp_value(value, 0.0f, 1.0f) + 0.5f));
                    return 1U;
                case PARAM_MONOB_FILTER_CUTOFF:
                    monob_synth_set_filter_cutoff_for_instance(ctx->instance_id, filter_ui127_to_cutoff_hz(value));
                    return 1U;
                case PARAM_MONOB_FILTER_RESONANCE:
                    monob_synth_set_filter_resonance_for_instance(ctx->instance_id, filter_ui127_to_resonance(value));
                    return 1U;
                case PARAM_MONOB_FILTER_EG_AMT:
                    monob_synth_set_filter_eg_amount_for_instance(ctx->instance_id, filter_ui127_to_eg_amount(value));
                    return 1U;
                case PARAM_MONOB_FILTER_ATTACK:
                    monob_synth_set_filter_attack_for_instance(ctx->instance_id, filter_ui127_to_attack_s(value));
                    return 1U;
                case PARAM_MONOB_FILTER_DECAY:
                    monob_synth_set_filter_decay_for_instance(ctx->instance_id, filter_ui127_to_decay_s(value));
                    return 1U;
                case PARAM_MONOB_FILTER_SUSTAIN:
                    monob_synth_set_filter_sustain_for_instance(ctx->instance_id, filter_ui127_to_sustain(value));
                    return 1U;
                case PARAM_MONOB_FILTER_RELEASE:
                    monob_synth_set_filter_release_for_instance(ctx->instance_id, filter_ui127_to_release_s(value));
                    return 1U;
                case PARAM_MONOB_FILTER_KEYTRK:
                    monob_synth_set_filter_keytrack_for_instance(ctx->instance_id, filter_ui127_to_keytrack(value));
                    return 1U;
                case PARAM_MONOB_FILTER_ENVRST:
                    monob_synth_set_filter_env_reset_for_instance(ctx->instance_id, filter_ui127_to_bool(value));
                    return 1U;
                case PARAM_MONOB_FILTER_ENVDLY:
                    monob_synth_set_filter_env_delay_for_instance(ctx->instance_id, filter_ui127_to_env_delay_s(value));
                    return 1U;
                default:
                    return 0U;
            }
        case (uint8_t)TRACK_RUNTIME_ENGINE_DRUM:
            return drum_synth_set_param_for_instance(ctx->instance_id, id, value);
        default:
            return 0U;
    }
}

static uint8_t filter_mod_locked_for_active_track(void)
{
    const uint8_t active_track = ui_get_active_track();
    const ui_track_family_t family = ui_get_track_family(active_track);
    const ui_track_type_t type = ui_get_track_type(active_track);

    return (ui_track_family_is_input(family) && (type == UI_TRACK_TYPE_AUDIO)) ? 1U : 0U;
}

static void filter_ui_state_init_defaults(void)
{
    for (uint32_t i = 0U; i < FILTER_TRACK_STATE_COUNT; ++i)
    {
        g_filter_ui_state[i].type = param_registry[PARAM_FILTER_TYPE].default_value;
        g_filter_ui_state[i].cutoff = param_registry[PARAM_FILTER_CUTOFF].default_value;
        g_filter_ui_state[i].resonance = param_registry[PARAM_FILTER_RESONANCE].default_value;
        g_filter_ui_state[i].eg_amount = param_registry[PARAM_FILTER_EG_AMT].default_value;
        g_filter_ui_state[i].attack = param_registry[PARAM_FILTER_ATTACK].default_value;
        g_filter_ui_state[i].decay = param_registry[PARAM_FILTER_DECAY].default_value;
        g_filter_ui_state[i].sustain = param_registry[PARAM_FILTER_SUSTAIN].default_value;
        g_filter_ui_state[i].release = param_registry[PARAM_FILTER_RELEASE].default_value;
        g_filter_ui_state[i].keytrack = param_registry[PARAM_FILTER_KEYTRK].default_value;
        g_filter_ui_state[i].env_reset = param_registry[PARAM_FILTER_ENVRST].default_value;
        g_filter_ui_state[i].env_delay = param_registry[PARAM_FILTER_ENVDLY].default_value;
        g_filter_ui_state[i].eq_low = param_registry[PARAM_FILTER_EQ_LOW].default_value;
        g_filter_ui_state[i].eq_mid = param_registry[PARAM_FILTER_EQ_MID].default_value;
        g_filter_ui_state[i].eq_high = param_registry[PARAM_FILTER_EQ_HIGH].default_value;
        g_filter_ui_state[i].drive = param_registry[PARAM_FILTER_DRIVE].default_value;
        g_filter_ui_state[i].decimator_bits = param_registry[PARAM_FILTER_DECIMATOR_BITS].default_value;
        g_filter_ui_state[i].decimator_rate = param_registry[PARAM_FILTER_DECIMATOR_RATE].default_value;
        g_filter_ui_state[i].decimator_rate2 = param_registry[PARAM_FILTER_DECIMATOR_RATE2].default_value;
    }
}

static uint8_t resolve_filter_target_track(uint32_t *out_track_id)
{
    uint8_t track_id = 0U;
    const uint8_t ui_track = ui_get_active_track();
    track_runtime_refresh_track(ui_track);
    if ((out_track_id == NULL) || (track_runtime_resolve_filter_target_track(ui_track, &track_id) == 0U))
    {
        return 0U;
    }

    *out_track_id = (uint32_t)track_id;
    return 1U;
}

static uint8_t resolve_filter_target_track_for_ui_track(uint8_t ui_track, uint32_t *out_track_id)
{
    uint8_t track_id = 0U;
    track_runtime_refresh_track(ui_track);
    if ((out_track_id == NULL) || (track_runtime_resolve_filter_target_track(ui_track, &track_id) == 0U))
    {
        return 0U;
    }

    *out_track_id = (uint32_t)track_id;
    return 1U;
}

static uint8_t resolve_filter_drive_target_track_for_ui_track(uint8_t ui_track, uint32_t *out_track_id)
{
    uint8_t track_id = 0U;
    track_runtime_refresh_track(ui_track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(ui_track);
    if ((out_track_id == NULL)
            || (ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (track_runtime_is_audio_routable(ui_track) == 0U))
    {
        return 0U;
    }

    if (track_runtime_get_mix_target_track(ui_track, &track_id) == 0U)
    {
        return 0U;
    }

    *out_track_id = (uint32_t)track_id;
    return 1U;
}

static filter_ui_state_t *resolve_filter_ui_state_for_track(uint8_t track)
{
    if (track >= FILTER_TRACK_STATE_COUNT)
    {
        return NULL;
    }
    return &g_filter_ui_state[track];
}

static void apply_filter_drive_runtime(uint32_t target_track, float drive_ui)
{
    const uint8_t drive_0_127 = (uint8_t)(clamp_value(drive_ui, 0.0f, 127.0f) + 0.5f);
    audio_float_set_track_saturation_drive_ui(target_track, drive_0_127);
}

static void apply_filter_decimator_bits_runtime(uint32_t target_track, float bits_ui)
{
    const uint8_t bits_0_127 = (uint8_t)(clamp_value(bits_ui, 0.0f, 127.0f) + 0.5f);
    audio_float_set_track_saturation_decimator_bits_ui(target_track, bits_0_127);
}

static void apply_filter_decimator_rate_runtime(uint32_t target_track, float rate_ui)
{
    const uint8_t rate_0_127 = (uint8_t)(clamp_value(rate_ui, 0.0f, 127.0f) + 0.5f);
    audio_float_set_track_saturation_decimator_rate_ui(target_track, rate_0_127);
}

static void apply_filter_decimator_rate2_runtime(uint32_t target_track, float rate_ui)
{
    const uint8_t rate_0_127 = (uint8_t)(clamp_value(rate_ui, 0.0f, 127.0f) + 0.5f);
    audio_float_set_track_saturation_decimator_rate2_ui(target_track, rate_0_127);
}

static void apply_filter_crunch_insert_runtime(uint32_t target_track, const filter_ui_state_t *state)
{
    if ((state == NULL) || (target_track >= MIXER_MAX_TRACKS))
    {
        return;
    }

    const uint8_t has_drive = (state->drive > 0.5f) ? 1U : 0U;
    const uint8_t has_bits = (state->decimator_bits > 0.5f) ? 1U : 0U;
    const uint8_t has_rate = (state->decimator_rate > 0.5f) ? 1U : 0U;
    const uint8_t has_rate2 = (state->decimator_rate2 > 0.5f) ? 1U : 0U;
    mixer_set_track_insert_slot(target_track, 1U, ((has_drive != 0U) || (has_bits != 0U) || (has_rate != 0U) || (has_rate2 != 0U)) ? 1 : -1);
}

uint8_t param_registry_get_track_value(param_id_t id, uint8_t track, float *out_value)
{
    if ((id >= PARAM_COUNT) || (out_value == NULL))
    {
        return 0U;
    }

    {
        uint8_t lfo_index = 0U;
        mod_lfo_param_t lfo_param = MOD_LFO_PARAM_DEST;
        if (param_lfo_map(id, &lfo_index, &lfo_param) != 0U)
        {
            return mod_lfo_v1_get_track_param(track, lfo_index, lfo_param, out_value);
        }
    }

    filter_ui_state_t *state = NULL;
    switch (id)
    {
        case PARAM_FILTER_DRIVE:
        case PARAM_FILTER_DECIMATOR_BITS:
        case PARAM_FILTER_DECIMATOR_RATE:
        case PARAM_FILTER_DECIMATOR_RATE2:
            if (track >= SEQ_TRACK_COUNT)
            {
                return 0U;
            }
            state = resolve_filter_ui_state_for_track(track);
            if (state == NULL)
            {
                return 0U;
            }
            break;

        case PARAM_FILTER_TYPE:
        case PARAM_FILTER_CUTOFF:
        case PARAM_FILTER_RESONANCE:
        case PARAM_FILTER_EG_AMT:
        case PARAM_FILTER_ATTACK:
        case PARAM_FILTER_DECAY:
        case PARAM_FILTER_SUSTAIN:
        case PARAM_FILTER_RELEASE:
        case PARAM_FILTER_KEYTRK:
        case PARAM_FILTER_ENVRST:
        case PARAM_FILTER_ENVDLY:
        case PARAM_FILTER_EQ_LOW:
        case PARAM_FILTER_EQ_MID:
        case PARAM_FILTER_EQ_HIGH:
            if (track >= SEQ_TRACK_COUNT)
            {
                return 0U;
            }
            state = resolve_filter_ui_state_for_track(track);
            if (state == NULL)
            {
                return 0U;
            }
            break;

        default:
        {
            const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
            if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_NONE)
                    && (rule.status != TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
            {
                if (track >= SEQ_TRACK_COUNT)
                {
                    return 0U;
                }

                if (param_runtime_cache_get(track, id, out_value) != 0U)
                {
                    return 1U;
                }

                *out_value = param_registry[id].default_value;
                return 1U;
            }

            *out_value = param_get(id);
            return 1U;
        }
    }

    switch (id)
    {
        case PARAM_FILTER_TYPE: *out_value = state->type; return 1U;
        case PARAM_FILTER_CUTOFF: *out_value = state->cutoff; return 1U;
        case PARAM_FILTER_RESONANCE: *out_value = state->resonance; return 1U;
        case PARAM_FILTER_EG_AMT: *out_value = state->eg_amount; return 1U;
        case PARAM_FILTER_ATTACK: *out_value = state->attack; return 1U;
        case PARAM_FILTER_DECAY: *out_value = state->decay; return 1U;
        case PARAM_FILTER_SUSTAIN: *out_value = state->sustain; return 1U;
        case PARAM_FILTER_RELEASE: *out_value = state->release; return 1U;
        case PARAM_FILTER_KEYTRK: *out_value = state->keytrack; return 1U;
        case PARAM_FILTER_ENVRST: *out_value = state->env_reset; return 1U;
        case PARAM_FILTER_ENVDLY: *out_value = state->env_delay; return 1U;
        case PARAM_FILTER_EQ_LOW: *out_value = state->eq_low; return 1U;
        case PARAM_FILTER_EQ_MID: *out_value = state->eq_mid; return 1U;
        case PARAM_FILTER_EQ_HIGH: *out_value = state->eq_high; return 1U;
        case PARAM_FILTER_DRIVE: *out_value = state->drive; return 1U;
        case PARAM_FILTER_DECIMATOR_BITS: *out_value = state->decimator_bits; return 1U;
        case PARAM_FILTER_DECIMATOR_RATE: *out_value = state->decimator_rate; return 1U;
        case PARAM_FILTER_DECIMATOR_RATE2: *out_value = state->decimator_rate2; return 1U;
        default: break;
    }

    return 0U;
}


uint8_t param_registry_apply_track_value_rt_fast(param_id_t id, uint8_t track, float value)
{
    if (id >= PARAM_COUNT)
    {
        return 0U;
    }

    const param_desc_t *const desc = &param_registry[id];
    const float clamped = clamp_value(value, desc->min, desc->max);
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);

    if (param_is_filter_ui_param(id) != 0U)
    {
        uint32_t target_track = 0U;
        filter_ui_state_t *state = resolve_filter_ui_state_for_track(track);
        filter_ui_state_t tmp_state;

        if ((id == PARAM_FILTER_DRIVE)
                || (id == PARAM_FILTER_DECIMATOR_BITS)
                || (id == PARAM_FILTER_DECIMATOR_RATE)
                || (id == PARAM_FILTER_DECIMATOR_RATE2))
        {
            if (resolve_filter_drive_target_track_for_ui_track(track, &target_track) == 0U)
            {
                return 0U;
            }
        }
        else
        {
            if (resolve_filter_target_track_for_ui_track(track, &target_track) == 0U)
            {
                return 0U;
            }
        }

        switch (id)
        {
            case PARAM_FILTER_TYPE:
                mixer_set_track_filter_type(target_track, (mixer_track_filter_type_t)((uint32_t)(clamp_value(clamped, 0.0f, 4.0f) + 0.5f)));
                return 1U;
            case PARAM_FILTER_CUTOFF:
                mixer_set_track_filter_cutoff(target_track, filter_ui127_to_cutoff_hz(clamped));
                return 1U;
            case PARAM_FILTER_RESONANCE:
                mixer_set_track_filter_resonance(target_track, filter_ui127_to_resonance(clamped));
                return 1U;
            case PARAM_FILTER_EG_AMT:
                mixer_set_track_filter_eg_amount(target_track, filter_ui127_to_eg_amount(clamped));
                return 1U;
            case PARAM_FILTER_ATTACK:
                mixer_set_track_filter_attack(target_track, filter_ui127_to_attack_s(clamped));
                return 1U;
            case PARAM_FILTER_DECAY:
                mixer_set_track_filter_decay(target_track, filter_ui127_to_decay_s(clamped));
                return 1U;
            case PARAM_FILTER_SUSTAIN:
                mixer_set_track_filter_sustain(target_track, filter_ui127_to_sustain(clamped));
                return 1U;
            case PARAM_FILTER_RELEASE:
                mixer_set_track_filter_release(target_track, filter_ui127_to_release_s(clamped));
                return 1U;
            case PARAM_FILTER_KEYTRK:
                mixer_set_track_filter_keytrack(target_track, filter_ui127_to_keytrack(clamped));
                return 1U;
            case PARAM_FILTER_ENVRST:
            case PARAM_FILTER_ENVDLY:
                return 1U;
            case PARAM_FILTER_EQ_LOW:
                mixer_set_track_filter_eq_low(target_track, filter_eq_ui127_to_db(clamped));
                return 1U;
            case PARAM_FILTER_EQ_MID:
                mixer_set_track_filter_eq_mid(target_track, filter_eq_ui127_to_db(clamped));
                return 1U;
            case PARAM_FILTER_EQ_HIGH:
                mixer_set_track_filter_eq_high(target_track, filter_eq_ui127_to_db(clamped));
                return 1U;
            case PARAM_FILTER_DRIVE:
                apply_filter_drive_runtime(target_track, clamped);
                if (state != NULL)
                {
                    tmp_state = *state;
                    tmp_state.drive = clamp_value(clamped, 0.0f, 127.0f);
                    apply_filter_crunch_insert_runtime(target_track, &tmp_state);
                }
                return 1U;
            case PARAM_FILTER_DECIMATOR_BITS:
                apply_filter_decimator_bits_runtime(target_track, clamped);
                if (state != NULL)
                {
                    tmp_state = *state;
                    tmp_state.decimator_bits = clamp_value(clamped, 0.0f, 127.0f);
                    apply_filter_crunch_insert_runtime(target_track, &tmp_state);
                }
                return 1U;
            case PARAM_FILTER_DECIMATOR_RATE:
                apply_filter_decimator_rate_runtime(target_track, clamped);
                if (state != NULL)
                {
                    tmp_state = *state;
                    tmp_state.decimator_rate = clamp_value(clamped, 0.0f, 127.0f);
                    apply_filter_crunch_insert_runtime(target_track, &tmp_state);
                }
                return 1U;
            case PARAM_FILTER_DECIMATOR_RATE2:
                apply_filter_decimator_rate2_runtime(target_track, clamped);
                if (state != NULL)
                {
                    tmp_state = *state;
                    tmp_state.decimator_rate2 = clamp_value(clamped, 0.0f, 127.0f);
                    apply_filter_crunch_insert_runtime(target_track, &tmp_state);
                }
                return 1U;
            default:
                return 0U;
        }
    }

    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE) || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX))
    {
        return param_runtime_apply_track(track, id, clamped);
    }

    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_BUFFER)
    {
        return param_runtime_apply_buffer_track(track, id, clamped);
    }

    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_COLORS)
    {
        const uint8_t applied = param_runtime_apply_colors_track(track, id, clamped);
        if (applied != 0U)
        {
            param_runtime_cache_set(track, id, clamped);
        }
        return applied;
    }

    return 0U;
}

static void param_registry_capture_mix_targets(uint8_t *out_mix_tracks)
{
    if (out_mix_tracks == NULL)
    {
        return;
    }

    track_runtime_refresh_all();
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        out_mix_tracks[track] = FILTER_RUNTIME_REBIND_NONE;

        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx == NULL)
                || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
                || (ctx->mix_track_id >= MIXER_MAX_TRACKS))
        {
            continue;
        }

        out_mix_tracks[track] = ctx->mix_track_id;
    }
}

static void param_registry_mark_runtime_global_dirty(void)
{
    track_runtime_invalidate_all();
}

static void param_registry_reapply_lane_bound_runtime_for_changed_tracks(const uint8_t *previous_mix_tracks)
{
    static const param_id_t k_lane_bound_params[] = {
        PARAM_FILTER_TYPE,
        PARAM_FILTER_CUTOFF,
        PARAM_FILTER_RESONANCE,
        PARAM_FILTER_EG_AMT,
        PARAM_FILTER_ATTACK,
        PARAM_FILTER_DECAY,
        PARAM_FILTER_SUSTAIN,
        PARAM_FILTER_RELEASE,
        PARAM_FILTER_KEYTRK,
        PARAM_FILTER_ENVRST,
        PARAM_FILTER_ENVDLY,
        PARAM_FILTER_EQ_LOW,
        PARAM_FILTER_EQ_MID,
        PARAM_FILTER_EQ_HIGH,
        PARAM_FILTER_DRIVE,
        PARAM_FILTER_DECIMATOR_BITS,
        PARAM_FILTER_DECIMATOR_RATE,
        PARAM_FILTER_DECIMATOR_RATE2,
        PARAM_MIX_LEVEL,
        PARAM_MIX_PAN,
        PARAM_MIX_SEND1,
        PARAM_MIX_SEND2,
        PARAM_MIX_MUTE,
        PARAM_HYBRID_GATE,
        PARAM_VCA_ATTACK,
        PARAM_VCA_DECAY,
        PARAM_VCA_SUSTAIN,
        PARAM_VCA_RELEASE
    };

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        if (previous_mix_tracks != NULL)
        {
            uint8_t mix_track = FILTER_RUNTIME_REBIND_NONE;
            const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
            if ((ctx != NULL)
                    && (ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
                    && (ctx->mix_track_id < MIXER_MAX_TRACKS))
            {
                mix_track = ctx->mix_track_id;
            }

            if (previous_mix_tracks[track] == mix_track)
            {
                continue;
            }
        }

        for (uint8_t i = 0U; i < (uint8_t)(sizeof(k_lane_bound_params) / sizeof(k_lane_bound_params[0])); ++i)
        {
            float value = 0.0f;
            if (param_registry_get_track_value(k_lane_bound_params[i], track, &value) == 0U)
            {
                continue;
            }

            (void)param_registry_apply_track_value(k_lane_bound_params[i], track, value);
        }
    }
}

static void param_registry_rebind_lane_runtime(const uint8_t *previous_mix_tracks)
{
    uint8_t next_mix_tracks[SEQ_TRACK_COUNT];

    if (previous_mix_tracks == NULL)
    {
        return;
    }

    param_registry_capture_mix_targets(next_mix_tracks);
    mixer_rebind_track_states(previous_mix_tracks, next_mix_tracks, SEQ_TRACK_COUNT);
    param_registry_mark_runtime_global_dirty();
}

uint8_t param_registry_apply_track_value(param_id_t id, uint8_t track, float value)
{
    if (id >= PARAM_COUNT)
    {
        return 0U;
    }

    const param_desc_t *const desc = &param_registry[id];
    const float clamped = clamp_value(value, desc->min, desc->max);

    {
        uint8_t lfo_index = 0U;
        mod_lfo_param_t lfo_param = MOD_LFO_PARAM_DEST;
        if (param_lfo_map(id, &lfo_index, &lfo_param) != 0U)
        {
            return mod_lfo_v1_set_track_param(track, lfo_index, lfo_param, clamped);
        }
    }

    uint32_t target_track = 0U;
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(track);
    switch (id)
    {
        case PARAM_FILTER_DRIVE:
        case PARAM_FILTER_DECIMATOR_BITS:
        case PARAM_FILTER_DECIMATOR_RATE:
        case PARAM_FILTER_DECIMATOR_RATE2:
            if (resolve_filter_drive_target_track_for_ui_track(track, &target_track) == 0U)
            {
                SEQ_BIND_LOG("[SEQ][REG][APPLY] tr=%u param=%u no_drive_target ui_active=%u\r\n",
                             (unsigned)track,
                             (unsigned)id,
                             (unsigned)ui_get_active_track());
                return 0U;
            }
            if (state == NULL)
            {
                return 0U;
            }
            break;

        case PARAM_FILTER_TYPE:
        case PARAM_FILTER_CUTOFF:
        case PARAM_FILTER_RESONANCE:
        case PARAM_FILTER_EG_AMT:
        case PARAM_FILTER_ATTACK:
        case PARAM_FILTER_DECAY:
        case PARAM_FILTER_SUSTAIN:
        case PARAM_FILTER_RELEASE:
        case PARAM_FILTER_KEYTRK:
        case PARAM_FILTER_ENVRST:
        case PARAM_FILTER_ENVDLY:
        case PARAM_FILTER_EQ_LOW:
        case PARAM_FILTER_EQ_MID:
        case PARAM_FILTER_EQ_HIGH:
            if (resolve_filter_target_track_for_ui_track(track, &target_track) == 0U)
            {
                SEQ_BIND_LOG("[SEQ][REG][APPLY] tr=%u param=%u no_target ui_active=%u\r\n",
                             (unsigned)track,
                             (unsigned)id,
                             (unsigned)ui_get_active_track());
                return 0U;
            }
            if (state == NULL)
            {
                return 0U;
            }
            break;

        default:
        {
            const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
            if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_NONE)
                    && (rule.status != TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
            {
                if (track >= SEQ_TRACK_COUNT)
                {
                    return 0U;
                }

                if (g_param_registry_batch_depth == 0U)
                {
                    track_runtime_refresh_track(track);
                }
                const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
                if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
                {
                    SEQ_BIND_LOG("[SEQ][REG][APPLY] tr=%u param=%u tone_blocked bind=%u reason=%u\r\n",
                                 (unsigned)track,
                                 (unsigned)id,
                                 (unsigned)((ctx != NULL) ? ctx->bind_state : 0xFFU),
                                 (unsigned)((ctx != NULL) ? ctx->bind_reason : 0xFFU));
                    return 0U;
                }

                if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
                {
                    if (id == PARAM_MIDI_PROGRAM)
                    {
                        if (param_track_supports_midi_tone(ctx) == 0U)
                        {
                            return 0U;
                        }
                        param_runtime_cache_set(track, id, clamped);
                        mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
                        seq_runtime_on_midi_program_live_change(track, clamped);
                        track_runtime_invalidate_track(track);
                        return 1U;
                    }

                    if (param_is_midi_cc_id(id) != 0U)
                    {
                        if (param_track_supports_midi_tone(ctx) == 0U)
                        {
                            return 0U;
                        }
                        const uint8_t cc_number = param_midi_cc_number_from_id(id);
                        const uint8_t cc_value = (uint8_t)(clamp_value(clamped, 0.0f, 127.0f) + 0.5f);
                        const uint8_t channel_1_16 = ui_get_track_midi_channel(track);
                        const uint8_t channel = (uint8_t)((channel_1_16 > 0U) ? (channel_1_16 - 1U) : 0U);
                        midi_cc(MIDI_DEST_BOTH, channel, cc_number, cc_value);
                        param_runtime_cache_set(track, id, clamped);
                        mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
                        track_runtime_invalidate_track(track);
                        return 1U;
                    }

                    if (param_runtime_apply_track(track, id, clamped) == 0U)
                    {
                        SEQ_BIND_LOG("[SEQ][REG][APPLY] tr=%u param=%u tone_unsupported engine=%u inst=%u\r\n",
                                     (unsigned)track,
                                     (unsigned)id,
                                     (unsigned)ctx->engine,
                                     (unsigned)ctx->instance_id);
                        return 0U;
                    }

                    track_runtime_invalidate_track(track);
                }
                else if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
                {
                    if (param_runtime_apply_track(track, id, clamped) == 0U)
                    {
                        SEQ_BIND_LOG("[SEQ][REG][APPLY] tr=%u param=%u mix_unsupported engine=%u mix=%u\r\n",
                                     (unsigned)track,
                                     (unsigned)id,
                                     (unsigned)ctx->engine,
                                     (unsigned)ctx->mix_track_id);
                        return 0U;
                    }

                    param_runtime_cache_set(track, id, clamped);
                    track_runtime_invalidate_track(track);
                }
                else if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_COLORS)
                {
                    (void)param_runtime_apply_colors_track(track, id, clamped);
                    param_runtime_cache_set(track, id, clamped);
                }
                else if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_BUFFER)
                {
                    if (param_runtime_apply_buffer_track(track, id, clamped) == 0U)
                    {
                        SEQ_BIND_LOG("[SEQ][REG][APPLY] tr=%u param=%u buffer_unsupported engine=%u inst=%u\r\n",
                                     (unsigned)track,
                                     (unsigned)id,
                                     (unsigned)ctx->engine,
                                     (unsigned)ctx->instance_id);
                        return 0U;
                    }

                    track_runtime_invalidate_track(track);
                }
                else
                {
                    param_runtime_cache_set(track, id, clamped);
                }

                SEQ_BIND_LOG("[SEQ][REG][APPLY] tr=%u param=%u domain=%u engine=%u inst=%u\r\n",
                             (unsigned)track,
                             (unsigned)id,
                             (unsigned)rule.domain,
                             (unsigned)ctx->engine,
                             (unsigned)ctx->instance_id);
                mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
                return 1U;
            }

            SEQ_BIND_LOG("[SEQ][REG][APPLY] tr=%u param=%u global_apply ui_active=%u\r\n",
                         (unsigned)track,
                         (unsigned)id,
                         (unsigned)ui_get_active_track());
            param_set(id, clamped);
            return 1U;
        }
    }

    SEQ_BIND_LOG("[SEQ][REG][APPLY] tr=%u param=%u -> target=%u ui_active=%u v=%.3f\r\n",
                 (unsigned)track,
                 (unsigned)id,
                 (unsigned)target_track,
                 (unsigned)ui_get_active_track(),
                 (double)clamped);

    switch (id)
    {
        case PARAM_FILTER_TYPE:
            mixer_set_track_filter_type(target_track, (mixer_track_filter_type_t)((uint32_t)(clamp_value(clamped, 0.0f, 4.0f) + 0.5f)));
            state->type = clamp_value(clamped, 0.0f, 4.0f);
            mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
            return 1U;
        case PARAM_FILTER_CUTOFF:
            mixer_set_track_filter_cutoff(target_track, filter_ui127_to_cutoff_hz(clamped));
            state->cutoff = filter_ui127_clamp(clamped);
            mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
            return 1U;
        case PARAM_FILTER_RESONANCE:
            mixer_set_track_filter_resonance(target_track, filter_ui127_to_resonance(clamped));
            state->resonance = filter_ui127_clamp(clamped);
            mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
            return 1U;
        case PARAM_FILTER_EG_AMT:
            mixer_set_track_filter_eg_amount(target_track, filter_ui127_to_eg_amount(clamped));
            state->eg_amount = filter_ui127_clamp(clamped);
            mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
            return 1U;
        case PARAM_FILTER_ATTACK:
            mixer_set_track_filter_attack(target_track, filter_ui127_to_attack_s(clamped));
            state->attack = filter_ui127_clamp(clamped);
            mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
            return 1U;
        case PARAM_FILTER_DECAY:
            mixer_set_track_filter_decay(target_track, filter_ui127_to_decay_s(clamped));
            state->decay = filter_ui127_clamp(clamped);
            mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
            return 1U;
        case PARAM_FILTER_SUSTAIN:
            mixer_set_track_filter_sustain(target_track, filter_ui127_to_sustain(clamped));
            state->sustain = filter_ui127_clamp(clamped);
            mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
            return 1U;
        case PARAM_FILTER_RELEASE:
            mixer_set_track_filter_release(target_track, filter_ui127_to_release_s(clamped));
            state->release = filter_ui127_clamp(clamped);
            mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
            return 1U;
        case PARAM_FILTER_KEYTRK:
            mixer_set_track_filter_keytrack(target_track, filter_ui127_to_keytrack(clamped));
            state->keytrack = filter_ui127_clamp(clamped);
            mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
            return 1U;
        case PARAM_FILTER_ENVRST:
            state->env_reset = filter_ui127_to_bool(clamped) ? 1.0f : 0.0f;
            mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
            return 1U;
        case PARAM_FILTER_ENVDLY:
            state->env_delay = filter_ui127_clamp(clamped);
            mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
            return 1U;
        case PARAM_FILTER_EQ_LOW:
            mixer_set_track_filter_eq_low(target_track, filter_eq_ui127_to_db(clamped));
            state->eq_low = filter_ui127_clamp(clamped);
            mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
            return 1U;
        case PARAM_FILTER_EQ_MID:
            mixer_set_track_filter_eq_mid(target_track, filter_eq_ui127_to_db(clamped));
            state->eq_mid = filter_ui127_clamp(clamped);
            mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
            return 1U;
        case PARAM_FILTER_EQ_HIGH:
            mixer_set_track_filter_eq_high(target_track, filter_eq_ui127_to_db(clamped));
            state->eq_high = filter_ui127_clamp(clamped);
            mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
            return 1U;
        case PARAM_FILTER_DRIVE:
            apply_filter_drive_runtime(target_track, clamp_value(clamped, 0.0f, 127.0f));
            state->drive = clamp_value(clamped, 0.0f, 127.0f);
            apply_filter_crunch_insert_runtime(target_track, state);
            mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
            return 1U;
        case PARAM_FILTER_DECIMATOR_BITS:
            apply_filter_decimator_bits_runtime(target_track, clamp_value(clamped, 0.0f, 127.0f));
            state->decimator_bits = clamp_value(clamped, 0.0f, 127.0f);
            apply_filter_crunch_insert_runtime(target_track, state);
            mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
            return 1U;
        case PARAM_FILTER_DECIMATOR_RATE:
            apply_filter_decimator_rate_runtime(target_track, clamp_value(clamped, 0.0f, 127.0f));
            state->decimator_rate = clamp_value(clamped, 0.0f, 127.0f);
            apply_filter_crunch_insert_runtime(target_track, state);
            mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
            return 1U;
        case PARAM_FILTER_DECIMATOR_RATE2:
            apply_filter_decimator_rate2_runtime(target_track, clamp_value(clamped, 0.0f, 127.0f));
            state->decimator_rate2 = clamp_value(clamped, 0.0f, 127.0f);
            apply_filter_crunch_insert_runtime(target_track, state);
            mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
            return 1U;
        default:
            break;
    }

    return 0U;
}

void param_registry_sync_filter_ui_for_active_track(void)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state == NULL)
    {
        return;
    }

    param_store_set_active(PARAM_FILTER_TYPE, state->type);
    param_store_set_active(PARAM_FILTER_CUTOFF, state->cutoff);
    param_store_set_active(PARAM_FILTER_RESONANCE, state->resonance);
    param_store_set_active(PARAM_FILTER_EG_AMT, state->eg_amount);
    param_store_set_active(PARAM_FILTER_ATTACK, state->attack);
    param_store_set_active(PARAM_FILTER_DECAY, state->decay);
    param_store_set_active(PARAM_FILTER_SUSTAIN, state->sustain);
    param_store_set_active(PARAM_FILTER_RELEASE, state->release);
    param_store_set_active(PARAM_FILTER_KEYTRK, state->keytrack);
    param_store_set_active(PARAM_FILTER_ENVRST, state->env_reset);
    param_store_set_active(PARAM_FILTER_ENVDLY, state->env_delay);
    param_store_set_active(PARAM_FILTER_EQ_LOW, state->eq_low);
    param_store_set_active(PARAM_FILTER_EQ_MID, state->eq_mid);
    param_store_set_active(PARAM_FILTER_EQ_HIGH, state->eq_high);
    param_store_set_active(PARAM_FILTER_DRIVE, state->drive);
    param_store_set_active(PARAM_FILTER_DECIMATOR_BITS, state->decimator_bits);
    param_store_set_active(PARAM_FILTER_DECIMATOR_RATE, state->decimator_rate);
    param_store_set_active(PARAM_FILTER_DECIMATOR_RATE2, state->decimator_rate2);

    if (filter_mod_locked_for_active_track() != 0U)
    {
        param_store_set_active(PARAM_FILTER_KEYTRK, 0.0f);
        param_store_set_active(PARAM_FILTER_ENVRST, 0.0f);
        param_store_set_active(PARAM_FILTER_ENVDLY, 0.0f);
    }
}

void param_registry_sync_ui_for_active_track(void)
{
    const uint8_t active_track = ui_get_active_track();
    const float seq_length = (float)seq_model_get_track_length(active_track);
    uint8_t track_div = 1U;
    uint8_t track_quant = 0U;
    uint8_t track_swing = 0U;

    for (uint16_t raw_id = 0U; raw_id < (uint16_t)PARAM_COUNT; ++raw_id)
    {
        const param_id_t id = (param_id_t)raw_id;
        const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
        if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_NONE)
                || (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
                || (param_is_filter_ui_param(id) != 0U))
        {
            continue;
        }

        float value = 0.0f;
        if (param_registry_get_track_value(id, active_track, &value) != 0U)
        {
            param_store_set_active(id, value);
        }
    }

    param_store_set_active(PARAM_SEQ_LENGTH, seq_length);
    if (seq_runtime_get_track_div(active_track, &track_div) != 0U)
    {
        param_store_set_active(PARAM_SEQ_DIV, seq_div_runtime_to_ui(track_div));
    }
    if (seq_runtime_get_track_quant(active_track, &track_quant) != 0U)
    {
        param_store_set_active(PARAM_SEQ_QUANT, (float)track_quant);
    }
    if (seq_runtime_get_track_swing(active_track, &track_swing) != 0U)
    {
        param_store_set_active(PARAM_SEQ_SWING, (float)track_swing);
    }

    param_registry_sync_filter_ui_for_active_track();
}

/*
 * Variante FILTER audio:
 * - le runtime audio n'expose plus que Off / EQ3 / SVF Peaks multimode.
 * - le système de paramètres conserve un jeu global `PARAM_FILTER_*`.
 * - la cible DSP est résolue dynamiquement depuis le contexte UI actif.
 */
static void apply_filter_type(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->type = clamp_value(v, 0.0f, 4.0f);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }

    mixer_set_track_filter_type(target_track, (mixer_track_filter_type_t)((uint32_t)(clamp_value(v, 0.0f, 4.0f) + 0.5f)));
}

static void apply_filter_cutoff(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->cutoff = filter_ui127_clamp(v);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    mixer_set_track_filter_cutoff(target_track, filter_ui127_to_cutoff_hz(v));
}

static void apply_filter_resonance(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->resonance = filter_ui127_clamp(v);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    mixer_set_track_filter_resonance(target_track, filter_ui127_to_resonance(v));
}

static void apply_filter_eg_amount(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->eg_amount = filter_ui127_clamp(v);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    mixer_set_track_filter_eg_amount(target_track, filter_ui127_to_eg_amount(v));
}

static void apply_filter_attack(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->attack = filter_ui127_clamp(v);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    mixer_set_track_filter_attack(target_track, filter_ui127_to_attack_s(v));
}

static void apply_filter_decay(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->decay = filter_ui127_clamp(v);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    mixer_set_track_filter_decay(target_track, filter_ui127_to_decay_s(v));
}

static void apply_filter_sustain(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->sustain = filter_ui127_clamp(v);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    mixer_set_track_filter_sustain(target_track, filter_ui127_to_sustain(v));
}

static void apply_filter_release(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->release = filter_ui127_clamp(v);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    mixer_set_track_filter_release(target_track, filter_ui127_to_release_s(v));
}

static void apply_filter_keytrack(float v)
{
    const uint8_t active_track = ui_get_active_track();
    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    if (filter_mod_locked_for_active_track() != 0U)
    {
        mixer_set_track_filter_keytrack(target_track, 0.0f);
        param_store_set_active(PARAM_FILTER_KEYTRK, 0.0f);
        return;
    }

    mixer_set_track_filter_keytrack(target_track, filter_ui127_to_keytrack(v));
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->keytrack = filter_ui127_clamp(v);
    }
}

static void apply_filter_env_reset(float v)
{
    if (filter_mod_locked_for_active_track() != 0U)
    {
        param_store_set_active(PARAM_FILTER_ENVRST, 0.0f);
        return;
    }

    filter_ui_state_t *state = resolve_filter_ui_state_for_track(ui_get_active_track());
    if (state != NULL)
    {
        state->env_reset = filter_ui127_to_bool(v) ? 1.0f : 0.0f;
    }
}

static void apply_filter_env_delay(float v)
{
    if (filter_mod_locked_for_active_track() != 0U)
    {
        param_store_set_active(PARAM_FILTER_ENVDLY, 0.0f);
        return;
    }

    filter_ui_state_t *state = resolve_filter_ui_state_for_track(ui_get_active_track());
    if (state != NULL)
    {
        state->env_delay = filter_ui127_clamp(v);
    }
}

static void apply_filter_eq_low(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->eq_low = filter_ui127_clamp(v);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    mixer_set_track_filter_eq_low(target_track, filter_eq_ui127_to_db(v));
}

static void apply_filter_eq_mid(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->eq_mid = filter_ui127_clamp(v);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    mixer_set_track_filter_eq_mid(target_track, filter_eq_ui127_to_db(v));
}

static void apply_filter_eq_high(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->eq_high = filter_ui127_clamp(v);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    mixer_set_track_filter_eq_high(target_track, filter_eq_ui127_to_db(v));
}

static void apply_filter_drive(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    const float drive_ui = clamp_value(v, 0.0f, 127.0f);
    if (state != NULL)
    {
        state->drive = drive_ui;
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_drive_target_track_for_ui_track(active_track, &target_track))
    {
        return;
    }

    apply_filter_drive_runtime(target_track, drive_ui);
    if (state != NULL)
    {
        apply_filter_crunch_insert_runtime(target_track, state);
    }
}

static void apply_filter_decimator_bits(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    const float bits_ui = clamp_value(v, 0.0f, 127.0f);
    if (state != NULL)
    {
        state->decimator_bits = bits_ui;
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_drive_target_track_for_ui_track(active_track, &target_track))
    {
        return;
    }

    apply_filter_decimator_bits_runtime(target_track, bits_ui);
    if (state != NULL)
    {
        apply_filter_crunch_insert_runtime(target_track, state);
    }
}

static void apply_filter_decimator_rate(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    const float rate_ui = clamp_value(v, 0.0f, 127.0f);
    if (state != NULL)
    {
        state->decimator_rate = rate_ui;
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_drive_target_track_for_ui_track(active_track, &target_track))
    {
        return;
    }

    apply_filter_decimator_rate_runtime(target_track, rate_ui);
    if (state != NULL)
    {
        apply_filter_crunch_insert_runtime(target_track, state);
    }
}

static void apply_filter_decimator_rate2(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    const float rate_ui = clamp_value(v, 0.0f, 127.0f);
    if (state != NULL)
    {
        state->decimator_rate2 = rate_ui;
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_drive_target_track_for_ui_track(active_track, &target_track))
    {
        return;
    }

    apply_filter_decimator_rate2_runtime(target_track, rate_ui);
    if (state != NULL)
    {
        apply_filter_crunch_insert_runtime(target_track, state);
    }
}

static void apply_monob_filter_type(float v) { monob_synth_set_filter_type((uint8_t)(clamp_value(v, 0.0f, 1.0f) + 0.5f)); }
static void apply_monob_filter_cutoff(float v) { monob_synth_set_filter_cutoff(filter_ui127_to_cutoff_hz(v)); }
static void apply_monob_filter_resonance(float v) { monob_synth_set_filter_resonance(filter_ui127_to_resonance(v)); }
static void apply_monob_filter_eg_amount(float v) { monob_synth_set_filter_eg_amount(filter_ui127_to_eg_amount(v)); }
static void apply_monob_filter_attack(float v) { monob_synth_set_filter_attack(filter_ui127_to_attack_s(v)); }
static void apply_monob_filter_decay(float v) { monob_synth_set_filter_decay(filter_ui127_to_decay_s(v)); }
static void apply_monob_filter_sustain(float v) { monob_synth_set_filter_sustain(filter_ui127_to_sustain(v)); }
static void apply_monob_filter_release(float v) { monob_synth_set_filter_release(filter_ui127_to_release_s(v)); }
static void apply_monob_filter_keytrack(float v) { monob_synth_set_filter_keytrack(filter_ui127_to_keytrack(v)); }
static void apply_monob_filter_env_reset(float v) { monob_synth_set_filter_env_reset(filter_ui127_to_bool(v)); }
static void apply_monob_filter_env_delay(float v) { monob_synth_set_filter_env_delay(filter_ui127_to_env_delay_s(v)); }

static void apply_monob_osc1_wave(float v) { apply_tone_live_track(PARAM_MONOB_OSC1_WAVE, v); }
static void apply_monob_osc2_wave(float v) { apply_tone_live_track(PARAM_MONOB_OSC2_WAVE, v); }
static void apply_monob_osc3_wave(float v) { apply_tone_live_track(PARAM_MONOB_OSC3_WAVE, v); }
static void apply_monob_sub_wave(float v) { apply_tone_live_track(PARAM_MONOB_SUB_WAVE, v); }
static void apply_monob_osc1_range(float v) { apply_tone_live_track(PARAM_MONOB_OSC1_RANGE, v); }
static void apply_monob_osc2_range(float v) { apply_tone_live_track(PARAM_MONOB_OSC2_RANGE, v); }
static void apply_monob_osc3_range(float v) { apply_tone_live_track(PARAM_MONOB_OSC3_RANGE, v); }
static void apply_monob_sub_octave(float v) { apply_tone_live_track(PARAM_MONOB_SUB_OCTAVE, v); }
static void apply_monob_osc1_detune(float v) { apply_tone_live_track(PARAM_MONOB_OSC1_DETUNE, v); }
static void apply_monob_osc2_detune(float v) { apply_tone_live_track(PARAM_MONOB_OSC2_DETUNE, v); }
static void apply_monob_osc3_detune(float v) { apply_tone_live_track(PARAM_MONOB_OSC3_DETUNE, v); }
static void apply_monob_osc1_mix(float v) { apply_tone_live_track(PARAM_MONOB_OSC1_MIX, v); }
static void apply_monob_osc2_mix(float v) { apply_tone_live_track(PARAM_MONOB_OSC2_MIX, v); }
static void apply_monob_osc3_mix(float v) { apply_tone_live_track(PARAM_MONOB_OSC3_MIX, v); }
static void apply_monob_sub_mix(float v) { apply_tone_live_track(PARAM_MONOB_SUB_MIX, v); }
static void apply_lfo1_dest(float v) { (void)mod_lfo_v1_set_track_param(ui_get_active_track(), 0U, MOD_LFO_PARAM_DEST, v); }
static void apply_lfo1_rate(float v) { (void)mod_lfo_v1_set_track_param(ui_get_active_track(), 0U, MOD_LFO_PARAM_RATE, v); }
static void apply_lfo1_depth(float v) { (void)mod_lfo_v1_set_track_param(ui_get_active_track(), 0U, MOD_LFO_PARAM_DEPTH, v); }
static void apply_lfo1_shape(float v) { (void)mod_lfo_v1_set_track_param(ui_get_active_track(), 0U, MOD_LFO_PARAM_SHAPE, v); }
static void apply_lfo2_dest(float v) { (void)mod_lfo_v1_set_track_param(ui_get_active_track(), 1U, MOD_LFO_PARAM_DEST, v); }
static void apply_lfo2_rate(float v) { (void)mod_lfo_v1_set_track_param(ui_get_active_track(), 1U, MOD_LFO_PARAM_RATE, v); }
static void apply_lfo2_depth(float v) { (void)mod_lfo_v1_set_track_param(ui_get_active_track(), 1U, MOD_LFO_PARAM_DEPTH, v); }
static void apply_lfo2_shape(float v) { (void)mod_lfo_v1_set_track_param(ui_get_active_track(), 1U, MOD_LFO_PARAM_SHAPE, v); }

static void param_registry_neutralize_filter_runtime_if_invalid(uint8_t track)
{
    uint8_t filter_track = 0U;
    uint8_t mix_track = 0U;

    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    track_runtime_refresh_track(track);
    if (track_runtime_resolve_filter_target_track(track, &filter_track) != 0U)
    {
        return;
    }

    if (track_runtime_is_audio_routable(track) == 0U)
    {
        return;
    }

    if (track_runtime_get_mix_target_track(track, &mix_track) == 0U)
    {
        return;
    }

    mixer_set_track_filter_type((uint32_t)mix_track, MIXER_TRACK_FILTER_OFF);
    mixer_track_filter_all_notes_off((uint32_t)mix_track);
}

static void param_registry_neutralize_vca_runtime_if_invalid(uint8_t track)
{
    uint8_t mix_track = 0U;

    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    track_runtime_refresh_track(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx != NULL)
            && (ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
            && ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
                || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_DRUM)
                || ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_INPUT)
                    && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_HYBRID))))
    {
        return;
    }

    if (track_runtime_is_audio_routable(track) == 0U)
    {
        return;
    }

    if (track_runtime_get_mix_target_track(track, &mix_track) == 0U)
    {
        return;
    }

    mixer_track_vca_all_notes_off((uint32_t)mix_track);
    mixer_set_track_vca_enabled((uint32_t)mix_track, 0U);
}

static void apply_cfg_track(float v)
{
    const uint8_t active_track = ui_get_active_track();
    uint8_t previous_mix_tracks[SEQ_TRACK_COUNT];
    const ui_track_family_t previous_family = ui_get_track_family(active_track);
    const ui_track_type_t previous_type = ui_get_track_type(active_track);
    const ui_track_family_t requested_family = (ui_track_family_t)((uint8_t)(clamp_value(v, 0.0f, (float)((uint8_t)UI_TRACK_FAMILY_COUNT - 1U)) + 0.5f));

    param_registry_capture_mix_targets(previous_mix_tracks);

    if (ui_set_track_family(active_track, requested_family) == false)
    {
        param_store_set_active(PARAM_CFG_TRACK, (float)ui_get_track_family(active_track));
        param_store_set_active(PARAM_CFG_TRACK_TYPE, (float)ui_get_track_type_index_for_family(ui_get_track_family(active_track), ui_get_track_type(active_track)));
        return;
    }

    param_store_set_active(PARAM_CFG_TRACK, (float)ui_get_track_family(active_track));
    param_store_set_active(PARAM_CFG_TRACK_TYPE, (float)ui_get_track_type_index_for_family(ui_get_track_family(active_track), ui_get_track_type(active_track)));
    if ((ui_get_track_family(active_track) == previous_family)
            && (ui_get_track_type(active_track) == previous_type))
    {
        return;
    }

    param_registry_rebind_lane_runtime(previous_mix_tracks);
    param_registry_neutralize_filter_runtime_if_invalid(active_track);
    param_registry_neutralize_vca_runtime_if_invalid(active_track);
    param_registry_reapply_lane_bound_runtime_for_changed_tracks(previous_mix_tracks);
    param_registry_mark_runtime_global_dirty();
}

static void apply_cfg_track_type(float v)
{
    g_param_cfg_track_type_apply_stage = 1U;
    const uint8_t active_track = ui_get_active_track();
    uint8_t previous_mix_tracks[SEQ_TRACK_COUNT];
    const ui_track_family_t active_family = ui_get_track_family(active_track);
    const ui_track_type_t previous_type = ui_get_track_type(active_track);
    const uint8_t requested_index = (uint8_t)(clamp_value(v, 0.0f, (float)((uint8_t)UI_TRACK_TYPE_COUNT - 1U)) + 0.5f);
    const ui_track_type_t requested_type = ui_get_track_type_from_family_index(active_family, requested_index);

    param_registry_capture_mix_targets(previous_mix_tracks);
    g_param_cfg_track_type_apply_stage = 2U;

    if (ui_set_track_type(active_track, requested_type) == false)
    {
        g_param_cfg_track_type_apply_stage = 3U;
        param_store_set_active(PARAM_CFG_TRACK_TYPE, (float)ui_get_track_type_index_for_family(active_family, ui_get_track_type(active_track)));
        return;
    }

    param_store_set_active(PARAM_CFG_TRACK_TYPE, (float)ui_get_track_type_index_for_family(active_family, ui_get_track_type(active_track)));
    if (ui_get_track_type(active_track) == previous_type)
    {
        g_param_cfg_track_type_apply_stage = 4U;
        return;
    }

    param_registry_rebind_lane_runtime(previous_mix_tracks);
    param_registry_neutralize_filter_runtime_if_invalid(active_track);
    param_registry_neutralize_vca_runtime_if_invalid(active_track);
    param_registry_reapply_lane_bound_runtime_for_changed_tracks(previous_mix_tracks);
    param_registry_mark_runtime_global_dirty();
    g_param_cfg_track_type_apply_stage = 4U;
}

static void apply_cfg_midi_ch(float v)
{
    const uint8_t active_track = ui_get_active_track();
    const uint8_t requested_channel = (uint8_t)(clamp_value(v, 1.0f, 16.0f) + 0.5f);
    (void)ui_set_track_midi_channel(active_track, requested_channel);
    param_store_set_active(PARAM_CFG_MIDI_CH, (float)ui_get_track_midi_channel(active_track));
}

static void apply_cfg_midi_src(float v)
{
    const uint8_t active_track = ui_get_active_track();
    const ui_track_midi_source_t requested_source =
            (ui_track_midi_source_t)((uint8_t)(clamp_value(v, 0.0f, 2.0f) + 0.5f));
    (void)ui_set_track_midi_source(active_track, requested_source);
    param_store_set_active(PARAM_CFG_MIDI_SRC, (float)ui_get_track_midi_source(active_track));
}

static void apply_cfg_rec(float v)
{
    uint8_t mode = (uint8_t)(clamp_value(v, 0.0f, 3.0f) + 0.5f);
    seq_runtime_set_rec_count_in_mode(mode);
    mode = seq_runtime_get_rec_count_in_mode();
    param_store_set_active(PARAM_CFG_REC, (float)mode);
}

static void apply_cfg_tempo(float v)
{
    uint32_t bpm_milli = (uint32_t)(clamp_value(v, 40.0f, 300.0f) * 1000.0f + 0.5f);
    seq_runtime_set_tempo_bpm_milli(bpm_milli);
    bpm_milli = seq_runtime_get_tempo_bpm_milli();
    param_store_set_active(PARAM_CFG_TEMPO, (float)bpm_milli / 1000.0f);
}

static void apply_cfg_sync(float v)
{
    const uint8_t mode = (uint8_t)(clamp_value(v, 0.0f, 2.0f) + 0.5f);
    seq_clock_src_t source = SEQ_CLOCK_SRC_INTERNAL;
    if (mode == 1U)
    {
        source = SEQ_CLOCK_SRC_EXTERNAL_MIDI;
    }
    else if (mode == 2U)
    {
        source = SEQ_CLOCK_SRC_EXTERNAL_USB;
    }

    seq_runtime_set_clock_source(source);

    uint8_t synced_mode = 0U;
    switch (seq_runtime_get_clock_source())
    {
        case SEQ_CLOCK_SRC_EXTERNAL_MIDI:
            synced_mode = 1U;
            break;
        case SEQ_CLOCK_SRC_EXTERNAL_USB:
            synced_mode = 2U;
            break;
        case SEQ_CLOCK_SRC_INTERNAL:
        default:
            synced_mode = 0U;
            break;
    }
    param_store_set_active(PARAM_CFG_SYNC, (float)synced_mode);
}

static void apply_cfg_rec_len(float v)
{
    uint8_t mode = (uint8_t)(clamp_value(v, 0.0f, 1.0f) + 0.5f);
    seq_runtime_set_rec_len_mode(mode);
    mode = seq_runtime_get_rec_len_mode();
    param_store_set_active(PARAM_CFG_REC_LEN, (float)mode);
}

static void apply_seq_length(float v)
{
    const uint8_t track = ui_get_active_track();
    (void)undo_v1_capture_before_edit(0U);
    seq_model_set_track_length(track, (uint8_t)(v + 0.5f));
    seq_runtime_on_track_length_changed(track);
}

static uint8_t seq_div_ui_to_runtime(float v)
{
    const uint8_t ui = (uint8_t)(clamp_value(v, 0.0f, 3.0f) + 0.5f);
    switch (ui)
    {
        case 1U: return 2U;
        case 2U: return 4U;
        case 3U: return 8U;
        case 0U:
        default: return 1U;
    }
}

static float seq_div_runtime_to_ui(uint8_t runtime_div)
{
    switch (runtime_div)
    {
        case 2U: return 1.0f;
        case 4U: return 2.0f;
        case 8U: return 3.0f;
        case 1U:
        default: return 0.0f;
    }
}

static void apply_seq_div(float v)
{
    seq_runtime_set_track_div(ui_get_active_track(), seq_div_ui_to_runtime(v));
}

static void apply_seq_quant(float v)
{
    seq_runtime_set_track_quant(ui_get_active_track(), (uint8_t)(v + 0.5f));
}

static void apply_seq_swing(float v)
{
    seq_runtime_set_track_swing(ui_get_active_track(), (uint8_t)(v + 0.5f));
}

static void apply_kbd_root(float v) { keyboard_runtime_set_root((uint8_t)(clamp_value(v, 0.0f, 11.0f) + 0.5f)); }
static void apply_kbd_scale(float v) { keyboard_runtime_set_scale((uint8_t)(clamp_value(v, 0.0f, (float)KBD_SCALE_CHROMATIC) + 0.5f)); }
static void apply_kbd_omnichord(float v) { keyboard_runtime_set_omnichord(v >= 0.5f); }
static void apply_kbd_note_order(float v) { keyboard_runtime_set_note_order((v >= 0.5f) ? NOTE_ORDER_FIFTHS : NOTE_ORDER_NATURAL); }
static void apply_kbd_chord_override(float v) { keyboard_runtime_set_chord_override(v >= 0.5f); }
static void apply_arp_hold(float v) { keyboard_runtime_set_arp_hold(v >= 0.5f); }
static void apply_arp_rate(float v) { keyboard_runtime_set_arp_rate((uint8_t)(clamp_value(v, 0.0f, 7.0f) + 0.5f)); }
static void apply_arp_oct(float v) { keyboard_runtime_set_arp_oct((uint8_t)(clamp_value(v, 0.0f, 4.0f) + 0.5f)); }
static void apply_arp_pattern(float v) { keyboard_runtime_set_arp_pattern((uint8_t)(clamp_value(v, 0.0f, 4.0f) + 0.5f)); }
static void apply_arp_gate(float v) { keyboard_runtime_set_arp_gate((uint8_t)(clamp_value(v, 1.0f, 127.0f) + 0.5f)); }
static void apply_arp_swing(float v) { keyboard_runtime_set_arp_swing((uint8_t)(clamp_value(v, 0.0f, 100.0f) + 0.5f)); }
static void apply_arp_accent(float v) { keyboard_runtime_set_arp_accent((uint8_t)(clamp_value(v, 0.0f, 3.0f) + 0.5f)); }
static void apply_arp_vel_acc(float v) { keyboard_runtime_set_arp_vel_acc((uint8_t)(clamp_value(v, 0.0f, 64.0f) + 0.5f)); }
static void apply_arp_strum(float v) { keyboard_runtime_set_arp_strum((uint8_t)(clamp_value(v, 0.0f, 4.0f) + 0.5f)); }
static void apply_arp_offset(float v) { keyboard_runtime_set_arp_offset((int8_t)(clamp_value(v, -24.0f, 24.0f) + ((v >= 0.0f) ? 0.5f : -0.5f))); }
static void apply_arp_trans(float v) { keyboard_runtime_set_arp_transpose((int8_t)(clamp_value(v, -24.0f, 24.0f) + ((v >= 0.0f) ? 0.5f : -0.5f))); }
static void apply_arp_spread(float v) { keyboard_runtime_set_arp_spread((uint8_t)(clamp_value(v, 0.0f, 12.0f) + 0.5f)); }
static void apply_arp_dir(float v) { keyboard_runtime_set_arp_dir((uint8_t)(clamp_value(v, 0.0f, 2.0f) + 0.5f)); }
static void apply_arp_sync(float v) { keyboard_runtime_set_arp_sync((uint8_t)(clamp_value(v, 0.0f, 2.0f) + 0.5f)); }

/*
 * Master gain authority is hardware Pot 5 (see brick6_update_master_from_pot5).
 * Keep PARAM_MASTER_GAIN inert to avoid any parallel control path rewriting
 * the final output gain after Pot 5 updates.
 */
static void apply_master_gain(float v) { (void)v; }
static void apply_post_gain(float v) { audio_float_set_postgain(v); }
static void apply_output_comp(float v) { audio_float_set_output_compensation(v); }


static void apply_bus_comp_threshold(float v) { audio_float_set_bus_comp_threshold_db(v); }
static void apply_bus_comp_ratio(float v) { audio_float_set_bus_comp_ratio(v); }
static void apply_bus_comp_attack_index(float v) { audio_float_set_bus_comp_attack_index((uint8_t)v); }
static void apply_bus_comp_release_index(float v) { audio_float_set_bus_comp_release_index((uint8_t)v); }
static void apply_bus_comp_makeup(float v) { audio_float_set_bus_comp_makeup_db(v); }
static void apply_bus_comp_auto_makeup(float v) { audio_float_set_bus_comp_auto_makeup((v >= 0.5f) ? 1U : 0U); }

/**
 * @brief Point d'entrée apply_daisy_threshold.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_daisy_threshold.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_daisy_threshold(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_threshold_db(comp, v);
}

/**
 * @brief Point d'entrée apply_daisy_ratio.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_daisy_ratio.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_daisy_ratio(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_ratio(comp, v);
}

/**
 * @brief Point d'entrée apply_daisy_attack.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_daisy_attack.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_daisy_attack(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_attack_s(comp, v);
}

/**
 * @brief Point d'entrée apply_daisy_release.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_daisy_release.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_daisy_release(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_release_s(comp, v);
}

/**
 * @brief Point d'entrée apply_daisy_makeup.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_daisy_makeup.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_daisy_makeup(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_makeup_db(comp, v);
}

/**
 * @brief Point d'entrée apply_daisy_auto_makeup.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_daisy_auto_makeup.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_daisy_auto_makeup(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_auto_makeup(comp, (v >= 0.5f) ? 1U : 0U);
}

/**
 * @brief Point d'entrée apply_daisy_mix.
 *
 * Rôle:
 * - Exécuter le traitement associé à apply_daisy_mix.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void apply_daisy_mix(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_mix(comp, v);
}

#define PARAM_DESC_EX(_id, _name, _type, _min, _max, _step, _default, _display, _unit, _labels, _apply) \
    [(_id)] = {                                                                  \
        .id = (_id),                                                             \
        .name = (_name),                                                         \
        .type = (_type),                                                         \
        .min = (_min),                                                           \
        .max = (_max),                                                           \
        .step = (_step),                                                         \
        .default_value = (_default),                                             \
        .display_type = (_display),                                              \
        .unit = (_unit),                                                         \
        .labels = (_labels),                                                     \
        .apply = (_apply),                                                       \
    }

#define PARAM_DESC(_id, _name, _type, _min, _max, _step, _default, _unit, _apply)                           \
    PARAM_DESC_EX((_id),                                                                                      \
                  (_name),                                                                                    \
                  (_type),                                                                                    \
                  (_min),                                                                                     \
                  (_max),                                                                                     \
                  (_step),                                                                                    \
                  (_default),                                                                                 \
                  (((_type) == PARAM_TYPE_BOOL) ? PARAM_DISPLAY_BOOL                                         \
                                                 : (((_type) == PARAM_TYPE_ENUM) ? PARAM_DISPLAY_ENUM        \
                                                                            : PARAM_DISPLAY_FLOAT)),         \
                  (_unit),                                                                                    \
                  NULL,                                                                                       \
                  (_apply))

static const char *const g_bool_labels[] = {"Off", "On", NULL};
static const char *const g_route_labels[] = {"None", "Master", "Cue", "Both", NULL};
static const char *const g_filter_type_labels[] = {"Off", "EQ3", "LP", "HP", "BP", NULL};
static const char *const g_reverb_type_labels[] = {"Mono", "Stereo", NULL};
static const char *const g_sampler_mode_labels[] = {"Shot", "RevShot", "Loop", "RevLoop", "Slice", "RevSlice", NULL};
static const char *const g_sampler_slice_count_labels[] = {"2", "4", "8", "16", "32", "64", NULL};
static const char *const g_monob_filter_type_labels[] = {"Off", "On", NULL};
static const char *const g_monob_wave_labels[] = {"Off", "Sine", "Square", "Tri", "Saw", NULL};
static const char *const g_monob_range_labels[] = {"16'", "8'", "4'", "2'", NULL};
static const char *const g_monob_sub_octave_labels[] = {"-1", "-2", "-3", "-4", NULL};
static const char *const g_track_family_labels[] = {"Off", "Input1", "Input2", "Input3", "Input4", "Synth", "Drum", "Master", "MIDI", NULL};
static const char *const g_track_midi_source_labels[] = {"INT", "EXT", "ALL", NULL};
static const char *const g_cfg_rec_labels[] = {"Off", "4st", "8st", "16st", NULL};
static const char *const g_cfg_sync_labels[] = {"INT", "MidiEXT", "UsbEXT", NULL};
static const char *const g_cfg_rec_len_labels[] = {"Overdub", "Pattern", NULL};
static const char *const g_seq_div_labels[] = {"OFF", "1/2", "1/4", "1/8", NULL};
static const char *const g_kbd_root_labels[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", NULL};
static const char *const g_kbd_scale_labels[] = {"Major", "NatMin", "Dorian", "Mixoly", "PntMaj", "PntMin", "Chrom", NULL};
static const char *const g_kbd_note_order_labels[] = {"Natural", "Fifths", NULL};
static const char *const g_arp_rate_labels[] = {"1/4", "1/8", "1/16", "1/32", "1/4t", "1/8t", "1/16t", "1/32t", NULL};
static const char *const g_arp_pattern_labels[] = {"Up", "Down", "UpDn", "Rnd", "Chord", NULL};
static const char *const g_arp_accent_labels[] = {"Off", "1st", "Alt", "Rnd", NULL};
static const char *const g_arp_strum_labels[] = {"Off", "Up", "Down", "Alt", "Rnd", NULL};
static const char *const g_arp_dir_labels[] = {"Normal", "PingPong", "RndWalk", NULL};
static const char *const g_arp_sync_labels[] = {"Int", "Clock", "Free", NULL};
static const char *const g_lfo_shape_labels[] = {"Sine", "Triangle", "Saw", "Square", "Random S&H", NULL};
static const char *const g_lfo_rate_labels[] = {"128", "64", "32", "16", "8", "4", "2", "1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64", "1/128", NULL};

const param_desc_t param_registry[PARAM_COUNT] = {
    PARAM_DESC_EX(PARAM_GRAN_DENSITY, "Gran Density", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, apply_gran_density),
    PARAM_DESC(PARAM_GRAN_PITCH, "Gran Pitch", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 0.1f, 0.0f, "st", apply_gran_pitch),
    PARAM_DESC_EX(PARAM_GRAN_MIX, "Gran Mix", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, apply_gran_mix),
    PARAM_DESC_EX(PARAM_GRAN_FREEZE, "Gran Freeze", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_gran_freeze),
    PARAM_DESC_EX(PARAM_GRAN_SPREAD, "Gran Spread", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, apply_gran_spread),
    PARAM_DESC_EX(PARAM_GRAN_STEREO, "Gran Stereo", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "", NULL, apply_gran_stereo),

    PARAM_DESC(PARAM_MIX_TRACK0_GAIN, "T0 Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK1_GAIN, "T1 Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK2_GAIN, "T2 Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK3_GAIN, "T3 Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, "", NULL),

    PARAM_DESC(PARAM_MIX_TRACK0_PAN, "T0 Pan", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK1_PAN, "T1 Pan", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK2_PAN, "T2 Pan", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK3_PAN, "T3 Pan", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, "", NULL),

    PARAM_DESC_EX(PARAM_MIX_TRACK0_MUTE, "Mute", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_MIX_TRACK1_MUTE, "T1 Mute", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_MIX_TRACK2_MUTE, "T2 Mute", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_MIX_TRACK3_MUTE, "T3 Mute", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),

    PARAM_DESC_EX(PARAM_MIX_TRACK0_ROUTE, "T0 Route", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_route_labels, NULL),
    PARAM_DESC_EX(PARAM_MIX_TRACK1_ROUTE, "T1 Route", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_route_labels, NULL),
    PARAM_DESC_EX(PARAM_MIX_TRACK2_ROUTE, "T2 Route", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_route_labels, NULL),
    PARAM_DESC_EX(PARAM_MIX_TRACK3_ROUTE, "T3 Route", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_route_labels, NULL),

    PARAM_DESC(PARAM_MIX_TRACK0_INSERT0, "T0 Insert0", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK0_INSERT1, "T0 Insert1", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK1_INSERT0, "T1 Insert0", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK1_INSERT1, "T1 Insert1", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK2_INSERT0, "T2 Insert0", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK2_INSERT1, "T2 Insert1", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK3_INSERT0, "T3 Insert0", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK3_INSERT1, "T3 Insert1", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", NULL),

    PARAM_DESC(PARAM_MIX_TRACK0_SEND0, "T0 Send0", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK0_SEND1, "T0 Send1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK1_SEND0, "T1 Send0", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK1_SEND1, "T1 Send1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK2_SEND0, "T2 Send0", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK2_SEND1, "T2 Send1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK3_SEND0, "T3 Send0", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_TRACK3_SEND1, "T3 Send1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),

    PARAM_DESC(PARAM_MIX_SEND0_FX, "Send0 FX", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_send0_fx),
    PARAM_DESC(PARAM_MIX_SEND1_FX, "Send1 FX", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_send1_fx),
    PARAM_DESC(PARAM_MIX_LEVEL, "Level", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_PAN, "Pan", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_SEND1, "Send1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_MIX_SEND2, "Send2", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", NULL),

    PARAM_DESC_EX(PARAM_BUS_COMP_THRESHOLD_DB, "BusComp Threshold", PARAM_TYPE_FLOAT, -60.0f, 0.0f, 0.5f, -18.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_bus_comp_threshold),
    PARAM_DESC_EX(PARAM_BUS_COMP_RATIO, "BusComp Ratio", PARAM_TYPE_FLOAT, 1.0f, 20.0f, 0.1f, 2.0f, PARAM_DISPLAY_RATIO, "", NULL, apply_bus_comp_ratio),
    /* DSP side clamps attack index to [0..5], keep registry range aligned for consistent UI. */
    PARAM_DESC(PARAM_BUS_COMP_ATTACK_INDEX, "BusComp Attack", PARAM_TYPE_ENUM, 0.0f, 5.0f, 1.0f, 0.0f, "idx", apply_bus_comp_attack_index),
    /* DSP side clamps release index to [0..4], keep registry range aligned for consistent UI. */
    PARAM_DESC(PARAM_BUS_COMP_RELEASE_INDEX, "BusComp Release", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 0.0f, "idx", apply_bus_comp_release_index),
    PARAM_DESC_EX(PARAM_BUS_COMP_MAKEUP_DB, "BusComp Makeup", PARAM_TYPE_FLOAT, 0.0f, 24.0f, 0.5f, 0.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_bus_comp_makeup),
    PARAM_DESC_EX(PARAM_BUS_COMP_AUTO_MAKEUP, "BusComp AutoMakeup", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_bus_comp_auto_makeup),
    PARAM_DESC_EX(PARAM_BUS_COMP_DRYWET, "BusComp DryWet", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_BUS_COMP_HPF_HZ, "BusComp HPF", PARAM_TYPE_FLOAT, 0.0f, 1000.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "Hz", NULL, NULL),

    PARAM_DESC_EX(PARAM_DAISY_COMP_THRESHOLD_DB, "Daisy Threshold", PARAM_TYPE_FLOAT, -40.0f, 0.0f, 0.5f, -18.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_daisy_threshold),
    PARAM_DESC_EX(PARAM_DAISY_COMP_RATIO, "Daisy Ratio", PARAM_TYPE_FLOAT, 1.0f, 20.0f, 0.1f, 2.0f, PARAM_DISPLAY_RATIO, "", NULL, apply_daisy_ratio),
    PARAM_DESC_EX(PARAM_DAISY_COMP_ATTACK_S, "Daisy Attack", PARAM_TYPE_FLOAT, 0.0001f, 0.5f, 0.0001f, 0.001f, PARAM_DISPLAY_TIME_MS, "s", NULL, apply_daisy_attack),
    PARAM_DESC_EX(PARAM_DAISY_COMP_RELEASE_S, "Daisy Release", PARAM_TYPE_FLOAT, 0.01f, 5.0f, 0.01f, 0.6f, PARAM_DISPLAY_TIME_MS, "s", NULL, apply_daisy_release),
    PARAM_DESC_EX(PARAM_DAISY_COMP_MAKEUP_DB, "Daisy Makeup", PARAM_TYPE_FLOAT, 0.0f, 24.0f, 0.5f, 0.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_daisy_makeup),
    PARAM_DESC_EX(PARAM_DAISY_COMP_AUTO_MAKEUP, "Daisy AutoMakeup", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 1.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_daisy_auto_makeup),
    PARAM_DESC_EX(PARAM_DAISY_COMP_MIX, "Daisy Mix", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_daisy_mix),

    PARAM_DESC_EX(PARAM_EQ_LOW_DB, "EQ Low", PARAM_TYPE_FLOAT, -24.0f, 24.0f, 0.5f, 0.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_eq_low_db),
    PARAM_DESC_EX(PARAM_EQ_MID_DB, "EQ Mid", PARAM_TYPE_FLOAT, -24.0f, 24.0f, 0.5f, 0.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_eq_mid_db),
    PARAM_DESC_EX(PARAM_EQ_HIGH_DB, "EQ High", PARAM_TYPE_FLOAT, -24.0f, 24.0f, 0.5f, 0.0f, PARAM_DISPLAY_DB, "dB", NULL, apply_eq_high_db),

    PARAM_DESC_EX(PARAM_SAT_TONE, "Sat Tone", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "%", NULL, apply_sat_tone),
    PARAM_DESC_EX(PARAM_SAT_BIAS, "Sat Bias", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "%", NULL, apply_sat_bias),
    PARAM_DESC_EX(PARAM_SAT_DRIVE, "Sat Drive", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "%", NULL, apply_sat_drive),
    PARAM_DESC_EX(PARAM_SAT_MIX, "Sat Mix", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_PERCENT, "%", NULL, apply_sat_mix),

    PARAM_DESC_EX(PARAM_FILTER_TYPE, "F Type", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_filter_type_labels, apply_filter_type),
    PARAM_DESC_EX(PARAM_FILTER_CUTOFF, "Cutoff", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 127.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_cutoff),
    PARAM_DESC_EX(PARAM_FILTER_RESONANCE, "Res", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_resonance),
    PARAM_DESC_EX(PARAM_FILTER_EG_AMT, "EG Amt", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_eg_amount),
    PARAM_DESC_EX(PARAM_FILTER_ATTACK, "Atk", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 34.3f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_attack),
    PARAM_DESC_EX(PARAM_FILTER_DECAY, "Dec", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 68.7f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_decay),
    PARAM_DESC_EX(PARAM_FILTER_SUSTAIN, "Sus", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 127.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_sustain),
    PARAM_DESC_EX(PARAM_FILTER_RELEASE, "Rel", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 68.7f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_release),
    PARAM_DESC_EX(PARAM_FILTER_KEYTRK, "KeyTrk", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_keytrack),
    PARAM_DESC_EX(PARAM_FILTER_ENVRST, "EnvRst", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 1.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_filter_env_reset),
    PARAM_DESC_EX(PARAM_FILTER_ENVDLY, "EnvDly", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_filter_env_delay),
    PARAM_DESC_EX(PARAM_FILTER_EQ_LOW, "Low", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 64.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_eq_low),
    PARAM_DESC_EX(PARAM_FILTER_EQ_MID, "Mid", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 64.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_eq_mid),
    PARAM_DESC_EX(PARAM_FILTER_EQ_HIGH, "High", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 64.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_eq_high),
    PARAM_DESC_EX(PARAM_FILTER_DRIVE, "Drive", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_drive),
    PARAM_DESC_EX(PARAM_FILTER_DECIMATOR_BITS, "Bits", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_decimator_bits),
    PARAM_DESC_EX(PARAM_FILTER_DECIMATOR_RATE, "Rate", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_decimator_rate),
    PARAM_DESC_EX(PARAM_FILTER_DECIMATOR_RATE2, "Rate2", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_filter_decimator_rate2),
    PARAM_DESC_EX(PARAM_VCA_ATTACK, "Atk", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_VCA_DECAY, "Dec", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_VCA_SUSTAIN, "Sus", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 127.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_VCA_RELEASE, "Rel", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),

    PARAM_DESC_EX(PARAM_CFG_TRACK, "Track", PARAM_TYPE_ENUM, 0.0f, (float)((uint8_t)UI_TRACK_FAMILY_COUNT - 1U), 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_track_family_labels, apply_cfg_track),
    PARAM_DESC_EX(PARAM_CFG_TRACK_TYPE, "Type", PARAM_TYPE_ENUM, 0.0f, (float)((uint8_t)UI_TRACK_TYPE_COUNT - 1U), 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", NULL, apply_cfg_track_type),
    PARAM_DESC_EX(PARAM_CFG_MIDI_CH, "Midi CH", PARAM_TYPE_INT, 1.0f, 16.0f, 1.0f, 1.0f, PARAM_DISPLAY_INT, "", NULL, apply_cfg_midi_ch),
    PARAM_DESC_EX(PARAM_CFG_MIDI_SRC, "Midi Src", PARAM_TYPE_ENUM, 0.0f, 2.0f, 1.0f, 2.0f, PARAM_DISPLAY_ENUM, "", g_track_midi_source_labels, apply_cfg_midi_src),
    PARAM_DESC_EX(PARAM_CFG_REC, "Préroll", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_cfg_rec_labels, apply_cfg_rec),
    PARAM_DESC_EX(PARAM_CFG_TEMPO, "Tempo", PARAM_TYPE_FLOAT, 40.0f, 300.0f, 0.1f, 120.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_cfg_tempo),
    PARAM_DESC_EX(PARAM_CFG_SYNC, "Sync", PARAM_TYPE_ENUM, 0.0f, 2.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_cfg_sync_labels, apply_cfg_sync),
    PARAM_DESC_EX(PARAM_CFG_REC_LEN, "Len", PARAM_TYPE_ENUM, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_cfg_rec_len_labels, apply_cfg_rec_len),
    PARAM_DESC_EX(PARAM_BUFFER_REC_LEN, "Rec Len", PARAM_TYPE_INT, 1.0f, 64.0f, 1.0f, 16.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_BUFFER_Q_REC, "Q Rec", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 1.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_BUFFER_Q_PLAY, "Q Play", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 1.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_BUFFER_RATE, "Rate", PARAM_TYPE_FLOAT, 0.25f, 4.0f, 0.01f, 1.0f, PARAM_DISPLAY_RATIO, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_BUFFER_FADE_IN, "Fade In", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_BUFFER_FADE_OUT, "Fade Out", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_BUFFER_XFADE, "XFade", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, NULL),

    PARAM_DESC_EX(PARAM_SEQ_LENGTH, "LENGTH", PARAM_TYPE_INT, 1.0f, 64.0f, 1.0f, 64.0f, PARAM_DISPLAY_INT, "", NULL, apply_seq_length),
    PARAM_DESC_EX(PARAM_SEQ_DIV, "DIV", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_seq_div_labels, apply_seq_div),
    PARAM_DESC_EX(PARAM_SEQ_QUANT, "QUANT", PARAM_TYPE_INT, 0.0f, 100.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "%", NULL, apply_seq_quant),
    PARAM_DESC_EX(PARAM_SEQ_SWING, "SWING", PARAM_TYPE_INT, 0.0f, 100.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "%", NULL, apply_seq_swing),

    PARAM_DESC_EX(PARAM_SEQ_PLAY_V1_NOTE, "NOTE", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 60.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V1_VEL, "VEL", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 100.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V1_LEN, "LEN", PARAM_TYPE_INT, 1.0f, 64.0f, 1.0f, 1.0f, PARAM_DISPLAY_INT, "stp", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V1_MICTIM, "MicTim", PARAM_TYPE_INT, -24.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V2_NOTE, "NOTE", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 60.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V2_VEL, "VEL", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V2_LEN, "LEN", PARAM_TYPE_INT, 1.0f, 64.0f, 1.0f, 1.0f, PARAM_DISPLAY_INT, "stp", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V2_MICTIM, "MicTim", PARAM_TYPE_INT, -24.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V3_NOTE, "NOTE", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 60.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V3_VEL, "VEL", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V3_LEN, "LEN", PARAM_TYPE_INT, 1.0f, 64.0f, 1.0f, 1.0f, PARAM_DISPLAY_INT, "stp", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V3_MICTIM, "MicTim", PARAM_TYPE_INT, -24.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V4_NOTE, "NOTE", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 60.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V4_VEL, "VEL", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V4_LEN, "LEN", PARAM_TYPE_INT, 1.0f, 64.0f, 1.0f, 1.0f, PARAM_DISPLAY_INT, "stp", NULL, NULL),
    PARAM_DESC_EX(PARAM_SEQ_PLAY_V4_MICTIM, "MicTim", PARAM_TYPE_INT, -24.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),

    PARAM_DESC_EX(PARAM_KBD_ROOT, "Root", PARAM_TYPE_ENUM, 0.0f, 11.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_kbd_root_labels, apply_kbd_root),
    PARAM_DESC_EX(PARAM_KBD_SCALE, "Scale", PARAM_TYPE_ENUM, 0.0f, 6.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_kbd_scale_labels, apply_kbd_scale),
    PARAM_DESC_EX(PARAM_KBD_OMNICHORD, "Omni", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_kbd_omnichord),
    PARAM_DESC_EX(PARAM_KBD_NOTE_ORDER, "Order", PARAM_TYPE_ENUM, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_kbd_note_order_labels, apply_kbd_note_order),
    PARAM_DESC_EX(PARAM_KBD_CHORD_OVERRIDE, "ChrOvr", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_kbd_chord_override),

    PARAM_DESC_EX(PARAM_ARP_HOLD, "Hold", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_arp_hold),
    PARAM_DESC_EX(PARAM_ARP_RATE, "Rate", PARAM_TYPE_ENUM, 0.0f, 7.0f, 1.0f, 2.0f, PARAM_DISPLAY_ENUM, "", g_arp_rate_labels, apply_arp_rate),
    PARAM_DESC_EX(PARAM_ARP_OCT, "Oct", PARAM_TYPE_INT, 0.0f, 4.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_arp_oct),
    PARAM_DESC_EX(PARAM_ARP_PATTERN, "Pattern", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_arp_pattern_labels, apply_arp_pattern),
    PARAM_DESC_EX(PARAM_ARP_GATE, "Gate", PARAM_TYPE_INT, 1.0f, 127.0f, 1.0f, 100.0f, PARAM_DISPLAY_INT, "", NULL, apply_arp_gate),
    PARAM_DESC_EX(PARAM_ARP_SWING, "Swing", PARAM_TYPE_INT, 0.0f, 100.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "%", NULL, apply_arp_swing),
    PARAM_DESC_EX(PARAM_ARP_ACCENT, "Accent", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_arp_accent_labels, apply_arp_accent),
    PARAM_DESC_EX(PARAM_ARP_VEL_ACC, "VelAcc", PARAM_TYPE_INT, 0.0f, 64.0f, 1.0f, 24.0f, PARAM_DISPLAY_INT, "", NULL, apply_arp_vel_acc),
    PARAM_DESC_EX(PARAM_ARP_STRUM, "Strum", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_arp_strum_labels, apply_arp_strum),
    PARAM_DESC_EX(PARAM_ARP_OFFSET, "Offset", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, apply_arp_offset),
    PARAM_DESC_EX(PARAM_ARP_TRANS, "Trans", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, apply_arp_trans),
    PARAM_DESC_EX(PARAM_ARP_SPREAD, "Spread", PARAM_TYPE_INT, 0.0f, 12.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, apply_arp_spread),
    PARAM_DESC_EX(PARAM_ARP_DIR, "Dir", PARAM_TYPE_ENUM, 0.0f, 2.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_arp_dir_labels, apply_arp_dir),
    PARAM_DESC_EX(PARAM_ARP_SYNC, "Sync", PARAM_TYPE_ENUM, 0.0f, 2.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_arp_sync_labels, apply_arp_sync),

    PARAM_DESC_EX(PARAM_MASTER_GAIN, "Master Gain", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_master_gain),
    PARAM_DESC_EX(PARAM_POST_GAIN, "Post Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_post_gain),
    PARAM_DESC_EX(PARAM_OUTPUT_COMP, "Output Comp", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_output_comp),

   
    PARAM_DESC(PARAM_DX7_ALGORITHM, "ALGO", PARAM_TYPE_INT, 0.0f, 31.0f, 1.0f, 4.0f, "", apply_dx7_algorithm),
    PARAM_DESC(PARAM_DX7_FEEDBACK, "FDBK", PARAM_TYPE_INT, 0.0f, 7.0f, 1.0f, 6.0f, "", apply_dx7_feedback),
    PARAM_DESC(PARAM_DX7_TRANSPOSE, "TRANS", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 1.0f, 0.0f, "st", apply_dx7_transpose),
    PARAM_DESC(PARAM_DX7_LFO_SPEED, "LFO SPD", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 34.0f, "", apply_dx7_lfo_speed),
    PARAM_DESC(PARAM_DX7_LFO_DELAY, "LFO DLY", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 33.0f, "", apply_dx7_lfo_delay),
    PARAM_DESC(PARAM_DX7_LFO_PITCH_MOD_DEPTH, "PMD", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 0.0f, "", apply_dx7_lfo_pitch_mod_depth),
    PARAM_DESC(PARAM_DX7_LFO_AMP_MOD_DEPTH, "AMD", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 0.0f, "", apply_dx7_lfo_amp_mod_depth),
    PARAM_DESC(PARAM_DX7_PITCH_BEND_RANGE, "BEND", PARAM_TYPE_INT, 0.0f, 12.0f, 1.0f, 2.0f, "st", apply_dx7_pitch_bend_range),
    PARAM_DESC(PARAM_DX7_PORTAMENTO_TIME, "PORTA", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, "", apply_dx7_portamento_time),
    PARAM_DESC_EX(PARAM_DX7_MONO_MODE, "MONO", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_dx7_mono_mode),
    PARAM_DESC(PARAM_DX7_OPERATOR_MASK, "OPS", PARAM_TYPE_INT, 0.0f, 63.0f, 1.0f, 63.0f, "", apply_dx7_operator_mask),
    PARAM_DESC(PARAM_DX7_OPERATOR_1_LEVEL, "OP1", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 79.0f, "", apply_dx7_operator_1_level),
    PARAM_DESC(PARAM_DX7_OPERATOR_2_LEVEL, "OP2", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 99.0f, "", apply_dx7_operator_2_level),
    PARAM_DESC(PARAM_DX7_OPERATOR_3_LEVEL, "OP3", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 89.0f, "", apply_dx7_operator_3_level),
    PARAM_DESC(PARAM_DX7_OPERATOR_4_LEVEL, "OP4", PARAM_TYPE_INT, 0.0f, 99.0f, 1.0f, 99.0f, "", apply_dx7_operator_4_level),

    PARAM_DESC_EX(PARAM_MONOB_FILTER_TYPE, "F Type", PARAM_TYPE_ENUM, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_monob_filter_type_labels, apply_monob_filter_type),
    PARAM_DESC_EX(PARAM_MONOB_FILTER_CUTOFF, "Cutoff", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 127.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_monob_filter_cutoff),
    PARAM_DESC_EX(PARAM_MONOB_FILTER_RESONANCE, "Res", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_monob_filter_resonance),
    PARAM_DESC_EX(PARAM_MONOB_FILTER_EG_AMT, "Eg amount", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_monob_filter_eg_amount),
    PARAM_DESC_EX(PARAM_MONOB_FILTER_ATTACK, "A", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 34.3f, PARAM_DISPLAY_FLOAT, "", NULL, apply_monob_filter_attack),
    PARAM_DESC_EX(PARAM_MONOB_FILTER_DECAY, "D", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 68.7f, PARAM_DISPLAY_FLOAT, "", NULL, apply_monob_filter_decay),
    PARAM_DESC_EX(PARAM_MONOB_FILTER_SUSTAIN, "S", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 127.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_monob_filter_sustain),
    PARAM_DESC_EX(PARAM_MONOB_FILTER_RELEASE, "R", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 68.7f, PARAM_DISPLAY_FLOAT, "", NULL, apply_monob_filter_release),
    PARAM_DESC_EX(PARAM_MONOB_FILTER_KEYTRK, "KeyTrk", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_monob_filter_keytrack),
    PARAM_DESC_EX(PARAM_MONOB_FILTER_ENVRST, "EnvRst", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 1.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, apply_monob_filter_env_reset),
    PARAM_DESC_EX(PARAM_MONOB_FILTER_ENVDLY, "EnvDly", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, apply_monob_filter_env_delay),
    PARAM_DESC_EX(PARAM_MONOB_OSC1_WAVE, "Osc1", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 4.0f, PARAM_DISPLAY_ENUM, "", g_monob_wave_labels, apply_monob_osc1_wave),
    PARAM_DESC_EX(PARAM_MONOB_OSC2_WAVE, "Osc2", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 2.0f, PARAM_DISPLAY_ENUM, "", g_monob_wave_labels, apply_monob_osc2_wave),
    PARAM_DESC_EX(PARAM_MONOB_OSC3_WAVE, "Osc3", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_monob_wave_labels, apply_monob_osc3_wave),
    PARAM_DESC_EX(PARAM_MONOB_SUB_WAVE, "Sub", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 2.0f, PARAM_DISPLAY_ENUM, "", g_monob_wave_labels, apply_monob_sub_wave),
    PARAM_DESC_EX(PARAM_MONOB_OSC1_RANGE, "O1 Rng", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_monob_range_labels, apply_monob_osc1_range),
    PARAM_DESC_EX(PARAM_MONOB_OSC2_RANGE, "O2 Rng", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_monob_range_labels, apply_monob_osc2_range),
    PARAM_DESC_EX(PARAM_MONOB_OSC3_RANGE, "O3 Rng", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_monob_range_labels, apply_monob_osc3_range),
    PARAM_DESC_EX(PARAM_MONOB_SUB_OCTAVE, "SubOct", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 1.0f, PARAM_DISPLAY_ENUM, "", g_monob_sub_octave_labels, apply_monob_sub_octave),
    PARAM_DESC(PARAM_MONOB_OSC1_DETUNE, "O1 Det", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 1.0f, 0.0f, "ct", apply_monob_osc1_detune),
    PARAM_DESC(PARAM_MONOB_OSC2_DETUNE, "O2 Det", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 1.0f, -7.0f, "ct", apply_monob_osc2_detune),
    PARAM_DESC(PARAM_MONOB_OSC3_DETUNE, "O3 Det", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 1.0f, 7.0f, "ct", apply_monob_osc3_detune),
    PARAM_DESC_EX(PARAM_MONOB_DRIFT, "-", PARAM_TYPE_FLOAT, 0.0f, 0.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_MONOB_OSC1_MIX, "O1 Mix", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.9f, PARAM_DISPLAY_PERCENT, "", NULL, apply_monob_osc1_mix),
    PARAM_DESC_EX(PARAM_MONOB_OSC2_MIX, "O2 Mix", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.6f, PARAM_DISPLAY_PERCENT, "", NULL, apply_monob_osc2_mix),
    PARAM_DESC_EX(PARAM_MONOB_OSC3_MIX, "O3 Mix", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.45f, PARAM_DISPLAY_PERCENT, "", NULL, apply_monob_osc3_mix),
    PARAM_DESC_EX(PARAM_MONOB_SUB_MIX, "SubMix", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.35f, PARAM_DISPLAY_PERCENT, "", NULL, apply_monob_sub_mix),
    PARAM_DESC_EX(PARAM_TB3_WAVEFORM, "TB3 OFF", PARAM_TYPE_ENUM, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_TB3_VOLUME, "TB3 OFF", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 100.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_TB3_ACCENT, "TB3 OFF", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_TB3_SLIDE_TIME, "TB3 OFF", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_TB3_CUTOFF, "TB3 OFF", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 100.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_TB3_RESONANCE, "TB3 OFF", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_TB3_ENV_MOD, "TB3 OFF", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_TB3_DECAY, "TB3 OFF", PARAM_TYPE_FLOAT, 0.0f, 127.0f, 1.0f, 64.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_BD_PITCH, "Pitch", PARAM_TYPE_BIPOLAR, -48.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_BD_DECAY, "Decay", PARAM_TYPE_FLOAT, 0.01f, 2.0f, 0.01f, 0.4f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_BD_PITCH_SWEEP, "Sweep", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.3f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_BD_SWEEP_DECAY, "Swp Dec", PARAM_TYPE_FLOAT, 0.01f, 1.0f, 0.01f, 0.1f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_BD_ATTACK, "Attack", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_BD_NOISE, "Noise", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_BD_HARMONICS, "Harm", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_BD_DRIVE, "Drive", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_CLAVES_PITCH, "Pitch", PARAM_TYPE_BIPOLAR, -48.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_CLAVES_INTERVAL, "Intvl", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 64.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_CLAVES_DECAY, "Decay", PARAM_TYPE_FLOAT, 0.01f, 0.5f, 0.01f, 0.1f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_CLAVES_BALANCE, "Balance", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_CLAVES_DRIVE, "Drive", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.2f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_HIHAT_DECAY, "Decay", PARAM_TYPE_FLOAT, 0.01f, 1.0f, 0.01f, 0.2f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_HIHAT_METAL, "Metal", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.7f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_HIHAT_HP_TONE, "HP", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 50.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_HIHAT_LP_TONE, "LP", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 81.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_HIHAT_GAP, "Gap", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_HIHAT_PEAK, "Peak", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_SNARE_PITCH, "Pitch", PARAM_TYPE_BIPOLAR, -48.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_SNARE_DECAY, "Decay", PARAM_TYPE_FLOAT, 0.05f, 1.0f, 0.01f, 0.4f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_SNARE_SNAP, "Snap", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.6f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_SNARE_NOISE, "Noise", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_SNARE_TONE_MIX, "Tone", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_SNARE_DRIVE, "Drive", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.2f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_SNARE_TUNE_INTERVAL, "Tune", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 32.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_TRX_SNARE_BUMP, "Bump", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.1f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_KICK_PITCH, "Pitch", PARAM_TYPE_BIPOLAR, -48.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_KICK_DECAY, "Decay", PARAM_TYPE_FLOAT, 0.01f, 2.0f, 0.01f, 0.5f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_KICK_FM_AMOUNT, "FM Amt", PARAM_TYPE_FLOAT, 0.0f, 50.0f, 0.01f, 20.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_KICK_PITCH_SWEEP, "Sweep", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 8.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_KICK_FEEDBACK, "Fdbk", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 4.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_KICK_MOD_FREQ, "ModFr", PARAM_TYPE_FLOAT, 50.0f, 2000.0f, 1.0f, 180.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_KICK_MOD_DECAY, "M Dec", PARAM_TYPE_FLOAT, 0.001f, 2.0f, 0.01f, 0.15f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_KICK_SWEEP_DECAY, "S Dec", PARAM_TYPE_FLOAT, 0.001f, 2.0f, 0.01f, 0.1f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_KICK_RATIO_MODE, "Ratio", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_KICK_RATIO_INDEX, "R Idx", PARAM_TYPE_INT, 0.0f, 63.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_KICK_MOD_ENV_SYNC, "Sync", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_SNARE_PITCH, "Pitch", PARAM_TYPE_BIPOLAR, -48.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_SNARE_DECAY, "Decay", PARAM_TYPE_FLOAT, 0.01f, 1.0f, 0.01f, 0.4f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_SNARE_FM_AMOUNT, "FM Amt", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 38.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_SNARE_NOISE, "Noise", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_SNARE_HP_TONE, "HP", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 24.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_SNARE_MOD_FREQ, "ModFr", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 51.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_SNARE_MOD_DECAY, "M Dec", PARAM_TYPE_FLOAT, 0.01f, 1.0f, 0.01f, 0.1f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_SNARE_NOISE_DECAY, "N Dec", PARAM_TYPE_FLOAT, 0.01f, 1.0f, 0.01f, 0.3f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_TOM_PITCH, "Pitch", PARAM_TYPE_BIPOLAR, -48.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_TOM_DECAY, "Decay", PARAM_TYPE_FLOAT, 0.01f, 2.0f, 0.01f, 0.7f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_TOM_PITCH_SWEEP, "Sweep", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 38.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_TOM_FM_AMOUNT, "FM Amt", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 38.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_TOM_MOD_FREQ, "ModFr", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 13.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_TOM_MOD_DECAY, "M Dec", PARAM_TYPE_FLOAT, 0.01f, 1.0f, 0.01f, 0.2f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_TOM_SWEEP_DECAY, "S Dec", PARAM_TYPE_FLOAT, 0.01f, 1.0f, 0.01f, 0.1f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_TOM_START_PHASE, "Phase", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_RIMSHOT_RIM_PITCH, "R Pitch", PARAM_TYPE_BIPOLAR, -48.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_RIMSHOT_RIM_DECAY, "R Dec", PARAM_TYPE_FLOAT, 0.01f, 0.5f, 0.01f, 0.05f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_RIMSHOT_BODY_MIX, "B Mix", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.4f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_RIMSHOT_HP_TONE, "HP", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 20.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_RIMSHOT_RIM_FM_AMOUNT, "R FM", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 38.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_RIMSHOT_BODY_PITCH, "B Pitch", PARAM_TYPE_BIPOLAR, -48.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_RIMSHOT_BODY_DECAY, "B Dec", PARAM_TYPE_FLOAT, 0.05f, 1.0f, 0.01f, 0.25f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_RIMSHOT_BODY_FM_AMOUNT, "B FM", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 25.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_RIMSHOT_MOD_DECAY, "M Dec", PARAM_TYPE_FLOAT, 0.01f, 0.5f, 0.01f, 0.05f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_CLAP_CLAP_COUNT, "Count", PARAM_TYPE_INT, 1.0f, 6.0f, 1.0f, 3.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_CLAP_CLAP_SPACING, "Space", PARAM_TYPE_INT, 0.0f, 5.0f, 1.0f, 1.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_CLAP_TAIL_DECAY, "Tail", PARAM_TYPE_FLOAT, 0.01f, 0.9f, 0.01f, 0.3f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_CLAP_HP_TONE, "HP", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 24.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_CLAP_FEEDBACK, "Fdbk", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.9f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_CLAP_FM_AMOUNT, "FM Amt", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 51.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_CLAP_BASE_FREQ, "Base", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 81.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_CLAP_MOD_FREQ, "ModFr", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 31.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_CLAP_MOD_DECAY, "M Dec", PARAM_TYPE_FLOAT, 0.01f, 1.0f, 0.01f, 0.05f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_CLAP_CLAP_DECAY, "C Dec", PARAM_TYPE_FLOAT, 0.005f, 0.6f, 0.01f, 0.02f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_COWBELL_PITCH, "Pitch", PARAM_TYPE_BIPOLAR, -48.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_COWBELL_DECAY_SHORT, "D Shrt", PARAM_TYPE_INT, 0.0f, 20.0f, 1.0f, 1.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_COWBELL_DECAY_LONG, "D Long", PARAM_TYPE_FLOAT, 0.01f, 1.0f, 0.01f, 0.1f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_COWBELL_FM_AMOUNT, "FM Amt", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 19.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_COWBELL_FEEDBACK, "Fdbk", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.3f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_COWBELL_ENV_MIX, "EnvMix", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.7f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_COWBELL_MOD_DECAY, "M Dec", PARAM_TYPE_FLOAT, 0.01f, 1.0f, 0.01f, 0.1f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_COWBELL_MOD_FREQ, "ModFr", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 76.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_CYMBAL_DECAY, "Decay", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 0.48f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_CYMBAL_SUSTAIN, "Sustain", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.3f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_CYMBAL_FM_AMOUNT, "FM Amt", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 42.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_CYMBAL_HP_TONE, "HP", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 13.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_CYMBAL_FEEDBACK, "Fdbk", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_CYMBAL_BASE_CARRIER, "Carr", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 42.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_CYMBAL_BASE_MOD, "Mod", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 42.0f, PARAM_DISPLAY_INT, "", NULL, NULL),
    PARAM_DESC_EX(PARAM_DRUM_FM_CYMBAL_MOD_DECAY, "M Dec", PARAM_TYPE_FLOAT, 0.05f, 2.0f, 0.01f, 0.2f, PARAM_DISPLAY_FLOAT, "", NULL, NULL),

    PARAM_DESC_EX(PARAM_LFO1_DEST, "Dest", PARAM_TYPE_INT, 0.0f, (float)PARAM_COUNT, 1.0f, (float)PARAM_COUNT, PARAM_DISPLAY_INT, "", NULL, apply_lfo1_dest),
    PARAM_DESC_EX(PARAM_LFO1_RATE, "Rate", PARAM_TYPE_ENUM, 0.0f, 14.0f, 1.0f, 7.0f, PARAM_DISPLAY_ENUM, "", g_lfo_rate_labels, apply_lfo1_rate),
    PARAM_DESC_EX(PARAM_LFO1_DEPTH, "Depth", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_lfo1_depth),
    PARAM_DESC_EX(PARAM_LFO1_SHAPE, "Shape", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_lfo_shape_labels, apply_lfo1_shape),
    PARAM_DESC_EX(PARAM_LFO2_DEST, "Dest", PARAM_TYPE_INT, 0.0f, (float)PARAM_COUNT, 1.0f, (float)PARAM_COUNT, PARAM_DISPLAY_INT, "", NULL, apply_lfo2_dest),
    PARAM_DESC_EX(PARAM_LFO2_RATE, "Rate", PARAM_TYPE_ENUM, 0.0f, 14.0f, 1.0f, 7.0f, PARAM_DISPLAY_ENUM, "", g_lfo_rate_labels, apply_lfo2_rate),
    PARAM_DESC_EX(PARAM_LFO2_DEPTH, "Depth", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_lfo2_depth),
    PARAM_DESC_EX(PARAM_LFO2_SHAPE, "Shape", PARAM_TYPE_ENUM, 0.0f, 4.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_lfo_shape_labels, apply_lfo2_shape),

    PARAM_DESC_EX(PARAM_MIX_REVERB_WET, "Wet", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_reverb_wet),
    PARAM_DESC_EX(PARAM_MIX_REVERB_SIZE, "Size", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.7f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_reverb_size),
    PARAM_DESC_EX(PARAM_MIX_REVERB_DECAY, "Decay", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.2f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_reverb_decay),
    PARAM_DESC_EX(PARAM_MIX_REVERB_PRED, "PreD", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_reverb_pred),
    PARAM_DESC_EX(PARAM_MIX_REVERB_TYPE, "Type", PARAM_TYPE_ENUM, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_reverb_type_labels, apply_mix_reverb_type),
    PARAM_DESC_EX(PARAM_MIX_REVERB_SURR, "Surr", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_mix_reverb_surr),

    PARAM_DESC_EX(PARAM_SAMPLER_SAMPLE, "Sample", PARAM_TYPE_ENUM, 0.0f, 63.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", NULL, apply_sampler_sample),
    PARAM_DESC_EX(PARAM_SAMPLER_GAIN, "Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_sampler_gain),
    PARAM_DESC_EX(PARAM_SAMPLER_START, "Start", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_sampler_start),
    PARAM_DESC_EX(PARAM_SAMPLER_END, "End", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_sampler_end),
    PARAM_DESC_EX(PARAM_SAMPLER_MODE, "Mode", PARAM_TYPE_ENUM, 0.0f, 5.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_sampler_mode_labels, apply_sampler_mode),
    PARAM_DESC_EX(PARAM_SAMPLER_TUNE, "Tune", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "st", NULL, apply_sampler_tune),
    PARAM_DESC_EX(PARAM_SAMPLER_FADE_IN, "Fade In", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_sampler_fade_in),
    PARAM_DESC_EX(PARAM_SAMPLER_FADE_OUT, "Fade Out", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, PARAM_DISPLAY_PERCENT, "", NULL, apply_sampler_fade_out),
    PARAM_DESC_EX(PARAM_SAMPLER_SLICE_COUNT, "Slice Count", PARAM_TYPE_ENUM, 0.0f, 5.0f, 1.0f, 0.0f, PARAM_DISPLAY_ENUM, "", g_sampler_slice_count_labels, apply_sampler_slice_count),

    PARAM_DESC_EX(PARAM_HYBRID_GATE, "Gate", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, PARAM_DISPLAY_BOOL, "", g_bool_labels, NULL),
    PARAM_DESC_EX(PARAM_MIDI_PROGRAM, "Program", PARAM_TYPE_INT, 0.0f, 128.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_program),
    PARAM_DESC_EX(PARAM_MIDI_CC1_1, "CC16", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc1_1),
    PARAM_DESC_EX(PARAM_MIDI_CC1_2, "CC17", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc1_2),
    PARAM_DESC_EX(PARAM_MIDI_CC1_3, "CC18", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc1_3),
    PARAM_DESC_EX(PARAM_MIDI_CC1_4, "CC19", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc1_4),
    PARAM_DESC_EX(PARAM_MIDI_CC2_1, "CC20", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc2_1),
    PARAM_DESC_EX(PARAM_MIDI_CC2_2, "CC21", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc2_2),
    PARAM_DESC_EX(PARAM_MIDI_CC2_3, "CC22", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc2_3),
    PARAM_DESC_EX(PARAM_MIDI_CC2_4, "CC23", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc2_4),
    PARAM_DESC_EX(PARAM_MIDI_CC3_1, "CC24", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc3_1),
    PARAM_DESC_EX(PARAM_MIDI_CC3_2, "CC25", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc3_2),
    PARAM_DESC_EX(PARAM_MIDI_CC3_3, "CC26", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc3_3),
    PARAM_DESC_EX(PARAM_MIDI_CC3_4, "CC27", PARAM_TYPE_INT, 0.0f, 127.0f, 1.0f, 0.0f, PARAM_DISPLAY_INT, "", NULL, apply_midi_cc3_4),
};

/**
 * @brief Point d'entrée param_registry_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_registry_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void param_registry_init(void)
{
    /* Registry is static metadata; runtime values are in param_store. */
    filter_ui_state_init_defaults();
    mod_lfo_v1_init();
    memset(&g_param_runtime_track_values, 0, sizeof(g_param_runtime_track_values));
    memset(&g_param_runtime_track_valid, 0, sizeof(g_param_runtime_track_valid));
}

/**
 * @brief Point d'entrée param_get.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_get.
 *
 * @param id Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
float param_get(param_id_t id)
{
    return param_store_get_active(id);
}

/**
 * @brief Point d'entrée param_set.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_set.
 *
 * @param id Paramètre d'entrée de l'API.
 * @param value Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void param_set(param_id_t id, float value)
{
    if (id >= PARAM_COUNT)
        return;

    const param_desc_t *desc = &param_registry[id];
    const float clamped = clamp_value(value, desc->min, desc->max);

    param_store_set_active(id, clamped);

    {
        const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
        if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_NONE)
                && (rule.status != TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
        {
            return;
        }
    }

    if (desc->apply != NULL)
    {
        desc->apply(clamped);
    }
}

/**
 * @brief Point d'entrée param_reset.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_reset.
 *
 * @param id Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void param_reset(param_id_t id)
{
    if (id >= PARAM_COUNT)
        return;

    param_set(id, param_registry[id].default_value);
}
