#include "Audio/audio_command_executor.h"

#include <string.h>

#include "ControlRT/control_audio_command.h"
#include "Audio/control_audio_fifo_audio.h"
#include "Seq/seq_engine.h"
#include "ControlRT/audio_state_transaction.h"
#include "ControlRT/prepared_audio_state.h"
#include "ControlRT/pattern_recall_diag.h"
#include "ControlRT/live_parameter_event.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Audio/audio_mod_matrix.h"
#include "Audio/audio_fx_runtime.h"
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
#include "Recorder/audio_recorder_ring.h"
#include "Audio/audio_recorder_capture_audio.h"
#include "Audio/live_parameter_audio_runtime.h"
#include "Audio/audio_waveform_capture_audio.h"
#include "Audio/synth_waveform_audio.h"
#include "Sampler/sampler_ram_audio_projection.h"
#include "Track/synth_polyphony.h"
#include "Track/control_music_output.h"
#include "Sampler/multi_sample_config.h"
#include "Sampler/wavetable_config.h"
#include "Audio/audio_wavetable_registry.h"
#include "Audio/multi_sample_audio_projection_audio.h"
#include "Audio/sample_classic_audio_projection_audio.h"
#include "Audio/sampler_ram_audio_projection_audio.h"
#include "Mod/mod_lfo_v1_audio.h"
#include "Mod/mod_env3.h"
#include "Param/param_global_control.h"
#include "Param/param_registry.h"
#include "Param/param_spec.h"
#include "Track/tone_param_codec.h"
#include "Audio/sd_preview_audio.h"
#include "Platform/brick_fatal.h"
#include "Platform/memory_layout.h"
#include "main.h"
#include "stm32h7xx.h"
#include <math.h>

typedef enum
{
    AUDIO_COMMAND_APPLY_OK = 0U,
    AUDIO_COMMAND_APPLY_INVALID,
    AUDIO_COMMAND_APPLY_PROGRAM_INSTALL,
    AUDIO_COMMAND_APPLY_POLYPHONY,
    AUDIO_COMMAND_APPLY_REBIND,
    AUDIO_COMMAND_APPLY_MAPPING
} audio_command_apply_result_t;
_Static_assert(PREPARED_AUDIO_TEMP_OWNER_COUNT == SEQ_TRACK_COUNT,
               "Prepared AUDIO temp owners must match AUDIO mod owners");
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
    uint16_t owner_tag;
} audio_seq_output_t;

static AUDIO_STATE_D3 audio_seq_output_t
    g_audio_seq_output[SEQ_LANE_CAPACITY][AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY];
static uint32_t g_audio_seq_next_output_handle;

static uint32_t audio_seq_allocate_output_handle(void)
{
    for (uint16_t attempt = 0U;
         attempt <= SEQ_LANE_CAPACITY * AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY;
         ++attempt)
    {
        g_audio_seq_next_output_handle =
            (g_audio_seq_next_output_handle + 1U) & UINT32_C(0x0FFFFFFF);
        if (g_audio_seq_next_output_handle == 0U)
            g_audio_seq_next_output_handle = 1U;
        const uint32_t handle = UINT32_C(0x20000000)
            | g_audio_seq_next_output_handle;
        uint8_t collision = 0U;
        for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
            for (uint8_t i = 0U; i < AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY; ++i)
                collision |= (uint8_t)(
                    (g_audio_seq_output[track][i].active != 0U)
                    && (g_audio_seq_output[track][i].id == handle));
        if (collision == 0U)
            return handle;
    }
    return 0U;
}
static uint32_t g_audio_seq_age;
static uint16_t g_audio_seq_track_mask;

static audio_command_apply_result_t audio_command_apply(
    const control_audio_command_t *command);
static void audio_command_executor_close_outputs_from(uint8_t track,
                                                       uint8_t first);

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

static void audio_command_prepare_synth_program_change(
    uint8_t entity, const control_audio_program_descriptor_t *target)
{
    if ((target == NULL) || (entity >= BRICK_ENTITY_CAPACITY)
            || (target->family != TRACK_RUNTIME_FAMILY_SYNTH)
            || ((target->type != TRACK_RUNTIME_TYPE_PRISM)
                && (target->type != TRACK_RUNTIME_TYPE_STACK)
                && (target->type != TRACK_RUNTIME_TYPE_WAVE)
                && (target->type != TRACK_RUNTIME_TYPE_FM))) return;
    const uint8_t target_voices =
        CONTROL_AUDIO_PROGRAM_DECODE_VOICES(target->flags);
    if (target_voices >= synth_polyphony_get_voice_count(entity)) return;
    seq_engine_control_disarm_track(entity);
    audio_command_executor_close_outputs_from(entity, target_voices);
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

static audio_command_apply_result_t audio_install_prepared_program(
    uint8_t entity, const control_audio_program_descriptor_t *descriptor)
{
    if ((descriptor == NULL) || (entity >= BRICK_ENTITY_CAPACITY))
        return AUDIO_COMMAND_APPLY_INVALID;
    const audio_note_engine_install_spec_t spec = {
        .entity_id = entity, .engine = descriptor->engine,
        .family = descriptor->family, .type = descriptor->type,
        .flags = descriptor->flags };
    return (audio_note_engine_adapter_install_prepared(&spec) != 0U)
        ? AUDIO_COMMAND_APPLY_OK : AUDIO_COMMAND_APPLY_PROGRAM_INSTALL;
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
    if ((command->id == CONTROL_AUDIO_CONFIG_POLY_VOICES)
            && (command->entity < SEQ_LANE_CAPACITY))
    {
        const float decoded = live_parameter_event_decode_float(
            (int32_t)command->value);
        const uint8_t voices = (uint8_t)decoded;
        if ((decoded == (float)voices) && (voices >= 1U)
                && (voices < synth_polyphony_get_voice_count(command->entity)))
        {
            seq_engine_control_disarm_track(command->entity);
            audio_command_executor_close_outputs_from(command->entity, voices);
        }
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
        return audio_recorder_capture_audio_start(command->id, command->value);
    }
    return audio_recorder_capture_audio_stop(command->id);
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
        if (command->entity < SEQ_LANE_CAPACITY)
        {
            memset(g_audio_seq_output[command->entity], 0,
                   sizeof(g_audio_seq_output[command->entity]));
            g_audio_seq_track_mask &=
                (uint16_t)~(uint16_t)(1U << command->entity);
        }
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
        /* PANIC owns the complete AUDIO note lifetime, including the SEQ
         * execution mirror.  Keep this inside the primitive: Project commit
         * invokes PANIC directly while applying its atomic state snapshot and
         * therefore does not pass through the FIFO post-processing below. */
        memset(g_audio_seq_output, 0, sizeof(g_audio_seq_output));
        g_audio_seq_track_mask = 0U;
    }
    return 1U;
}

