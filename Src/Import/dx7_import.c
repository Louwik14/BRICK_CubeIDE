#include "Import/dx7_import.h"

#include <math.h>
#include <string.h>

#include "Mod/mod_env3_control.h"
#include "Mod/mod_lfo_types.h"
#include "Mod/mod_lfo_v1_control.h"
#include "Mod/mod_matrix.h"
#include "Param/param_filter.h"
#include "Param/param_ids.h"
#include "Storage/persistent_control_codec.h"
#include "Storage/persistent_key_catalog.h"
#include "Track/audio_fx_control_state.h"
#include "Track/fm_control_state.h"
#include "Track/polyphony_control.h"
#include "Track/track_sound_state.h"
#include "Track/vca_control_state.h"

static const float s_ams[4] = { 0.0f, 0.2588f, 0.4274f, 1.0f };
static const float s_pms[8] = {
    0.0f, 0.078125f, 0.15625f, 0.2578125f,
    0.4296875f, 0.71875f, 1.1953125f, 2.0f
};

float dx7_import_lfo_frequency_hz(uint8_t speed)
{
    if (speed > 99U) speed = 99U;
    int scaled = (speed == 0U) ? 1 : ((int)speed * 165) >> 6;
    scaled *= (scaled < 160) ? 11 : (11 + ((scaled - 160) >> 4));
    return (float)scaled * 0.005865f;
}

float dx7_import_pitch_depth_semitones(uint8_t depth, uint8_t sensitivity)
{
    if (depth > 99U) depth = 99U;
    if (sensitivity > 7U) sensitivity = 7U;
    return 6.0f * ((float)depth / 99.0f) * s_pms[sensitivity];
}

static float delay_attack_value(uint8_t delay)
{
    if (delay == 0U) return 0.0f;
    int d = 99 - (int)delay;
    d = (16 + (d & 15)) << (1 + (d >> 4));
    const float first = (float)d * 0.005865f;
    const int second_code = ((d & 0xff80) < 0x80) ? 0x80 : (d & 0xff80);
    const float second = (float)second_code * 0.005865f;
    const float seconds = 0.5f / first + 0.5f / second;
    float value = log2f(seconds / 0.001f) * (127.0f / 12.287712379549449f);
    if (value < 0.0f) value = 0.0f;
    if (value > 127.0f) value = 127.0f;
    return value;
}

static void lfo_mapping(uint8_t dx_wave, mod_lfo_shape_t *shape, float *phase)
{
    *phase = 0.0f;
    switch (dx_wave)
    {
        case 0U: *shape = MOD_LFO_SHAPE_TRIANGLE; *phase = 180.0f; break;
        case 1U: *shape = MOD_LFO_SHAPE_REVERSE_SAW; break;
        case 2U: *shape = MOD_LFO_SHAPE_SAW; break;
        case 3U: *shape = MOD_LFO_SHAPE_SQUARE; *phase = 180.0f; break;
        case 4U: *shape = MOD_LFO_SHAPE_SINE; *phase = 180.0f; break;
        default: *shape = MOD_LFO_SHAPE_RANDOM_SH; break;
    }
}

