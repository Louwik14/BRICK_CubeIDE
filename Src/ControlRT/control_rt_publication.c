#include "ControlRT/control_rt_publication.h"

#include <string.h>
#include <math.h>

#include "IPC/control_audio_fifo_control.h"
#include "IPC/control_audio_timing.h"
#include "IPC/live_clock_control.h"
#include "Platform/memory_layout.h"
#include "Seq/seq_note_trace.h"
#include "Track/track_runtime.h"
#include "Track/entity_types.h"
#include "Param/param_ids.h"
#include "Param/param_spec.h"
#include "IPC/live_parameter_event.h"
#include "IPC/audio_recorder_capture_contract.h"
#include "IPC/audio_rec_bus_contract.h"
#include "IPC/audio_wave_table_projection.h"
#include "IPC/audio_state_snapshot.h"
#include "ControlRT/audio_state_snapshot_control.h"
#include "IPC/fm_dsp_projection.h"
#include "IPC/synth_waveform_contract.h"
#include "Mod/mod_matrix.h"
#include "Param/engine_model_catalog.h"
#include "Audio/mixer.h"
#include "Sampler/multi_sample_config.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_config.h"
#include "main.h"

static uint8_t control_rt_program_is_valid(uint8_t entity, uint32_t value)
{
    if (entity >= BRICK_ENTITY_CAPACITY) return 0U;
    const control_audio_program_descriptor_t d =
        control_audio_program_unpack(value);
    if ((d.engine >= (uint8_t)TRACK_RUNTIME_ENGINE_COUNT)
            || (d.family > (uint8_t)TRACK_RUNTIME_FAMILY_OTHER)
            || (d.type >= (uint8_t)TRACK_RUNTIME_TYPE_COUNT)
            || ((d.flags & (uint8_t)~CONTROL_AUDIO_PROGRAM_FLAG_MASK) != 0U)
            || ((d.flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER) != 0U
                && (d.flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_CHILD) != 0U))
        return 0U;
    if ((d.flags & (CONTROL_AUDIO_PROGRAM_FLAG_CAN_FILTER
                    | CONTROL_AUDIO_PROGRAM_FLAG_CAN_SYNTH
                    | CONTROL_AUDIO_PROGRAM_FLAG_CAN_PLAY))
            != track_runtime_compute_flags((track_runtime_family_t)d.family,
                (track_runtime_type_t)d.type))
        return 0U;
    if (((d.flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER) != 0U)
            && (entity != BRICK_ENTITY_GROUP_MASTER_ID))
        return 0U;
    if (((d.flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_CHILD) != 0U)
            && (entity < BRICK_ENTITY_FIRST_GROUP_CHILD_ID))
        return 0U;
    if ((d.family != TRACK_RUNTIME_FAMILY_SYNTH)
            && ((d.flags & CONTROL_AUDIO_PROGRAM_VOICE_MASK) != 0U))
        return 0U;
    if ((d.flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER) != 0U)
        return (uint8_t)((d.family == TRACK_RUNTIME_FAMILY_SAMPLER)
            && (d.type == TRACK_RUNTIME_TYPE_GROUP)
            && (d.engine == TRACK_RUNTIME_ENGINE_NONE));
    if (d.type == TRACK_RUNTIME_TYPE_GROUP) return 0U;
    if (d.family == TRACK_RUNTIME_FAMILY_OFF)
        return (uint8_t)((d.type == TRACK_RUNTIME_TYPE_NONE)
            && (d.engine == TRACK_RUNTIME_ENGINE_NONE));
    if (d.family == TRACK_RUNTIME_FAMILY_MIDI)
        return (uint8_t)((d.type == TRACK_RUNTIME_TYPE_MIDI)
            && (d.engine == TRACK_RUNTIME_ENGINE_NONE));
    return (uint8_t)(d.engine == (uint8_t)track_runtime_choose_engine(
        (track_runtime_family_t)d.family, (track_runtime_type_t)d.type));
}