static audio_command_apply_result_t audio_command_apply_patch_state_commit(
    const control_audio_command_t *commit)
{
    const uint8_t transition = CONTROL_AUDIO_COMMAND_KIND(commit);
    __DMB();
    const uint16_t count = g_audio_state_transaction.count;
    const control_audio_command_t *const commands =
        g_audio_state_transaction.command;
    if ((transition > CONTROL_AUDIO_STATE_PATCH) || (count == 0U)
            || (count > AUDIO_STATE_TRANSACTION_COMMAND_CAPACITY))
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
        const control_audio_program_descriptor_t target =
            control_audio_program_unpack(programs[entity]->value);
        audio_command_prepare_synth_program_change(entity, &target);
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

static uint8_t audio_prepared_apply_float(uint8_t entity, param_id_t id,
                                          float value, uint8_t scope)
{
    uint32_t bits = 0U;
    memcpy(&bits, &value, sizeof(bits));
    return live_parameter_audio_runtime_apply_param(entity, id, bits, scope);
}

static uint8_t audio_prepared_apply_tone(
    uint8_t entity, const tone_program_control_t *tone)
{
    const uint8_t count = tone_param_codec_count(tone->tag);
    for (uint8_t slot = 0U; slot < count; ++slot)
    {
        param_id_t id;
        float value;
        if (!tone_param_codec_slot_to_param(tone->tag, slot, &id)
                || !tone_program_control_get_from(tone, id, &value)) return 0U;
        /* MIDI PROGRAM/CC values are CONTROL/SEQ output state.  They are part
         * of the persistent TONE payload, but have no AUDIO endpoint. */
        if ((id == PARAM_MIDI_PROGRAM)
                || ((id >= PARAM_MIDI_CC1_1) && (id <= PARAM_MIDI_CC3_4)))
            continue;
        if (!audio_prepared_apply_float(entity, id, value,
                CONTROL_AUDIO_PARAM_KIND_BASE_TRACK)) return 0U;
    }
    return 1U;
}

static uint8_t audio_prepared_apply_fm(
    uint8_t entity, const fm_control_state_t *fm)
{
    track_audio_runtime_ctx_t ctx;
    if ((audio_note_engine_adapter_current_ctx(entity, &ctx) == 0U)
            || (ctx.program_route.engine != TRACK_RUNTIME_ENGINE_FM)) return 0U;
    brick6_fm_runtime_set_base_voice(ctx.program_route.instance_id, &fm->base);
    if (audio_note_engine_adapter_project_track_configuration(entity) == 0U)
        return 0U;
    static const param_id_t macro_ids[] = {
        PARAM_FM_RATIO, PARAM_FM_BRIGHT, PARAM_FM_BODY, PARAM_FM_DETAIL,
        PARAM_FM_METAL, PARAM_FM_ENV_ATTACK, PARAM_FM_ENV_DECAY,
        PARAM_FM_ENV_SUSTAIN, PARAM_FM_ENV_RELEASE, PARAM_FM_PLAY_VEL,
        PARAM_FM_PLAY_KEY, PARAM_FM_PLAY_PITCH_ENV, PARAM_FM_PLAY_PITCH_TIME };
    for (uint8_t i = 0U; i < (uint8_t)(sizeof(macro_ids)/sizeof(macro_ids[0])); ++i)
    {
        float value;
        if (!fm_control_state_get_public_param_from(fm, macro_ids[i], &value)
                || !audio_prepared_apply_float(entity, macro_ids[i], value,
                    CONTROL_AUDIO_PARAM_KIND_BASE_TRACK)) return 0U;
    }
    return 1U;
}

static uint8_t audio_prepared_apply_common(
    uint8_t entity, const prepared_audio_entity_state_t *state)
{
    static const param_id_t filter_ids[12U] = {
        PARAM_FILTER_MORPH, PARAM_FILTER_CUTOFF, PARAM_FILTER_RESONANCE,
        PARAM_FILTER_EG_AMT, PARAM_FILTER_ATTACK, PARAM_FILTER_DECAY,
        PARAM_FILTER_SUSTAIN, PARAM_FILTER_RELEASE, PARAM_FILTER_KEYTRK,
        PARAM_FILTER_ENVRST, PARAM_FILTER_ENVDLY, PARAM_ENV_RETRIG_FILTER };
    static const param_id_t vca_ids[6U] = {
        PARAM_VCA_ATTACK, PARAM_VCA_DECAY, PARAM_VCA_SUSTAIN,
        PARAM_VCA_RELEASE, PARAM_FILTER_MODE, PARAM_ENV_RETRIG_VCA };
    static const param_id_t mixer_ids[5U] = {
        PARAM_MIX_LEVEL, PARAM_MIX_PAN, PARAM_MIX_SEND1,
        PARAM_MIX_SEND2, PARAM_MIX_SEND3 };
    const float *const filter = (const float *)&state->filter;
    const float *const vca = (const float *)&state->vca;
    const float *const mixer = (const float *)&state->mixer;
#if BRICK_PATTERN_RECALL_DIAG
    pattern_recall_diag_phase(PATTERN_DIAG_PHASE_FILTER_APPLY);
#endif
    for (uint8_t i = 0U; i < 12U; ++i)
        if (param_registry_projected_track_param_is_applicable(filter_ids[i],
                (track_family_t)state->ui_family,
                (track_runtime_type_t)state->program.type,
                state->topology_role == ENTITY_ROLE_GROUP_MASTER,
                state->topology_role == ENTITY_ROLE_GROUP_CHILD)
                && !audio_prepared_apply_float(entity, filter_ids[i], filter[i],
                    CONTROL_AUDIO_PARAM_KIND_BASE_TRACK)) return 0U;
#if BRICK_PATTERN_RECALL_DIAG
    pattern_recall_diag_phase(PATTERN_DIAG_PHASE_VCA_APPLY);
#endif
    for (uint8_t i = 0U; i < 6U; ++i)
        if (param_registry_projected_track_param_is_applicable(vca_ids[i],
                (track_family_t)state->ui_family,
                (track_runtime_type_t)state->program.type,
                state->topology_role == ENTITY_ROLE_GROUP_MASTER,
                state->topology_role == ENTITY_ROLE_GROUP_CHILD)
                && !audio_prepared_apply_float(entity, vca_ids[i], vca[i],
                    CONTROL_AUDIO_PARAM_KIND_BASE_TRACK)) return 0U;
#if BRICK_PATTERN_RECALL_DIAG
    pattern_recall_diag_phase(PATTERN_DIAG_PHASE_MIXER_APPLY);
#endif
    for (uint8_t i = 0U; i < 5U; ++i)
        if (param_registry_projected_track_param_is_applicable(mixer_ids[i],
                (track_family_t)state->ui_family,
                (track_runtime_type_t)state->program.type,
                state->topology_role == ENTITY_ROLE_GROUP_MASTER,
                state->topology_role == ENTITY_ROLE_GROUP_CHILD)
                && !audio_prepared_apply_float(entity, mixer_ids[i], mixer[i],
                    CONTROL_AUDIO_PARAM_KIND_BASE_TRACK)) return 0U;
    const uint8_t configurable_polyphony = (uint8_t)(
        ((state->program.family == TRACK_RUNTIME_FAMILY_SAMPLER)
            && (state->program.type == TRACK_RUNTIME_TYPE_MULTI))
        || ((state->program.family == TRACK_RUNTIME_FAMILY_SYNTH)
            && ((state->program.type == TRACK_RUNTIME_TYPE_PRISM)
                || (state->program.type == TRACK_RUNTIME_TYPE_STACK)
                || (state->program.type == TRACK_RUNTIME_TYPE_WAVE)
                || (state->program.type == TRACK_RUNTIME_TYPE_FM))));
    if (configurable_polyphony != 0U)
    {
#if BRICK_PATTERN_RECALL_DIAG
        pattern_recall_diag_phase(PATTERN_DIAG_PHASE_POLY_APPLY);
#endif
        const float voices = (float)state->polyphony.voice_count;
        if (!audio_prepared_apply_float(entity,
                (param_id_t)CONTROL_AUDIO_CONFIG_POLY_VOICES, voices,
                CONTROL_AUDIO_PARAM_KIND_BASE_TRACK)
                || !audio_prepared_apply_float(entity, PARAM_CFG_POLY_SPREAD,
                    state->polyphony.spread,
                    CONTROL_AUDIO_PARAM_KIND_BASE_TRACK)) return 0U;
    }
    const audio_fx_control_state_t *const fx = &state->audio_fx;
#if BRICK_PATTERN_RECALL_DIAG
    pattern_recall_diag_phase(PATTERN_DIAG_PHASE_FX_APPLY);
#endif
    if (entity < BRICK_ENTITY_TOP_LEVEL_COUNT)
    {
        if (!audio_fx_runtime_set_filter_pos(entity, fx->config.filter_position)
                || !audio_fx_runtime_set_order(entity, fx->config.order)) return 0U;
        for (uint8_t slot = 0U; slot < 2U; ++slot)
            if (!audio_fx_runtime_set_spatial_mode(entity, (audio_fx_slot_t)slot,
                    fx->config.spatial_mode[slot])) return 0U;
    }
    static const param_id_t fx_ids[8U] = {
        PARAM_AUDIO_FX_MODEL, PARAM_AUDIO_FX_P1, PARAM_AUDIO_FX_P2,
        PARAM_AUDIO_FX_P3, PARAM_AUDIO_FX_B_MODEL, PARAM_AUDIO_FX_B_P1,
        PARAM_AUDIO_FX_B_P2, PARAM_AUDIO_FX_B_P3 };
    const float fx_values[8U] = { (float)fx->model[0], fx->p1[0], fx->p2[0],
        fx->p3[0], (float)fx->model[1], fx->p1[1], fx->p2[1], fx->p3[1] };
    for (uint8_t i = 0U; i < 8U; ++i)
        if (param_registry_projected_track_param_is_applicable(fx_ids[i],
                (track_family_t)state->ui_family,
                (track_runtime_type_t)state->program.type,
                state->topology_role == ENTITY_ROLE_GROUP_MASTER,
                state->topology_role == ENTITY_ROLE_GROUP_CHILD)
                && !audio_prepared_apply_float(entity, fx_ids[i], fx_values[i],
                    CONTROL_AUDIO_PARAM_KIND_BASE_TRACK)) return 0U;
    for (uint8_t slot = 0U; slot < 2U; ++slot)
    {
        const param_id_t id = slot
            ? PARAM_GROUP_FX_B_LEVEL : PARAM_GROUP_FX_A_LEVEL;
        if (param_registry_projected_track_param_is_applicable(id,
                (track_family_t)state->ui_family,
                (track_runtime_type_t)state->program.type,
                state->topology_role == ENTITY_ROLE_GROUP_MASTER,
                state->topology_role == ENTITY_ROLE_GROUP_CHILD)
                && !audio_prepared_apply_float(entity,id,fx->group_level[slot],
                    CONTROL_AUDIO_PARAM_KIND_BASE_TRACK)) return 0U;
    }
    return 1U;
}

static uint8_t audio_prepared_apply_mod(
    uint8_t entity, const prepared_audio_mod_state_t *mod)
{
    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
    {
        const mod_lfo_control_value_t *const value = &mod->lfo.lfo[lfo];
        const float fields[MOD_LFO_PARAM_COUNT] = {
            value->rate, value->shape, value->trigger, value->phase };
        for (uint8_t param = 0U; param < MOD_LFO_PARAM_COUNT; ++param)
            if (!mod_lfo_v1_set_track_param_audio(entity, lfo,
                    (mod_lfo_param_t)param, fields[param])) return 0U;
    }
    const float env[MOD_ENV3_PARAM_COUNT] = { mod->envelope.attack,
        mod->envelope.decay, mod->envelope.sustain, mod->envelope.release };
    for (uint8_t param = 0U; param < MOD_ENV3_PARAM_COUNT; ++param)
        if (!mod_env3_audio_apply_track_param(entity,
                (mod_env3_param_t)param, env[param])) return 0U;
    mod_env3_audio_apply_retrigger(entity, mod->envelope.retrigger);
    for (uint8_t op = 0U; op < 2U; ++op)
    {
        for (uint8_t input = 0U; input < 2U; ++input)
            if (!audio_mod_matrix_set_multi_source(entity, op, input,
                    mod->multi_source[op][input])) return 0U;
        if (!audio_mod_matrix_set_slew_source(entity, op,
                mod->slew_source[op])
                || !audio_mod_matrix_set_slew_amount(entity, op,
                    mod->slew_amount[op])) return 0U;
    }
    for (uint8_t route = 0U; route < 8U; ++route)
    {
        const prepared_audio_mod_route_t *const r = &mod->route[route];
        if (!audio_mod_matrix_set_route_source(entity, route, r->source)
                || !audio_mod_matrix_set_route_destination(entity, route,
                    r->destination)
                || !audio_mod_matrix_set_route_depth(entity, route, r->depth)
                || !audio_mod_matrix_set_route_enabled(entity, route,
                    r->enabled)) return 0U;
    }
    audio_mod_matrix_rebuild_track(entity);
    return audio_note_engine_adapter_project_track_configuration(entity);
}

static uint8_t audio_prepared_apply_resource(
    uint8_t entity, const prepared_audio_resource_state_t *resource)
{
    if (resource->kind == PREPARED_AUDIO_RESOURCE_NONE) return 1U;
    if (resource->kind == PREPARED_AUDIO_RESOURCE_SAMPLER)
    {
        track_audio_runtime_ctx_t ctx;
        if (audio_note_engine_adapter_current_ctx(entity, &ctx) == 0U) return 0U;
        uint16_t current = UINT16_MAX;
        if (ctx.type == TRACK_RUNTIME_TYPE_MULTI)
        {
            if ((brick6_sampler_runtime_get_multi_instrument(entity, &current)
                    != 0U) && (current == resource->sampler_runtime)) return 1U;
            brick6_sampler_runtime_set_multi_instrument(
                entity, resource->sampler_runtime);
            if ((resource->sampler_runtime != UINT16_MAX)
                    && ((brick6_sampler_runtime_get_multi_instrument(
                        entity, &current) == 0U)
                        || (current != resource->sampler_runtime))) return 0U;
        }
        else
        {
            if ((brick6_sampler_runtime_get_sample(entity, &current) != 0U)
                    && (current == resource->sampler_runtime)) return 1U;
            brick6_sampler_runtime_set_sample(entity, resource->sampler_runtime);
        }
        g_audio_state_rebind_mask |= (uint16_t)(1U << entity);
        return 1U;
    }
    if (resource->kind != PREPARED_AUDIO_RESOURCE_WAVETABLE) return 0U;
    track_audio_runtime_ctx_t ctx;
    if ((audio_note_engine_adapter_current_ctx(entity, &ctx) == 0U)
            || (ctx.program_route.engine != TRACK_RUNTIME_ENGINE_WAVE)) return 0U;
    const uint8_t voices = synth_polyphony_get_voice_count(entity);
    if ((voices == 0U) || (voices > SYNTH_POLYPHONY_MAX_VOICES)) return 0U;
    for (uint8_t voice = 0U; voice < voices; ++voice)
    {
        const uint8_t instance = synth_polyphony_get_slot(entity, voice);
        if (instance >= BRICK6_WAVE_VOICE_INSTANCE_COUNT) return 0U;
        for (uint8_t osc = 0U; osc < BRICK6_WAVE_OSC_COUNT; ++osc)
            brick6_wave_runtime_set_osc_table_wavetable_generation(instance,
                osc, resource->wavetable[osc].wavetable_slot,
                resource->wavetable[osc].generation);
    }
    return 1U;
}

#if BRICK_PATTERN_RECALL_DIAG
static void audio_pattern_diag_failure(uint8_t subsystem, uint8_t entity,
    uint16_t field, uint8_t code, uint32_t actual, uint32_t expected,
    uint32_t capacity, uint32_t context)
{
    pattern_recall_diag_failure(1U, subsystem, entity, field, code, actual,
                                expected, capacity, context);
}

static uint8_t audio_pattern_diag_expected_engine(
    const control_audio_program_descriptor_t *program,
    uint8_t *out_engine)
{
    uint8_t engine = TRACK_RUNTIME_ENGINE_NONE;
    if (program->family == TRACK_RUNTIME_FAMILY_OFF)
    {
        if (program->type != TRACK_RUNTIME_TYPE_NONE) return 0U;
    }
    else if (program->family == TRACK_RUNTIME_FAMILY_MIDI)
    {
        if (program->type != TRACK_RUNTIME_TYPE_MIDI) return 0U;
    }
    else if (program->family == TRACK_RUNTIME_FAMILY_EXTERNAL)
    {
        if (program->type != TRACK_RUNTIME_TYPE_EXTERNAL) return 0U;
        engine = TRACK_RUNTIME_ENGINE_AUDIO_TRACK;
    }
    else if (program->family == TRACK_RUNTIME_FAMILY_DRUM)
    {
        if (program->type != TRACK_RUNTIME_TYPE_DRUM_MD) return 0U;
        engine = TRACK_RUNTIME_ENGINE_DRUM;
    }
    else if (program->family == TRACK_RUNTIME_FAMILY_SAMPLER)
    {
        if ((program->type != TRACK_RUNTIME_TYPE_RAM)
                && (program->type != TRACK_RUNTIME_TYPE_STREAM)
                && (program->type != TRACK_RUNTIME_TYPE_MULTI)) return 0U;
        engine = TRACK_RUNTIME_ENGINE_SAMPLER;
    }
    else if (program->family == TRACK_RUNTIME_FAMILY_SYNTH)
    {
        switch ((track_runtime_type_t)program->type)
        {
            case TRACK_RUNTIME_TYPE_PRISM: engine = TRACK_RUNTIME_ENGINE_PRISM; break;
            case TRACK_RUNTIME_TYPE_STACK: engine = TRACK_RUNTIME_ENGINE_STACK; break;
            case TRACK_RUNTIME_TYPE_WAVE: engine = TRACK_RUNTIME_ENGINE_WAVE; break;
            case TRACK_RUNTIME_TYPE_FM: engine = TRACK_RUNTIME_ENGINE_FM; break;
            case TRACK_RUNTIME_TYPE_TB303: engine = TRACK_RUNTIME_ENGINE_TB303; break;
            case TRACK_RUNTIME_TYPE_ACID: engine = TRACK_RUNTIME_ENGINE_ACID; break;
            default: return 0U;
        }
    }
    else if (!((program->family == TRACK_RUNTIME_FAMILY_OTHER)
            && (program->type == TRACK_RUNTIME_TYPE_GROUP)
            && ((program->flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER)
                != 0U))) return 0U;
    *out_engine = engine;
    return 1U;
}

static uint8_t audio_pattern_diag_synth_renderer(uint8_t engine)
{
    return (uint8_t)((engine == TRACK_RUNTIME_ENGINE_DRUM)
        || (engine == TRACK_RUNTIME_ENGINE_PRISM)
        || (engine == TRACK_RUNTIME_ENGINE_STACK)
        || (engine == TRACK_RUNTIME_ENGINE_WAVE)
        || (engine == TRACK_RUNTIME_ENGINE_FM)
        || (engine == TRACK_RUNTIME_ENGINE_TB303)
        || (engine == TRACK_RUNTIME_ENGINE_ACID));
}

static void audio_pattern_diag_param(uint8_t subsystem, uint8_t entity,
                                     param_id_t id, float value)
{
    if ((id < PARAM_COUNT)
            && (param_spec_audio_command_value_is_valid(id, value) != 0U))
        return;
    uint32_t bits = 0U;
    memcpy(&bits, &value, sizeof(bits));
    audio_pattern_diag_failure(subsystem, entity, id,
        PATTERN_DIAG_CODE_DOMAIN, bits, 0U, PARAM_COUNT, id);
}

static void audio_pattern_diag_entity_values(uint8_t entity,
    const prepared_audio_entity_state_t *target)
{
    if (target->program.family == TRACK_RUNTIME_FAMILY_MIDI) return;
    if (target->product_kind == PREPARED_AUDIO_PRODUCT_TONE)
    {
        const uint8_t count = tone_param_codec_count(target->product.tone.tag);
        for (uint8_t slot = 0U; slot < count; ++slot)
        {
            param_id_t id;
            float value;
            if (!tone_param_codec_slot_to_param(
                    target->product.tone.tag, slot, &id)
                    || !tone_program_control_get_from(
                        &target->product.tone, id, &value))
            {
                audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_TONE,
                    entity, slot, PATTERN_DIAG_CODE_DOMAIN,
                    target->product.tone.tag, 0U, count, 0U);
                continue;
            }
            if ((id == PARAM_MIDI_PROGRAM)
                    || ((id >= PARAM_MIDI_CC1_1)
                        && (id <= PARAM_MIDI_CC3_4))) continue;
            audio_pattern_diag_param(PATTERN_DIAG_SUBSYSTEM_TONE,
                                      entity, id, value);
        }
    }
    else if (target->product_kind == PREPARED_AUDIO_PRODUCT_FM)
    {
        static const param_id_t ids[] = {
            PARAM_FM_RATIO, PARAM_FM_BRIGHT, PARAM_FM_BODY, PARAM_FM_DETAIL,
            PARAM_FM_METAL, PARAM_FM_ENV_ATTACK, PARAM_FM_ENV_DECAY,
            PARAM_FM_ENV_SUSTAIN, PARAM_FM_ENV_RELEASE, PARAM_FM_PLAY_VEL,
            PARAM_FM_PLAY_KEY, PARAM_FM_PLAY_PITCH_ENV,
            PARAM_FM_PLAY_PITCH_TIME };
        for (uint8_t i = 0U; i < (uint8_t)(sizeof(ids) / sizeof(ids[0])); ++i)
        {
            float value = 0.0f;
            if (!fm_control_state_get_public_param_from(
                    &target->product.fm, ids[i], &value))
                audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_FM,
                    entity, ids[i], PATTERN_DIAG_CODE_DOMAIN, 0U, 1U,
                    PARAM_COUNT, i);
            else
                audio_pattern_diag_param(PATTERN_DIAG_SUBSYSTEM_FM,
                                          entity, ids[i], value);
        }
    }
    static const param_id_t filter_ids[12U] = {
        PARAM_FILTER_MORPH, PARAM_FILTER_CUTOFF, PARAM_FILTER_RESONANCE,
        PARAM_FILTER_EG_AMT, PARAM_FILTER_ATTACK, PARAM_FILTER_DECAY,
        PARAM_FILTER_SUSTAIN, PARAM_FILTER_RELEASE, PARAM_FILTER_KEYTRK,
        PARAM_FILTER_ENVRST, PARAM_FILTER_ENVDLY, PARAM_ENV_RETRIG_FILTER };
    static const param_id_t vca_ids[6U] = {
        PARAM_VCA_ATTACK, PARAM_VCA_DECAY, PARAM_VCA_SUSTAIN,
        PARAM_VCA_RELEASE, PARAM_FILTER_MODE, PARAM_ENV_RETRIG_VCA };
    static const param_id_t mixer_ids[5U] = {
        PARAM_MIX_LEVEL, PARAM_MIX_PAN, PARAM_MIX_SEND1,
        PARAM_MIX_SEND2, PARAM_MIX_SEND3 };
    const float *const filter = (const float *)&target->filter;
    const float *const vca = (const float *)&target->vca;
    const float *const mixer = (const float *)&target->mixer;
    for (uint8_t i = 0U; i < 12U; ++i)
        if (param_registry_projected_track_param_is_applicable(filter_ids[i],
                (track_family_t)target->ui_family,
                (track_runtime_type_t)target->program.type,
                target->topology_role == ENTITY_ROLE_GROUP_MASTER,
                target->topology_role == ENTITY_ROLE_GROUP_CHILD))
            audio_pattern_diag_param(PATTERN_DIAG_SUBSYSTEM_FILTER,
                                      entity, filter_ids[i], filter[i]);
    for (uint8_t i = 0U; i < 6U; ++i)
        if (param_registry_projected_track_param_is_applicable(vca_ids[i],
                (track_family_t)target->ui_family,
                (track_runtime_type_t)target->program.type,
                target->topology_role == ENTITY_ROLE_GROUP_MASTER,
                target->topology_role == ENTITY_ROLE_GROUP_CHILD))
            audio_pattern_diag_param(PATTERN_DIAG_SUBSYSTEM_VCA,
                                      entity, vca_ids[i], vca[i]);
    for (uint8_t i = 0U; i < 5U; ++i)
        if (param_registry_projected_track_param_is_applicable(mixer_ids[i],
                (track_family_t)target->ui_family,
                (track_runtime_type_t)target->program.type,
                target->topology_role == ENTITY_ROLE_GROUP_MASTER,
                target->topology_role == ENTITY_ROLE_GROUP_CHILD))
            audio_pattern_diag_param(PATTERN_DIAG_SUBSYSTEM_MIXER,
                                      entity, mixer_ids[i], mixer[i]);
    if (target->modulation_present != 0U)
    {
        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            const mod_lfo_control_value_t *const value =
                &target->modulation.lfo.lfo[lfo];
            const float fields[MOD_LFO_PARAM_COUNT] = {
                value->rate, value->shape, value->trigger, value->phase };
            for (uint8_t param = 0U; param < MOD_LFO_PARAM_COUNT; ++param)
                if (!isfinite(fields[param]))
                {
                    uint32_t bits = 0U;
                    memcpy(&bits, &fields[param], sizeof(bits));
                    audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_MOD,
                        entity, (uint16_t)(lfo * MOD_LFO_PARAM_COUNT + param),
                        PATTERN_DIAG_CODE_DOMAIN, bits, 0U,
                        MOD_LFO_PARAM_COUNT, lfo);
                }
        }
        for (uint8_t op = 0U; op < 2U; ++op)
        {
            if ((target->modulation.multi_source[op][0]
                    >= MOD_MATRIX_SOURCE_COUNT)
                    || (target->modulation.multi_source[op][1]
                        >= MOD_MATRIX_SOURCE_COUNT)
                    || (target->modulation.slew_source[op]
                        >= MOD_MATRIX_SOURCE_COUNT)
                    || !isfinite(target->modulation.slew_amount[op]))
                audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_MOD,
                    entity, (uint16_t)(32U + op), PATTERN_DIAG_CODE_DOMAIN,
                    (uint32_t)target->modulation.multi_source[op][0]
                        | ((uint32_t)target->modulation.multi_source[op][1]
                            << 8U)
                        | ((uint32_t)target->modulation.slew_source[op]
                            << 16U),
                    0U, MOD_MATRIX_SOURCE_COUNT, 0U);
        }
    }
}