static uint8_t init_modulation(persist_control_patch_t *patch, uint8_t entity,
                               const dx7_voice_t *voice)
{
    track_sound_state_t sound;
    mod_lfo_control_bank_t lfos;
    mod_env3_control_state_t env;
    track_sound_state_make_default(&sound);
    mod_lfo_v1_make_default(&lfos);
    mod_env3_control_make_default(&env);
    patch->modulation_present = 1U;
    mod_lfo_shape_t shape;
    float phase;
    lfo_mapping(voice->lfo_waveform, &shape, &phase);
    /* Negative rates are the BRICK free-running Hz domain. */
    lfos.lfo[0].rate = -dx7_import_lfo_frequency_hz(voice->lfo_speed);
    lfos.lfo[0].shape = (float)shape;
    lfos.lfo[0].trigger = (float)(voice->lfo_key_sync
        ? MOD_LFO_TRIG_POLY_TRIG : MOD_LFO_TRIG_FREE);
    lfos.lfo[0].phase = phase;
    for (uint8_t i = 0U; i < 3U; ++i)
    {
        patch->modulation.lfos[i].rate = lfos.lfo[i].rate;
        patch->modulation.lfos[i].phase_offset = lfos.lfo[i].phase;
        if (!persist_key_lfo_shape_to_disk((mod_lfo_shape_t)(uint8_t)lfos.lfo[i].shape,
                &patch->modulation.lfos[i].shape_key)
                || !persist_key_lfo_trigger_to_disk(
                    (mod_lfo_trig_mode_t)(uint8_t)lfos.lfo[i].trigger,
                    &patch->modulation.lfos[i].trigger_key)) return 0U;
    }
    if (voice->lfo_delay != 0U)
    {
        env.attack = delay_attack_value(voice->lfo_delay);
        env.decay = 0.0f; env.sustain = 127.0f; env.release = 0.0f; env.retrigger = 1.0f;
        sound.mod_multi_source[0][0] = MOD_MATRIX_SOURCE_LFO1;
        sound.mod_multi_source[0][1] = MOD_MATRIX_SOURCE_ENV3;
    }
    patch->modulation.envelope = (persist_control_mod_envelope_t){
        env.attack, env.decay, env.sustain, env.release, (uint8_t)(env.retrigger >= 0.5f)
    };
    for (uint8_t i = 0U; i < 2U; ++i)
    {
        if (!persist_key_mod_source_to_disk(sound.mod_multi_source[i][0],
                &patch->modulation.multi[i].source_a_key)
                || !persist_key_mod_source_to_disk(sound.mod_multi_source[i][1],
                    &patch->modulation.multi[i].source_b_key)
                || !persist_key_mod_source_to_disk(sound.mod_slew_source[i],
                    &patch->modulation.slew[i].source_key)) return 0U;
        patch->modulation.slew[i].amount = sound.mod_slew_amount[i];
    }
    for (uint8_t i = 0U; i < PERSIST_CONTROL_MOD_ROUTE_COUNT; ++i)
    {
        if (!persist_key_mod_source_to_disk(MOD_MATRIX_SOURCE_NONE,
                &patch->modulation.routes[i].source_key)) return 0U;
        patch->modulation.routes[i].destination_entity = entity;
        patch->modulation.routes[i].destination_parameter = PERSIST_CONTROL_KEY_NONE;
    }
    return 1U;
}

static uint8_t add_route(persist_control_patch_t *patch, uint8_t entity,
                         uint8_t *route_index, uint8_t source,
                         param_id_t destination, float depth)
{
    if (*route_index >= PERSIST_CONTROL_MOD_ROUTE_COUNT || depth == 0.0f) return 0U;
    persist_control_mod_route_t *route = &patch->modulation.routes[*route_index];
    if (!persist_key_mod_source_to_disk(source, &route->source_key)
            || !persist_key_mod_destination_to_disk(entity, destination,
                &route->destination_entity, &route->destination_parameter)) return 0U;
    route->depth = depth;
    route->enabled = 1U;
    ++*route_index;
    return 1U;
}

static void clean_name(const dx7_voice_t *voice, persist_control_patch_t *patch)
{
    size_t first = 0U;
    size_t end = DX7_VOICE_NAME_BYTES;
    while (first < end && voice->name[first] == ' ') ++first;
    while (end > first && voice->name[end - 1U] == ' ') --end;
    for (size_t i = first; i < end; ++i)
    {
        const uint8_t c = (uint8_t)voice->name[i];
        const uint8_t allowed = (uint8_t)(c == ' ' || c == '_' || c == '-'
            || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')
            || (c >= 'a' && c <= 'z'));
        patch->name[i - first] = allowed ? (char)c : '_';
    }
    patch->name_length = (uint16_t)(end - first);
    if (end == first)
    {
        memcpy(patch->name, "DX7 Voice", 9U);
        patch->name_length = 9U;
    }
}

