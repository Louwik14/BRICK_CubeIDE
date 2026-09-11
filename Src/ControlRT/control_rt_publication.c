#include "ControlRT/control_rt_publication.h"

#include <string.h>

#include "IPC/control_audio_fifo_control.h"
#include "IPC/control_audio_timing.h"
#include "IPC/live_clock_control.h"
#include "Platform/memory_layout.h"
#include "Seq/seq_note_trace.h"
#include "Track/track_runtime.h"
#include "Track/entity_types.h"
#include "Param/param_ids.h"
#include "IPC/audio_recorder_capture_contract.h"
#include "IPC/audio_rec_bus_contract.h"
#include "IPC/audio_wave_table_projection.h"
#include "IPC/audio_state_snapshot.h"
#include "ControlRT/audio_state_snapshot_control.h"
#include "IPC/fm_dsp_projection.h"
#include "Mod/mod_matrix.h"
#include "Param/engine_model_catalog.h"
#include "Sampler/multi_sample_config.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_config.h"
#include "main.h"

static uint8_t control_rt_program_is_structural(uint8_t entity, uint32_t value)
{
    if (entity >= BRICK_ENTITY_CAPACITY) return 0U;
    const control_audio_program_descriptor_t d =
        control_audio_program_unpack(value);
    return control_audio_program_descriptor_is_structural(&d,
        (uint8_t)TRACK_RUNTIME_ENGINE_COUNT,
        (uint8_t)TRACK_RUNTIME_FAMILY_OTHER,
        (uint8_t)TRACK_RUNTIME_TYPE_COUNT);
}

static uint8_t control_rt_param_is_structural(
    const control_audio_command_t *command)
{
    const uint8_t scope = CONTROL_AUDIO_COMMAND_KIND(command);
    if (command->id < PARAM_COUNT)
    {
        if (scope == CONTROL_AUDIO_PARAM_KIND_BASE_GLOBAL)
            return (uint8_t)(command->entity == 0U);
        return (uint8_t)((command->entity < BRICK_ENTITY_CAPACITY)
            && ((scope == CONTROL_AUDIO_PARAM_KIND_BASE_TRACK)
                || (scope == CONTROL_AUDIO_PARAM_KIND_TEMP_TRACK)
                || (scope == CONTROL_AUDIO_PARAM_KIND_CLEAR_TEMP_TRACK)));
    }
    if (command->id == CONTROL_AUDIO_CONFIG_POLY_VOICES)
        return (uint8_t)((command->entity < BRICK_ENTITY_CAPACITY)
            && (scope == CONTROL_AUDIO_PARAM_KIND_BASE_TRACK));
    const uint16_t fm_base_words =
        (uint16_t)((sizeof(track_tone_fm_base_voice_t) + 3U) / 4U);
    if ((command->id >= CONTROL_AUDIO_FM_BASE_WORD_FIRST)
            && (command->id < CONTROL_AUDIO_FM_BASE_WORD_FIRST + fm_base_words))
        return (uint8_t)((scope == 0U)
            && (command->entity < BRICK_ENTITY_CAPACITY));
    if (command->id == CONTROL_AUDIO_PARAM_PREVIEW_GAIN)
        return (uint8_t)(scope == 0U);
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
            && ((command->value & 0xFFFF0000UL) == 0U));
    if (command->id == CONTROL_AUDIO_PARAM_AUDIO_WAVEFORM_REQUEST)
        return (uint8_t)((scope == 0U)
            && (command->entity < BRICK_ENTITY_CAPACITY)
            && ((command->value & ~3UL) == 0U));
    if (command->id == CONTROL_AUDIO_PARAM_SYNTH_WAVEFORM_REQUEST)
        return (uint8_t)((scope == 0U)
            && (command->entity < BRICK_ENTITY_CAPACITY)
            && ((command->value & ~0x303UL) == 0U));
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
        if ((command->entity >= SEQ_TRACK_COUNT)
                || (scope < CONTROL_AUDIO_PARAM_KIND_BASE_MATRIX_FIRST)
                || (scope > CONTROL_AUDIO_PARAM_KIND_BASE_MATRIX_LAST))
            return 0U;
        const uint8_t index = (uint8_t)(
            scope - CONTROL_AUDIO_PARAM_KIND_BASE_MATRIX_FIRST);
        if (command->id == CONTROL_AUDIO_MOD_MULTI_SOURCE) return index < 4U;
        if ((command->id == CONTROL_AUDIO_MOD_SLEW_SOURCE)
                || (command->id == CONTROL_AUDIO_MOD_SLEW_AMOUNT))
            return index < 2U;
        if (command->id >= CONTROL_AUDIO_FX_FILTER_POSITION)
            return (uint8_t)((command->id != CONTROL_AUDIO_FX_SPATIAL_MODE)
                || (index < AUDIO_FX_SLOT_COUNT));
        return 1U;
    }
    if ((command->id >= CONTROL_AUDIO_PARAM_MIX_INSERT_FIRST)
            && (command->id <= CONTROL_AUDIO_PARAM_MIX_INSERT_LAST))
        return (uint8_t)((scope == 0U)
            && (command->entity < (BRICK_ENTITY_CAPACITY + 1U)));
    if (command->id == CONTROL_AUDIO_PARAM_MIX_ROUTE)
        return (uint8_t)((scope == 0U)
            && (command->entity < (BRICK_ENTITY_CAPACITY + 1U)));
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

