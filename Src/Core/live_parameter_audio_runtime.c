#include "Core/live_parameter_audio_runtime.h"

#include <string.h>

#include "Audio/audio_global_runtime.h"
#include "Audio/audio_mod_matrix.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Core/live_parameter_event.h"
#include "Param/param_registry.h"
#include "Param/param_registry_backends.h"
#include "Seq/seq_types.h"
#include "Platform/memory_layout.h"

SEQ_STATE_D2 static float g_live_parameter_audio_poly_voices[SEQ_LANE_CAPACITY];
SEQ_STATE_D2 static float g_live_parameter_audio_poly_spread[SEQ_LANE_CAPACITY];
/* AUDIO authority.  Its lifetime is the track lifetime; PROGRAM never writes
 * it.  Engine runtimes contain only native/DSP projections of these values. */
SEQ_STATE_D2 static float
    g_track_tone_audio[SEQ_LANE_CAPACITY][SEQ_PARAM_TONE_SLOT_COUNT];

static uint8_t live_parameter_audio_tone_store(uint8_t entity,
                                               param_id_t parameter,
                                               float value)
{
    track_audio_runtime_ctx_t ctx;
    uint8_t slot;
    if ((entity >= SEQ_LANE_CAPACITY)
            || (audio_note_engine_adapter_current_ctx(entity, &ctx) == 0U)
            || (track_runtime_tone_param_to_slot(
                (track_runtime_type_t)ctx.type, parameter, &slot) == 0U)
            || (slot >= SEQ_PARAM_TONE_SLOT_COUNT))
        return 0U;
    const float span = param_registry[parameter].max
        - param_registry[parameter].min;
    g_track_tone_audio[entity][slot] = (span > 0.0f)
        ? (value - param_registry[parameter].min) / span : 0.0f;
    return 1U;
}
static float live_parameter_audio_runtime_clamp(param_id_t parameter,
                                                float value)
{
    if (parameter >= PARAM_COUNT) return value;
    if (value < param_registry[parameter].min)
        return param_registry[parameter].min;
    if (value > param_registry[parameter].max)
        return param_registry[parameter].max;
    return value;
}

static uint8_t live_parameter_audio_runtime_changes_matrix_context(param_id_t id)
{
    switch (id)
    {
        case PARAM_AUDIO_FX_MODEL: case PARAM_AUDIO_FX_B_MODEL:
        case PARAM_DRUM_MD_MODEL:
        case PARAM_PRISM_OSC1_MODEL: case PARAM_PRISM_OSC2_MODEL:
        case PARAM_STACK_OSC1_MODEL: case PARAM_STACK_OSC2_MODEL:
        case PARAM_STACK_OSC3_MODEL:
        case PARAM_LFO1_TRIG: case PARAM_LFO2_TRIG: case PARAM_LFO3_TRIG:
            return 1U;
        default: return 0U;
    }
}

void live_parameter_audio_runtime_init(void)
{
    memset(g_track_tone_audio, 0, sizeof(g_track_tone_audio));
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
    {
        g_live_parameter_audio_poly_voices[track] =
            param_registry[PARAM_CFG_POLY_VOICES].default_value;
        g_live_parameter_audio_poly_spread[track] =
            param_registry[PARAM_CFG_POLY_SPREAD].default_value;
    }
}

uint8_t live_parameter_audio_runtime_tone_get(uint8_t entity, uint8_t slot,
                                              float *out_normalized)
{
    if ((entity >= SEQ_LANE_CAPACITY) || (slot >= SEQ_PARAM_TONE_SLOT_COUNT)
            || (out_normalized == NULL)) return 0U;
    *out_normalized = g_track_tone_audio[entity][slot];
    return 1U;
}

