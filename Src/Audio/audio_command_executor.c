#include "Audio/audio_command_executor.h"

#include <string.h>

#include "IPC/control_audio_command.h"
#include "IPC/control_audio_fifo_audio.h"
#include "IPC/audio_state_snapshot.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Audio/audio_mod_matrix.h"
#include "Audio/metronome_runtime.h"
#include "Audio/mixer.h"
#include "Audio/drum_synth.h"
#include "Audio/audio_transport_runtime.h"
#include "Audio/brick6_looper_runtime.h"
#include "Audio/Engines/fm_engine.h"
#include "Audio/Engines/tb303_engine.h"
#include "Audio/Engines/Sampler/brick6_sampler_runtime.h"
#include "Audio/Engines/wavetable_engine.h"
#include "Audio/Engines/audio_engine_dispatch.h"
#include "Audio/control_routing_audio.h"
#include "Audio/audio_rec_bus_runtime.h"
#include "IPC/audio_recorder_capture.h"
#include "Audio/audio_recorder_capture_audio.h"
#include "Audio/live_parameter_audio_runtime.h"
#include "Audio/audio_waveform_capture_audio.h"
#include "Audio/synth_waveform_audio.h"
#include "IPC/live_parameter_event.h"
#include "Track/synth_polyphony.h"
#include "Mod/mod_lfo_v1_audio.h"
#include "Mod/mod_env3.h"
#include "Audio/sd_preview_audio.h"
#include "Platform/brick_fatal.h"
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
    BRICK6_WAVE_VOICE_INSTANCE_COUNT * BRICK6_WAVE_OSC_COUNT];
static track_tone_fm_base_voice_t g_audio_fm_base_projection[BRICK_ENTITY_CAPACITY];

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
        if (current.program_route.mix_track_id < MIXER_MAX_TRACKS)
        {
            mixer_track_vca_all_notes_off(current.program_route.mix_track_id);
            mixer_track_filter_all_notes_off(current.program_route.mix_track_id);
        }
    }
    synth_polyphony_all_notes_off(entity);
    synth_polyphony_reset_track(entity);
    brick6_sampler_runtime_reset_track(entity);
    brick6_looper_runtime_prepare_replace(entity);
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