static uint8_t audio_pattern_diag_runtime_preflight(
    const control_audio_command_t *commit, const prepared_audio_state_t *state,
    uint16_t changed)
{
    const uint16_t failures_before =
        g_pattern_recall_diag.runtime_failure_count;
    uint16_t target_voice_total = 0U;
    uint16_t input_seen = 0U;
    g_pattern_recall_diag.changed_program_mask = changed;
    pattern_recall_diag_phase(PATTERN_DIAG_PHASE_AUDIO_RUNTIME_PREFLIGHT);
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        const prepared_audio_entity_state_t *const target =
            &state->entity[entity];
        const control_audio_program_descriptor_t *const program =
            &target->program;
        track_audio_runtime_ctx_t current = {0};
        const uint8_t has_current = audio_note_engine_adapter_current_ctx(
            entity, &current);
        const uint8_t held =
            audio_note_engine_adapter_diag_held_count(entity);
        uint8_t expected_engine = TRACK_RUNTIME_ENGINE_NONE;
        uint8_t target_voices = CONTROL_AUDIO_PROGRAM_DECODE_VOICES(
            program->flags);
        if (target_voices == 0U) target_voices = 1U;
        const uint8_t trim = ((changed & (uint16_t)(1U << entity)) != 0U
                && program->family == TRACK_RUNTIME_FAMILY_SYNTH
                && held > target_voices)
            ? (uint8_t)(held - target_voices) : 0U;
        g_pattern_recall_diag.polyphony[entity] =
            (pattern_recall_diag_polyphony_t){
                .current_renderer = has_current
                    ? current.program_route.engine : TRACK_RUNTIME_ENGINE_NONE,
                .current_held_count = held,
                .target_polyphony = target_voices,
                .trim_count = trim,
                .owner_renderer = program->engine,
                .changed = (changed & (uint16_t)(1U << entity)) ? 1U : 0U
            };
        if (!control_audio_program_descriptor_is_structural(program,
                TRACK_RUNTIME_ENGINE_COUNT, TRACK_RUNTIME_FAMILY_OTHER,
                TRACK_RUNTIME_TYPE_COUNT)
                || !audio_pattern_diag_expected_engine(
                    program, &expected_engine)
                || (program->engine != expected_engine))
            audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_PROGRAM,
                entity, 0U, PATTERN_DIAG_CODE_INSTALL,
                control_audio_program_pack(program), expected_engine,
                TRACK_RUNTIME_ENGINE_COUNT,
                has_current ? ((uint32_t)current.program_route.engine
                    | ((uint32_t)current.family << 8U)
                    | ((uint32_t)current.type << 16U)
                    | ((uint32_t)current.flags << 24U)) : 0U);
        if ((target->active == 0U)
                != (program->family == TRACK_RUNTIME_FAMILY_OFF))
            audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_PROGRAM,
                entity, 1U, PATTERN_DIAG_CODE_MISMATCH, target->active,
                program->family != TRACK_RUNTIME_FAMILY_OFF, 1U,
                control_audio_program_pack(program));
        if ((target->active != 0U)
                && ((target->midi_channel < 1U)
                    || (target->midi_channel > 16U)
                    || (target->midi_source >= TRACK_MIDI_SOURCE_COUNT)))
            audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_PROGRAM,
                entity, CONTROL_AUDIO_PARAM_MIDI_CONFIG,
                PATTERN_DIAG_CODE_DOMAIN,
                (uint32_t)target->midi_channel
                    | ((uint32_t)target->midi_source << 8U),
                1U, TRACK_MIDI_SOURCE_COUNT, 16U);
        if (audio_pattern_diag_synth_renderer(program->engine) != 0U)
        {
            const uint8_t owned = (program->engine == TRACK_RUNTIME_ENGINE_DRUM
                    || program->engine == TRACK_RUNTIME_ENGINE_TB303
                    || program->engine == TRACK_RUNTIME_ENGINE_ACID)
                ? 1U : target_voices;
            target_voice_total = (uint16_t)(target_voice_total + owned);
            if ((owned < 1U) || (owned > SYNTH_POLYPHONY_MAX_VOICES))
                audio_pattern_diag_failure(
                    PATTERN_DIAG_SUBSYSTEM_POLYPHONY, entity, 0U,
                    PATTERN_DIAG_CODE_RANGE, owned, 1U,
                    SYNTH_POLYPHONY_MAX_VOICES,
                    ((uint32_t)held << 16U) | trim);
            if (((changed & (uint16_t)(1U << entity)) == 0U)
                    && (held > owned))
                audio_pattern_diag_failure(
                    PATTERN_DIAG_SUBSYSTEM_REBIND, entity, 0U,
                    PATTERN_DIAG_CODE_HELD_OUTPUTS, held, 0U, owned,
                    ((uint32_t)current.program_route.engine << 8U)
                        | program->engine);
        }
        if ((target->program.family == TRACK_RUNTIME_FAMILY_MIDI)
                && ((target->modulation_present != 0U)
                    || (target->resource.kind
                        != PREPARED_AUDIO_RESOURCE_NONE)))
            audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_PROGRAM,
                entity, 2U, PATTERN_DIAG_CODE_ENDPOINT,
                ((uint32_t)target->modulation_present << 8U)
                    | target->resource.kind, 0U, 0U, program->family);
        if ((target->modulation_present != 0U)
                && (entity >= PREPARED_AUDIO_TEMP_OWNER_COUNT))
            audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_MOD,
                entity, 0U, PATTERN_DIAG_CODE_CAPACITY, entity, 0U,
                PREPARED_AUDIO_TEMP_OWNER_COUNT, 0U);
        audio_pattern_diag_entity_values(entity, target);

        const prepared_audio_resource_state_t *const resource =
            &target->resource;
        if (resource->kind == PREPARED_AUDIO_RESOURCE_NONE)
        {
            /* NONE is a complete, valid target. */
        }
        else if (resource->kind == PREPARED_AUDIO_RESOURCE_SAMPLER)
        {
            const uint16_t id = resource->sampler_runtime;
            uint8_t ready = (resource->present == 0U && id == UINT16_MAX)
                ? 1U : 0U;
            if (resource->present != 0U)
            {
                if (program->type == TRACK_RUNTIME_TYPE_MULTI)
                    ready = multi_sample_audio_projection_is_ready(id);
                else if (program->type == TRACK_RUNTIME_TYPE_RAM)
                {
                    sampler_ram_audio_descriptor_t descriptor;
                    ready = sampler_ram_audio_projection_resolve(
                        id, &descriptor);
                }
                else if (program->type == TRACK_RUNTIME_TYPE_STREAM)
                    ready = sample_classic_audio_projection_is_ready(id);
            }
            if (ready == 0U)
                audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_RESOURCE,
                    entity, 0U, PATTERN_DIAG_CODE_NOT_READY, id,
                    resource->present, UINT16_MAX, program->type);
        }
        else if (resource->kind == PREPARED_AUDIO_RESOURCE_WAVETABLE)
        {
            for (uint8_t osc = 0U; osc < BRICK6_WAVE_OSC_COUNT; ++osc)
            {
                const audio_wave_table_selection_t *const selection =
                    &resource->wavetable[osc];
                if (selection->wavetable_slot == WAVETABLE_POOL_INVALID_SLOT)
                {
                    if (selection->generation != 0U)
                        audio_pattern_diag_failure(
                            PATTERN_DIAG_SUBSYSTEM_RESOURCE, entity, osc,
                            PATTERN_DIAG_CODE_GENERATION,
                            selection->generation, 0U,
                            WAVETABLE_POOL_MAX_SLOTS,
                            selection->wavetable_slot);
                    continue;
                }
                audio_wavetable_descriptor_t descriptor;
                if (!audio_wavetable_registry_resolve(
                        selection->wavetable_slot, selection->generation,
                        &descriptor))
                    audio_pattern_diag_failure(
                        PATTERN_DIAG_SUBSYSTEM_RESOURCE, entity, osc,
                        PATTERN_DIAG_CODE_NOT_READY,
                        selection->generation, 1U,
                        WAVETABLE_POOL_MAX_SLOTS,
                        selection->wavetable_slot);
            }
        }
        else
            audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_RESOURCE,
                entity, 0U, PATTERN_DIAG_CODE_DOMAIN, resource->kind,
                PREPARED_AUDIO_RESOURCE_NONE,
                PREPARED_AUDIO_RESOURCE_WAVETABLE, program->type);
    }
    if (target_voice_total > SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
        audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_POLYPHONY,
            BRICK_ENTITY_INVALID_ID, 1U, PATTERN_DIAG_CODE_VOICE_BUDGET,
            target_voice_total, 0U, SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET,
            changed);
    for (uint8_t input = 0U; input < ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT;
         ++input)
    {
        const uint8_t owner = state->input_owner[input];
        if (owner == BRICK_ENTITY_INVALID_ID) continue;
        if ((owner >= TRACK_COUNT)
                || (state->entity[owner].active == 0U)
                || (state->entity[owner].program.family
                    != TRACK_RUNTIME_FAMILY_EXTERNAL)
                || ((input_seen & (uint16_t)(1U << owner)) != 0U))
            audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_INPUT,
                owner, input, PATTERN_DIAG_CODE_ENDPOINT, owner, 0U,
                TRACK_COUNT, state->entity[owner].program.family);
        if (owner < BRICK_ENTITY_CAPACITY)
            input_seen |= (uint16_t)(1U << owner);
    }
    for (uint8_t index = 0U; index < PARAM_GLOBAL_CONTROL_VALUE_COUNT;
         ++index)
    {
        param_id_t id = PARAM_COUNT;
        float value = 0.0f;
        if (!param_global_audio_command_state_get_at(
                &state->global, index, &id, &value) || !isfinite(value))
        {
            uint32_t bits = 0U;
            memcpy(&bits, &value, sizeof(bits));
            audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_GLOBAL,
                BRICK_ENTITY_INVALID_ID, id, PATTERN_DIAG_CODE_DOMAIN,
                bits, 0U, PARAM_GLOBAL_CONTROL_VALUE_COUNT, index);
        }
        else if (param_spec_audio_command_value_is_valid(id, value) == 0U)
            audio_pattern_diag_param(PATTERN_DIAG_SUBSYSTEM_GLOBAL,
                                     BRICK_ENTITY_INVALID_ID, id, value);
    }
    if ((state->tempo_milli_bpm == 0U) || (state->step_q16 == 0U)
            || (state->metronome_level > 127U))
        audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_TRANSPORT,
            BRICK_ENTITY_INVALID_ID, 0U, PATTERN_DIAG_CODE_RANGE,
            state->tempo_milli_bpm, 1U, state->step_q16,
            state->metronome_level);
    for (uint8_t owner = 0U; owner < PREPARED_AUDIO_TEMP_OWNER_COUNT; ++owner)
        for (uint16_t word = 0U; word < PREPARED_AUDIO_PARAM_MASK_WORDS; ++word)
        {
            const uint32_t pending = state->temp_clear_mask[owner][word];
            if ((word == (PREPARED_AUDIO_PARAM_MASK_WORDS - 1U))
                    && ((pending >> (PARAM_COUNT & 31U)) != 0U)
                    && ((PARAM_COUNT & 31U) != 0U))
                audio_pattern_diag_failure(
                    PATTERN_DIAG_SUBSYSTEM_TEMP_CLEAR, owner, word,
                    PATTERN_DIAG_CODE_RANGE, pending, 0U, PARAM_COUNT, 0U);
        }
    pattern_recall_diag_identity(g_pattern_recall_diag.candidate_generation,
        g_pattern_recall_diag.prepared_seq_generation, commit->value,
        CONTROL_AUDIO_COMMAND_KIND(commit), commit->effective_sample_time);
    return (g_pattern_recall_diag.runtime_failure_count == failures_before)
        ? 1U : 0U;
}
#endif

