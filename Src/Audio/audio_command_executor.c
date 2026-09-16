#include "Audio/audio_command_executor.h"

#include <string.h>

#include "IPC/control_audio_command.h"
#include "IPC/control_audio_fifo_audio.h"
#include "Seq/seq_rt_pass1.h"
#include "IPC/audio_state_snapshot.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Audio/audio_mod_matrix.h"
#include "Audio/metronome_runtime.h"
#include "Audio/mixer.h"
#include "Audio/drum_synth.h"
#include "Audio/audio_transport_runtime.h"
#include "Audio/Engines/fm_engine.h"
#include "Audio/Engines/tb303_engine.h"
#include "Audio/Engines/acid_engine.h"
#include "Audio/Engines/Sampler/brick6_sampler_runtime.h"
#include "Audio/Engines/wavetable_engine.h"
#include "Audio/Engines/audio_engine_dispatch.h"
#include "Audio/audio_rec_bus_runtime.h"
#include "IPC/audio_recorder_capture.h"
#include "Audio/audio_recorder_capture_audio.h"
#include "Audio/live_parameter_audio_runtime.h"
#include "Audio/audio_waveform_capture_audio.h"
#include "Audio/synth_waveform_audio.h"
#include "IPC/live_parameter_event.h"
#include "Param/param_registry.h"
#include "Param/param_value_policy.h"
#include "IPC/sampler_ram_audio_projection.h"
#include "Track/synth_polyphony.h"
#include "Track/control_music_output.h"
#include "Sampler/multi_sample_config.h"
#include "Sampler/wavetable_config.h"
#include "Mod/mod_lfo_v1_audio.h"
#include "Mod/mod_env3.h"
#include "Audio/sd_preview_audio.h"
#include "Platform/brick_fatal.h"
#include "Platform/memory_layout.h"
#include "main.h"
#include "stm32h7xx.h"

typedef enum
{
    AUDIO_COMMAND_APPLY_OK = 0U,
    AUDIO_COMMAND_APPLY_INVALID,
    AUDIO_COMMAND_APPLY_PROGRAM_INSTALL,
    AUDIO_COMMAND_APPLY_POLYPHONY,
    AUDIO_COMMAND_APPLY_REBIND,
    AUDIO_COMMAND_APPLY_MAPPING
} audio_command_apply_result_t;
brick_fatal_record_t g_audio_command_fatal_record;
static uint32_t g_audio_wavetable_generation[
    BRICK_ENTITY_CAPACITY * BRICK6_WAVE_OSC_COUNT];
static track_tone_fm_base_voice_t g_audio_fm_base_projection[BRICK_ENTITY_CAPACITY];
static uint8_t g_audio_state_rebind_deferred;
static uint16_t g_audio_state_rebind_mask;

typedef struct
{
    uint32_t id;
    uint32_t occurrence_id;
    uint32_t age;
    uint8_t note;
    uint8_t active;
} audio_seq_rt_output_t;

static AUDIO_STATE_D3 audio_seq_rt_output_t
    g_audio_seq_rt_output[SEQ_LANE_CAPACITY][AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY];
static AUDIO_STATE_D3 audio_seq_rt_output_t
    g_audio_legacy_seq_output[SEQ_LANE_CAPACITY][AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY];
static uint32_t g_audio_seq_rt_age;
static uint16_t g_audio_seq_rt_track_mask;

static audio_command_apply_result_t audio_command_apply(
    const control_audio_command_t *command);

static void audio_command_close_entity(uint8_t entity)
{
    if (entity >= BRICK_ENTITY_CAPACITY) return;
    audio_note_engine_program_t current;
    if (audio_note_engine_adapter_current(entity, &current) != 0U)
    {
        if (current.program_route.engine == TRACK_RUNTIME_ENGINE_DRUM)
            drum_synth_all_notes_off_for_instance(
                current.program_route.instance_id);
        if (current.program_route.engine == TRACK_RUNTIME_ENGINE_TB303)
            brick6_tb303_runtime_all_notes_off(current.program_route.instance_id);
        if (current.program_route.engine == TRACK_RUNTIME_ENGINE_ACID)
            brick6_acid_runtime_all_notes_off(current.program_route.instance_id);
        if (current.program_route.mix_track_id < MIXER_MAX_TRACKS)
        {
            mixer_track_vca_all_notes_off(current.program_route.mix_track_id);
            mixer_track_filter_all_notes_off(current.program_route.mix_track_id);
        }
    }
    synth_polyphony_all_notes_off(entity);
    synth_polyphony_reset_track(entity);
    brick6_sampler_runtime_reset_track(entity);
}

static void audio_command_close_external_entities(void)
{
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        audio_note_engine_program_t current;
        if ((audio_note_engine_adapter_current(entity, &current) == 0U)
                || (current.type != TRACK_RUNTIME_TYPE_EXTERNAL))
            continue;

        mod_lfo_v1_note_release(entity);
        if ((current.has_mix_target != 0U)
                && (current.mix_track_id < MIXER_MAX_TRACKS))
            mixer_track_vca_all_notes_off(current.mix_track_id);
        if ((current.has_filter_target != 0U)
                && (current.filter_track_id < MIXER_MAX_TRACKS))
            mixer_track_filter_all_notes_off(current.filter_track_id);
    }
}