static audio_command_apply_result_t audio_command_apply_program(
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
    if (command->id == CONTROL_AUDIO_PARAM_LOOPER_ROUTE)
        return control_routing_audio_set_mask(command->entity,
                                               (uint16_t)command->value);
    if (command->id == CONTROL_AUDIO_PARAM_WAVETABLE_GEN)
    {
        if (command->entity >= (BRICK6_WAVE_VOICE_INSTANCE_COUNT
                                * BRICK6_WAVE_OSC_COUNT)) return 0U;
        g_audio_wavetable_generation[command->entity] = command->value;
        return 1U;
    }
    if (command->id == CONTROL_AUDIO_PARAM_WAVETABLE_SET)
    {
        if (command->entity >= (BRICK6_WAVE_VOICE_INSTANCE_COUNT
                                * BRICK6_WAVE_OSC_COUNT)) return 0U;
        const uint8_t instance = command->entity / BRICK6_WAVE_OSC_COUNT;
        const uint8_t osc = command->entity % BRICK6_WAVE_OSC_COUNT;
        brick6_wave_runtime_set_osc_table_wavetable_generation(
            instance, osc, (uint16_t)command->value,
            g_audio_wavetable_generation[command->entity]);
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
            brick6_sampler_runtime_set_sample(command->entity,
                                              (uint16_t)command->value);
        return audio_note_engine_adapter_initialize_held_outputs(
            command->entity);
    }
    if (command->id == CONTROL_AUDIO_LOOPER_PLAY_AUTO)
    {
        brick6_looper_runtime_set_play_auto(command->entity,
                                            command->value != 0U ? 1U : 0U);
        return 1U;
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
        brick6_sampler_runtime_stop_multi_instrument(command->entity);
        return 1U;
    }
    if (command->id == CONTROL_AUDIO_PARAM_RAM_RESOURCE_STOP)
    {
        brick6_sampler_runtime_stop_ram_slot(command->entity, command->value);
        return 1U;
    }
    if (command->id == CONTROL_AUDIO_PARAM_WAVE_RESOURCE_STOP)
    {
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
        brick6_looper_runtime_on_transport_start(
            command->effective_sample_time);
        return 1U;
    }
    if (kind == CONTROL_AUDIO_TRANSPORT_STOP)
    {
        audio_transport_runtime_set_running(0U);
        brick6_looper_runtime_on_transport_stop();
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
    if ((command->id & AUDIO_RECORDER_LOOPER_RECORD_ID_FLAG) != 0U)
    {
        if (CONTROL_AUDIO_COMMAND_KIND(command) == CONTROL_AUDIO_RECORD_START)
        {
            const uint8_t replace_valid = (uint8_t)(
                (command->id & AUDIO_RECORDER_LOOPER_REPLACE_VALID_FLAG) != 0U);
            const uint8_t overdub = (uint8_t)(
                (command->id & AUDIO_RECORDER_LOOPER_OVERDUB_FLAG) != 0U);
            const uint8_t replace_track = (uint8_t)((command->id
                >> AUDIO_RECORDER_LOOPER_REPLACE_TRACK_SHIFT)
                & AUDIO_RECORDER_LOOPER_REPLACE_TRACK_MASK);
            if ((replace_valid != 0U) && (replace_track != command->entity))
                brick6_looper_runtime_prepare_replace(replace_track);
            if ((overdub == 0U)
                    && ((replace_valid == 0U) || (replace_track != command->entity)))
                brick6_looper_runtime_prepare_replace(command->entity);
            brick6_looper_runtime_arm_live_record_start(command->entity,
                (uint8_t)command->id, command->value,
                (uint8_t)(command->id
                    >> AUDIO_RECORDER_LOOPER_PLAY_AUTO_SHIFT) & 1U,
                overdub,
                command->effective_sample_time);
        }
        else
            brick6_looper_runtime_arm_record_stop(
                command->effective_sample_time);
        return 1U;
    }
    if (CONTROL_AUDIO_COMMAND_KIND(command) == CONTROL_AUDIO_RECORD_START)
    {
        const uint8_t applied = audio_recorder_capture_audio_start(command->entity,
            command->id, command->value);
        if ((applied != 0U)
                && (command->entity == (uint8_t)AUDIO_RECORDER_CLIENT_LOOPER))
            brick6_looper_runtime_on_record_start(
                command->effective_sample_time);
        return applied;
    }
    const uint8_t applied = audio_recorder_capture_audio_stop(command->entity,
        command->id);
    if ((applied != 0U)
            && (command->entity == (uint8_t)AUDIO_RECORDER_CLIENT_LOOPER))
        brick6_looper_runtime_on_record_stop(command->effective_sample_time);
    return applied;
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
            brick6_looper_runtime_stop_playback(entity);
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
    if ((CONTROL_AUDIO_COMMAND_KIND(commit) != 0U)
            || (audio_state_snapshot_resolve(
                commit->value, &commands, &count) == 0U))
        return AUDIO_COMMAND_APPLY_INVALID;
    const control_audio_command_t panic = {
        .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(
            CONTROL_AUDIO_COMMAND_PANIC, CONTROL_AUDIO_PANIC_GLOBAL)
    };
    if (audio_command_apply_panic(&panic) == 0U)
        return AUDIO_COMMAND_APPLY_INVALID;
    for (uint16_t i = 0U; i < count; ++i)
    {
        const uint8_t opcode = CONTROL_AUDIO_COMMAND_OPCODE(&commands[i]);
        if ((opcode != CONTROL_AUDIO_COMMAND_PROGRAM)
                && (opcode != CONTROL_AUDIO_COMMAND_PARAM))
            return AUDIO_COMMAND_APPLY_INVALID;
        if (opcode != CONTROL_AUDIO_COMMAND_PARAM)
            brick6_fm_runtime_finalize_pending();
        const audio_command_apply_result_t result =
            audio_command_apply(&commands[i]);
        if (result != AUDIO_COMMAND_APPLY_OK) return result;
    }
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
    g_audio_command_fatal_record.capacity = (uint32_t)result;
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
}

uint16_t __attribute__((noinline)) audio_command_executor_apply_due(
    uint64_t sample_time, uint32_t head_limit)
{
    uint16_t applied = 0U;
    control_audio_command_t command;
    while ((control_audio_fifo_audio_tail_before(head_limit) != 0U)
            && (control_audio_fifo_audio_peek(&command) != 0U)
            && (command.effective_sample_time <= sample_time))
    {
        if (CONTROL_AUDIO_COMMAND_OPCODE(&command) != CONTROL_AUDIO_COMMAND_PARAM)
            brick6_fm_runtime_finalize_pending();
        const audio_command_apply_result_t result =
            audio_command_apply(&command);
        if (result != AUDIO_COMMAND_APPLY_OK)
            AUDIO_COMMAND_FATAL(&command, result);
        (void)control_audio_fifo_audio_pop();
        ++applied;
    }
    brick6_fm_runtime_finalize_pending();
    audio_mod_matrix_finalize_dirty();
    return applied;
}