static audio_command_apply_result_t audio_command_apply_prepared_state_commit(
    const control_audio_command_t *commit)
{
    const uint8_t transition = CONTROL_AUDIO_COMMAND_KIND(commit);
#if BRICK_PATTERN_RECALL_DIAG
    if ((g_pattern_recall_diag.magic != PATTERN_RECALL_DIAG_MAGIC)
            || (g_pattern_recall_diag.prepared_audio_generation
                != commit->value))
        pattern_recall_diag_reset(commit->value, commit->value, commit->value);
    const uint16_t runtime_failures_at_enter =
        g_pattern_recall_diag.runtime_failure_count;
    pattern_recall_diag_phase(PATTERN_DIAG_PHASE_AUDIO_IRQ_ENTER);
    pattern_recall_diag_identity(g_pattern_recall_diag.candidate_generation,
        g_pattern_recall_diag.prepared_seq_generation, commit->value,
        transition, commit->effective_sample_time);
    if (transition > CONTROL_AUDIO_STATE_PROJECT)
        audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_META,
            commit->entity, 0U, PATTERN_DIAG_CODE_RANGE, transition, 0U,
            CONTROL_AUDIO_STATE_PROJECT, commit->opcode_kind);
    if (commit->entity >= PREPARED_AUDIO_SLOT_COUNT)
        audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_META,
            commit->entity, 1U, PATTERN_DIAG_CODE_CAPACITY, commit->entity,
            0U, PREPARED_AUDIO_SLOT_COUNT, 0U);
    if (commit->value == 0U)
        audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_META,
            commit->entity, 2U, PATTERN_DIAG_CODE_GENERATION, 0U, 1U,
            UINT32_MAX, 0U);
    if (g_pattern_recall_diag.runtime_failure_count
            != runtime_failures_at_enter)
    {
        __DMB();
        return AUDIO_COMMAND_APPLY_INVALID;
    }