static audio_command_apply_result_t audio_command_install_program(
    const control_audio_command_t *command)
{
    if (CONTROL_AUDIO_COMMAND_KIND(command) != 0U)
        return AUDIO_COMMAND_APPLY_INVALID;
    const control_audio_program_descriptor_t descriptor =
        control_audio_program_unpack(command->value);
    const audio_note_engine_install_spec_t spec = {
        .entity_id = command->entity,
        .engine = descriptor.engine,
        .family = descriptor.family,
        .type = descriptor.type,
        .flags = descriptor.flags
    };
    if (audio_note_engine_adapter_install_prepared(&spec) == 0U)
        return AUDIO_COMMAND_APPLY_PROGRAM_INSTALL;
    return AUDIO_COMMAND_APPLY_OK;
}

static audio_command_apply_result_t audio_command_apply_program(
    const control_audio_command_t *command)
{
    const audio_command_apply_result_t result =
        audio_command_install_program(command);
    if (result != AUDIO_COMMAND_APPLY_OK) return result;
    if (g_audio_state_rebind_deferred != 0U)
    {
        g_audio_state_rebind_mask |= (uint16_t)(1U << command->entity);
        return AUDIO_COMMAND_APPLY_OK;
    }
    return (audio_note_engine_adapter_initialize_held_outputs(command->entity)
            != 0U)
        ? AUDIO_COMMAND_APPLY_OK : AUDIO_COMMAND_APPLY_REBIND;
}

