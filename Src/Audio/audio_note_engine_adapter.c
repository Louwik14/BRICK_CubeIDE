#include "Audio/audio_note_engine_adapter.h"
#include "Audio/audio_mod_matrix.h"
#include "Audio/audio_fx_runtime.h"

#include <stddef.h>
#include <string.h>

#include "Audio/drum_synth.h"
#include "Audio/mixer.h"
#include "Audio/Engines/prism_engine.h"
#include "Audio/Engines/fm_engine.h"
#include "Audio/Engines/Sampler/brick6_sampler_runtime.h"
#include "Audio/Engines/stack_engine.h"
#include "Audio/Engines/wavetable_engine.h"
#include "Platform/brick_build_config.h"
#include "IPC/control_audio_command.h"
#include "Track/synth_polyphony.h"
#include "Mod/mod_lfo_v1.h"

static track_audio_runtime_ctx_t g_audio_track_ctx[BRICK_ENTITY_CAPACITY];
static uint16_t g_audio_entity_mask_by_engine[TRACK_RUNTIME_ENGINE_COUNT];
static uint8_t g_audio_entity_by_mix_lane[MIXER_MAX_TRACKS];

#define AUDIO_PHYSICAL_OUTPUT_CAPACITY 8U

_Static_assert(AUDIO_PHYSICAL_OUTPUT_CAPACITY >= SYNTH_POLYPHONY_MAX_VOICES,
               "AUDIO output mapping must cover synth polyphony");
_Static_assert(AUDIO_PHYSICAL_OUTPUT_CAPACITY
                   >= SAMPLER_MULTI_MAX_VOICES_PER_TRACK,
               "AUDIO output mapping must cover Multi polyphony");

typedef struct
{
    uint32_t output_id;
    uint8_t note;
    uint8_t velocity;
    uint8_t gate;
    uint8_t reserved;
} audio_physical_output_t;

_Static_assert(sizeof(audio_physical_output_t) == 8U,
               "AUDIO physical output entry size changed");

/* FIFO-derived execution mirror only: CONTROL remains the sole authority for
 * admission, stealing and musical lifetime.  PROGRAM reads this physical
 * mapping synchronously; it never creates or kills an output here. */
static audio_physical_output_t
    g_audio_physical_output[BRICK_ENTITY_CAPACITY][AUDIO_PHYSICAL_OUTPUT_CAPACITY];

static int8_t audio_note_engine_find_output(brick_entity_id_t entity_id,
                                            uint32_t output_id)
{
    if ((entity_id >= BRICK_ENTITY_CAPACITY) || (output_id == 0U))
        return -1;
    for (uint8_t i = 0U; i < AUDIO_PHYSICAL_OUTPUT_CAPACITY; ++i)
        if ((g_audio_physical_output[entity_id][i].gate != 0U)
                && (g_audio_physical_output[entity_id][i].output_id == output_id))
            return (int8_t)i;
    return -1;
}

static int8_t audio_note_engine_find_free_output(brick_entity_id_t entity_id)
{
    if (entity_id >= BRICK_ENTITY_CAPACITY)
        return -1;
    for (uint8_t i = 0U; i < AUDIO_PHYSICAL_OUTPUT_CAPACITY; ++i)
        if (g_audio_physical_output[entity_id][i].gate == 0U)
            return (int8_t)i;
    return -1;
}

static uint8_t audio_note_engine_commit_output(brick_entity_id_t entity_id,
                                               uint32_t output_id,
                                               uint8_t note,
                                               uint8_t velocity,
                                               uint8_t gate)
{
    int8_t index = audio_note_engine_find_output(entity_id, output_id);
    if (gate == 0U)
    {
        if (index >= 0)
            memset(&g_audio_physical_output[entity_id][(uint8_t)index], 0,
                   sizeof(g_audio_physical_output[entity_id][0]));
        return 1U;
    }
    if (index < 0)
        index = audio_note_engine_find_free_output(entity_id);
    if (index < 0)
        return 0U;
    g_audio_physical_output[entity_id][(uint8_t)index] =
        (audio_physical_output_t){
            .output_id = output_id,
            .note = note,
            .velocity = velocity,
            .gate = 1U
        };
    return 1U;
}