static uint8_t control_rt_param_is_valid(const control_audio_command_t *command)
{
    const uint8_t scope = CONTROL_AUDIO_COMMAND_KIND(command);
    if (command->id < PARAM_COUNT)
    {
        const float value = live_parameter_event_decode_float(
            (int32_t)command->value);
        if (param_spec_value_is_valid((param_id_t)command->id, value) == 0U)
            return 0U;
        if (scope == LIVE_PARAMETER_EVENT_SCOPE_GLOBAL)
            return (uint8_t)(track_runtime_get_effective_param_status(
                0U, (param_id_t)command->id)
                == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED);
        if ((scope != LIVE_PARAMETER_EVENT_SCOPE_TRACK)
                && (scope != LIVE_PARAMETER_AUDIO_SCOPE_RUNTIME_TEMP))
            return 0U;
        return (uint8_t)((command->entity < BRICK_ENTITY_CAPACITY)
            && (track_runtime_get_effective_param_status(command->entity,
                (param_id_t)command->id) == TRACK_RUNTIME_PARAM_ALLOWED));
    }
    if (command->id == CONTROL_AUDIO_CONFIG_POLY_VOICES)
    {
        const float voices = live_parameter_event_decode_float(
            (int32_t)command->value);
        return (uint8_t)((command->entity < BRICK_ENTITY_CAPACITY)
            && (scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK)
            && isfinite(voices) && (voices >= 1.0f)
            && (voices <= 8.0f) && (voices == floorf(voices))
            && track_runtime_validate_polyphony_budget(
                command->entity, (uint8_t)voices));
    }
    if (command->id == CONTROL_AUDIO_PARAM_CLEAR_RUNTIME_TEMP)
    {
        const float endpoint = live_parameter_event_decode_float(
            (int32_t)command->value);
        return (uint8_t)((command->entity < BRICK_ENTITY_CAPACITY)
            && (scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK)
            && isfinite(endpoint) && (endpoint >= 0.0f)
            && (endpoint < (float)PARAM_COUNT)
            && (endpoint == floorf(endpoint)));
    }
    const uint16_t fm_base_words =
        (uint16_t)((sizeof(track_tone_fm_base_voice_t) + 3U) / 4U);
    if ((command->id >= CONTROL_AUDIO_FM_BASE_WORD_FIRST)
            && (command->id < CONTROL_AUDIO_FM_BASE_WORD_FIRST + fm_base_words))
        return (uint8_t)((scope == 0U)
            && (command->entity < BRICK_ENTITY_CAPACITY));
    if (command->id == CONTROL_AUDIO_PARAM_PREVIEW_GAIN)
    {
        const float gain = live_parameter_event_decode_float(
            (int32_t)command->value);
        return (uint8_t)((scope == 0U) && isfinite(gain));
    }
    if (command->id == CONTROL_AUDIO_PARAM_PREVIEW_ACTIVE)
        return (uint8_t)((scope == 0U) && (command->entity <= 1U));
    if (command->id == CONTROL_AUDIO_PARAM_REC_BUS)
    {
        const uint32_t allowed = 0xFFFFU | (3UL << 16)
            | ((uint32_t)(AUDIO_REC_BUS_SOURCE_LINE_DIRECT
                | AUDIO_REC_BUS_SOURCE_MIC_LOGICAL
                | AUDIO_REC_BUS_CAPTURE_ENABLED
                | AUDIO_REC_BUS_SOURCE_USB_DIRECT) << 18);
        return (uint8_t)((scope == 0U)
            && ((command->value & ~allowed) == 0U)
            && (((command->value >> 16) & 3U) <= AUDIO_REC_BUS_ARM_TRIG));
    }
    if (command->id == CONTROL_AUDIO_PARAM_INPUT_OWNER)
        return (uint8_t)((scope == 0U)
            && (command->entity < ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT)
            && (((uint8_t)command->value < BRICK_ENTITY_CAPACITY)
                || ((uint8_t)command->value == BRICK_ENTITY_INVALID_ID)));
    if (command->id == CONTROL_AUDIO_PARAM_LOOPER_ROUTE)
        return (uint8_t)((scope == 0U)
            && (command->entity < BRICK_ENTITY_CAPACITY)
            && ((command->value & ~0xFFFFUL) == 0U));
    if ((command->id == CONTROL_AUDIO_PARAM_WAVETABLE_GEN)
            || (command->id == CONTROL_AUDIO_PARAM_WAVETABLE_SET))
        return (uint8_t)((scope == 0U)
            && (command->entity < (BRICK_ENTITY_CAPACITY
                * AUDIO_WAVETABLE_OSC_COUNT)));
    if (command->id == CONTROL_AUDIO_PARAM_MIDI_CONFIG)
        return (uint8_t)((command->entity < BRICK_ENTITY_CAPACITY)
            && (scope == 0U)
            && ((command->value & 0xFFU) >= 1U)
            && ((command->value & 0xFFU) <= 16U)
            && (((command->value >> 8) & 0xFFU)
                <= TRACK_RUNTIME_MIDI_SOURCE_ALL)
            && ((command->value & 0xFFFF0000UL) == 0U));
    if (command->id == CONTROL_AUDIO_PARAM_AUDIO_WAVEFORM_REQUEST)
        return (uint8_t)((scope == 0U)
            && (command->entity < BRICK_ENTITY_CAPACITY)
            && ((command->value & ~3UL) == 0U));
    if (command->id == CONTROL_AUDIO_PARAM_SYNTH_WAVEFORM_REQUEST)
        return (uint8_t)((scope == 0U)
            && (command->entity < BRICK_ENTITY_CAPACITY)
            && ((command->value & ~0x303UL) == 0U)
            && ((command->value & 0xFFU) <= SYNTH_WAVEFORM_ENGINE_PRISM));
    if ((command->id == CONTROL_AUDIO_PARAM_TRANSPORT_TEMPO)
            || (command->id == CONTROL_AUDIO_PARAM_TRANSPORT_STEP_Q16))
        return (uint8_t)((scope == 0U) && (command->value != 0U));
    if (command->id == CONTROL_AUDIO_PARAM_METRONOME_LEVEL)
        return (uint8_t)((scope == 0U) && (command->value <= 127U));
    if (command->id == CONTROL_AUDIO_SAMPLER_ASSET)
        return (uint8_t)((scope == 0U)
            && (command->entity < BRICK_ENTITY_CAPACITY));
    if (command->id == CONTROL_AUDIO_LOOPER_PLAY_AUTO)
        return (uint8_t)((scope == 0U)
            && (command->entity < BRICK_ENTITY_CAPACITY)
            && (command->value <= 1U));
    if ((command->id >= CONTROL_AUDIO_MOD_ROUTE_SOURCE)
            && (command->id <= CONTROL_AUDIO_FX_SPATIAL_MODE))
    {
        const float value = live_parameter_event_decode_float(
            (int32_t)command->value);
        if ((command->entity >= SEQ_TRACK_COUNT) || !isfinite(value)
                || (scope < LIVE_PARAMETER_AUDIO_SCOPE_MATRIX_SLOT_BASE)
                || (scope > LIVE_PARAMETER_AUDIO_SCOPE_MATRIX_SLOT_LAST))
            return 0U;
        const uint8_t index = (uint8_t)(
            scope - LIVE_PARAMETER_AUDIO_SCOPE_MATRIX_SLOT_BASE);
        if ((command->id == CONTROL_AUDIO_MOD_ROUTE_SOURCE)
                || (command->id == CONTROL_AUDIO_MOD_MULTI_SOURCE)
                || (command->id == CONTROL_AUDIO_MOD_SLEW_SOURCE))
        {
            if ((value < 0.0f) || (value >= (float)MOD_MATRIX_SOURCE_COUNT)
                    || (value != floorf(value))) return 0U;
            if ((command->id == CONTROL_AUDIO_MOD_MULTI_SOURCE)
                    && (index >= 4U)) return 0U;
            if ((command->id == CONTROL_AUDIO_MOD_SLEW_SOURCE)
                    && (index >= 2U)) return 0U;
            return 1U;
        }
        if (command->id == CONTROL_AUDIO_MOD_ROUTE_DESTINATION)
            return (uint8_t)((value >= 0.0f) && (value <= 65535.0f)
                && (value == floorf(value)));
        if (command->id == CONTROL_AUDIO_MOD_ROUTE_ENABLED)
            return (uint8_t)((value == 0.0f) || (value == 1.0f));
        if (command->id == CONTROL_AUDIO_MOD_SLEW_AMOUNT)
            return (uint8_t)((index < 2U) && (value >= 0.0f)
                && (value <= 1.0f));
        if (command->id == CONTROL_AUDIO_MOD_ROUTE_DEPTH)
            return (uint8_t)((value >= -127.0f) && (value <= 127.0f));
        if (value != floorf(value)) return 0U;
        if (command->id == CONTROL_AUDIO_FX_FILTER_POSITION)
            return (uint8_t)((value >= 0.0f)
                && (value < (float)AUDIO_FX_FILTER_POS_COUNT));
        if (command->id == CONTROL_AUDIO_FX_ORDER)
            return (uint8_t)((value >= 0.0f)
                && (value < (float)AUDIO_FX_ORDER_COUNT));
        return (uint8_t)((index < AUDIO_FX_SLOT_COUNT)
            && (value >= 0.0f) && (value < 4.0f));
    }
    if ((command->id >= CONTROL_AUDIO_PARAM_MIX_INSERT_FIRST)
            && (command->id <= CONTROL_AUDIO_PARAM_MIX_INSERT_LAST))
        return (uint8_t)((scope == 0U)
            && (command->entity < MIXER_MAX_TRACKS));
    if (command->id == CONTROL_AUDIO_PARAM_MIX_ROUTE)
        return (uint8_t)((scope == 0U)
            && (command->entity < MIXER_MAX_TRACKS));
    if (command->id == CONTROL_AUDIO_PARAM_MULTI_RESOURCE_STOP)
        return (uint8_t)((scope == 0U)
            && (command->entity < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS));
    if (command->id == CONTROL_AUDIO_PARAM_RAM_RESOURCE_STOP)
        return (uint8_t)((scope == 0U)
            && (command->entity < SAMPLER_RAM_POOL_MAX_SLOTS));
    if (command->id == CONTROL_AUDIO_PARAM_WAVE_RESOURCE_STOP)
        return (uint8_t)((scope == 0U)
            && (command->entity < WAVETABLE_POOL_MAX_SLOTS));
    return 0U;
}