static uint8_t control_rt_command_is_structural(
    const control_audio_command_t *command)
{
    if (command == NULL) return 0U;
    const uint8_t opcode = CONTROL_AUDIO_COMMAND_OPCODE(command);
    const uint8_t kind = CONTROL_AUDIO_COMMAND_KIND(command);
    switch (opcode)
    {
        case CONTROL_AUDIO_COMMAND_PROGRAM:
            return (uint8_t)((kind == 0U)
                && control_rt_program_is_structural(command->entity, command->value));
        case CONTROL_AUDIO_COMMAND_PARAM:
            return control_rt_param_is_structural(command);
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
            return (uint8_t)((kind <= CONTROL_AUDIO_STATE_PROJECT)
                && (command->value != 0U));
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
static control_audio_state_transition_kind_t g_audio_state_snapshot_transition;
static uint64_t g_control_rt_first_unpublished_sample;

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
    if (control_rt_command_is_structural(command) == 0U) return 0U;
    if (CONTROL_AUDIO_COMMAND_OPCODE(command) != CONTROL_AUDIO_COMMAND_PARAM)
        return 1U;
    if (CONTROL_AUDIO_COMMAND_KIND(command)
            == CONTROL_AUDIO_PARAM_KIND_BASE_GLOBAL)
        return track_runtime_audio_projection_param_is_current(
            command->entity, command->id);
    return 1U;
}

void audio_state_snapshot_control_init(void)
{
    memset(&g_audio_prepared_state, 0, sizeof(g_audio_prepared_state));
    g_audio_state_snapshot_depth = 0U;
    g_audio_state_snapshot_transition = CONTROL_AUDIO_STATE_PATTERN;
}

uint8_t audio_state_snapshot_control_active(void)
{
    return (g_audio_state_snapshot_depth != 0U) ? 1U : 0U;
}

uint8_t audio_state_snapshot_control_begin(
    control_audio_state_transition_kind_t transition)
{
    if ((transition > CONTROL_AUDIO_STATE_PROJECT)
            || (g_audio_state_snapshot_depth == UINT8_MAX)) return 0U;
    if (g_audio_state_snapshot_depth == 0U)
    {
        g_audio_prepared_state.valid_magic = 0U;
        g_audio_prepared_state.count = 0U;
        g_audio_state_snapshot_transition = transition;
    }
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
        if (control_audio_command_state_class(&commands[i])
                != CONTROL_AUDIO_COMMAND_DURABLE_STATE) return 0U;
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
        if (control_audio_command_state_class(&command)
                != CONTROL_AUDIO_COMMAND_DURABLE_STATE)
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
                        != CONTROL_AUDIO_PARAM_KIND_BASE_GLOBAL)
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
    if (g_audio_state_snapshot_depth == 1U)
    {
        /* Re-project every PROGRAM (and its type-owned tone/polyphony state)
         * from the final CONTROL authorities.  Commands observed while the
         * product state was being installed are only staging inputs; they are
         * never retained across snapshot lifetimes. */
        if (track_runtime_project_audio_state_all() == 0U)
            return 0U;
    }
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
            CONTROL_AUDIO_COMMAND_AUDIO_STATE_COMMIT,
            g_audio_state_snapshot_transition)
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
        return 0U;
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

uint8_t control_rt_build_param_command(uint8_t entity, uint16_t param_id,
                                       uint32_t value, uint8_t param_kind,
                                       uint64_t sample_time,
                                       control_audio_command_t *out_command)
{
    if ((out_command == NULL) || (param_kind > 0x1FU)) return 0U;
    *out_command = (control_audio_command_t){
        .effective_sample_time = sample_time,
        .value = value,
        .id = param_id,
        .entity = entity,
        .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(
            CONTROL_AUDIO_COMMAND_PARAM, param_kind)
    };
    return 1U;
}

uint8_t control_rt_publish_batch_scheduled(
    const control_audio_command_t *commands, uint16_t count)
{
    if ((commands == NULL) || (count == 0U)) return 0U;
    for (uint16_t i = 0U; i < count; ++i)
        if (control_rt_command_is_structural(&commands[i]) == 0U) return 0U;
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
        accepted = control_audio_fifo_publish_batch(commands, count);
    if ((accepted != 0U) && (g_control_audio_horizon.active == 0U))
        control_rt_advance_first_unpublished_sample(
            commands[count - 1U].effective_sample_time);
    return accepted;
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

uint8_t control_rt_publish_param(uint8_t entity, uint16_t param_id,
                                 uint32_t value, uint32_t target_detail,
                                 uint64_t sample_time)
{
    control_audio_command_t c;
    if ((target_detail > UINT8_MAX)
            || (control_rt_build_param_command(entity, param_id, value,
                (uint8_t)target_detail, sample_time, &c) == 0U)) return 0U;
    return control_rt_publish(&c);
}

uint8_t control_rt_publish_param_now(uint8_t entity, uint16_t param_id,
                                     uint32_t value, uint32_t target_detail)
{
    control_audio_command_t command;
    if ((target_detail > UINT8_MAX)
            || (control_rt_build_param_command(entity, param_id, value,
                (uint8_t)target_detail, 0U, &command) == 0U)) return 0U;
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
