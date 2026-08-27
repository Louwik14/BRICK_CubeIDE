#include "Audio/audio_note_engine_adapter.h"
#include "Audio/audio_mod_matrix.h"
#include "Audio/audio_fx_runtime.h"

#include <stddef.h>
#include <string.h>

#include "Audio/drum_synth.h"
#include "Audio/mixer.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_fm_runtime.h"
#include "Core/brick6_sampler_runtime.h"
#include "Core/brick6_stack_runtime.h"
#include "Core/brick6_wave_runtime.h"
#include "Core/brick6_looper_runtime.h"
#include "Core/brick_build_config.h"
#include "Core/synth_polyphony.h"
#include "Mod/mod_lfo_v1.h"
#include "Storage/memory_layout.h"

static uint32_t g_audio_mono_output[BRICK_ENTITY_CAPACITY];
static uint32_t g_audio_installed_generation[BRICK_ENTITY_CAPACITY];
static track_audio_runtime_ctx_t g_audio_track_ctx[BRICK_ENTITY_CAPACITY];
static uint16_t g_audio_entity_mask_by_engine[TRACK_RUNTIME_ENGINE_COUNT];
static uint8_t g_audio_entity_by_mix_lane[MIXER_MAX_TRACKS];
D3_IPC static audio_binding_snapshot_t
    g_audio_binding_snapshot[BRICK_ENTITY_CAPACITY];
D3_IPC static volatile uint32_t g_audio_binding_snapshot_sequence;

static void audio_note_engine_adapter_rebuild_binding_projections(void)
{
    memset(g_audio_entity_mask_by_engine, 0,
           sizeof(g_audio_entity_mask_by_engine));
    memset(g_audio_entity_by_mix_lane, BRICK_ENTITY_INVALID_ID,
           sizeof(g_audio_entity_by_mix_lane));
    for (brick_entity_id_t entity = 0U;
         entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        const track_audio_binding_t *const binding =
            &g_audio_track_ctx[entity].audio_binding;
        if (binding->bind_state != TRACK_RUNTIME_BIND_BOUND)
            continue;
        if (binding->engine < (uint8_t)TRACK_RUNTIME_ENGINE_COUNT)
        {
            g_audio_entity_mask_by_engine[binding->engine] |=
                (uint16_t)(1U << entity);
        }
        if ((binding->mix_track_id < MIXER_MAX_TRACKS)
                && (g_audio_entity_by_mix_lane[binding->mix_track_id]
                    == BRICK_ENTITY_INVALID_ID))
        {
            g_audio_entity_by_mix_lane[binding->mix_track_id] = entity;
        }
    }
}