#endif
    if ((transition > CONTROL_AUDIO_STATE_PROJECT)
            || (commit->entity >= PREPARED_AUDIO_SLOT_COUNT)
            || (commit->value == 0U)) return AUDIO_COMMAND_APPLY_INVALID;
    const prepared_audio_slot_t *const slot =
        &g_prepared_audio_slots[commit->entity];
#if BRICK_PATTERN_RECALL_DIAG
    if (slot->ready == 0U)
        audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_META,
            commit->entity, 3U, PATTERN_DIAG_CODE_STALE, slot->ready, 1U,
            1U, slot->reserved);
    if (slot->reserved == 0U)
        audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_META,
            commit->entity, 4U, PATTERN_DIAG_CODE_STALE, slot->reserved, 1U,
            1U, slot->ready);
    if (slot->generation != commit->value)
        audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_META,
            commit->entity, 5U, PATTERN_DIAG_CODE_GENERATION,
            slot->generation, commit->value, UINT32_MAX, 0U);
    if (slot->transition != transition)
        audio_pattern_diag_failure(PATTERN_DIAG_SUBSYSTEM_META,
            commit->entity, 6U, PATTERN_DIAG_CODE_MISMATCH,
            slot->transition, transition, CONTROL_AUDIO_STATE_PROJECT, 0U);
    if (g_pattern_recall_diag.runtime_failure_count
            != runtime_failures_at_enter)
    {
        __DMB();
        return AUDIO_COMMAND_APPLY_INVALID;
    }