static uint8_t audio_command_apply_param(const control_audio_command_t *command)
{
    const uint16_t fm_base_words =
        (uint16_t)((sizeof(track_tone_fm_base_voice_t) + 3U) / 4U);
    if ((command->id >= CONTROL_AUDIO_FM_BASE_WORD_FIRST)
            && (command->id < CONTROL_AUDIO_FM_BASE_WORD_FIRST + fm_base_words))
    {
        if (command->entity >= BRICK_ENTITY_CAPACITY) return 0U;
        const uint16_t word = command->id - CONTROL_AUDIO_FM_BASE_WORD_FIRST;
        const uint16_t offset = (uint16_t)(word * 4U);
        uint16_t bytes = (uint16_t)(sizeof(track_tone_fm_base_voice_t) - offset);
        if (bytes > 4U) bytes = 4U;
        memcpy((uint8_t *)&g_audio_fm_base_projection[command->entity] + offset,
               &command->value, bytes);
        if (word + 1U == fm_base_words)
        {
            track_audio_runtime_ctx_t ctx;
            if ((audio_note_engine_adapter_current_ctx(command->entity, &ctx) == 0U)
                    || (ctx.program_route.engine != TRACK_RUNTIME_ENGINE_FM)) return 0U;
            brick6_fm_runtime_set_base_voice(ctx.program_route.instance_id,
                &g_audio_fm_base_projection[command->entity]);
            if (audio_note_engine_adapter_project_track_configuration(
                    command->entity) == 0U)
                return 0U;
        }
        return 1U;
    }
    if (command->id == CONTROL_AUDIO_PARAM_PREVIEW_GAIN)
        return sd_preview_audio_apply_gain(command->value);
    if (command->id == CONTROL_AUDIO_PARAM_PREVIEW_ACTIVE)
        return sd_preview_audio_apply_active(command->entity);
    if (command->id == CONTROL_AUDIO_PARAM_REC_BUS)
        return audio_rec_bus_runtime_apply(command->value);
    if (command->id == CONTROL_AUDIO_PARAM_INPUT_OWNER)
        return brick6_audio_runtime_set_input_owner(command->entity,
                                                     (uint8_t)command->value);
    if (command->id == CONTROL_AUDIO_PARAM_WAVETABLE_GEN)
    {
        if (command->entity >= (BRICK_ENTITY_CAPACITY
                                * BRICK6_WAVE_OSC_COUNT)) return 0U;
        g_audio_wavetable_generation[command->entity] = command->value;
        return 1U;
    }
    if (command->id == CONTROL_AUDIO_PARAM_WAVETABLE_SET)
    {
        if (command->entity >= (BRICK_ENTITY_CAPACITY
                                * BRICK6_WAVE_OSC_COUNT)) return 0U;
        const uint8_t track = command->entity / BRICK6_WAVE_OSC_COUNT;
        const uint8_t osc = command->entity % BRICK6_WAVE_OSC_COUNT;
        track_audio_runtime_ctx_t ctx;
        if ((audio_note_engine_adapter_current_ctx(track, &ctx) == 0U)
                || (ctx.program_route.active == 0U)
                || (ctx.program_route.engine != TRACK_RUNTIME_ENGINE_WAVE))
            return 0U;
        const uint8_t voice_count = synth_polyphony_get_voice_count(track);
        if ((voice_count == 0U)
                || (voice_count > SYNTH_POLYPHONY_MAX_VOICES)) return 0U;
        for (uint8_t voice = 0U; voice < voice_count; ++voice)
            if (synth_polyphony_get_slot(track, voice)
                    >= BRICK6_WAVE_VOICE_INSTANCE_COUNT) return 0U;
        for (uint8_t voice = 0U; voice < voice_count; ++voice)
        {
            const uint8_t instance = synth_polyphony_get_slot(track, voice);
            brick6_wave_runtime_set_osc_table_wavetable_generation(
                instance, osc, (uint16_t)command->value,
                g_audio_wavetable_generation[command->entity]);
        }
        return 1U;
    }
    if (command->id == CONTROL_AUDIO_PARAM_MIDI_CONFIG)
        return audio_note_engine_adapter_apply_midi_config(command->entity,
            (uint8_t)(command->value & 0xFFU),
            (uint8_t)((command->value >> 8) & 0xFFU));
    if (command->id == CONTROL_AUDIO_PARAM_AUDIO_WAVEFORM_REQUEST)
    {
        audio_waveform_capture_audio_apply_control(command->entity,
            (uint8_t)(command->value&1U),(uint8_t)((command->value>>1)&1U));
        return 1U;
    }
    if (command->id == CONTROL_AUDIO_PARAM_SYNTH_WAVEFORM_REQUEST)
        return synth_waveform_audio_apply_request(command->entity,
            (synth_waveform_engine_t)(command->value&0xFFU),
            (uint8_t)((command->value>>8)&3U));
    if (command->id == CONTROL_AUDIO_PARAM_TRANSPORT_TEMPO)
        return audio_transport_runtime_set_tempo(command->value);
    if (command->id == CONTROL_AUDIO_PARAM_TRANSPORT_STEP_Q16)
        return audio_transport_runtime_set_step_q16(command->value);
    if (command->id == CONTROL_AUDIO_PARAM_METRONOME_LEVEL)
    {
        metronome_runtime_set_level_u7((uint8_t)command->value);
        return 1U;
    }
    if (command->id == CONTROL_AUDIO_SAMPLER_ASSET)
    {
        track_audio_runtime_ctx_t ctx;
        if (audio_note_engine_adapter_current_ctx(command->entity, &ctx) == 0U)
            return 0U;
        if (ctx.type == TRACK_RUNTIME_TYPE_MULTI)
        {
            uint16_t current_instrument = 0U;
            if (brick6_sampler_runtime_get_multi_instrument(
                    command->entity, &current_instrument) == 0U)
                return 0U;
            if (current_instrument == (uint16_t)command->value)
                return 1U;
            brick6_sampler_runtime_set_multi_instrument(command->entity,
                                                        (uint16_t)command->value);
            if ((brick6_sampler_runtime_get_multi_instrument(
                    command->entity, &current_instrument) == 0U)
                    || (current_instrument != (uint16_t)command->value))
                return 0U;
        }
        else
        {
            uint16_t current_sample = 0U;
            if ((brick6_sampler_runtime_get_sample(
                    command->entity, &current_sample) != 0U)
                    && (current_sample == (uint16_t)command->value))
                return 1U;
            brick6_sampler_runtime_set_sample(command->entity,
                                              (uint16_t)command->value);
        }
        if (g_audio_state_rebind_deferred != 0U)
        {
            g_audio_state_rebind_mask |= (uint16_t)(1U << command->entity);
            return 1U;
        }
        return audio_note_engine_adapter_initialize_held_outputs(
            command->entity);
    }
    if (command->id == CONTROL_AUDIO_PARAM_MIX_ROUTE)
        return mixer_audio_set_route(command->entity, command->value);
    if ((command->id >= CONTROL_AUDIO_PARAM_MIX_INSERT_FIRST)
            && (command->id <= CONTROL_AUDIO_PARAM_MIX_INSERT_LAST))
        return mixer_audio_set_insert_slot(command->entity,
            command->id - CONTROL_AUDIO_PARAM_MIX_INSERT_FIRST,
            (int8_t)(int32_t)command->value);
    if (command->id == CONTROL_AUDIO_PARAM_MULTI_RESOURCE_STOP)
    {
        if (command->entity >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS) return 0U;
        brick6_sampler_runtime_stop_multi_instrument(command->entity);
        return 1U;
    }
    if (command->id == CONTROL_AUDIO_PARAM_RAM_RESOURCE_STOP)
    {
        if (command->entity >= SAMPLER_RAM_AUDIO_MAX_SLOTS) return 0U;
        brick6_sampler_runtime_stop_ram_slot(command->entity, command->value);
        return 1U;
    }
    if (command->id == CONTROL_AUDIO_PARAM_WAVE_RESOURCE_STOP)
    {
        if (command->entity >= WAVETABLE_POOL_MAX_SLOTS) return 0U;
        brick6_wave_runtime_stop_wavetable_slot(command->entity, command->value);
        return 1U;
    }
    return live_parameter_audio_runtime_apply_param(command->entity,
        command->id, command->value, CONTROL_AUDIO_COMMAND_KIND(command));
}