static uint8_t control_rt_command_is_valid(
    const control_audio_command_t *command)
{
    if (command == NULL) return 0U;
    const uint8_t opcode = CONTROL_AUDIO_COMMAND_OPCODE(command);
    const uint8_t kind = CONTROL_AUDIO_COMMAND_KIND(command);
    switch (opcode)
    {
        case CONTROL_AUDIO_COMMAND_PROGRAM:
            return (uint8_t)((kind == 0U)
                && control_rt_program_is_valid(command->entity, command->value));
        case CONTROL_AUDIO_COMMAND_PARAM:
            return control_rt_param_is_valid(command);
        case CONTROL_AUDIO_COMMAND_NOTE:
            return (uint8_t)((command->entity < BRICK_ENTITY_CAPACITY)
                && (kind <= CONTROL_AUDIO_NOTE_ON) && (command->value != 0U)
                && ((command->id & 0xFFU) <= 127U)
                && ((command->id >> 8) <= 127U));
        case CONTROL_AUDIO_COMMAND_TRANSPORT:
            return kind <= CONTROL_AUDIO_TRANSPORT_LOCATE;
        case CONTROL_AUDIO_COMMAND_RECORD:
            if (kind > CONTROL_AUDIO_RECORD_START) return 0U;
            if ((command->id & AUDIO_RECORDER_LOOPER_RECORD_ID_FLAG) != 0U)
                return (uint8_t)((command->entity < BRICK_ENTITY_CAPACITY)
                    && ((kind == CONTROL_AUDIO_RECORD_STOP)
                        || (command->value != 0U)));
            return (uint8_t)((command->entity != AUDIO_RECORDER_CLIENT_NONE)
                && (command->id != 0U)
                && ((kind == CONTROL_AUDIO_RECORD_STOP)
                    || (command->value != 0U)));
        case CONTROL_AUDIO_COMMAND_PANIC:
            return (uint8_t)((kind <= CONTROL_AUDIO_PANIC_ENTITY)
                && ((kind == CONTROL_AUDIO_PANIC_GLOBAL)
                    || (command->entity < BRICK_ENTITY_CAPACITY)));
        case CONTROL_AUDIO_COMMAND_AUDIO_STATE_COMMIT:
            return (uint8_t)((kind == 0U) && (command->value != 0U));
        default:
            return 0U;
    }
}