uint8_t live_parameter_audio_runtime_apply_tone_slot(uint8_t entity,
                                                     uint8_t slot,
                                                     float normalized)
{
    if ((entity >= SEQ_LANE_CAPACITY) || (slot >= SEQ_PARAM_TONE_SLOT_COUNT))
        return 0U;
    normalized = (normalized < 0.0f) ? 0.0f
        : ((normalized > 1.0f) ? 1.0f : normalized);
    g_track_tone_audio[entity][slot] = normalized;

    track_audio_runtime_ctx_t ctx;
    param_id_t id;
    if ((audio_note_engine_adapter_current_ctx(entity, &ctx) == 0U)
            || (track_runtime_tone_slot_to_param(
                (track_runtime_type_t)ctx.type, slot, &id) == 0U)
            || (param_registry_track_value_is_audio_command(id, entity) == 0U))
        return 1U;
    const float value = param_registry[id].min
        + normalized * (param_registry[id].max - param_registry[id].min);
    if (param_backend_apply_prepared_track_value_audio(entity, id, value) == 0U)
        return 0U;
    audio_mod_matrix_base_update(entity, id, value);
    return 1U;
}

uint8_t live_parameter_audio_runtime_apply_param(uint8_t entity,
                                                 uint16_t parameter_id,
                                                 uint32_t value_bits,
                                                 uint8_t scope)
{
    if (parameter_id >= PARAM_COUNT) return 0U;
    const float decoded = live_parameter_event_decode_float((int32_t)value_bits);
    if ((scope >= LIVE_PARAMETER_AUDIO_SCOPE_MATRIX_SLOT_BASE)
            && (scope <= LIVE_PARAMETER_AUDIO_SCOPE_MATRIX_SLOT_LAST))
        return audio_mod_matrix_apply_param(entity,
            (uint8_t)(scope - LIVE_PARAMETER_AUDIO_SCOPE_MATRIX_SLOT_BASE),
            (param_id_t)parameter_id, decoded);
    if (scope == LIVE_PARAMETER_EVENT_SCOPE_GLOBAL)
    {
        if (parameter_id == PARAM_MASTER_GAIN)
            return audio_note_engine_adapter_set_master(decoded);
        return audio_global_runtime_apply(parameter_id, decoded);
    }
    if (((scope != LIVE_PARAMETER_EVENT_SCOPE_TRACK)
            && (scope != LIVE_PARAMETER_AUDIO_SCOPE_RUNTIME_TEMP))
            || (entity >= SEQ_LANE_CAPACITY))
        return 0U;

    const float value = live_parameter_audio_runtime_clamp(
        (param_id_t)parameter_id, decoded);
    if ((parameter_id >= PARAM_MOD_MULTI_1_A)
            && (parameter_id <= PARAM_MOD_SLEW_2_AMOUNT))
        return audio_mod_matrix_apply_param(entity, UINT8_MAX,
                                            (param_id_t)parameter_id, value);
    if ((parameter_id == PARAM_CFG_POLY_VOICES)
            || (parameter_id == PARAM_CFG_POLY_SPREAD))
    {
        if (parameter_id == PARAM_CFG_POLY_VOICES)
            g_live_parameter_audio_poly_voices[entity] = value;
        else
            g_live_parameter_audio_poly_spread[entity] = value;
        const uint8_t applied = audio_note_engine_adapter_apply_polyphony(
            entity, (uint8_t)g_live_parameter_audio_poly_voices[entity],
            g_live_parameter_audio_poly_spread[entity]);
        if (applied != 0U)
            audio_mod_matrix_base_update(entity, parameter_id, value);
        return applied;
    }

    const uint8_t tone_stored = (scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK)
        ? live_parameter_audio_tone_store(
            entity, (param_id_t)parameter_id, value) : 0U;
    const uint8_t applied = param_registry_apply_track_value_audio(
        parameter_id, entity, value);
    if (((applied != 0U) || (tone_stored != 0U))
            && (scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK))
    {
        audio_mod_matrix_base_update(entity, parameter_id, value);
        if (live_parameter_audio_runtime_changes_matrix_context(
                (param_id_t)parameter_id) != 0U)
        {
            audio_mod_matrix_rebuild_track(entity);
        }
    }
    return (uint8_t)((applied != 0U) || (tone_stored != 0U));
}