dx7_import_result_t dx7_import_voice(const dx7_voice_t *voice,
                                     uint8_t destination_entity,
                                     persist_control_patch_t *patch)
{
    if (voice == NULL || patch == NULL || destination_entity >= PERSIST_CONTROL_ENTITY_COUNT)
        return DX7_IMPORT_INVALID_ARGUMENT;
    if (voice->algorithm > 31U || voice->feedback > 7U
            || voice->oscillator_key_sync > 1U || voice->lfo_speed > 99U
            || voice->lfo_delay > 99U || voice->pitch_mod_depth > 99U
            || voice->amplitude_mod_depth > 99U || voice->lfo_key_sync > 1U
            || voice->lfo_waveform > 5U || voice->pitch_mod_sensitivity > 7U
            || voice->transpose > 48U) return DX7_IMPORT_INVALID_VOICE;
    memset(patch, 0, sizeof(*patch));
    patch->family = PERSIST_FAMILY_SYNTH; patch->type = PERSIST_TYPE_FM;
    patch->fm_present = 1U;
    fm_control_state_make_default(&patch->fm);
    param_filter_control_make_default(&patch->filter);
    vca_control_state_make_default(&patch->vca);
    audio_fx_control_state_make_default(&patch->audio_fx);
    polyphony_control_make_default(&patch->polyphony);
    clean_name(voice, patch);
    for (uint8_t op = 0U; op < DX7_OPERATOR_COUNT; ++op)
    {
        const dx7_operator_t *s = &voice->operators[op];
        for (uint8_t i = 0U; i < 4U; ++i)
            if (s->rates[i] > 99U || s->levels[i] > 99U)
                return DX7_IMPORT_INVALID_VOICE;
        if (s->breakpoint > 99U || s->left_depth > 99U || s->right_depth > 99U
                || s->left_curve > 3U || s->right_curve > 3U
                || s->rate_scaling > 7U || s->amplitude_mod_sensitivity > 3U
                || s->velocity_sensitivity > 7U || s->output_level > 99U
                || s->oscillator_mode > 1U || s->coarse > 31U || s->fine > 99U
                || s->detune < -7 || s->detune > 7)
            return DX7_IMPORT_INVALID_VOICE;
        track_tone_fm_operator_base_t *d = &patch->fm.base.operators[op];
        memcpy(d->rates, s->rates, 4U); memcpy(d->levels, s->levels, 4U);
        d->breakpoint = s->breakpoint; d->left_depth = s->left_depth;
        d->right_depth = s->right_depth; d->left_curve = s->left_curve;
        d->right_curve = s->right_curve; d->rate_scaling = s->rate_scaling;
        d->velocity_sensitivity = s->velocity_sensitivity;
        d->output_level = s->output_level; d->mode = s->oscillator_mode;
        d->coarse = s->coarse; d->fine = s->fine; d->detune = s->detune;
        d->enabled = 1U;
    }
    memcpy(patch->fm.base.pitch_rates, voice->pitch_rates, 4U);
    memcpy(patch->fm.base.pitch_levels, voice->pitch_levels, 4U);
    patch->fm.base.transpose_cents = (uint16_t)voice->transpose * 100U;
    patch->fm.base.algorithm = voice->algorithm;
    patch->fm.base.feedback = voice->feedback;
    patch->fm.base.key_sync = voice->oscillator_key_sync;
    if (!fm_control_state_validate(&patch->fm)
            || !init_modulation(patch, destination_entity, voice))
        return DX7_IMPORT_INVALID_VOICE;

    const uint8_t source = voice->lfo_delay != 0U
        ? MOD_MATRIX_SOURCE_MULTI1 : MOD_MATRIX_SOURCE_LFO1;
    uint8_t route = 0U;
    const float pitch_semitones = dx7_import_pitch_depth_semitones(
        voice->pitch_mod_depth, voice->pitch_mod_sensitivity);
    if (pitch_semitones != 0.0f && !add_route(patch, destination_entity, &route,
            source, PARAM_FM_TRANSPOSE, pitch_semitones * (127.0f / 48.0f)))
        return DX7_IMPORT_KEY_ERROR;
    for (uint8_t op = 0U; op < DX7_OPERATOR_COUNT; ++op)
    {
        const uint8_t ams = voice->operators[op].amplitude_mod_sensitivity;
        if (voice->amplitude_mod_depth == 0U || ams == 0U) continue;
        /* DX amplitude modulation is attenuation-only.  Center its unipolar
           range around the native bipolar LFO and offset the static level. */
        const float level_span = (float)voice->amplitude_mod_depth * s_ams[ams];
        float base = (float)patch->fm.base.operators[op].output_level - 0.5f * level_span;
        if (base < 0.0f) base = 0.0f;
        patch->fm.base.operators[op].output_level = (uint8_t)(base + 0.5f);
        const param_id_t destination = (param_id_t)(PARAM_FM_OP1_LEVEL
            + op * (uint8_t)BRICK6_FM_OPERATOR_PARAM_COUNT);
        if (!add_route(patch, destination_entity, &route, source, destination,
                0.5f * level_span * (127.0f / 99.0f))) return DX7_IMPORT_KEY_ERROR;
    }
    return DX7_IMPORT_OK;
}

dx7_import_result_t dx7_import_voices(const dx7_voice_t *voices,
                                      size_t voice_count,
                                      uint8_t destination_entity,
                                      persist_control_patch_t *patches,
                                      size_t patch_capacity)
{
    if (voices == NULL || patches == NULL || voice_count > patch_capacity)
        return DX7_IMPORT_INVALID_ARGUMENT;
    for (size_t i = 0U; i < voice_count; ++i)
    {
        const dx7_import_result_t result = dx7_import_voice(
            &voices[i], destination_entity, &patches[i]);
        if (result != DX7_IMPORT_OK) return result;
    }
    return DX7_IMPORT_OK;
}