typedef struct
{
    control_audio_command_t command[CONTROL_AUDIO_FIFO_CONTRACT_BURST];
    control_audio_command_t ordered[CONTROL_AUDIO_FIFO_CONTRACT_BURST];
    uint16_t count;
    uint16_t limit;
    uint16_t frames;
    uint64_t first_sample;
    uint8_t active;
} control_audio_horizon_t;

CONTROL_STATE_SDRAM static control_audio_horizon_t g_control_audio_horizon;
static uint8_t g_audio_state_snapshot_depth;
static volatile uint32_t g_control_audio_horizon_capacity_failure_count;
static uint64_t g_control_rt_first_unpublished_sample;

static uint8_t audio_state_snapshot_command_is_transient(
    const control_audio_command_t *command)
{
    if ((command == NULL)
            || (CONTROL_AUDIO_COMMAND_OPCODE(command)
                != CONTROL_AUDIO_COMMAND_PARAM)) return 0U;
    return (uint8_t)((command->id == CONTROL_AUDIO_PARAM_CLEAR_RUNTIME_TEMP)
        || (command->id == CONTROL_AUDIO_PARAM_MULTI_RESOURCE_STOP)
        || (command->id == CONTROL_AUDIO_PARAM_RAM_RESOURCE_STOP)
        || (command->id == CONTROL_AUDIO_PARAM_WAVE_RESOURCE_STOP)
        || (command->id == CONTROL_AUDIO_PARAM_AUDIO_WAVEFORM_REQUEST)
        || (command->id == CONTROL_AUDIO_PARAM_SYNTH_WAVEFORM_REQUEST));
}

static uint8_t audio_state_snapshot_same_key(
    const control_audio_command_t *left,
    const control_audio_command_t *right)
{
    const uint8_t opcode = CONTROL_AUDIO_COMMAND_OPCODE(left);
    if (opcode != CONTROL_AUDIO_COMMAND_OPCODE(right)) return 0U;
    if (opcode == CONTROL_AUDIO_COMMAND_PROGRAM)
        return (uint8_t)(left->entity == right->entity);
    return (uint8_t)((opcode == CONTROL_AUDIO_COMMAND_PARAM)
        && (left->entity == right->entity) && (left->id == right->id)
        && (CONTROL_AUDIO_COMMAND_KIND(left)
            == CONTROL_AUDIO_COMMAND_KIND(right)));
}

