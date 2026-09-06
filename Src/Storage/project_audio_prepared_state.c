#include "Storage/project_audio_prepared_state.h"

#include <string.h>

#include "IPC/audio_prepared_state.h"
#include "IPC/audio_wavetable_registry_contract.h"
#include "Param/param_global_control.h"
#include "Param/live_parameter_migration.h"
#include "Param/param_registry.h"
#include "Platform/intercore_cache.h"
#include "Seq/metronome_control.h"
#include "Seq/seq_runtime.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Storage/project_control.h"
#include "Track/audio_fx_control_state.h"
#include "Track/control_routing.h"
#include "Track/entity_topology.h"
#include "Track/fm_control_state.h"
#include "Track/polyphony_control.h"
#include "Track/track_input_ownership.h"
#include "Track/track_mute.h"
#include "Track/track_runtime.h"
#include "Track/track_sound_state.h"
#include "Mod/mod_lfo_v1_control.h"

static uint8_t project_audio_prepared_wave_selection(
    uint8_t entity, uint8_t osc, audio_wave_table_selection_t *out)
{
    uint16_t logical = 0U;
    uint16_t global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    uint16_t slot = WAVETABLE_POOL_INVALID_SLOT;
    const sample_global_slot_t *sample;
    const wavetable_slot_t *table;

    if ((out == NULL)
            || (project_control_track_asset_get_logical(
                entity, (project_control_asset_role_t)(
                    PROJECT_CONTROL_ASSET_WAVE_OSC1 + osc), &logical) == 0U)
            || (project_control_resolve_wavetable_runtime(logical, &global) == 0U)
            || (sample_global_pool_resolve_backend(
                global, SAMPLE_GLOBAL_KIND_WAVETABLE, &slot) == 0U))
        return 0U;
    sample = sample_global_pool_get_slot(global);
    table = wavetable_pool_get_slot(slot);
    if ((sample == NULL) || (table == NULL)
            || (sample->state != SAMPLE_GLOBAL_STATE_READY)
            || (table->state != WAVETABLE_SLOT_READY)
            || (table->generation == 0U))
        return 0U;
    *out = (audio_wave_table_selection_t){
        .wavetable_slot = slot,
        .generation = table->generation
    };
    return 1U;
}

static uint8_t project_audio_prepared_fill_entity(
    uint8_t entity, audio_prepared_entity_state_t *out)
{
    track_runtime_descriptor_t descriptor;
    entity_topology_descriptor_t topology;
    const track_sound_state_t *sound;
    polyphony_control_state_t polyphony;
    audio_fx_control_state_t audio_fx;
    fm_control_state_t fm;

    if ((out == NULL) || (entity_topology_get(entity, &topology) == 0U))
        return 0U;

    memset(out, 0, sizeof(*out));
    out->valid = 1U;
    out->active = topology.active;
    if (topology.active == 0U)
        return 1U;
    if ((track_runtime_get_descriptor(entity, &descriptor) == 0U)
            || (polyphony_control_capture(entity, &polyphony) == 0U)
            || (audio_fx_control_state_capture(entity, &audio_fx) == 0U))
        return 0U;
    out->program = (control_audio_program_descriptor_t){
        .engine = (uint8_t)descriptor.engine,
        .family = (uint8_t)descriptor.family,
        .type = (uint8_t)descriptor.type,
        .flags = descriptor.flags
    };
    if (topology.role == ENTITY_ROLE_GROUP_MASTER)
        out->program.flags |= CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER;
    else if (topology.role == ENTITY_ROLE_GROUP_CHILD)
        out->program.flags |= CONTROL_AUDIO_PROGRAM_FLAG_GROUP_CHILD;
    out->polyphony_voice_count = polyphony.voice_count;
    out->muted = (track_mute_get(entity) > 0) ? 1U : 0U;
    out->midi_channel_1_16 = descriptor.midi_channel_1_16;
    out->midi_source = (uint8_t)track_runtime_get_midi_source(entity);

    out->fx_filter_position = (uint8_t)audio_fx.config.filter_position;
    out->fx_order = (uint8_t)audio_fx.config.order;
    out->fx_spatial_mode[0U] = audio_fx.config.spatial_mode[0U];
    out->fx_spatial_mode[1U] = audio_fx.config.spatial_mode[1U];

    sound = track_sound_state_get_const(entity);
    if (sound == NULL) return 0U;
    out->matrix_valid = 1U;
    memcpy(out->matrix, sound->mod_matrix, sizeof(out->matrix));
    memcpy(out->multi_source, sound->mod_multi_source,
           sizeof(out->multi_source));
    memcpy(out->slew_source, sound->mod_slew_source,
           sizeof(out->slew_source));
    memcpy(out->slew_amount, sound->mod_slew_amount,
           sizeof(out->slew_amount));

    const track_runtime_ctx_t *const runtime_ctx = track_runtime_get_ctx(entity);
    const uint8_t audio_routable = track_runtime_is_audio_routable_ctx(runtime_ctx);
    for (param_id_t id = 0U; id < PARAM_COUNT; ++id)
    {
        float value = 0.0f;
        if ((audio_routable != 0U)
                && (param_registry_track_value_is_audio_command(id, entity) != 0U)
                && (param_registry_get_track_value(id, entity, &value) != 0U))
        {
            out->track_value_valid[id] = 1U;
            out->track_value[id] = value;
        }
    }
    out->track_value_valid[PARAM_CFG_POLY_SPREAD] = 1U;
    out->track_value[PARAM_CFG_POLY_SPREAD] = polyphony.spread;

    brick_entity_id_t modulation_owner = BRICK_ENTITY_INVALID_ID;
    if ((entity_topology_mod_owner(entity, &modulation_owner) != 0U)
            && (modulation_owner == entity))
    {
        out->lfo_valid = 1U;
        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
            for (uint8_t parameter = 0U;
                 parameter < (uint8_t)MOD_LFO_PARAM_COUNT; ++parameter)
                if (mod_lfo_v1_get_track_param(
                        entity, lfo, (mod_lfo_param_t)parameter,
                        &out->lfo_value[lfo][parameter]) == 0U)
                    return 0U;
    }

    if (descriptor.type == TRACK_RUNTIME_TYPE_FM)
    {
        if ((fm_control_state_get(entity, &fm) == 0U)
                || (fm_control_state_validate(&fm) == 0U))
            return 0U;
        out->fm_base_valid = 1U;
        out->fm_base = fm.base;
    }
    if ((descriptor.type == TRACK_RUNTIME_TYPE_STREAM)
            || (descriptor.type == TRACK_RUNTIME_TYPE_RAM)
            || (descriptor.type == TRACK_RUNTIME_TYPE_MULTI))
    {
        uint16_t logical = 0U;
        if (project_control_track_asset_get_logical(
                entity, PROJECT_CONTROL_ASSET_SAMPLER, &logical) != 0U)
        {
            if (descriptor.type == TRACK_RUNTIME_TYPE_MULTI)
            {
                if (project_control_resolve_multi_runtime(
                        logical, &out->sampler_asset) == 0U)
                    return 0U;
            }
            else if (project_control_resolve_sample_runtime(
                         logical, &out->sampler_asset, NULL) == 0U)
                return 0U;
            out->sampler_asset_valid = 1U;
        }
    }
    if (descriptor.type == TRACK_RUNTIME_TYPE_WAVE)
    {
        for (uint8_t osc = 0U; osc < 2U; ++osc)
        {
            persist_control_asset_ref_t asset;
            if (project_control_track_asset_get(
                    entity, (project_control_asset_role_t)(
                        PROJECT_CONTROL_ASSET_WAVE_OSC1 + osc), &asset) == 0U)
                continue;
            if (project_audio_prepared_wave_selection(
                    entity, osc, &out->wave_selection[osc]) == 0U)
                return 0U;
            out->wave_selection_valid[osc] = 1U;
        }
    }
    for (uint8_t source = 0U; source < BRICK_ENTITY_CAPACITY; ++source)
        if (control_routing_get_looper_source(entity, source) != 0U)
            out->looper_route_mask |= (uint16_t)(1U << source);
    return 1U;
}

