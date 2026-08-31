#include "Audio/audio_command_executor.h"

#include <string.h>

#include "IPC/control_audio_command.h"
#include "IPC/control_audio_fifo_audio.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Audio/audio_mod_matrix.h"
#include "Audio/metronome_runtime.h"
#include "Audio/mixer.h"
#include "Audio/drum_synth.h"
#include "Audio/audio_transport_runtime.h"
#include "Audio/brick6_looper_runtime.h"
#include "Audio/Engines/fm_engine.h"
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
#include "Mod/mod_lfo_v1.h"
#include "Mod/mod_env3.h"
#include "Audio/sd_preview_audio.h"

#define AUDIO_PARAM_MULTI_RESOURCE_STOP   0xFFF5U
#define AUDIO_PARAM_RAM_RESOURCE_STOP     0xFFF6U
#define AUDIO_PARAM_WAVE_RESOURCE_STOP    0xFFF7U

static uint32_t g_audio_command_invariant_failures;
static uint32_t g_audio_wavetable_generation[
    BRICK6_WAVE_VOICE_INSTANCE_COUNT * BRICK6_WAVE_OSC_COUNT];

static void audio_command_close_entity(uint8_t entity)
{
    if (entity >= BRICK_ENTITY_CAPACITY) return;
    audio_note_engine_program_t current;
    if (audio_note_engine_adapter_current(entity, &current) != 0U)
    {
        if (current.program_route.engine == TRACK_RUNTIME_ENGINE_DRUM)
            drum_synth_all_notes_off_for_instance(
                current.program_route.instance_id);
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

static uint8_t audio_command_apply_program(const control_audio_command_t *command)
{
    const control_audio_program_descriptor_t descriptor =
        control_audio_program_unpack(command->value);
    const audio_note_engine_install_spec_t spec = {
        .entity_id = command->entity,
        .engine = descriptor.engine,
        .family = descriptor.family,
        .type = descriptor.type,
        .flags = descriptor.flags
    };
    track_audio_runtime_ctx_t current;
    const uint8_t have_current =
        audio_note_engine_adapter_current_ctx(command->entity, &current);
    if ((have_current != 0U)
            && ((current.program_route.engine
                    == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
                || (spec.engine == TRACK_RUNTIME_ENGINE_SAMPLER)))
        brick6_sampler_runtime_replace_track_renderer(command->entity);
    if (audio_note_engine_adapter_install_prepared(&spec) == 0U)
        return 0U;
    return audio_note_engine_adapter_initialize_held_outputs(command->entity);
}

static uint8_t audio_command_apply_param(const control_audio_command_t *command)
{
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
    if (command->id == CONTROL_AUDIO_PARAM_MIX_ROUTE)
        return mixer_audio_set_route(command->entity, command->value);
    if ((command->id >= CONTROL_AUDIO_PARAM_MIX_INSERT_FIRST)
            && (command->id <= CONTROL_AUDIO_PARAM_MIX_INSERT_LAST))
        return mixer_audio_set_insert_slot(command->entity,
            command->id - CONTROL_AUDIO_PARAM_MIX_INSERT_FIRST,
            (int8_t)(int32_t)command->value);
    if (command->id == AUDIO_PARAM_MULTI_RESOURCE_STOP)
    {
        brick6_sampler_runtime_stop_multi_instrument(command->entity);
        return 1U;
    }
    if (command->id == AUDIO_PARAM_RAM_RESOURCE_STOP)
    {
        brick6_sampler_runtime_stop_ram_slot(command->entity, command->value);
        return 1U;
    }
    if (command->id == AUDIO_PARAM_WAVE_RESOURCE_STOP)
    {
        brick6_wave_runtime_stop_wavetable_slot(command->entity, command->value);
        return 1U;
    }
    return live_parameter_audio_runtime_apply_param(command->entity,
        command->id, command->value, CONTROL_AUDIO_COMMAND_KIND(command));
}

static uint8_t audio_command_apply_note(const control_audio_command_t *command)
{
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
            brick6_looper_runtime_stop_playback(entity);
            audio_note_engine_adapter_forget_outputs(entity);
        }
    }
    return 1U;
}

static uint8_t audio_command_apply(const control_audio_command_t *command)
{
    switch (CONTROL_AUDIO_COMMAND_OPCODE(command))
    {
        case CONTROL_AUDIO_COMMAND_PROGRAM: return audio_command_apply_program(command);
        case CONTROL_AUDIO_COMMAND_PARAM: return audio_command_apply_param(command);
        case CONTROL_AUDIO_COMMAND_NOTE: return audio_command_apply_note(command);
        case CONTROL_AUDIO_COMMAND_TRANSPORT: return audio_command_apply_transport(command);
        case CONTROL_AUDIO_COMMAND_RECORD: return audio_command_apply_record(command);
        case CONTROL_AUDIO_COMMAND_PANIC: return audio_command_apply_panic(command);
        default: return 0U;
    }
}

void audio_command_executor_init(void)
{
    g_audio_command_invariant_failures = 0U;
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
        if (audio_command_apply(&command) == 0U)
            ++g_audio_command_invariant_failures;
        (void)control_audio_fifo_audio_pop();
        ++applied;
    }
    brick6_fm_runtime_finalize_pending();
    audio_mod_matrix_finalize_dirty();
    return applied;
}