static uint8_t audio_state_snapshot_command_is_current(
    const control_audio_command_t *command)
{
    if (control_rt_command_is_valid(command) == 0U) return 0U;
    if (CONTROL_AUDIO_COMMAND_OPCODE(command) != CONTROL_AUDIO_COMMAND_PARAM)
        return 1U;
    track_runtime_descriptor_t descriptor;
    const uint16_t fm_words =
        (uint16_t)((sizeof(track_tone_fm_base_voice_t) + 3U) / 4U);
    if ((command->id >= CONTROL_AUDIO_FM_BASE_WORD_FIRST)
            && (command->id < CONTROL_AUDIO_FM_BASE_WORD_FIRST + fm_words))
        return (uint8_t)(track_runtime_get_descriptor(
            command->entity, &descriptor)
            && (descriptor.engine == TRACK_RUNTIME_ENGINE_FM));
    if (command->id == CONTROL_AUDIO_SAMPLER_ASSET)
        return (uint8_t)(track_runtime_get_descriptor(
            command->entity, &descriptor)
            && ((descriptor.type == TRACK_RUNTIME_TYPE_STREAM)
                || (descriptor.type == TRACK_RUNTIME_TYPE_RAM)
                || (descriptor.type == TRACK_RUNTIME_TYPE_MULTI)));
    if (command->id == CONTROL_AUDIO_LOOPER_PLAY_AUTO)
        return (uint8_t)(track_runtime_get_descriptor(
            command->entity, &descriptor)
            && (descriptor.type == TRACK_RUNTIME_TYPE_LOOPER));
    return 1U;
}

void audio_state_snapshot_control_init(void)
{
    memset(&g_audio_prepared_state, 0, sizeof(g_audio_prepared_state));
    g_audio_state_snapshot_depth = 0U;
}

uint8_t audio_state_snapshot_control_active(void)
{
    return (g_audio_state_snapshot_depth != 0U) ? 1U : 0U;
}

uint8_t audio_state_snapshot_control_begin(void)
{
    if (g_audio_state_snapshot_depth == UINT8_MAX) return 0U;
    if (g_audio_state_snapshot_depth == 0U)
        g_audio_prepared_state.valid_magic = 0U;
    ++g_audio_state_snapshot_depth;
    return 1U;
}

void audio_state_snapshot_control_abort(void)
{
    g_audio_state_snapshot_depth = 0U;
}

uint8_t audio_state_snapshot_control_batch_is_projectable(
    const control_audio_command_t *commands, uint16_t count)
{
    if ((commands == NULL) || (count == 0U)) return 0U;
    for (uint16_t i = 0U; i < count; ++i)
    {
        const uint8_t opcode = CONTROL_AUDIO_COMMAND_OPCODE(&commands[i]);
        if (((opcode != CONTROL_AUDIO_COMMAND_PROGRAM)
                && (opcode != CONTROL_AUDIO_COMMAND_PARAM))
                || (audio_state_snapshot_command_is_transient(
                    &commands[i]) != 0U)) return 0U;
    }
    return 1U;
}

uint8_t audio_state_snapshot_control_absorb(
    const control_audio_command_t *commands, uint16_t count)
{
    if ((commands == NULL) || (count == 0U)) return 0U;
    for (uint16_t input = 0U; input < count; ++input)
    {
        control_audio_command_t command = commands[input];
        const uint8_t opcode = CONTROL_AUDIO_COMMAND_OPCODE(&command);
        if (((opcode != CONTROL_AUDIO_COMMAND_PROGRAM)
                && (opcode != CONTROL_AUDIO_COMMAND_PARAM))
                || (audio_state_snapshot_command_is_transient(&command) != 0U))
            continue;
        command.effective_sample_time = 0U;
        uint16_t index = 0U;
        while ((index < g_audio_prepared_state.count)
                && (audio_state_snapshot_same_key(
                    &g_audio_prepared_state.command[index], &command) == 0U))
            ++index;
        if (index < g_audio_prepared_state.count)
            g_audio_prepared_state.command[index] = command;
        else
        {
            if (g_audio_prepared_state.count
                    >= AUDIO_STATE_SNAPSHOT_COMMAND_CAPACITY) return 0U;
            g_audio_prepared_state.command[g_audio_prepared_state.count++] = command;
        }
        if (opcode != CONTROL_AUDIO_COMMAND_PROGRAM) continue;
        for (uint16_t old = 0U; old < g_audio_prepared_state.count; )
        {
            control_audio_command_t *const candidate =
                &g_audio_prepared_state.command[old];
            if ((CONTROL_AUDIO_COMMAND_OPCODE(candidate)
                    == CONTROL_AUDIO_COMMAND_PARAM)
                    && (candidate->entity == command.entity)
                    && (CONTROL_AUDIO_COMMAND_KIND(candidate)
                        != LIVE_PARAMETER_EVENT_SCOPE_GLOBAL)
                    && (audio_state_snapshot_command_is_current(
                        candidate) == 0U))
            {
                g_audio_prepared_state.command[old] =
                    g_audio_prepared_state.command[--g_audio_prepared_state.count];
                continue;
            }
            ++old;
        }
    }
    return 1U;
}

