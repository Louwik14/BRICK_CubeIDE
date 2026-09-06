#include "ControlRT/control_rt_publication.h"

#include <string.h>

#include "IPC/control_audio_fifo_control.h"
#include "IPC/control_audio_timing.h"
#include "IPC/live_clock_control.h"
#include "Platform/memory_layout.h"
#include "stm32h7xx.h"

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
static volatile uint32_t g_control_audio_horizon_capacity_failure_count;
static volatile uint8_t g_control_rt_publication_suppressed;

void control_rt_publication_init(void)
{
    control_audio_fifo_control_init();
    g_control_audio_horizon.count = 0U;
    g_control_audio_horizon.limit = 0U;
    g_control_audio_horizon.frames = 0U;
    g_control_audio_horizon.first_sample = 0U;
    g_control_audio_horizon.active = 0U;
    g_control_audio_horizon_capacity_failure_count = 0U;
    g_control_rt_publication_suppressed = 0U;
}

void control_rt_publication_suppress_begin(void)
{
    g_control_rt_publication_suppressed = 1U;
    __DMB();
}

void control_rt_publication_suppress_end(void)
{
    __DMB();
    g_control_rt_publication_suppressed = 0U;
}

uint8_t control_rt_publication_horizon_active(void)
{
    return g_control_audio_horizon.active;
}

uint8_t control_rt_now_sample(uint64_t *out_sample_time)
{
    return live_clock_read_audio_sample(out_sample_time) ? 1U : 0U;
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
    return 1U;
}

void control_rt_publication_abort_horizon(void)
{
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
    return accepted;
}

static uint8_t control_rt_publish(const control_audio_command_t *command)
{
    if (g_control_rt_publication_suppressed != 0U)
        return (command != NULL) ? 1U : 0U;
    return (g_control_audio_horizon.active != 0U)
        ? ((control_rt_publication_free() != 0U)
            ? control_rt_publication_stage(command, 1U) : 0U)
        : control_audio_fifo_publish(command);
}

uint8_t control_rt_publish_batch_scheduled(
    const control_audio_command_t *commands, uint16_t count)
{
    if (g_control_rt_publication_suppressed != 0U)
        return ((commands != NULL) && (count != 0U)) ? 1U : 0U;
    return (g_control_audio_horizon.active != 0U)
        ? ((control_rt_publication_free() >= count)
            ? control_rt_publication_stage(commands, count) : 0U)
        : control_audio_fifo_publish_batch(commands, count);
}

uint8_t control_rt_publish_batch_captured(control_audio_command_t *commands,
                                          uint16_t count,
                                          uint32_t capture_tick,
                                          uint64_t minimum_sample)
{
    if (g_control_rt_publication_suppressed != 0U)
        return ((commands != NULL) && (count != 0U)) ? 1U : 0U;
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
    if (g_control_rt_publication_suppressed != 0U)
        return ((commands != NULL) && (count != 0U)) ? 1U : 0U;
    uint64_t sample_time = 0U;
    if ((commands == NULL) || (count == 0U)
            || (count > CONTROL_AUDIO_FIFO_CONTRACT_BURST)
            || !control_rt_now_sample(&sample_time))
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

uint8_t control_rt_publish_audio_state_commit(uint32_t generation)
{
    uint64_t sample_time = 0U;
    control_audio_command_t command;
    if ((generation == 0U) || (control_rt_now_sample(&sample_time) == 0U))
        return 0U;
    command = (control_audio_command_t){
        .effective_sample_time = sample_time,
        .value = generation,
        .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(
            CONTROL_AUDIO_COMMAND_STATE_COMMIT, 0U)
    };
    return control_rt_publish(&command);
}
