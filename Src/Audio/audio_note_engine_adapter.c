#include "Audio/audio_note_engine_adapter.h"

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
#include "Param/param_registry_runtime_state.h"
#include "Storage/memory_layout.h"

static uint32_t g_audio_mono_occurrence[BRICK_ENTITY_CAPACITY];
static uint32_t g_audio_installed_generation[BRICK_ENTITY_CAPACITY];
static track_audio_runtime_ctx_t g_audio_track_ctx[BRICK_ENTITY_CAPACITY];
static audio_binding_snapshot_t g_audio_binding_snapshot[BRICK_ENTITY_CAPACITY];
static volatile uint32_t g_audio_binding_snapshot_sequence;

static void audio_note_engine_adapter_publish_snapshots(void)
{
    ++g_audio_binding_snapshot_sequence;
    __DMB();
    for (brick_entity_id_t entity = 0U;
         entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        float sample_id_value = 0.0f;
        brick6_sampler_ram_playhead_snapshot_t playhead = {0};
        (void)param_registry_runtime_cache_get(
            entity, PARAM_SAMPLER_SAMPLE, &sample_id_value);
        (void)brick6_sampler_runtime_get_ram_playhead(
            entity, (uint16_t)sample_id_value, &playhead);
        const uint8_t is_multi = (uint8_t)(
            (g_audio_track_ctx[entity].audio_binding.engine
                == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
            && (g_audio_track_ctx[entity].type
                == (uint8_t)TRACK_RUNTIME_TYPE_MULTI));
        g_audio_binding_snapshot[entity] = (audio_binding_snapshot_t){
            .binding = g_audio_track_ctx[entity].audio_binding,
            .family = g_audio_track_ctx[entity].family,
            .type = g_audio_track_ctx[entity].type,
            .flags = g_audio_track_ctx[entity].flags,
            .physical_voice_count = (is_multi != 0U)
                ? brick6_sampler_runtime_get_multi_voice_count(entity)
                : synth_polyphony_get_voice_count(entity),
            .physical_voice_capacity = (is_multi != 0U)
                ? SAMPLER_MULTI_MAX_VOICES_PER_TRACK
                : synth_polyphony_get_available_for_track(entity),
            .sampler_slice_mode_active =
                brick6_sampler_runtime_ram_slice_mode_active(entity),
            .sampler_playhead_active = playhead.active,
            .sampler_playhead_reverse = playhead.reverse,
            .sampler_playhead_sample_id = playhead.sample_id,
            .sampler_playhead_frame = playhead.frame,
            .sampler_playhead_frame_count = playhead.frame_count
        };
    }
    __DMB();
    ++g_audio_binding_snapshot_sequence;
    __DMB();
}

void audio_note_engine_adapter_audio_publish_snapshot(void)
{
    audio_note_engine_adapter_publish_snapshots();
}

void audio_note_engine_adapter_init(void)
{
    memset(g_audio_mono_occurrence, 0, sizeof(g_audio_mono_occurrence));
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
    audio_note_engine_adapter_publish_snapshots();
}

const track_audio_runtime_ctx_t *audio_note_engine_adapter_audio_ctx(
    brick_entity_id_t entity_id)
{
    return (entity_id < BRICK_ENTITY_CAPACITY)
        ? &g_audio_track_ctx[entity_id] : NULL;
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
    if ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_LOOPER))
        return 0U;
    return (uint8_t)((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
        || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
        || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_DRUM)
        || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_EXTERNAL));
}

uint8_t audio_note_engine_adapter_resolve(
    brick_entity_id_t entity_id,
    uint32_t binding_generation,
    audio_note_engine_binding_t *out_binding)
{
    if ((out_binding == NULL) || (entity_id >= BRICK_ENTITY_CAPACITY))
        return 0U;

    const track_audio_runtime_ctx_t *const ctx =
        audio_note_engine_adapter_audio_ctx(entity_id);
    const track_audio_binding_t *const current =
        (ctx != NULL) ? &ctx->audio_binding : NULL;
    if ((current == NULL) || (current->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (current->generation != binding_generation))
        return 0U;

    *out_binding = (audio_note_engine_binding_t){
        .audio_binding = *current,
        .type = (track_runtime_type_t)ctx->type,
        .has_mix_target = (current->mix_track_id < MIXER_MAX_TRACKS) ? 1U : 0U,
        .mix_track_id = current->mix_track_id,
        .has_filter_target = ((ctx->flags & 1U) != 0U)
            && (current->mix_track_id < MIXER_MAX_TRACKS),
        .filter_track_id = current->mix_track_id,
        .supports_vca_gate = (uint8_t)(
            (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
            || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_DRUM))
    };
    return 1U;
}

uint8_t audio_note_engine_adapter_apply(
    const audio_note_engine_binding_t *binding,
    uint8_t note,
    uint8_t velocity,
    uint8_t is_note_on,
    uint32_t occurrence_token)
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
            && (occurrence_token != 0U))
    {
        if (is_note_on != 0U)
        {
            g_audio_mono_occurrence[entity_id] = occurrence_token;
        }
        else if (g_audio_mono_occurrence[entity_id] != occurrence_token)
        {
            return 1U;
        }
    }

    const uint8_t voice = (uses_voice_allocator == 0U)
        ? SYNTH_POLYPHONY_NO_VOICE
        : ((is_note_on != 0U)
            ? synth_polyphony_note_on_occurrence_from(
                entity_id, note, SYNTH_POLY_SOURCE_SEQUENCER,
                occurrence_token)
            : synth_polyphony_note_off_occurrence_from(
                entity_id, SYNTH_POLY_SOURCE_SEQUENCER,
                occurrence_token));
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
            if (is_note_on != 0U)
                return brick6_sampler_runtime_trigger_multi_track_note_velocity_token(
                    entity_id, note, velocity, occurrence_token);
            brick6_sampler_runtime_note_off_multi_track_note_token(
                entity_id, note, occurrence_token);
            return 1U;
        }
        if (is_note_on != 0U)
            brick6_sampler_runtime_trigger_note_velocity(entity_id, note,
                                                          velocity);
        else
            brick6_sampler_runtime_note_off_note(entity_id, note);
    }

    if ((is_poly_synth == 0U) && (is_multi_sampler == 0U)
            && (occurrence_token != 0U))
        g_audio_mono_occurrence[entity_id] =
            (is_note_on != 0U) ? occurrence_token : 0U;
    return 1U;
}