uint8_t audio_state_snapshot_control_commit(void)
{
    if (g_audio_state_snapshot_depth == 0U) return 0U;
    --g_audio_state_snapshot_depth;
    if (g_audio_state_snapshot_depth != 0U) return 1U;
    uint16_t count = 0U;
    for (uint16_t i = 0U; i < g_audio_prepared_state.count; ++i)
        if (audio_state_snapshot_command_is_current(
                &g_audio_prepared_state.command[i]) != 0U)
            g_audio_prepared_state.command[count++] =
                g_audio_prepared_state.command[i];
    g_audio_prepared_state.count = count;
    uint16_t program_count = 0U;
    for (uint16_t i = 0U; i < count; ++i)
    {
        if (CONTROL_AUDIO_COMMAND_OPCODE(&g_audio_prepared_state.command[i])
                != CONTROL_AUDIO_COMMAND_PROGRAM) continue;
        const control_audio_command_t program =
            g_audio_prepared_state.command[i];
        memmove(&g_audio_prepared_state.command[program_count + 1U],
                &g_audio_prepared_state.command[program_count],
                (size_t)(i - program_count)
                    * sizeof(g_audio_prepared_state.command[0]));
        g_audio_prepared_state.command[program_count++] = program;
    }
    uint32_t generation = 0U;
    if ((count == 0U) || (audio_state_snapshot_publish(
            g_audio_prepared_state.command, count, &generation) == 0U))
        return 0U;
    control_audio_command_t commit = {
        .value = generation,
        .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(
            CONTROL_AUDIO_COMMAND_AUDIO_STATE_COMMIT, 0U)
    };
    if (control_rt_publish_batch_now(&commit, 1U) == 0U) return 0U;
    const uint32_t commit_head = control_audio_fifo_control_head_snapshot();
    while (control_audio_fifo_control_head_consumed(commit_head) == 0U)
    {
        /* SAI preempts CONTROL every 64 frames on H743; on H747 the AUDIO
         * core advances tail concurrently.  Crossing tail proves apply done. */
        __DMB();
    }
    return 1U;
}

void control_rt_publication_init(void)
{
    control_audio_fifo_control_init();
    audio_state_snapshot_control_init();
    g_control_audio_horizon.count = 0U;
    g_control_audio_horizon.limit = 0U;
    g_control_audio_horizon.frames = 0U;
    g_control_audio_horizon.first_sample = 0U;
    g_control_audio_horizon.active = 0U;
    g_control_audio_horizon_capacity_failure_count = 0U;
    g_control_rt_first_unpublished_sample = 0U;
}

uint8_t control_rt_publication_horizon_active(void)
{
    return g_control_audio_horizon.active;
}

uint8_t control_rt_now_sample(uint64_t *out_sample_time)
{
    return live_clock_read_audio_sample(out_sample_time) ? 1U : 0U;
}

uint64_t control_rt_first_unpublished_sample(uint64_t minimum_sample)
{
    return (g_control_rt_first_unpublished_sample > minimum_sample)
        ? g_control_rt_first_unpublished_sample : minimum_sample;
}

void control_rt_advance_first_unpublished_sample(uint64_t first_sample)
{
    if (first_sample > g_control_rt_first_unpublished_sample)
        g_control_rt_first_unpublished_sample = first_sample;
}

uint8_t control_rt_resolve_asap_sample(uint64_t minimum_sample,
                                       uint64_t *out_sample_time)
{
    uint64_t sample_time = 0U;
    if ((out_sample_time == NULL)
            || (control_rt_now_sample(&sample_time) == 0U))
        return 0U;
    if (sample_time < minimum_sample)
        sample_time = minimum_sample;
    *out_sample_time = control_rt_first_unpublished_sample(sample_time);
    return 1U;
}

uint8_t control_rt_capture_tick_to_sample(uint32_t capture_tick,
                                          uint64_t minimum_sample,
                                          uint64_t *out_sample_time)
{
    uint64_t sample_time = 0U;
    if (!live_clock_tim5_to_guarded_sample_time(capture_tick, &sample_time))
        return 0U;
    if (sample_time < minimum_sample)
        sample_time = minimum_sample;
    sample_time = control_rt_first_unpublished_sample(sample_time);
    if (out_sample_time != NULL)
        *out_sample_time = sample_time;
    return (out_sample_time != NULL) ? 1U : 0U;
}

uint8_t control_rt_publication_begin_horizon(uint64_t first_sample,
                                             uint16_t frames)
{
    if ((g_control_audio_horizon.active != 0U) || (frames == 0U)
            || (frames > CONTROL_AUDIO_MAX_PUBLICATION_HORIZON_FRAMES))
        return 0U;
    if (first_sample < g_control_rt_first_unpublished_sample)
        return 0U;
    const uint16_t free = control_audio_fifo_control_free();
    if (free < CONTROL_AUDIO_FIFO_CONTRACT_BURST)
    {
        ++g_control_audio_horizon_capacity_failure_count;
        return 0U;
    }
    g_control_audio_horizon.count = 0U;
    g_control_audio_horizon.limit = CONTROL_AUDIO_FIFO_CONTRACT_BURST;
    g_control_audio_horizon.frames = frames;
    g_control_audio_horizon.first_sample = first_sample;
    g_control_audio_horizon.active = 1U;
    seq_note_trace_horizon_begin(first_sample);
    return 1U;
}