void project_audio_prepared_state_init(void)
{
    memset(&g_audio_prepared_state, 0, sizeof(g_audio_prepared_state));
    g_audio_prepared_state.version = AUDIO_PREPARED_STATE_VERSION;
    intercore_cache_publish(&g_audio_prepared_state,
                            sizeof(g_audio_prepared_state));
}

uint8_t project_audio_prepared_state_build(void)
{
    audio_prepared_state_t *const state = &g_audio_prepared_state;
    float modfx_model = 0.0f;
    state->ready = 0U;
    __DMB();
    memset((void *)state->entity, 0, sizeof(state->entity));
    memset((void *)state->global_value_valid, 0,
           sizeof(state->global_value_valid));
    state->version = AUDIO_PREPARED_STATE_VERSION;
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
        if (project_audio_prepared_fill_entity(
                entity, &state->entity[entity]) == 0U)
            return 0U;
    (void)param_registry_query_global(PARAM_MODFX_MODEL, &modfx_model);
    for (param_id_t id = 0U; id < PARAM_COUNT; ++id)
    {
        float value = 0.0f;
        float command_value = 0.0f;
        if (((live_parameter_is_audio_owned(id) != 0U)
                    || (id == PARAM_MASTER_GAIN))
                && (param_registry_query_global(id, &value) != 0U)
                && (param_registry_prepare_global_audio_command(
                    id, value, (uint8_t)(modfx_model + 0.5f),
                    &command_value) != 0U))
        {
            state->global_value_valid[id] = 1U;
            state->global_value[id] = command_value;
        }
    }
    memset(state->input_owner, TRACK_INPUT_OWNER_NONE,
           sizeof(state->input_owner));
    for (uint8_t input = 0U;
         input < ENTITY_TOPOLOGY_AUDIO_SOURCE_COUNT; ++input)
    {
        uint8_t owner = TRACK_INPUT_OWNER_NONE;
        if (track_input_ownership_get_external_owner(input, &owner) != 0U)
            state->input_owner[input] = owner;
    }
    state->metronome_level = metronome_control_get_level();
    state->tempo_bpm_milli = seq_runtime_get_effective_tempo_bpm_milli();
    state->transport_step_q16 = seq_runtime_get_samples_per_step_q16();
    return 1U;
}

uint32_t project_audio_prepared_state_publish(void)
{
    uint32_t generation = g_audio_prepared_state.generation + 1U;
    if (generation == 0U) generation = 1U;
    g_audio_prepared_state.ready = 0U;
    __DMB();
    g_audio_prepared_state.generation = generation;
    intercore_cache_publish(&g_audio_prepared_state,
                            sizeof(g_audio_prepared_state));
    g_audio_prepared_state.ready = 1U;
    __DMB();
    intercore_cache_publish(&g_audio_prepared_state,
                            sizeof(g_audio_prepared_state));
    return generation;
}