static uint8_t audio_command_apply_note(const control_audio_command_t *command)
{
    if ((command->entity >= BRICK_ENTITY_CAPACITY)
            || (CONTROL_AUDIO_COMMAND_KIND(command) > CONTROL_AUDIO_NOTE_ON)
            || (command->value == 0U))
        return 0U;
    if ((command->value & CONTROL_AUDIO_NOTE_METRONOME_MASK)
            == CONTROL_AUDIO_NOTE_METRONOME_PREFIX)
    {
        metronome_runtime_trigger_at(0U,
            (command->value & 1U) ? METRONOME_CLICK_ACCENT
                                  : METRONOME_CLICK_NORMAL);
        return 1U;
    }
    return audio_note_engine_adapter_apply_output(command->entity,
        (uint8_t)command->id, (uint8_t)(command->id >> 8),
        CONTROL_AUDIO_COMMAND_KIND(command) == CONTROL_AUDIO_NOTE_ON,
        command->value);
}

static uint8_t audio_command_apply_transport(
    const control_audio_command_t *command)
{
    const uint8_t kind = CONTROL_AUDIO_COMMAND_KIND(command);
    if (kind == CONTROL_AUDIO_TRANSPORT_START)
    {
        audio_transport_runtime_set_running(1U);
        return 1U;
    }
    if (kind == CONTROL_AUDIO_TRANSPORT_STOP)
    {
        audio_transport_runtime_set_running(0U);
        metronome_runtime_stop();
        return 1U;
    }
    return (kind == CONTROL_AUDIO_TRANSPORT_CONTINUE)
        || (kind == CONTROL_AUDIO_TRANSPORT_LOCATE);
}

static uint8_t audio_command_apply_record(const control_audio_command_t *command)
{
    if (CONTROL_AUDIO_COMMAND_KIND(command) > CONTROL_AUDIO_RECORD_START)
        return 0U;
    if (CONTROL_AUDIO_COMMAND_KIND(command) == CONTROL_AUDIO_RECORD_START)
    {
        return audio_recorder_capture_audio_start(command->entity,
            command->id, command->value);
    }
    return audio_recorder_capture_audio_stop(command->entity, command->id);
}

static uint8_t audio_command_apply_panic(const control_audio_command_t *command)
{
    if ((CONTROL_AUDIO_COMMAND_KIND(command) > CONTROL_AUDIO_PANIC_ENTITY)
            || ((CONTROL_AUDIO_COMMAND_KIND(command)
                    == CONTROL_AUDIO_PANIC_ENTITY)
                && (command->entity >= BRICK_ENTITY_CAPACITY)))
        return 0U;
    if (CONTROL_AUDIO_COMMAND_KIND(command) == CONTROL_AUDIO_PANIC_ENTITY)
    {
        audio_command_close_entity(command->entity);
        audio_note_engine_adapter_forget_outputs(command->entity);
    }
    else
    {
        synth_polyphony_panic();
        drum_synth_all_notes_off_all();
        brick6_sampler_runtime_stop_transport_clips();
        audio_command_close_external_entities();
        for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
        {
            audio_note_engine_program_t current;
            if ((audio_note_engine_adapter_current(entity,&current)!=0U)
                    && (current.program_route.engine==TRACK_RUNTIME_ENGINE_TB303))
                brick6_tb303_runtime_all_notes_off(current.program_route.instance_id);
            if ((audio_note_engine_adapter_current(entity,&current)!=0U)
                    && (current.program_route.engine==TRACK_RUNTIME_ENGINE_ACID))
                brick6_acid_runtime_all_notes_off(current.program_route.instance_id);
            audio_note_engine_adapter_forget_outputs(entity);
        }
    }
    return 1U;
}