void control_rt_publication_abort_horizon(void)
{
    seq_note_trace_horizon_abort(g_control_audio_horizon.first_sample,
        g_control_audio_horizon.first_sample + g_control_audio_horizon.frames);
    g_control_audio_horizon.count = 0U;
    g_control_audio_horizon.active = 0U;
}

uint16_t control_rt_publication_free(void)
{
    return (g_control_audio_horizon.active != 0U)
        ? (uint16_t)(g_control_audio_horizon.limit
            - g_control_audio_horizon.count)
        : control_audio_fifo_control_free();
}

uint32_t control_rt_publication_capacity_failure_count(void)
{
    return g_control_audio_horizon_capacity_failure_count;
}

static uint8_t control_rt_publication_stage(
    const control_audio_command_t *commands, uint16_t count)
{
    if ((commands == NULL) || (count == 0U)
            || ((uint32_t)g_control_audio_horizon.count + count
                > g_control_audio_horizon.limit))
        return 0U;
    const uint64_t end = g_control_audio_horizon.first_sample
        + g_control_audio_horizon.frames;
    for (uint16_t i = 0U; i < count; ++i)
        if ((commands[i].effective_sample_time
                < g_control_audio_horizon.first_sample)
                || (commands[i].effective_sample_time >= end))
            return 0U;
    memcpy(&g_control_audio_horizon.command[g_control_audio_horizon.count],
           commands, (size_t)count * sizeof(commands[0]));
    g_control_audio_horizon.count = (uint16_t)(
        g_control_audio_horizon.count + count);
    return 1U;
}

uint8_t control_rt_publication_commit_horizon(void)
{
    if (g_control_audio_horizon.active == 0U)
        return 0U;
    uint16_t ordered_count = 0U;
    for (uint16_t frame = 0U; frame < g_control_audio_horizon.frames; ++frame)
    {
        const uint64_t sample = g_control_audio_horizon.first_sample + frame;
        for (uint16_t i = 0U; i < g_control_audio_horizon.count; ++i)
            if (g_control_audio_horizon.command[i].effective_sample_time
                    == sample)
                g_control_audio_horizon.ordered[ordered_count++] =
                    g_control_audio_horizon.command[i];
    }
    const uint16_t count = g_control_audio_horizon.count;
    g_control_audio_horizon.active = 0U;
    g_control_audio_horizon.count = 0U;
    uint8_t accepted = 1U;
    if (count != 0U)
        accepted = (ordered_count == count)
            ? control_audio_fifo_publish_batch(
                g_control_audio_horizon.ordered, ordered_count)
            : 0U;
    if (accepted != 0U)
    {
        control_rt_advance_first_unpublished_sample(
            g_control_audio_horizon.first_sample
                + g_control_audio_horizon.frames);
        for (uint16_t i = 0U; i < ordered_count; ++i)
        {
            const control_audio_command_t *const command =
                &g_control_audio_horizon.ordered[i];
            if (CONTROL_AUDIO_COMMAND_OPCODE(command)
                    != CONTROL_AUDIO_COMMAND_NOTE)
                continue;
            uint8_t trace_track = 0U;
            uint8_t trace_step = 0U;
            if (seq_note_trace_output_is_watched(
                    command->value, &trace_track, &trace_step) == 0U)
                continue;
            seq_note_trace_record(
                (CONTROL_AUDIO_COMMAND_KIND(command) == CONTROL_AUDIO_NOTE_ON)
                    ? SEQ_NOTE_TRACE_FIFO_NOTE_ON
                    : SEQ_NOTE_TRACE_FIFO_NOTE_OFF,
                trace_track, trace_step, command->effective_sample_time,
                0U, command->value, command->id);
        }
        seq_note_trace_horizon_commit(g_control_audio_horizon.first_sample,
            g_control_audio_horizon.first_sample
                + g_control_audio_horizon.frames);
    }
    else
        seq_note_trace_horizon_abort(g_control_audio_horizon.first_sample,
            g_control_audio_horizon.first_sample
                + g_control_audio_horizon.frames);
    return accepted;
}

static uint8_t control_rt_publish(const control_audio_command_t *command)
{
    return control_rt_publish_batch_scheduled(command, 1U);
}

uint8_t control_rt_publish_batch_scheduled(
    const control_audio_command_t *commands, uint16_t count)
{
    if ((commands == NULL) || (count == 0U)) return 0U;
    for (uint16_t i = 0U; i < count; ++i)
        if (control_rt_command_is_valid(&commands[i]) == 0U) return 0U;
    if ((g_control_audio_horizon.active == 0U)
            && (commands != NULL) && (count != 0U)
            && (commands[0].effective_sample_time
                < g_control_rt_first_unpublished_sample))
        return 0U;
    uint8_t accepted = 0U;
    if (g_control_audio_horizon.active != 0U)
        accepted = (control_rt_publication_free() >= count)
            ? control_rt_publication_stage(commands, count) : 0U;
    else if ((audio_state_snapshot_control_active() != 0U)
            && (audio_state_snapshot_control_batch_is_projectable(
                commands, count) != 0U))
        accepted = audio_state_snapshot_control_absorb(commands, count);
    else
    {
        accepted = control_audio_fifo_publish_batch(commands, count);
        if ((accepted != 0U)
                && (audio_state_snapshot_control_absorb(
                    commands, count) == 0U)) Error_Handler();
    }
    if ((accepted != 0U) && (g_control_audio_horizon.active == 0U))
        control_rt_advance_first_unpublished_sample(
            commands[count - 1U].effective_sample_time);
    return accepted;
}