static void audio_note_engine_adapter_rebuild_program_projections(void)
{
    memset(g_audio_entity_mask_by_engine, 0,
           sizeof(g_audio_entity_mask_by_engine));
    memset(g_audio_entity_by_mix_lane, BRICK_ENTITY_INVALID_ID,
           sizeof(g_audio_entity_by_mix_lane));
    for (brick_entity_id_t entity = 0U;
         entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        const audio_program_route_t *const program =
            &g_audio_track_ctx[entity].program_route;
        if (program->active == 0U)
            continue;
        if (program->engine < (uint8_t)TRACK_RUNTIME_ENGINE_COUNT)
        {
            g_audio_entity_mask_by_engine[program->engine] |=
                (uint16_t)(1U << entity);
        }
        if ((program->mix_track_id < MIXER_MAX_TRACKS)
                && (g_audio_entity_by_mix_lane[program->mix_track_id]
                    == BRICK_ENTITY_INVALID_ID))
        {
            g_audio_entity_by_mix_lane[program->mix_track_id] = entity;
        }
    }
}

void audio_note_engine_adapter_init(void)
{
    memset(g_audio_track_ctx, 0, sizeof(g_audio_track_ctx));
    memset(g_audio_physical_output, 0, sizeof(g_audio_physical_output));
    for (brick_entity_id_t entity = 0U;
         entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        g_audio_track_ctx[entity].program_route.entity_id = entity;
        g_audio_track_ctx[entity].program_route.mix_track_id = 0xFFU;
        g_audio_track_ctx[entity].program_route.instance_id = 0xFFU;
    }
    audio_note_engine_adapter_rebuild_program_projections();
    mixer_rebuild_static_plan();
}