static audio_command_apply_result_t audio_command_apply_state_commit(
    const control_audio_command_t *commit)
{
    const control_audio_command_t *commands = NULL;
    uint16_t count = 0U;
    const uint8_t transition = CONTROL_AUDIO_COMMAND_KIND(commit);
    if ((transition > CONTROL_AUDIO_STATE_PATCH)
            || (audio_state_snapshot_resolve(
                commit->value, &commands, &count) == 0U))
        return AUDIO_COMMAND_APPLY_INVALID;

    const control_audio_command_t *programs[BRICK_ENTITY_CAPACITY] = {0};
    for (uint16_t i = 0U; i < count; ++i)
    {
        const uint8_t opcode = CONTROL_AUDIO_COMMAND_OPCODE(&commands[i]);
        if ((opcode != CONTROL_AUDIO_COMMAND_PROGRAM)
                && (opcode != CONTROL_AUDIO_COMMAND_PARAM))
            return AUDIO_COMMAND_APPLY_INVALID;
        if (opcode != CONTROL_AUDIO_COMMAND_PROGRAM) continue;
        if ((commands[i].entity >= BRICK_ENTITY_CAPACITY)
                || (programs[commands[i].entity] != NULL))
            return AUDIO_COMMAND_APPLY_INVALID;
        programs[commands[i].entity] = &commands[i];
    }
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
        if (programs[entity] == NULL) return AUDIO_COMMAND_APPLY_INVALID;

    uint16_t changed = 0U;
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        const control_audio_program_descriptor_t target =
            control_audio_program_unpack(programs[entity]->value);
        track_audio_runtime_ctx_t current;
        const uint8_t same = (uint8_t)(
            (audio_note_engine_adapter_current_ctx(entity, &current) != 0U)
            && (current.program_route.engine == target.engine)
            && (current.family == target.family)
            && (current.type == target.type)
            && (current.flags == target.flags));
        if ((transition == CONTROL_AUDIO_STATE_PROJECT) || (same == 0U))
            changed |= (uint16_t)(1U << entity);
    }

    if (transition == CONTROL_AUDIO_STATE_PROJECT)
    {
        const control_audio_command_t panic = {
            .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(
                CONTROL_AUDIO_COMMAND_PANIC, CONTROL_AUDIO_PANIC_GLOBAL)
        };
        if (audio_command_apply_panic(&panic) == 0U)
            return AUDIO_COMMAND_APPLY_INVALID;
    }

    /* Release every installation which changes before acquiring any target.
     * The AUDIO output ledger is retained for Pattern and rebound once after
     * the final programs and resource parameters are installed. */
    const control_audio_command_t off = {
        .value = (uint32_t)TRACK_RUNTIME_FAMILY_OFF << 8,
        .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(
            CONTROL_AUDIO_COMMAND_PROGRAM, 0U)
    };
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        if ((changed & (uint16_t)(1U << entity)) == 0U) continue;
        audio_command_close_entity(entity);
        control_audio_command_t release = off;
        release.entity = entity;
        const audio_command_apply_result_t result =
            audio_command_install_program(&release);
        if (result != AUDIO_COMMAND_APPLY_OK) return result;
    }

    g_audio_state_rebind_deferred = 1U;
    g_audio_state_rebind_mask = changed;
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        if ((changed & (uint16_t)(1U << entity)) == 0U) continue;
        const audio_command_apply_result_t result =
            audio_command_install_program(programs[entity]);
        if (result != AUDIO_COMMAND_APPLY_OK)
        {
            g_audio_state_rebind_deferred = 0U;
            return result;
        }
    }
    brick6_fm_runtime_finalize_pending();
    for (uint16_t i = 0U; i < count; ++i)
    {
        if (CONTROL_AUDIO_COMMAND_OPCODE(&commands[i])
                != CONTROL_AUDIO_COMMAND_PARAM) continue;
        const audio_command_apply_result_t result =
            audio_command_apply(&commands[i]);
        if (result != AUDIO_COMMAND_APPLY_OK)
        {
            g_audio_state_rebind_deferred = 0U;
            return result;
        }
    }
    g_audio_state_rebind_deferred = 0U;
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        if ((g_audio_state_rebind_mask & (uint16_t)(1U << entity)) == 0U)
            continue;
        if (audio_note_engine_adapter_initialize_held_outputs(entity) == 0U)
            return AUDIO_COMMAND_APPLY_REBIND;
    }
    g_audio_state_rebind_mask = 0U;
    brick6_fm_runtime_finalize_pending();
    audio_mod_matrix_finalize_dirty();
    return AUDIO_COMMAND_APPLY_OK;
}

static audio_command_apply_result_t audio_command_apply(
    const control_audio_command_t *command)
{
    switch (CONTROL_AUDIO_COMMAND_OPCODE(command))
    {
        case CONTROL_AUDIO_COMMAND_PROGRAM: return audio_command_apply_program(command);
        case CONTROL_AUDIO_COMMAND_PARAM:
            if (audio_command_apply_param(command) != 0U)
                return AUDIO_COMMAND_APPLY_OK;
            if (command->id == CONTROL_AUDIO_SAMPLER_ASSET)
                return AUDIO_COMMAND_APPLY_REBIND;
            return (command->id == CONTROL_AUDIO_CONFIG_POLY_VOICES)
                ? AUDIO_COMMAND_APPLY_POLYPHONY : AUDIO_COMMAND_APPLY_INVALID;
        case CONTROL_AUDIO_COMMAND_NOTE:
            return (audio_command_apply_note(command) != 0U)
                ? AUDIO_COMMAND_APPLY_OK : AUDIO_COMMAND_APPLY_MAPPING;
        case CONTROL_AUDIO_COMMAND_TRANSPORT:
            return (audio_command_apply_transport(command) != 0U)
                ? AUDIO_COMMAND_APPLY_OK : AUDIO_COMMAND_APPLY_INVALID;
        case CONTROL_AUDIO_COMMAND_RECORD:
            return (audio_command_apply_record(command) != 0U)
                ? AUDIO_COMMAND_APPLY_OK : AUDIO_COMMAND_APPLY_INVALID;
        case CONTROL_AUDIO_COMMAND_PANIC:
            return (audio_command_apply_panic(command) != 0U)
                ? AUDIO_COMMAND_APPLY_OK : AUDIO_COMMAND_APPLY_INVALID;
        case CONTROL_AUDIO_COMMAND_AUDIO_STATE_COMMIT:
            return audio_command_apply_state_commit(command);
        default: return AUDIO_COMMAND_APPLY_INVALID;
    }
}