uint8_t control_rt_publish_batch_captured(control_audio_command_t *commands,
                                          uint16_t count,
                                          uint32_t capture_tick,
                                          uint64_t minimum_sample)
{
    uint64_t sample_time = 0U;
    if ((commands == NULL) || (count == 0U)
            || (count > CONTROL_AUDIO_FIFO_CONTRACT_BURST)
            || !control_rt_capture_tick_to_sample(
                capture_tick, minimum_sample, &sample_time))
        return 0U;
    for (uint16_t i = 0U; i < count; ++i)
        commands[i].effective_sample_time = sample_time;
    return control_rt_publish_batch_scheduled(commands, count);
}

uint8_t control_rt_publish_batch_now(control_audio_command_t *commands,
                                     uint16_t count)
{
    uint64_t sample_time = 0U;
    if ((commands == NULL) || (count == 0U)
            || (count > CONTROL_AUDIO_FIFO_CONTRACT_BURST)
            || !control_rt_resolve_asap_sample(0U, &sample_time))
        return 0U;
    for (uint16_t i = 0U; i < count; ++i)
        commands[i].effective_sample_time = sample_time;
    return control_rt_publish_batch_scheduled(commands, count);
}

uint8_t control_rt_publish_program(uint8_t entity, uint32_t descriptor,
                                   uint64_t sample_time)
{
    const control_audio_command_t c = { .effective_sample_time = sample_time,
        .value = descriptor, .entity = entity,
        .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(CONTROL_AUDIO_COMMAND_PROGRAM, 0U) };
    return control_rt_publish(&c);
}

uint8_t control_rt_publish_program_now(uint8_t entity, uint32_t descriptor)
{
    control_audio_command_t command = {
        .value = descriptor,
        .entity = entity,
        .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(
            CONTROL_AUDIO_COMMAND_PROGRAM, 0U)
    };
    return control_rt_publish_batch_now(&command, 1U);
}

uint8_t control_rt_publish_param(uint8_t entity, uint16_t param_id,
                                 uint32_t value, uint32_t target_detail,
                                 uint64_t sample_time)
{
    const control_audio_command_t c = { .effective_sample_time = sample_time,
        .value = value, .id = param_id, .entity = entity,
        .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(CONTROL_AUDIO_COMMAND_PARAM,
            target_detail & 0x1FU) };
    return control_rt_publish(&c);
}

uint8_t control_rt_publish_param_now(uint8_t entity, uint16_t param_id,
                                     uint32_t value, uint32_t target_detail)
{
    control_audio_command_t command = {
        .value = value,
        .id = param_id,
        .entity = entity,
        .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(
            CONTROL_AUDIO_COMMAND_PARAM, target_detail & 0x1FU)
    };
    return control_rt_publish_batch_now(&command, 1U);
}

uint8_t control_rt_publish_note(uint8_t entity, uint8_t kind,
                                uint32_t output_id, uint8_t note,
                                uint8_t velocity, uint64_t sample_time)
{
    const control_audio_command_t c = { .effective_sample_time = sample_time,
        .value = output_id, .id = (uint16_t)note | ((uint16_t)velocity << 8),
        .entity = entity,
        .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(CONTROL_AUDIO_COMMAND_NOTE,
            kind) };
    return control_rt_publish(&c);
}

uint8_t control_rt_publish_transport(uint8_t kind, uint32_t position,
                                     uint64_t sample_time)
{
    const control_audio_command_t c = { .effective_sample_time = sample_time,
        .value = position,
        .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(
            CONTROL_AUDIO_COMMAND_TRANSPORT, kind) };
    return control_rt_publish(&c);
}

uint8_t control_rt_publish_record(uint8_t kind, uint32_t session_id,
                                  uint32_t config, uint8_t client,
                                  uint64_t sample_time)
{
    const control_audio_command_t c = { .effective_sample_time = sample_time,
        .value = session_id, .id = (uint16_t)config, .entity = client,
        .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(CONTROL_AUDIO_COMMAND_RECORD,
            kind) };
    return control_rt_publish(&c);
}

uint8_t control_rt_publish_panic(uint8_t kind, uint8_t entity,
                                 uint64_t sample_time)
{
    const control_audio_command_t c = { .effective_sample_time = sample_time,
        .entity = entity,
        .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(CONTROL_AUDIO_COMMAND_PANIC,
            kind) };
    return control_rt_publish(&c);
}