#endif
    if ((slot->ready == 0U) || (slot->reserved == 0U)
            || (slot->generation != commit->value)
            || (slot->transition != transition)) return AUDIO_COMMAND_APPLY_INVALID;
    __DMB();
    const prepared_audio_state_t *const state = &slot->state;
    uint16_t changed = 0U;
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        const control_audio_program_descriptor_t *const target =
            &state->entity[entity].program;
        track_audio_runtime_ctx_t current;
        const uint8_t same = (uint8_t)(
            (audio_note_engine_adapter_current_ctx(entity, &current) != 0U)
            && (current.program_route.engine == target->engine)
            && (current.family == target->family)
            && (current.type == target->type)
            && (current.flags == target->flags));
        if ((transition == CONTROL_AUDIO_STATE_PROJECT) || (same == 0U))
            changed |= (uint16_t)(1U << entity);
    }
#if BRICK_PATTERN_RECALL_DIAG
    if (!audio_pattern_diag_runtime_preflight(commit, state, changed))
    {
        __DMB();
        return AUDIO_COMMAND_APPLY_INVALID;
    }
#endif
    if (transition == CONTROL_AUDIO_STATE_PROJECT)
    {
        const control_audio_command_t panic = { .opcode_kind =
            CONTROL_AUDIO_COMMAND_TAG(CONTROL_AUDIO_COMMAND_PANIC,
                                      CONTROL_AUDIO_PANIC_GLOBAL) };
        if (!audio_command_apply_panic(&panic)) return AUDIO_COMMAND_APPLY_INVALID;
    }
    else
    {
        /* PROGRAM installation below already materializes the target synth
         * voice count encoded in flags.  Trim outputs against the old engine
         * while its allocator still owns them; after installation,
         * synth_polyphony_get_voice_count() already reports the target and a
         * late comparison can no longer see that a shrink occurred. */
#if BRICK_PATTERN_RECALL_DIAG
        pattern_recall_diag_phase(PATTERN_DIAG_PHASE_POLYPHONY_TRIM);
#endif
        for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
            if ((changed & (uint16_t)(1U << entity)) != 0U)
                audio_command_prepare_synth_program_change(
                    entity, &state->entity[entity].program);
    }
    const control_audio_program_descriptor_t off = {
        .family = TRACK_RUNTIME_FAMILY_OFF, .type = TRACK_RUNTIME_TYPE_NONE };