static _Noreturn void audio_command_fatal_at(
    const control_audio_command_t *command, audio_command_apply_result_t result,
    const char *file, uint32_t line, const char *function)
{
    brick_fatal_code_t code = BRICK_FATAL_AUDIO_INVALID_COMMAND;
    const char *message = "AUDIO_COMMAND_INVALID";
    if (result == AUDIO_COMMAND_APPLY_PROGRAM_INSTALL)
    {
        code = BRICK_FATAL_AUDIO_PROGRAM_INSTALL;
        message = "AUDIO_PROGRAM_INSTALL_FAILED";
    }
    else if (result == AUDIO_COMMAND_APPLY_POLYPHONY)
    {
        code = BRICK_FATAL_AUDIO_POLYPHONY;
        message = "AUDIO_POLYPHONY_APPLY_FAILED";
    }
    else if (result == AUDIO_COMMAND_APPLY_REBIND)
    {
        code = BRICK_FATAL_AUDIO_REBIND;
        message = "AUDIO_RESOURCE_REBIND_FAILED";
    }
    else if (result == AUDIO_COMMAND_APPLY_MAPPING)
    {
        code = BRICK_FATAL_AUDIO_MAPPING;
        message = "AUDIO_PARAMETER_MAPPING_FAILED";
    }
    __disable_irq();
    g_audio_command_fatal_record.message = message;
    g_audio_command_fatal_record.file = file;
    g_audio_command_fatal_record.line = line;
    g_audio_command_fatal_record.function = function;
    g_audio_command_fatal_record.code = (uint32_t)code;
    g_audio_command_fatal_record.entity = command->entity;
    g_audio_command_fatal_record.context =
        ((uint32_t)command->opcode_kind << 16) | command->id;
    g_audio_command_fatal_record.requested = command->value;
    g_audio_command_fatal_record.capacity =
        (result == AUDIO_COMMAND_APPLY_MAPPING)
        ? AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY : (uint32_t)result;
    __DMB();
    Error_Handler();
    for (;;) {}
}

#define AUDIO_COMMAND_FATAL(command, result) \
    audio_command_fatal_at((command), (result), __FILE__, (uint32_t)__LINE__, \
                           __func__)

void audio_command_executor_init(void)
{
    memset(g_audio_wavetable_generation, 0,
           sizeof(g_audio_wavetable_generation));
    g_audio_state_rebind_deferred = 0U;
    g_audio_state_rebind_mask = 0U;
    memset(g_audio_seq_rt_output, 0, sizeof(g_audio_seq_rt_output));
    memset(g_audio_legacy_seq_output, 0, sizeof(g_audio_legacy_seq_output));
    g_audio_seq_rt_age = 0U;
    g_audio_seq_rt_track_mask = 0U;
}

static void audio_command_executor_close_outputs(
    uint8_t track, audio_seq_rt_output_t *outputs)
{
    for (uint8_t i = 0U; i < AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY; ++i)
    {
        if (outputs[i].active == 0U) continue;
        if (audio_note_engine_adapter_apply_output(track, outputs[i].note, 0U,
                0U, outputs[i].id) == 0U)
            Error_Handler();
        seq_rt_pass1_audio_retire_occurrence(outputs[i].occurrence_id);
        outputs[i] = (audio_seq_rt_output_t){0};
    }
}

void audio_command_executor_seq_rt_begin_block(uint16_t track_mask)
{
    const uint16_t changed = g_audio_seq_rt_track_mask ^ track_mask;
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
    {
        const uint16_t bit = (uint16_t)(1U << track);
        if ((changed & bit) == 0U) continue;
        if ((track_mask & bit) == 0U)
            audio_command_executor_close_outputs(
                track, g_audio_seq_rt_output[track]);
    }
    g_audio_seq_rt_track_mask = track_mask;
}

static void audio_command_executor_track_legacy_note(
    const control_audio_command_t *command)
{
    if ((command->entity >= SEQ_LANE_CAPACITY)
            || (control_music_output_handle_is_internal(command->value) == 0U))
        return;
    audio_seq_rt_output_t *const outputs =
        g_audio_legacy_seq_output[command->entity];
    if (CONTROL_AUDIO_COMMAND_KIND(command) == CONTROL_AUDIO_NOTE_OFF)
    {
        for (uint8_t i = 0U; i < AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY; ++i)
            if ((outputs[i].active != 0U)
                    && (outputs[i].id == command->value))
                outputs[i] = (audio_seq_rt_output_t){0};
        return;
    }
    for (uint8_t i = 0U; i < AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY; ++i)
    {
        if ((outputs[i].active != 0U) && (outputs[i].id != command->value))
            continue;
        outputs[i] = (audio_seq_rt_output_t){
            .id=command->value,.age=++g_audio_seq_rt_age,
            .note=(uint8_t)command->id,.active=1U};
        return;
    }
}

static void audio_command_executor_close_legacy_index(uint8_t track,
                                                       uint8_t index)
{
    audio_seq_rt_output_t *const output =
        &g_audio_legacy_seq_output[track][index];
    if (output->active == 0U) return;
    if (audio_note_engine_adapter_apply_output(track, output->note, 0U, 0U,
            output->id) == 0U)
        Error_Handler();
    seq_rt_pass1_audio_retire_legacy(output->id);
    *output = (audio_seq_rt_output_t){0};
}