static float audio_note_engine_adapter_decode_float(uint32_t bits)
{
    union { uint32_t u; float f; } value = { .u = bits };
    return value.f;
}

static uint8_t audio_note_engine_adapter_mix_target_available(
    brick_entity_id_t entity_id, uint8_t mix_track)
{
    if (mix_track >= MIXER_MAX_TRACKS)
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

static track_runtime_engine_t audio_note_engine_adapter_choose_engine(
    track_runtime_family_t family, track_runtime_type_t type)
{
    if ((family == TRACK_RUNTIME_FAMILY_EXTERNAL)
            && (type == TRACK_RUNTIME_TYPE_EXTERNAL))
        return TRACK_RUNTIME_ENGINE_AUDIO_TRACK;
    if (family == TRACK_RUNTIME_FAMILY_DRUM)
        return ((type == TRACK_RUNTIME_TYPE_DRUM_MD)
                || (type == TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG))
            ? TRACK_RUNTIME_ENGINE_DRUM : TRACK_RUNTIME_ENGINE_NONE;
    if (family == TRACK_RUNTIME_FAMILY_SAMPLER)
    {
        if (type == TRACK_RUNTIME_TYPE_LOOPER)
            return TRACK_RUNTIME_ENGINE_LOOPER;
        if ((type == TRACK_RUNTIME_TYPE_RAM)
                || (type == TRACK_RUNTIME_TYPE_STREAM)
                || (type == TRACK_RUNTIME_TYPE_MULTI))
            return TRACK_RUNTIME_ENGINE_SAMPLER;
    }
    if (family == TRACK_RUNTIME_FAMILY_SYNTH)
    {
        if (type == TRACK_RUNTIME_TYPE_PRISM) return TRACK_RUNTIME_ENGINE_PRISM;
        if (type == TRACK_RUNTIME_TYPE_STACK) return TRACK_RUNTIME_ENGINE_STACK;
        if (type == TRACK_RUNTIME_TYPE_WAVE) return TRACK_RUNTIME_ENGINE_WAVE;
        if (type == TRACK_RUNTIME_TYPE_FM) return TRACK_RUNTIME_ENGINE_FM;
    }
    return TRACK_RUNTIME_ENGINE_NONE;
}

void audio_note_engine_adapter_install_intent(
    const control_audio_event_t *event)
{
    if ((event == NULL) || (event->entity_id >= BRICK_ENTITY_CAPACITY))
        return;

    const brick_entity_id_t entity_id = event->entity_id;
    const track_runtime_family_t family =
        (track_runtime_family_t)event->provenance;
    const track_runtime_type_t type = (track_runtime_type_t)event->note;
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
        .family = (uint8_t)family,
        .type = (uint8_t)type,
        .flags = event->flags
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
    else if (type == TRACK_RUNTIME_TYPE_GROUP)
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
            uint8_t voices = event->param_id != 0U
                ? (uint8_t)event->param_id : 1U;
            if (installed.engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
                voices = 1U;
            (void)synth_polyphony_set_voice_count(entity_id, voices);
            synth_polyphony_set_spread(
                entity_id,
                audio_note_engine_adapter_decode_float(event->source_generation));
            installed.instance_id = synth_polyphony_get_slot(entity_id, 0U);
        }
    }

    ctx->audio_binding = installed;
    g_audio_installed_generation[entity_id] = installed.generation;
    audio_note_engine_adapter_publish_snapshots();
}

uint8_t audio_note_engine_adapter_apply_polyphony(
    brick_entity_id_t entity_id, uint8_t voice_count, float spread)
{
    const track_audio_runtime_ctx_t *const ctx =
        audio_note_engine_adapter_audio_ctx(entity_id);
    const track_audio_binding_t *const binding =
        (ctx != NULL) ? &ctx->audio_binding : NULL;
    if ((binding == NULL) || (binding->bind_state != TRACK_RUNTIME_BIND_BOUND))
        return 0U;
    if (binding->engine == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
    {
        brick6_sampler_runtime_set_multi_voice_count(entity_id, voice_count);
        brick6_sampler_runtime_set_multi_spread(entity_id, spread);
        audio_note_engine_adapter_publish_snapshots();
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
    (void)synth_polyphony_set_voice_count(entity_id, voice_count);
    synth_polyphony_set_spread(entity_id, spread);
    audio_note_engine_adapter_publish_snapshots();
    return 1U;
}

uint8_t audio_note_engine_adapter_set_mute(brick_entity_id_t entity_id,
                                           uint8_t muted)
{
    const track_audio_runtime_ctx_t *const ctx =
        audio_note_engine_adapter_audio_ctx(entity_id);
    const track_audio_binding_t *const binding =
        (ctx != NULL) ? &ctx->audio_binding : NULL;
    if ((binding == NULL) || (binding->mix_track_id >= MIXER_MAX_TRACKS))
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