uint8_t audio_note_engine_adapter_current_ctx(
    brick_entity_id_t entity_id,
    track_audio_runtime_ctx_t *out_context)
{
    if ((entity_id >= BRICK_ENTITY_CAPACITY) || (out_context == NULL))
        return 0U;
    *out_context = g_audio_track_ctx[entity_id];
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

uint8_t audio_note_engine_adapter_ctx_is_audio_routable(
    const track_audio_runtime_ctx_t *ctx)
{
    return (uint8_t)((ctx != NULL)
        && (ctx->program_route.active != 0U)
        && (ctx->program_route.mix_track_id < MIXER_MAX_TRACKS));
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

uint8_t audio_note_engine_adapter_current(
    brick_entity_id_t entity_id,
    audio_note_engine_program_t *out_program)
{
    if ((out_program == NULL) || (entity_id >= BRICK_ENTITY_CAPACITY))
        return 0U;
    const track_audio_runtime_ctx_t *const ctx = &g_audio_track_ctx[entity_id];
    if (ctx->program_route.active == 0U) return 0U;
    *out_program = (audio_note_engine_program_t){
        .program_route = ctx->program_route,
        .type = (track_runtime_type_t)ctx->type,
        .has_mix_target = ctx->program_route.mix_track_id < MIXER_MAX_TRACKS,
        .mix_track_id = ctx->program_route.mix_track_id,
        .has_filter_target = ctx->has_filter_target,
        .filter_track_id = ctx->filter_track_id,
        .supports_vca_gate = ctx->supports_vca_gate
    };
    return 1U;
}

static uint8_t audio_note_engine_adapter_initialize_held_renderer(
    const audio_note_engine_program_t *program,
    uint8_t note, uint8_t velocity, uint32_t output_id)
{
    if ((program == NULL) || (program->program_route.active == 0U)) return 0U;
    const brick_entity_id_t entity_id = program->program_route.entity_id;
    const track_runtime_engine_t engine =
        (track_runtime_engine_t)program->program_route.engine;
    const uint8_t voice = ((engine == TRACK_RUNTIME_ENGINE_PRISM)
            || (engine == TRACK_RUNTIME_ENGINE_STACK)
            || (engine == TRACK_RUNTIME_ENGINE_WAVE)
            || (engine == TRACK_RUNTIME_ENGINE_FM))
        ? synth_polyphony_voice_for_output(
            entity_id, SYNTH_POLY_SOURCE_MUSICAL_OUTPUT, output_id)
        : SYNTH_POLYPHONY_NO_VOICE;
    if ((engine != TRACK_RUNTIME_ENGINE_SAMPLER)
            && (voice == SYNTH_POLYPHONY_NO_VOICE)) return 1U;
    const uint8_t instance = (voice == SYNTH_POLYPHONY_NO_VOICE)
        ? program->program_route.instance_id
        : SYNTH_POLYPHONY_INSTANCE(entity_id, voice);
    if (engine == TRACK_RUNTIME_ENGINE_PRISM)
    {
        brick6_braids_runtime_sync_voice(program->program_route.instance_id, instance);
        brick6_braids_runtime_initialize_held_note(
            instance, (float)note, (float)velocity / 127.0f);
    }
    else if (engine == TRACK_RUNTIME_ENGINE_STACK)
    {
        brick6_stack_runtime_sync_voice(program->program_route.instance_id, instance);
        brick6_stack_runtime_initialize_held_note(instance, note, velocity);
    }
    else if (engine == TRACK_RUNTIME_ENGINE_WAVE)
    {
        brick6_wave_runtime_sync_voice(program->program_route.instance_id, instance);
        brick6_wave_runtime_initialize_held_note(instance, note, velocity);
    }
    else if (engine == TRACK_RUNTIME_ENGINE_FM)
    {
        brick6_fm_runtime_sync_voice(program->program_route.instance_id, instance);
        brick6_fm_runtime_initialize_held_note(instance, note, velocity);
    }
    else if (engine == TRACK_RUNTIME_ENGINE_SAMPLER)
        return brick6_sampler_runtime_initialize_held_note(entity_id, note,
            velocity, output_id, (uint8_t)(program->type == TRACK_RUNTIME_TYPE_MULTI));
    return 1U;
}

static uint8_t audio_note_engine_adapter_apply_physical(
    const audio_note_engine_program_t *program,
    uint8_t note,
    uint8_t velocity,
    uint8_t is_note_on,
    uint32_t output_id)
{
    if ((program == NULL)
            || (program->program_route.entity_id >= BRICK_ENTITY_CAPACITY)
            || (program->program_route.active == 0U))
        return 0U;

    const brick_entity_id_t entity_id = program->program_route.entity_id;
    const track_runtime_engine_t engine =
        (track_runtime_engine_t)program->program_route.engine;
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
        && (program->type == TRACK_RUNTIME_TYPE_MULTI));

    if ((is_poly_synth == 0U) && (is_multi_sampler == 0U)
            && (output_id != 0U))
    {
        if ((is_note_on == 0U)
                && (audio_note_engine_find_output(entity_id, output_id) < 0))
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
        return audio_note_engine_commit_output(entity_id, output_id,
            note, velocity, is_note_on);

    if (is_note_on != 0U)
        mod_lfo_v1_note_trigger(entity_id);
    else
        mod_lfo_v1_note_release(entity_id);

    const uint8_t instance = (voice == SYNTH_POLYPHONY_NO_VOICE)
        ? program->program_route.instance_id
        : SYNTH_POLYPHONY_INSTANCE(entity_id, voice);
    if ((is_note_on != 0U) && (is_poly_synth != 0U)
            && (voice != SYNTH_POLYPHONY_NO_VOICE))
    {
        mod_lfo_v1_poly_voice_reset(instance);
        mod_lfo_v1_poly_note_trigger(entity_id, instance);
    }

    if ((is_poly_synth != 0U) && (voice != SYNTH_POLYPHONY_NO_VOICE)
            && (program->has_mix_target != 0U))
    {
        if (is_note_on != 0U)
            mixer_track_poly_note_on(entity_id, program->mix_track_id,
                                     voice, note, velocity);
        else
            mixer_track_poly_note_off(entity_id, voice, note);
    }
    else if (program->has_filter_target != 0U)
    {
        if (is_note_on != 0U)
            mixer_track_filter_note_on(program->filter_track_id, note, velocity);
        else
            mixer_track_filter_note_off(program->filter_track_id, note);
    }
    if ((is_poly_synth == 0U) && (program->supports_vca_gate != 0U)
            && (program->has_mix_target != 0U)
            && (is_multi_sampler == 0U))
    {
        if (is_note_on != 0U)
            mixer_track_vca_note_on(program->mix_track_id, note, velocity);
        else
            mixer_track_vca_note_off(program->mix_track_id, note);
    }

    if (engine == TRACK_RUNTIME_ENGINE_DRUM)
    {
        if (is_note_on != 0U)
            drum_synth_note_on_for_instance(program->program_route.instance_id,
                                            note, velocity);
        else
            drum_synth_note_off_for_instance(program->program_route.instance_id,
                                             note);
    }
    else if (engine == TRACK_RUNTIME_ENGINE_PRISM)
    {
        if (is_note_on != 0U)
        {
            brick6_braids_runtime_sync_voice(
                program->program_route.instance_id, instance);
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
                program->program_route.instance_id, instance);
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
                program->program_route.instance_id, instance);
            brick6_wave_runtime_note_on(instance, note, velocity);
        }
        else
            brick6_wave_runtime_note_off(instance, note);
    }
    else if (engine == TRACK_RUNTIME_ENGINE_FM)
    {
        if (is_note_on != 0U)
        {
            brick6_fm_runtime_sync_voice(program->program_route.instance_id,
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
            const uint8_t committed = audio_note_engine_commit_output(
                entity_id, output_id, note, velocity, is_note_on);
            return (uint8_t)((committed != 0U) && (sampler_result != 0U));
        }
        if (is_note_on != 0U)
            brick6_sampler_runtime_trigger_note_velocity(entity_id, note,
                                                          velocity);
        else
            brick6_sampler_runtime_note_off_note(entity_id, note);
    }

    return audio_note_engine_commit_output(entity_id, output_id,
        note, velocity, is_note_on);
}

uint8_t audio_note_engine_adapter_apply_output(
    brick_entity_id_t entity_id, uint8_t note, uint8_t velocity,
    uint8_t is_note_on, uint32_t output_id)
{
    if (entity_id >= BRICK_ENTITY_CAPACITY)
        return 0U;
    audio_note_engine_program_t program;
    if (audio_note_engine_adapter_current(entity_id, &program) == 0U)
        return audio_note_engine_commit_output(entity_id, output_id,
            note, velocity, is_note_on);
    return audio_note_engine_adapter_apply_physical(&program, note, velocity,
        is_note_on, output_id);
}

static uint8_t audio_note_engine_adapter_mix_target_available(
    brick_entity_id_t entity_id, uint8_t mix_track)
{
    if (mix_track >= MIXER_MAX_TRACKS)
        return 0U;
    if ((mix_track == MIXER_GROUP_BUS_TRACK)
            && ((g_audio_track_ctx[entity_id].flags
                & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER) == 0U))
        return 0U;
    for (brick_entity_id_t other = 0U;
         other < BRICK_ENTITY_CAPACITY; ++other)
    {
        const audio_program_route_t *const program =
            &g_audio_track_ctx[other].program_route;
        if ((other != entity_id)
                && (program->active != 0U)
                && (program->mix_track_id == mix_track))
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

uint8_t audio_note_engine_adapter_install_prepared(
    const audio_note_engine_install_spec_t *spec)
{
    if ((spec == NULL) || (spec->entity_id >= BRICK_ENTITY_CAPACITY))
        return 0U;

    const brick_entity_id_t entity_id = spec->entity_id;
    const track_runtime_family_t family =
        (track_runtime_family_t)spec->family;
    const track_runtime_type_t type = (track_runtime_type_t)spec->type;
    const track_runtime_engine_t requested_engine =
        track_runtime_choose_engine(family, type);
    track_audio_runtime_ctx_t *const ctx = &g_audio_track_ctx[entity_id];
    const uint8_t requested_voices = (ctx->program_route.active != 0U)
        ? synth_polyphony_get_voice_count(entity_id) : 1U;
    const uint8_t preserve_synth_slots = (uint8_t)(
        (ctx->program_route.active != 0U)
        && (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
        && (family == TRACK_RUNTIME_FAMILY_SYNTH)
        && (synth_polyphony_get_voice_count(entity_id) == requested_voices));
    const uint8_t previous_mix = ctx->program_route.mix_track_id;
    if (preserve_synth_slots == 0U)
        (void)synth_polyphony_set_track_active(entity_id, 0U, 0U);
    const uint8_t midi_channel = ctx->midi_channel_1_16;
    const uint8_t midi_source = ctx->midi_source;
    *ctx = (track_audio_runtime_ctx_t){
        .midi_channel_1_16 = midi_channel,
        .midi_source = midi_source,
        .family = (uint8_t)family,
        .type = (uint8_t)type,
        .flags = (uint8_t)(track_runtime_compute_flags(family, type)
            | spec->topology_flags)
    };
    audio_program_route_t installed = {
        .entity_id = entity_id,
        .mix_track_id = 0xFFU,
        .engine = (uint8_t)requested_engine,
        .instance_id = 0xFFU,
        .active = 0U
    };
    uint8_t success = 1U;

    if ((family == TRACK_RUNTIME_FAMILY_OFF)
            || (family == TRACK_RUNTIME_FAMILY_OTHER))
        installed.active = 0U;
    else if ((spec->topology_flags
                & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER) != 0U)
    {
        installed.mix_track_id = MIXER_GROUP_BUS_TRACK;
        installed.active = 1U;
    }
    else if (family == TRACK_RUNTIME_FAMILY_MIDI)
        installed.active = 1U;
    else if (requested_engine != TRACK_RUNTIME_ENGINE_NONE)
    {
        installed.mix_track_id = audio_note_engine_adapter_choose_mix_target(
            entity_id, previous_mix);
        if (installed.mix_track_id == 0xFFU)
            success = 0U;
        else
        {
            installed.active = 1U;
            installed.instance_id = (uint8_t)entity_id;
        }
    }
    else
        success = 0U;

    if ((requested_engine == TRACK_RUNTIME_ENGINE_LOOPER)
            && (installed.active != 0U))
    {
        uint8_t looper_count = 0U;
        for (brick_entity_id_t other = 0U;
             other < BRICK_ENTITY_CAPACITY; ++other)
            if ((other != entity_id)
                    && (g_audio_track_ctx[other].program_route.active != 0U)
                    && (g_audio_track_ctx[other].program_route.engine
                        == (uint8_t)TRACK_RUNTIME_ENGINE_LOOPER))
                ++looper_count;
        if (looper_count >= BRICK6_LOOPER_GLOBAL_CAP)
        {
            installed.engine = (uint8_t)TRACK_RUNTIME_ENGINE_NONE;
            installed.instance_id = 0xFFU;
            installed.mix_track_id = 0xFFU;
            installed.active = 0U;
            success = 0U;
        }
        else
            installed.instance_id = 0U;
    }

    if ((installed.active != 0U)
            && ((installed.engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
                || (installed.engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
                || (installed.engine == (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
                || (installed.engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
                || (installed.engine == (uint8_t)TRACK_RUNTIME_ENGINE_FM)))
    {
        const uint8_t renderer_ready = (preserve_synth_slots != 0U)
            ? synth_polyphony_replace_renderer(entity_id, installed.engine)
            : synth_polyphony_set_track_active(entity_id, 1U,
                                                installed.engine);
        if (renderer_ready == 0U)
        {
            installed.engine = (uint8_t)TRACK_RUNTIME_ENGINE_NONE;
            installed.instance_id = 0xFFU;
            installed.active = 0U;
            success = 0U;
        }
        else
        {
            uint8_t voices = requested_voices;
            if (installed.engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
                voices = 1U;
            if ((preserve_synth_slots == 0U)
                    && (synth_polyphony_set_voice_count(entity_id, voices)
                        != voices))
            {
                (void)synth_polyphony_set_track_active(entity_id, 0U, 0U);
                installed.engine = (uint8_t)TRACK_RUNTIME_ENGINE_NONE;
                installed.instance_id = 0xFFU;
                installed.active = 0U;
                success = 0U;
            }
            if (success != 0U)
                installed.instance_id = synth_polyphony_get_slot(entity_id, 0U);
        }
    }

    ctx->program_route = installed;
    ctx->has_filter_target = (uint8_t)((installed.active != 0U)
        && ((ctx->flags & CONTROL_AUDIO_PROGRAM_FLAG_CAN_FILTER) != 0U)
        && (installed.mix_track_id < MIXER_MAX_TRACKS));
    ctx->filter_track_id = installed.mix_track_id;
    ctx->supports_vca_gate = (uint8_t)((installed.active != 0U)
        && !((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_LOOPER))
        && ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
            || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_DRUM)
            || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_EXTERNAL)));
    audio_note_engine_adapter_rebuild_program_projections();
    mixer_rebuild_static_plan();
    audio_mod_matrix_rebuild_track(entity_id);
    audio_fx_runtime_rebuild_entity_plan(entity_id);
    return success;
}

uint8_t audio_note_engine_adapter_apply_midi_config(
    brick_entity_id_t entity_id, uint8_t channel_1_16, uint8_t source)
{
    if ((entity_id >= BRICK_ENTITY_CAPACITY)
            || (channel_1_16 < 1U) || (channel_1_16 > 16U))
        return 0U;
    g_audio_track_ctx[entity_id].midi_channel_1_16 = channel_1_16;
    g_audio_track_ctx[entity_id].midi_source = source;
    audio_mod_matrix_rebuild_track(entity_id);
    return 1U;
}

uint8_t audio_note_engine_adapter_initialize_held_outputs(
    brick_entity_id_t entity_id)
{
    audio_note_engine_program_t program;
    if (audio_note_engine_adapter_current(entity_id, &program) == 0U)
        return 1U;
    const track_runtime_engine_t engine =
        (track_runtime_engine_t)program.program_route.engine;
    const uint8_t synth_engine = (uint8_t)((engine == TRACK_RUNTIME_ENGINE_PRISM)
        || (engine == TRACK_RUNTIME_ENGINE_STACK)
        || (engine == TRACK_RUNTIME_ENGINE_WAVE)
        || (engine == TRACK_RUNTIME_ENGINE_FM));
    for (uint8_t i = 0U; i < AUDIO_PHYSICAL_OUTPUT_CAPACITY; ++i)
    {
        const audio_physical_output_t held =
            g_audio_physical_output[entity_id][i];
        if (held.gate == 0U)
            continue;
        if (synth_engine != 0U)
        {
            (void)synth_polyphony_bind_held_output(entity_id, held.note,
                SYNTH_POLY_SOURCE_MUSICAL_OUTPUT, held.output_id);
        }
        if (audio_note_engine_adapter_initialize_held_renderer(&program,
                held.note, held.velocity, held.output_id) == 0U)
            continue;
    }
    return 1U;
}

void audio_note_engine_adapter_forget_outputs(brick_entity_id_t entity_id)
{
    if (entity_id >= BRICK_ENTITY_CAPACITY)
        return;
    memset(g_audio_physical_output[entity_id], 0,
           sizeof(g_audio_physical_output[entity_id]));
}

uint8_t audio_note_engine_adapter_apply_polyphony(
    brick_entity_id_t entity_id, uint8_t voice_count, float spread)
{
    track_audio_runtime_ctx_t ctx;
    if ((audio_note_engine_adapter_current_ctx(entity_id, &ctx) == 0U)
            || (ctx.program_route.active == 0U))
        return 0U;
    const audio_program_route_t *const program = &ctx.program_route;
    if (program->engine == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
    {
        const uint8_t previous_voice_count =
            brick6_sampler_runtime_get_multi_voice_count(entity_id);
        brick6_sampler_runtime_set_multi_voice_count(entity_id, voice_count);
        brick6_sampler_runtime_set_multi_spread(entity_id, spread);
        if (previous_voice_count
                != brick6_sampler_runtime_get_multi_voice_count(entity_id))
        {
            audio_fx_runtime_rebuild_entity_plan(entity_id);
        }
        return 1U;
    }
    if ((program->engine != (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
            && (program->engine != (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
            && (program->engine != (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
            && (program->engine != (uint8_t)TRACK_RUNTIME_ENGINE_FM)
            && (program->engine != (uint8_t)TRACK_RUNTIME_ENGINE_DRUM))
        return 0U;
    if (program->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
        voice_count = 1U;
    const uint8_t previous_voice_count =
        synth_polyphony_get_voice_count(entity_id);
    (void)synth_polyphony_set_voice_count(entity_id, voice_count);
    synth_polyphony_set_spread(entity_id, spread);
    if (previous_voice_count != synth_polyphony_get_voice_count(entity_id))
    {
        if (audio_note_engine_adapter_initialize_held_outputs(entity_id) == 0U)
            return 0U;
        audio_fx_runtime_rebuild_entity_plan(entity_id);
    }
    return 1U;
}

uint8_t audio_note_engine_adapter_set_mute(brick_entity_id_t entity_id,
                                           uint8_t muted)
{
    track_audio_runtime_ctx_t ctx;
    if (audio_note_engine_adapter_current_ctx(entity_id, &ctx) == 0U)
        return 0U;
    const audio_program_route_t *const program = &ctx.program_route;
    if (program->mix_track_id >= MIXER_MAX_TRACKS)
        return 0U;
    mixer_set_track_mute(program->mix_track_id, muted != 0U ? 1U : 0U);
    return 1U;
}

uint8_t audio_note_engine_adapter_set_master(float gain)
{
    mixer_set_master(gain);
    return 1U;
}