#if BRICK_PATTERN_RECALL_DIAG
    pattern_recall_diag_phase(PATTERN_DIAG_PHASE_PROGRAM_CLOSE);
#endif
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
        if ((changed & (uint16_t)(1U << entity)) != 0U)
            audio_command_close_entity(entity);
#if BRICK_PATTERN_RECALL_DIAG
    pattern_recall_diag_phase(PATTERN_DIAG_PHASE_PROGRAM_OFF);
#endif
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
        if ((changed & (uint16_t)(1U << entity)) != 0U)
        {
            const audio_command_apply_result_t result =
                audio_install_prepared_program(entity, &off);
            if (result != AUDIO_COMMAND_APPLY_OK) return result;
        }
    g_audio_state_rebind_deferred = 1U;
    g_audio_state_rebind_mask = changed;
#if BRICK_PATTERN_RECALL_DIAG
    pattern_recall_diag_phase(PATTERN_DIAG_PHASE_PROGRAM_INSTALL);
#endif
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
        if ((changed & (uint16_t)(1U << entity)) != 0U)
        {
            const audio_command_apply_result_t result =
                audio_install_prepared_program(entity,
                    &state->entity[entity].program);
            if (result != AUDIO_COMMAND_APPLY_OK)
            { g_audio_state_rebind_deferred = 0U; return result; }
        }
    brick6_fm_runtime_finalize_pending();
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        const prepared_audio_entity_state_t *const target = &state->entity[entity];
        if (target->active == 0U) continue;
        if (!audio_note_engine_adapter_apply_midi_config(entity,
                target->midi_channel, target->midi_source)) goto invalid;
        /* A MIDI PROGRAM is active for sequencing/routing but deliberately has
         * no audio-routable Tone/Common/FX/Mute/Mod/Resource endpoint. */
        if (target->program.family == TRACK_RUNTIME_FAMILY_MIDI) continue;
#if BRICK_PATTERN_RECALL_DIAG
        pattern_recall_diag_phase(PATTERN_DIAG_PHASE_PRODUCT_APPLY);
#endif
        if (target->product_kind == PREPARED_AUDIO_PRODUCT_FM)
        {
            if (!audio_prepared_apply_fm(entity, &target->product.fm)) goto invalid;
        }
        else if ((target->product_kind != PREPARED_AUDIO_PRODUCT_TONE)
                || !audio_prepared_apply_tone(entity, &target->product.tone))
            goto invalid;
        if (!audio_prepared_apply_common(entity, target)) goto invalid;
        if (!audio_prepared_apply_float(entity, PARAM_MIX_MUTE,
                (float)target->muted,
                CONTROL_AUDIO_PARAM_KIND_BASE_TRACK)) goto invalid;
        if ((target->modulation_present != 0U)
                )
        {
#if BRICK_PATTERN_RECALL_DIAG
            pattern_recall_diag_phase(PATTERN_DIAG_PHASE_MOD_APPLY);
#endif
            if (!audio_prepared_apply_mod(entity, &target->modulation))
            goto invalid;
        }
#if BRICK_PATTERN_RECALL_DIAG
        pattern_recall_diag_phase(PATTERN_DIAG_PHASE_RESOURCE_APPLY);
#endif
        if (!audio_prepared_apply_resource(entity, &target->resource)) goto invalid;
    }
#if BRICK_PATTERN_RECALL_DIAG
    pattern_recall_diag_phase(PATTERN_DIAG_PHASE_GLOBAL_APPLY);
#endif
    for (uint8_t index = 0U; index < PARAM_GLOBAL_CONTROL_VALUE_COUNT; ++index)
    {
        param_id_t id;
        float value;
        if (!param_global_audio_command_state_get_at(
                &state->global,index,&id,&value)
                || !audio_prepared_apply_float(0U,id,value,
                    CONTROL_AUDIO_PARAM_KIND_BASE_GLOBAL)) goto invalid;
    }
    if (!brick6_audio_runtime_set_input_owner(0U,state->input_owner[0U])
            || !brick6_audio_runtime_set_input_owner(1U,state->input_owner[1U])
            || !audio_transport_runtime_set_tempo(state->tempo_milli_bpm)
            || !audio_transport_runtime_set_step_q16(state->step_q16)) goto invalid;
    metronome_runtime_set_level_u7(state->metronome_level);
#if BRICK_PATTERN_RECALL_DIAG
    pattern_recall_diag_phase(PATTERN_DIAG_PHASE_TEMP_CLEAR);
#endif
    for (uint8_t entity = 0U;
         entity < PREPARED_AUDIO_TEMP_OWNER_COUNT; ++entity)
        for (uint16_t word = 0U; word < PREPARED_AUDIO_PARAM_MASK_WORDS; ++word)
        {
            uint32_t pending = state->temp_clear_mask[entity][word];
            while (pending != 0U)
            {
                const uint8_t bit = (uint8_t)__builtin_ctz(pending);
                const param_id_t id = (param_id_t)(word * 32U + bit);
                if (id >= PARAM_COUNT) goto invalid;
                uint32_t zero = 0U;
                if (!live_parameter_audio_runtime_apply_param(entity,id,zero,
                        CONTROL_AUDIO_PARAM_KIND_CLEAR_TEMP_TRACK)) goto invalid;
                pending &= pending - 1U;
            }
        }
    g_audio_state_rebind_deferred = 0U;
#if BRICK_PATTERN_RECALL_DIAG
    pattern_recall_diag_phase(PATTERN_DIAG_PHASE_REBIND);