static uint8_t audio_command_executor_apply_seq_rt_event(
    const seq_rt_event_t *event)
{
    if ((event == 0) || (event->track >= SEQ_LANE_CAPACITY)) return 0U;
    if (event->kind == SEQ_RT_EVENT_PARAM)
    {
        if (event->occurrence_id >= PARAM_COUNT) return 0U;
        const float value=param_value_policy_decode_u16(
            &param_registry[event->occurrence_id],(uint16_t)event->value);
        const uint8_t kind=(event->velocity==SEQ_RT_PARAM_TEMP)
            ?CONTROL_AUDIO_PARAM_KIND_TEMP_TRACK
            :((event->velocity==SEQ_RT_PARAM_CLEAR_TEMP)
                ?CONTROL_AUDIO_PARAM_KIND_CLEAR_TEMP_TRACK
                :CONTROL_AUDIO_PARAM_KIND_BASE_TRACK);
        return live_parameter_audio_runtime_apply_param(event->track,
            (uint16_t)event->occurrence_id,
            (uint32_t)live_parameter_event_encode_float(value),kind);
    }
    audio_seq_rt_output_t *const outputs = g_audio_seq_rt_output[event->track];
    if (event->kind == SEQ_RT_EVENT_NOTE_OFF)
    {
        for (uint8_t i = 0U; i < AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY; ++i)
        {
            if ((outputs[i].active == 0U)
                    || (outputs[i].occurrence_id != event->occurrence_id))
                continue;
            const uint8_t ok = audio_note_engine_adapter_apply_output(
                event->track, outputs[i].note, 0U, 0U, outputs[i].id);
            outputs[i] = (audio_seq_rt_output_t){0};
            return ok;
        }
        return 1U;
    }

    for (uint8_t i = 0U; i < AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY; ++i)
        if ((g_audio_legacy_seq_output[event->track][i].active != 0U)
                && (g_audio_legacy_seq_output[event->track][i].note
                    == event->note))
            audio_command_executor_close_legacy_index(event->track, i);

    uint8_t target = AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY;
    uint8_t oldest = 0U;
    for (uint8_t i = 0U; i < AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY; ++i)
    {
        if ((outputs[i].active != 0U) && (outputs[i].note == event->note))
        {
            target = i;
            break;
        }
        if (outputs[i].active == 0U)
        {
            target = i;
            break;
        }
        if (outputs[i].age < outputs[oldest].age) oldest = i;
    }
    if (target == AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY) target = oldest;
    if (outputs[target].active != 0U)
    {
        if (audio_note_engine_adapter_apply_output(event->track,
                outputs[target].note, 0U, 0U, outputs[target].id) == 0U)
            return 0U;
        seq_rt_pass1_audio_retire_occurrence(outputs[target].occurrence_id);
    }
    outputs[target] = (audio_seq_rt_output_t){0};

    uint8_t active_count = 0U;
    uint8_t oldest_legacy = AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY;
    uint32_t oldest_legacy_age = UINT32_MAX;
    for (uint8_t i = 0U; i < AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY; ++i)
    {
        if (outputs[i].active != 0U) ++active_count;
        const audio_seq_rt_output_t *const legacy =
            &g_audio_legacy_seq_output[event->track][i];
        if (legacy->active == 0U) continue;
        ++active_count;
        if (legacy->age < oldest_legacy_age)
        {
            oldest_legacy_age = legacy->age;
            oldest_legacy = i;
        }
    }
    if ((active_count >= AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY)
            && (oldest_legacy < AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY))
        audio_command_executor_close_legacy_index(event->track,
                                                   oldest_legacy);
    const uint32_t output_id = UINT32_C(0x20000000)
        | (event->occurrence_id & UINT32_C(0x1FFFFFFF));
    if (audio_note_engine_adapter_apply_output(event->track, event->note,
            event->velocity, 1U, output_id) == 0U)
        return 0U;
    outputs[target] = (audio_seq_rt_output_t){
        .id=output_id,.occurrence_id=event->occurrence_id,
        .age=++g_audio_seq_rt_age,.note=event->note,.active=1U};
    return 1U;
}

uint16_t audio_command_executor_apply_seq_rt_due(uint64_t sample_time)
{
    uint16_t applied = 0U;
    seq_rt_event_t event;
    while (seq_rt_pass1_audio_pop_due(sample_time, &event) != 0U)
    {
        if (audio_command_executor_apply_seq_rt_event(&event) == 0U)
            Error_Handler();
        ++applied;
    }
    return applied;
}