static void audio_note_engine_adapter_write_snapshot(
    brick_entity_id_t entity)
{
    const track_audio_runtime_ctx_t *const ctx = &g_audio_track_ctx[entity];
    const uint8_t is_multi = (uint8_t)(
        (ctx->audio_binding.engine
            == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
        && (ctx->type
            == (uint8_t)TRACK_RUNTIME_TYPE_MULTI));
    const uint8_t ram_slice_mode_active = (uint8_t)(
        (ctx->audio_binding.bind_state == TRACK_RUNTIME_BIND_BOUND)
        && (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
        && (ctx->audio_binding.engine
            == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
        && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_RAM)
        && (brick6_sampler_runtime_audio_slice_count(entity) != 0U));
    g_audio_binding_snapshot[entity] = (audio_binding_snapshot_t){
        .binding = ctx->audio_binding,
        .family = ctx->family,
        .type = ctx->type,
        .flags = ctx->flags,
        .configured_voice_count = (is_multi != 0U)
            ? brick6_sampler_runtime_get_multi_voice_count(entity)
            : synth_polyphony_get_voice_count(entity),
        .physical_voice_capacity = (is_multi != 0U)
            ? SAMPLER_MULTI_MAX_VOICES_PER_TRACK
            : synth_polyphony_get_available_for_track(entity),
        .sampler_slice_mode_active = ram_slice_mode_active,
        .midi_channel_1_16 = ctx->midi_channel_1_16,
        .midi_source = ctx->midi_source,
        .has_filter_target = ctx->has_filter_target,
        .filter_track_id = ctx->filter_track_id,
        .supports_vca_gate = ctx->supports_vca_gate
    };
}

static void audio_note_engine_adapter_publish_snapshots(void)
{
    ++g_audio_binding_snapshot_sequence;
    __DMB();
    for (brick_entity_id_t entity = 0U;
         entity < BRICK_ENTITY_CAPACITY; ++entity)
        audio_note_engine_adapter_write_snapshot(entity);
    __DMB();
    ++g_audio_binding_snapshot_sequence;
    __DMB();
}

void audio_note_engine_adapter_audio_publish_snapshot(void)
{
    audio_note_engine_adapter_publish_snapshots();
}

void audio_note_engine_adapter_audio_publish_snapshot_entity(
    brick_entity_id_t entity_id)
{
    if (entity_id >= BRICK_ENTITY_CAPACITY)
        return;
    ++g_audio_binding_snapshot_sequence;
    __DMB();
    audio_note_engine_adapter_write_snapshot(entity_id);
    __DMB();
    ++g_audio_binding_snapshot_sequence;
    __DMB();
}

void audio_note_engine_adapter_init(void)
{
    memset(g_audio_mono_output, 0, sizeof(g_audio_mono_output));
    memset(g_audio_installed_generation, 0,
           sizeof(g_audio_installed_generation));
    memset(g_audio_track_ctx, 0, sizeof(g_audio_track_ctx));
    memset(g_audio_binding_snapshot, 0, sizeof(g_audio_binding_snapshot));
    g_audio_binding_snapshot_sequence = 0U;
    for (brick_entity_id_t entity = 0U;
         entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        g_audio_track_ctx[entity].audio_binding.entity_id = entity;
        g_audio_track_ctx[entity].audio_binding.mix_track_id = 0xFFU;
        g_audio_track_ctx[entity].audio_binding.instance_id = 0xFFU;
    }
    audio_note_engine_adapter_rebuild_binding_projections();
    audio_note_engine_adapter_publish_snapshots();
    mixer_rebuild_static_plan();
}

uint8_t audio_note_engine_adapter_audio_ctx_snapshot(
    brick_entity_id_t entity_id,
    track_audio_runtime_ctx_t *out_context)
{
    audio_binding_snapshot_t snapshot;
    if ((out_context == NULL)
            || (audio_note_engine_adapter_snapshot_read(entity_id, &snapshot) == 0U))
        return 0U;
    *out_context = (track_audio_runtime_ctx_t){
        .audio_binding = snapshot.binding,
        .midi_channel_1_16 = snapshot.midi_channel_1_16,
        .midi_source = snapshot.midi_source,
        .family = snapshot.family,
        .type = snapshot.type,
        .flags = snapshot.flags,
        .has_filter_target = snapshot.has_filter_target,
        .filter_track_id = snapshot.filter_track_id,
        .supports_vca_gate = snapshot.supports_vca_gate
    };
    return 1U;
}

uint16_t audio_note_engine_adapter_entity_mask(
    track_runtime_engine_t engine)
{
    return ((uint8_t)engine < (uint8_t)TRACK_RUNTIME_ENGINE_COUNT)
        ? g_audio_entity_mask_by_engine[(uint8_t)engine] : 0U;
}

brick_entity_id_t audio_note_engine_adapter_entity_for_mix_lane(
    uint8_t mix_track_id)
{
    return (mix_track_id < MIXER_MAX_TRACKS)
        ? g_audio_entity_by_mix_lane[mix_track_id]
        : BRICK_ENTITY_INVALID_ID;
}

uint8_t audio_note_engine_adapter_snapshot_read(
    brick_entity_id_t entity_id,
    audio_binding_snapshot_t *out_snapshot)
{
    if ((entity_id >= BRICK_ENTITY_CAPACITY) || (out_snapshot == NULL))
        return 0U;
    for (;;)
    {
        const uint32_t before = g_audio_binding_snapshot_sequence;
        __DMB();
        if ((before & 1U) != 0U)
            continue;
        *out_snapshot = g_audio_binding_snapshot[entity_id];
        __DMB();
        if (before == g_audio_binding_snapshot_sequence)
            return 1U;
    }
}

uint8_t audio_note_engine_adapter_ctx_is_audio_routable(
    const track_audio_runtime_ctx_t *ctx)
{
    return (uint8_t)((ctx != NULL)
        && (ctx->audio_binding.bind_state == TRACK_RUNTIME_BIND_BOUND)
        && (ctx->audio_binding.mix_track_id < MIXER_MAX_TRACKS));
}

uint8_t audio_note_engine_adapter_ctx_supports_vca_gate(
    const track_audio_runtime_ctx_t *ctx)
{
    if (audio_note_engine_adapter_ctx_is_audio_routable(ctx) == 0U)
        return 0U;
    return ctx->supports_vca_gate;
}

uint8_t audio_note_engine_adapter_ctx_filter_target(
    const track_audio_runtime_ctx_t *ctx,
    uint8_t *out_track)
{
    if ((ctx == NULL) || (out_track == NULL)
            || (ctx->has_filter_target == 0U))
        return 0U;
    *out_track = ctx->filter_track_id;
    return 1U;
}

uint8_t audio_note_engine_adapter_audio_midi_channel_zero_based(
    const track_audio_runtime_ctx_t *ctx,
    uint8_t *out_channel)
{
    if ((ctx == NULL) || (out_channel == NULL)
            || (ctx->midi_channel_1_16 < 1U)
            || (ctx->midi_channel_1_16 > 16U))
        return 0U;
    *out_channel = (uint8_t)(ctx->midi_channel_1_16 - 1U);
    return 1U;
}

uint8_t audio_note_engine_adapter_resolve(
    brick_entity_id_t entity_id,
    uint32_t binding_generation,
    audio_note_engine_binding_t *out_binding)
{
    if ((out_binding == NULL) || (entity_id >= BRICK_ENTITY_CAPACITY))
        return 0U;

    track_audio_runtime_ctx_t ctx;
    if ((audio_note_engine_adapter_audio_ctx_snapshot(entity_id, &ctx) == 0U)
            || (ctx.audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx.audio_binding.generation != binding_generation))
        return 0U;

    *out_binding = (audio_note_engine_binding_t){
        .audio_binding = ctx.audio_binding,
        .type = (track_runtime_type_t)ctx.type,
        .has_mix_target = (ctx.audio_binding.mix_track_id < MIXER_MAX_TRACKS) ? 1U : 0U,
        .mix_track_id = ctx.audio_binding.mix_track_id,
        .has_filter_target = ctx.has_filter_target,
        .filter_track_id = ctx.filter_track_id,
        .supports_vca_gate = ctx.supports_vca_gate
    };
    return 1U;
}

uint8_t audio_note_engine_adapter_apply(
    const audio_note_engine_binding_t *binding,
    uint8_t note,
    uint8_t velocity,
    uint8_t is_note_on,
    uint32_t output_id)
{
    if ((binding == NULL)
            || (binding->audio_binding.entity_id >= BRICK_ENTITY_CAPACITY)
            || (binding->audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND))
        return 0U;

    const brick_entity_id_t entity_id = binding->audio_binding.entity_id;
    const track_runtime_engine_t engine =
        (track_runtime_engine_t)binding->audio_binding.engine;
    const uint8_t poly_count = synth_polyphony_get_voice_count(entity_id);
    const uint8_t is_poly_synth = (uint8_t)((poly_count > 1U)
        && ((engine == TRACK_RUNTIME_ENGINE_PRISM)
            || (engine == TRACK_RUNTIME_ENGINE_STACK)
            || (engine == TRACK_RUNTIME_ENGINE_WAVE)
            || (engine == TRACK_RUNTIME_ENGINE_FM)));
    const uint8_t uses_voice_allocator = (uint8_t)((is_poly_synth != 0U)
        || (engine == TRACK_RUNTIME_ENGINE_FM));
    const uint8_t is_multi_sampler = (uint8_t)((engine
            == TRACK_RUNTIME_ENGINE_SAMPLER)
        && (binding->type == TRACK_RUNTIME_TYPE_MULTI));

    if ((is_poly_synth == 0U) && (is_multi_sampler == 0U)
            && (output_id != 0U))
    {
        if (is_note_on != 0U)
        {
            g_audio_mono_output[entity_id] = output_id;
        }
        else if (g_audio_mono_output[entity_id] != output_id)
        {
            return 1U;
        }
    }

    const uint8_t voice = (uses_voice_allocator == 0U)
        ? SYNTH_POLYPHONY_NO_VOICE
        : ((is_note_on != 0U)
            ? synth_polyphony_note_on_output_from(
                entity_id, note, SYNTH_POLY_SOURCE_MUSICAL_OUTPUT,
                output_id)
            : synth_polyphony_note_off_output_from(
                entity_id, SYNTH_POLY_SOURCE_MUSICAL_OUTPUT,
                output_id));
    if ((uses_voice_allocator != 0U) && (voice == SYNTH_POLYPHONY_NO_VOICE))
        return 0U;

    if (is_note_on != 0U)
        mod_lfo_v1_note_trigger(entity_id);
    else
        mod_lfo_v1_note_release(entity_id);

    const uint8_t instance = (voice == SYNTH_POLYPHONY_NO_VOICE)
        ? binding->audio_binding.instance_id
        : SYNTH_POLYPHONY_INSTANCE(entity_id, voice);
    if ((is_note_on != 0U) && (is_poly_synth != 0U)
            && (voice != SYNTH_POLYPHONY_NO_VOICE))
    {
        mod_lfo_v1_poly_voice_reset(instance);
        mod_lfo_v1_poly_note_trigger(entity_id, instance);
    }

    if ((is_poly_synth != 0U) && (voice != SYNTH_POLYPHONY_NO_VOICE)
            && (binding->has_mix_target != 0U))
    {
        if (is_note_on != 0U)
            mixer_track_poly_note_on(entity_id, binding->mix_track_id,
                                     voice, note, velocity);
        else
            mixer_track_poly_note_off(entity_id, voice, note);
    }
    else if (binding->has_filter_target != 0U)
    {
        if (is_note_on != 0U)
            mixer_track_filter_note_on(binding->filter_track_id, note, velocity);
        else
            mixer_track_filter_note_off(binding->filter_track_id, note);
    }
    if ((is_poly_synth == 0U) && (binding->supports_vca_gate != 0U)
            && (binding->has_mix_target != 0U)
            && (is_multi_sampler == 0U))
    {
        if (is_note_on != 0U)
            mixer_track_vca_note_on(binding->mix_track_id, note, velocity);
        else
            mixer_track_vca_note_off(binding->mix_track_id, note);
    }

    if (engine == TRACK_RUNTIME_ENGINE_DRUM)
    {
        if (is_note_on != 0U)
            drum_synth_note_on_for_instance(binding->audio_binding.instance_id,
                                            note, velocity);
        else
            drum_synth_note_off_for_instance(binding->audio_binding.instance_id,
                                             note);
    }
    else if (engine == TRACK_RUNTIME_ENGINE_PRISM)
    {
        if (is_note_on != 0U)
        {
            brick6_braids_runtime_sync_voice(
                binding->audio_binding.instance_id, instance);
            brick6_braids_runtime_note_on(instance, (float)note,
                                          (float)velocity / 127.0f);
        }
        else
            brick6_braids_runtime_note_off(instance, note);
    }
    else if (engine == TRACK_RUNTIME_ENGINE_STACK)
    {
        if (is_note_on != 0U)
        {
            brick6_stack_runtime_sync_voice(
                binding->audio_binding.instance_id, instance);
            brick6_stack_runtime_note_on(instance, note, velocity);
        }
        else
            brick6_stack_runtime_note_off(instance, note);
    }
    else if (engine == TRACK_RUNTIME_ENGINE_WAVE)
    {
        if (is_note_on != 0U)
        {
            brick6_wave_runtime_sync_voice(
                binding->audio_binding.instance_id, instance);
            brick6_wave_runtime_note_on(instance, note, velocity);
        }
        else
            brick6_wave_runtime_note_off(instance, note);
    }
    else if (engine == TRACK_RUNTIME_ENGINE_FM)
    {
        if (is_note_on != 0U)
        {
            brick6_fm_runtime_sync_voice(binding->audio_binding.instance_id,
                                         instance);
            brick6_fm_runtime_note_on(instance, note, velocity);
        }
        else
            brick6_fm_runtime_note_off(instance, note);
    }
    else if (engine == TRACK_RUNTIME_ENGINE_SAMPLER)
    {
        if (is_multi_sampler != 0U)
        {
            uint8_t sampler_result;
            if (is_note_on != 0U)
                sampler_result = brick6_sampler_runtime_trigger_multi_track_note_velocity_output(
                    entity_id, note, velocity, output_id);
            else
            {
                brick6_sampler_runtime_note_off_multi_track_note_output(
                    entity_id, note, output_id);
                sampler_result = 1U;
            }
            return sampler_result;
        }
        if (is_note_on != 0U)
            brick6_sampler_runtime_trigger_note_velocity(entity_id, note,
                                                          velocity);
        else
            brick6_sampler_runtime_note_off_note(entity_id, note);
    }

    if ((is_poly_synth == 0U) && (is_multi_sampler == 0U)
            && (output_id != 0U))
        g_audio_mono_output[entity_id] =
            (is_note_on != 0U) ? output_id : 0U;
    return 1U;
}

static uint8_t audio_note_engine_adapter_mix_target_available(
    brick_entity_id_t entity_id, uint8_t mix_track)
{
    if (mix_track >= MIXER_MAX_TRACKS)
        return 0U;
    if ((mix_track == MIXER_GROUP_BUS_TRACK)
            && ((g_audio_track_ctx[entity_id].flags
                & AUDIO_RUNTIME_FLAG_GROUP_MASTER) == 0U))
        return 0U;
    for (brick_entity_id_t other = 0U;
         other < BRICK_ENTITY_CAPACITY; ++other)
    {
        const track_audio_binding_t *const binding =
            &g_audio_track_ctx[other].audio_binding;
        if ((other != entity_id)
                && (binding->bind_state == TRACK_RUNTIME_BIND_BOUND)
                && (binding->mix_track_id == mix_track))
            return 0U;
    }
    return 1U;
}

static uint8_t audio_note_engine_adapter_choose_mix_target(
    brick_entity_id_t entity_id, uint8_t previous)
{
    if (audio_note_engine_adapter_mix_target_available(
            entity_id, previous) != 0U)
        return previous;
    if (audio_note_engine_adapter_mix_target_available(
            entity_id, (uint8_t)entity_id) != 0U)
        return (uint8_t)entity_id;
    for (uint8_t mix_track = 0U; mix_track < MIXER_MAX_TRACKS; ++mix_track)
        if (audio_note_engine_adapter_mix_target_available(
                entity_id, mix_track) != 0U)
            return mix_track;
    return 0xFFU;
}

void audio_note_engine_adapter_install_intent(
    const control_audio_event_t *event)
{
    audio_note_engine_install_spec_t spec;
    if (audio_note_engine_adapter_prepare_install_spec(event, &spec) == 0U)
        return;

    audio_note_engine_adapter_install_prepared(&spec);
}

void audio_note_engine_adapter_install_prepared(
    const audio_note_engine_install_spec_t *spec)
{
    if ((spec == NULL) || (spec->entity_id >= BRICK_ENTITY_CAPACITY))
        return;

    const brick_entity_id_t entity_id = spec->entity_id;
    const track_runtime_family_t family =
        (track_runtime_family_t)spec->family;
    const track_runtime_type_t type = (track_runtime_type_t)spec->type;
    const track_runtime_engine_t requested_engine =
        audio_note_engine_adapter_choose_engine(family, type);
    track_audio_runtime_ctx_t *const ctx = &g_audio_track_ctx[entity_id];
    const uint8_t previous_mix = ctx->audio_binding.mix_track_id;
    uint32_t generation = ctx->audio_binding.generation + 1U;
    if (generation == 0U)
        generation = 1U;

    if (requested_engine != TRACK_RUNTIME_ENGINE_LOOPER)
        brick6_looper_runtime_prepare_replace(entity_id);

    (void)synth_polyphony_set_track_active(entity_id, 0U, 0U);
    *ctx = (track_audio_runtime_ctx_t){
        .midi_channel_1_16 = spec->midi_channel_1_16,
        .midi_source = spec->midi_source,
        .family = (uint8_t)family,
        .type = (uint8_t)type,
        .flags = spec->flags
    };
    track_audio_binding_t installed = {
        .entity_id = entity_id,
        .mix_track_id = 0xFFU,
        .engine = (uint8_t)requested_engine,
        .instance_id = 0xFFU,
        .bind_state = TRACK_RUNTIME_BIND_UNBOUND,
        .bind_reason = TRACK_RUNTIME_BIND_REASON_UNSUPPORTED,
        .generation = generation
    };

    if ((family == TRACK_RUNTIME_FAMILY_OFF)
            || (family == TRACK_RUNTIME_FAMILY_OTHER))
        installed.bind_reason = TRACK_RUNTIME_BIND_REASON_TRACK_OFF;
    else if ((spec->flags & AUDIO_RUNTIME_FLAG_GROUP_MASTER) != 0U)
    {
        installed.mix_track_id = MIXER_GROUP_BUS_TRACK;
        installed.bind_state = TRACK_RUNTIME_BIND_BOUND;
        installed.bind_reason = TRACK_RUNTIME_BIND_REASON_NONE;
    }
    else if (family == TRACK_RUNTIME_FAMILY_MIDI)
    {
        installed.bind_state = TRACK_RUNTIME_BIND_BOUND;
        installed.bind_reason = TRACK_RUNTIME_BIND_REASON_NONE;
    }
    else if (requested_engine != TRACK_RUNTIME_ENGINE_NONE)
    {
        installed.mix_track_id = audio_note_engine_adapter_choose_mix_target(
            entity_id, previous_mix);
        if (installed.mix_track_id == 0xFFU)
        {
            installed.bind_state = TRACK_RUNTIME_BIND_QUOTA_BLOCKED;
            installed.bind_reason = TRACK_RUNTIME_BIND_REASON_QUOTA_EXCEEDED;
        }
        else
        {
            installed.bind_state = TRACK_RUNTIME_BIND_BOUND;
            installed.bind_reason = TRACK_RUNTIME_BIND_REASON_NONE;
            installed.instance_id = (uint8_t)entity_id;
        }
    }

    if ((requested_engine == TRACK_RUNTIME_ENGINE_LOOPER)
            && (installed.bind_state == TRACK_RUNTIME_BIND_BOUND))
    {
        uint8_t looper_count = 0U;
        for (brick_entity_id_t other = 0U;
             other < BRICK_ENTITY_CAPACITY; ++other)
            if ((other != entity_id)
                    && (g_audio_track_ctx[other].audio_binding.bind_state
                        == TRACK_RUNTIME_BIND_BOUND)
                    && (g_audio_track_ctx[other].audio_binding.engine
                        == (uint8_t)TRACK_RUNTIME_ENGINE_LOOPER))
                ++looper_count;
        if (looper_count >= BRICK6_LOOPER_GLOBAL_CAP)
        {
            installed.engine = (uint8_t)TRACK_RUNTIME_ENGINE_NONE;
            installed.instance_id = 0xFFU;
            installed.mix_track_id = 0xFFU;
            installed.bind_state = TRACK_RUNTIME_BIND_QUOTA_BLOCKED;
            installed.bind_reason = TRACK_RUNTIME_BIND_REASON_QUOTA_EXCEEDED;
        }
        else
            installed.instance_id = 0U;
    }

    if ((installed.bind_state == TRACK_RUNTIME_BIND_BOUND)
            && ((installed.engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
                || (installed.engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
                || (installed.engine == (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
                || (installed.engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
                || (installed.engine == (uint8_t)TRACK_RUNTIME_ENGINE_FM)))
    {
        if (synth_polyphony_set_track_active(entity_id, 1U,
                                             installed.engine) == 0U)
        {
            installed.engine = (uint8_t)TRACK_RUNTIME_ENGINE_NONE;
            installed.instance_id = 0xFFU;
            installed.bind_state = TRACK_RUNTIME_BIND_QUOTA_BLOCKED;
            installed.bind_reason = TRACK_RUNTIME_BIND_REASON_QUOTA_EXCEEDED;
        }
        else
        {
            uint8_t voices = spec->voice_count;
            if (installed.engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
                voices = 1U;
            (void)synth_polyphony_set_voice_count(entity_id, voices);
            synth_polyphony_set_spread(entity_id, spec->voice_spread);
            installed.instance_id = synth_polyphony_get_slot(entity_id, 0U);
        }
    }

    ctx->audio_binding = installed;
    ctx->has_filter_target = (uint8_t)((installed.bind_state == TRACK_RUNTIME_BIND_BOUND)
        && ((ctx->flags & AUDIO_RUNTIME_FLAG_CAN_FILTER) != 0U)
        && (installed.mix_track_id < MIXER_MAX_TRACKS));
    ctx->filter_track_id = installed.mix_track_id;
    ctx->supports_vca_gate = (uint8_t)((installed.bind_state == TRACK_RUNTIME_BIND_BOUND)
        && !((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_LOOPER))
        && ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
            || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_DRUM)
            || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_EXTERNAL)));
    g_audio_installed_generation[entity_id] = installed.generation;
    audio_note_engine_adapter_rebuild_binding_projections();
    audio_note_engine_adapter_publish_snapshots();
    mixer_rebuild_static_plan();
    audio_mod_matrix_rebuild_track(entity_id);
    audio_fx_runtime_rebuild_entity_plan(entity_id);
}

uint8_t audio_note_engine_adapter_apply_polyphony(
    brick_entity_id_t entity_id, uint8_t voice_count, float spread)
{
    track_audio_runtime_ctx_t ctx;
    if ((audio_note_engine_adapter_audio_ctx_snapshot(entity_id, &ctx) == 0U)
            || (ctx.audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND))
        return 0U;
    const track_audio_binding_t *const binding = &ctx.audio_binding;
    if (binding->engine == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
    {
        const uint8_t previous_voice_count =
            brick6_sampler_runtime_get_multi_voice_count(entity_id);
        brick6_sampler_runtime_set_multi_voice_count(entity_id, voice_count);
        brick6_sampler_runtime_set_multi_spread(entity_id, spread);
        if (previous_voice_count
                != brick6_sampler_runtime_get_multi_voice_count(entity_id))
        {
            audio_note_engine_adapter_publish_snapshots();
            audio_fx_runtime_rebuild_entity_plan(entity_id);
        }
        return 1U;
    }
    if ((binding->engine != (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
            && (binding->engine != (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
            && (binding->engine != (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
            && (binding->engine != (uint8_t)TRACK_RUNTIME_ENGINE_FM)
            && (binding->engine != (uint8_t)TRACK_RUNTIME_ENGINE_DRUM))
        return 0U;
    if (binding->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
        voice_count = 1U;
    const uint8_t previous_voice_count =
        synth_polyphony_get_voice_count(entity_id);
    (void)synth_polyphony_set_voice_count(entity_id, voice_count);
    synth_polyphony_set_spread(entity_id, spread);
    if (previous_voice_count != synth_polyphony_get_voice_count(entity_id))
    {
        audio_note_engine_adapter_publish_snapshots();
        audio_fx_runtime_rebuild_entity_plan(entity_id);
    }
    return 1U;
}

uint8_t audio_note_engine_adapter_set_mute(brick_entity_id_t entity_id,
                                           uint8_t muted)
{
    track_audio_runtime_ctx_t ctx;
    if (audio_note_engine_adapter_audio_ctx_snapshot(entity_id, &ctx) == 0U)
        return 0U;
    const track_audio_binding_t *const binding = &ctx.audio_binding;
    if (binding->mix_track_id >= MIXER_MAX_TRACKS)
        return 0U;
    mixer_set_track_mute(binding->mix_track_id, muted != 0U ? 1U : 0U);
    return 1U;
}

uint8_t audio_note_engine_adapter_set_master(float gain)
{
    mixer_set_master(gain);
    return 1U;
}

uint32_t audio_note_engine_adapter_installed_generation(
    brick_entity_id_t entity_id)
{
    return (entity_id < BRICK_ENTITY_CAPACITY)
        ? g_audio_installed_generation[entity_id] : 0U;
}