#endif
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
        if ((g_audio_state_rebind_mask & (uint16_t)(1U << entity)) != 0U)
            if (!audio_note_engine_adapter_initialize_held_outputs(entity))
                return AUDIO_COMMAND_APPLY_REBIND;
    g_audio_state_rebind_mask = 0U;
    brick6_fm_runtime_finalize_pending();
    audio_mod_matrix_finalize_dirty();
#if BRICK_PATTERN_RECALL_DIAG
    pattern_recall_diag_phase(PATTERN_DIAG_PHASE_AUDIO_COMMIT_DONE);
#endif
    return AUDIO_COMMAND_APPLY_OK;
invalid:
    g_audio_state_rebind_deferred = 0U;
    return AUDIO_COMMAND_APPLY_INVALID;
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
            return (CONTROL_AUDIO_COMMAND_KIND(command)
                    == CONTROL_AUDIO_STATE_PATCH)
                ? audio_command_apply_patch_state_commit(command)
                : audio_command_apply_prepared_state_commit(command);
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
    memset(g_audio_seq_output, 0, sizeof(g_audio_seq_output));
    g_audio_seq_next_output_handle = 0U;
    g_audio_seq_age = 0U;
    g_audio_seq_track_mask = 0U;
}

static void audio_command_executor_close_step_outputs(uint8_t track)
{
    audio_seq_output_t *const outputs = g_audio_seq_output[track];
    for (uint8_t i = 0U; i < AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY; ++i)
    {
        if ((outputs[i].active == 0U)
                || ((outputs[i].owner_tag & SEQ_ENGINE_NOTE_LIVE) != 0U)) continue;
        if (audio_note_engine_adapter_apply_output(track, outputs[i].note, 0U,
                0U, outputs[i].id) == 0U)
            Error_Handler();
        outputs[i] = (audio_seq_output_t){0};
    }
}

static void audio_command_executor_close_outputs_from(uint8_t track,
                                                       uint8_t first)
{
    audio_seq_output_t *const outputs = g_audio_seq_output[track];
    for (uint8_t i = first; i < AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY; ++i)
    {
        if (outputs[i].active == 0U) continue;
        if (audio_note_engine_adapter_apply_output(track, outputs[i].note, 0U,
                0U, outputs[i].id) == 0U)
            Error_Handler();
        outputs[i] = (audio_seq_output_t){0};
    }
}

void audio_command_executor_seq_begin_block(uint16_t track_mask)
{
    const uint16_t changed = g_audio_seq_track_mask ^ track_mask;
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
    {
        const uint16_t bit = (uint16_t)(1U << track);
        if ((changed & bit) == 0U) continue;
        if ((track_mask & bit) == 0U)
            audio_command_executor_close_step_outputs(track);
    }
    g_audio_seq_track_mask = track_mask;
}

static uint8_t audio_command_executor_apply_seq_event(
    uint8_t terminal_kind,const seq_terminal_event_t *event)
{
    if(event==0)return 0U;
    if ((terminal_kind == SEQ_ENGINE_EVENT_PARAM)
            || (terminal_kind == SEQ_ENGINE_EVENT_TRANSITION_PARAM))
    {
        if((event->param.track>=SEQ_LANE_CAPACITY)
                ||(event->param.param_id>=PARAM_COUNT))return 0U;
        const uint8_t kind=(event->param.semantic==SEQ_ENGINE_PARAM_TEMP)
            ?CONTROL_AUDIO_PARAM_KIND_TEMP_TRACK
            :((event->param.semantic==SEQ_ENGINE_PARAM_CLEAR_TEMP)
                ?CONTROL_AUDIO_PARAM_KIND_CLEAR_TEMP_TRACK
                :CONTROL_AUDIO_PARAM_KIND_BASE_TRACK);
        return live_parameter_audio_runtime_apply_param(event->param.track,
            event->param.param_id,event->param.value32,kind);
    }
    if(event->note.track>=SEQ_LANE_CAPACITY)return 0U;
    audio_seq_output_t *const outputs = g_audio_seq_output[event->note.track];
    if(event->note.logical_slot>=AUDIO_NOTE_ENGINE_OUTPUT_CAPACITY)return 0U;
    const uint8_t target=event->note.logical_slot;
    if (terminal_kind == SEQ_ENGINE_EVENT_NOTE_OFF)
    {
        if(outputs[target].active==0U)return 1U;
        /* Release is identity-qualified.  A later owner may already have
         * replaced this occurrence when several logical transitions collapse
         * onto one AUDIO boundary; an old OFF must never close that owner. */
        if(outputs[target].occurrence_id!=event->note.occurrence_id
                ||outputs[target].owner_tag!=event->note.reserved)return 1U;
        const uint8_t ok=audio_note_engine_adapter_apply_output(event->note.track,
            outputs[target].note,0U,0U,outputs[target].id);
        outputs[target]=(audio_seq_output_t){0};return ok;
    }
    if(terminal_kind!=SEQ_ENGINE_EVENT_NOTE_ON)return 1U;

    /* The AUDIO output table is the single authority for physical ownership.
     * NOTE_ON means "install this occurrence in this logical output slot".
     * Replacement is committed here as one ordered AUDIO transition, so a
     * skipped/filtered earlier OFF can never leave an old physical owner in
     * conflict with the new SEQ reservation. */
    if(outputs[target].active!=0U)
    {
        if(outputs[target].occurrence_id==event->note.occurrence_id
                &&outputs[target].owner_tag==event->note.reserved)return 1U;
        if(audio_note_engine_adapter_apply_output(event->note.track,
                outputs[target].note,0U,0U,outputs[target].id)==0U)
            return 0U;
        outputs[target]=(audio_seq_output_t){0};
    }

    /* The occurrence namespace uses the high bits.  Truncating it aliases
     * KEY, MIDI, STEP and FX lifetimes with equal counters. */
    const uint32_t output_id = audio_seq_allocate_output_handle();
    if (output_id == 0U)
        return 0U;
    if (audio_note_engine_adapter_apply_output(event->note.track,event->note.note,
            event->note.velocity, 1U, output_id) == 0U)
        return 0U;
    outputs[target] = (audio_seq_output_t){
        .id=output_id,.occurrence_id=event->note.occurrence_id,
        .age=++g_audio_seq_age,.note=event->note.note,.active=1U,
        .owner_tag=event->note.reserved};
    return 1U;
}

uint16_t audio_command_executor_apply_seq_due(uint64_t sample_time)
{
    uint16_t applied = 0U;
    uint8_t terminal_kind;
    seq_terminal_event_t event;
    while (seq_engine_audio_pop_due(sample_time,&terminal_kind,&event) != 0U)
    {
        if (audio_command_executor_apply_seq_event(terminal_kind,&event) == 0U)
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
        if ((opcode == CONTROL_AUDIO_COMMAND_TRANSPORT)
                && (CONTROL_AUDIO_COMMAND_KIND(&command)
                    == CONTROL_AUDIO_TRANSPORT_STOP))
            seq_engine_audio_force_stop(command.effective_sample_time,1U);
        else if ((opcode == CONTROL_AUDIO_COMMAND_PANIC)
                && (CONTROL_AUDIO_COMMAND_KIND(&command)
                    == CONTROL_AUDIO_PANIC_GLOBAL))
            seq_engine_audio_force_stop(command.effective_sample_time,0U);
        const audio_command_apply_result_t result =
            audio_command_apply(&command);
        if (result != AUDIO_COMMAND_APPLY_OK)
            AUDIO_COMMAND_FATAL(&command, result);
        if ((opcode == CONTROL_AUDIO_COMMAND_TRANSPORT)
                && (CONTROL_AUDIO_COMMAND_KIND(&command)
                    == CONTROL_AUDIO_TRANSPORT_STOP))
        {
            memset(g_audio_seq_output, 0, sizeof(g_audio_seq_output));
            g_audio_seq_track_mask = 0U;
        }
        (void)control_audio_fifo_audio_pop();
        ++applied;
    }
    brick6_fm_runtime_finalize_pending();
    audio_mod_matrix_finalize_dirty();
    return applied;
}