uint16_t __attribute__((noinline)) audio_command_executor_apply_due(
    uint64_t sample_time, uint32_t head_limit,
    uint64_t discard_transient_before)
{
    uint16_t applied = 0U;
    control_audio_command_t command;
    while ((control_audio_fifo_audio_tail_before(head_limit) != 0U)
            && (control_audio_fifo_audio_peek(&command) != 0U)
            && (command.effective_sample_time <= sample_time))
    {
        const uint8_t opcode = CONTROL_AUDIO_COMMAND_OPCODE(&command);
        const uint8_t stale_note_on = (uint8_t)(
            (opcode == CONTROL_AUDIO_COMMAND_NOTE)
            && (CONTROL_AUDIO_COMMAND_KIND(&command) == CONTROL_AUDIO_NOTE_ON));
        const uint8_t stale_temporary_param = (uint8_t)(
            (opcode == CONTROL_AUDIO_COMMAND_PARAM)
            && ((CONTROL_AUDIO_COMMAND_KIND(&command)
                    == CONTROL_AUDIO_PARAM_KIND_TEMP_TRACK)
                || (CONTROL_AUDIO_COMMAND_KIND(&command)
                    == CONTROL_AUDIO_PARAM_KIND_SEQ_TEMP_TRACK)));
        const uint8_t stale_request = (uint8_t)(
            control_audio_command_state_class(&command)
                == CONTROL_AUDIO_COMMAND_REQUEST);
        if ((discard_transient_before != 0U)
                && (command.effective_sample_time < discard_transient_before)
                && ((stale_note_on != 0U) || (stale_temporary_param != 0U)
                    || (stale_request != 0U)))
        {
            /* An xrun made this one-shot action inaudible.  CLEAR_TEMP is a
             * required release, so it follows durable state, NOTE_OFF,
             * transport, record and panic through normal FIFO order. */
            (void)control_audio_fifo_audio_pop();
            ++applied;
            continue;
        }
        if (CONTROL_AUDIO_COMMAND_OPCODE(&command) != CONTROL_AUDIO_COMMAND_PARAM)
            brick6_fm_runtime_finalize_pending();
        if ((opcode == CONTROL_AUDIO_COMMAND_PARAM)
                && (seq_rt_pass1_audio_suppress_legacy_param(
                    CONTROL_AUDIO_COMMAND_KIND(&command),command.entity)!=0U))
        {
            (void)control_audio_fifo_audio_pop();
            ++applied;
            continue;
        }
        if ((CONTROL_AUDIO_COMMAND_OPCODE(&command) == CONTROL_AUDIO_COMMAND_NOTE)
                && ((command.value & CONTROL_AUDIO_NOTE_METRONOME_MASK)
                    != CONTROL_AUDIO_NOTE_METRONOME_PREFIX))
        {
            seq_rt_pass1_audio_observe_note(command.effective_sample_time,
                CONTROL_AUDIO_COMMAND_KIND(&command), command.entity,
                (uint8_t)command.id, (uint8_t)(command.id >> 8),
                command.value);
            if (seq_rt_pass1_audio_suppress_legacy(
                    CONTROL_AUDIO_COMMAND_KIND(&command), command.entity,
                    command.value) != 0U)
            {
                (void)control_audio_fifo_audio_pop();
                ++applied;
                continue;
            }
        }
        if (((opcode == CONTROL_AUDIO_COMMAND_TRANSPORT)
                && (CONTROL_AUDIO_COMMAND_KIND(&command)
                    == CONTROL_AUDIO_TRANSPORT_STOP))
                || ((opcode == CONTROL_AUDIO_COMMAND_PANIC)
                    && (CONTROL_AUDIO_COMMAND_KIND(&command)
                        == CONTROL_AUDIO_PANIC_GLOBAL)))
            seq_rt_pass1_audio_force_stop(command.effective_sample_time);
        const audio_command_apply_result_t result =
            audio_command_apply(&command);
        if (result != AUDIO_COMMAND_APPLY_OK)
            AUDIO_COMMAND_FATAL(&command, result);
        if (opcode == CONTROL_AUDIO_COMMAND_NOTE)
            audio_command_executor_track_legacy_note(&command);
        if (((opcode == CONTROL_AUDIO_COMMAND_TRANSPORT)
                && (CONTROL_AUDIO_COMMAND_KIND(&command)
                    == CONTROL_AUDIO_TRANSPORT_STOP))
                || ((opcode == CONTROL_AUDIO_COMMAND_PANIC)
                    && (CONTROL_AUDIO_COMMAND_KIND(&command)
                        == CONTROL_AUDIO_PANIC_GLOBAL)))
        {
            memset(g_audio_seq_rt_output, 0, sizeof(g_audio_seq_rt_output));
            memset(g_audio_legacy_seq_output, 0,
                   sizeof(g_audio_legacy_seq_output));
            g_audio_seq_rt_track_mask = 0U;
        }
        else if ((opcode == CONTROL_AUDIO_COMMAND_PANIC)
                && (command.entity < SEQ_LANE_CAPACITY))
        {
            memset(g_audio_seq_rt_output[command.entity], 0,
                   sizeof(g_audio_seq_rt_output[command.entity]));
            memset(g_audio_legacy_seq_output[command.entity], 0,
                   sizeof(g_audio_legacy_seq_output[command.entity]));
        }
        (void)control_audio_fifo_audio_pop();
        ++applied;
    }
    brick6_fm_runtime_finalize_pending();
    audio_mod_matrix_finalize_dirty();
    return applied;
}
