#include "IPC/control_audio_fifo_layout.h"
#include "IPC/audio_state_snapshot.h"
#include "Platform/memory_layout.h"
#include "Platform/intercore_cache.h"

#include <string.h>

D3_IPC control_audio_fifo_layout_t g_control_audio_fifo_layout;
AUDIO_STORAGE_SHARED_SDRAM control_audio_command_t
    g_control_audio_fifo_commands[CONTROL_AUDIO_FIFO_CAPACITY];
AUDIO_STATE_SHARED_SDRAM audio_prepared_state_t g_audio_prepared_state;

static uint32_t g_audio_state_snapshot_generation;

static uint32_t audio_state_snapshot_checksum(
    const control_audio_command_t *commands, uint16_t count)
{
    const uint8_t *const bytes = (const uint8_t *)commands;
    const uint32_t byte_count = (uint32_t)count * sizeof(commands[0]);
    uint32_t checksum = 2166136261UL;
    for (uint32_t i = 0U; i < byte_count; ++i)
        checksum = (checksum ^ bytes[i]) * 16777619UL;
    return checksum;
}

uint8_t audio_state_snapshot_publish(
    const control_audio_command_t *commands, uint16_t count,
    uint32_t *out_generation)
{
    if ((commands == NULL) || (count == 0U)
            || (count > AUDIO_STATE_SNAPSHOT_COMMAND_CAPACITY)
            || (out_generation == NULL)) return 0U;
    uint32_t generation = g_audio_state_snapshot_generation + 1U;
    if (generation == 0U) generation = 1U;
    audio_prepared_state_t *const state = &g_audio_prepared_state;
    state->valid_magic = 0U;
    intercore_cache_publish(state, 32U);
    if (commands != state->command)
        memcpy(state->command, commands, (size_t)count * sizeof(commands[0]));
    state->generation = generation;
    state->count = count;
    state->reserved0 = 0U;
    state->checksum = audio_state_snapshot_checksum(commands, count);
    memset(state->reserved, 0, sizeof(state->reserved));
    state->valid_magic = AUDIO_STATE_SNAPSHOT_VALID_MAGIC;
    intercore_cache_publish(state,
        32U + (size_t)count * sizeof(commands[0]));
    g_audio_state_snapshot_generation = generation;
    *out_generation = generation;
    return 1U;
}

uint8_t audio_state_snapshot_resolve(uint32_t generation,
    const control_audio_command_t **out_commands, uint16_t *out_count)
{
    if ((generation == 0U) || (out_commands == NULL) || (out_count == NULL))
        return 0U;
    audio_prepared_state_t *const state = &g_audio_prepared_state;
    intercore_cache_consume(state, sizeof(*state));
    if ((state->valid_magic != AUDIO_STATE_SNAPSHOT_VALID_MAGIC)
            || (state->generation != generation) || (state->count == 0U)
            || (state->count > AUDIO_STATE_SNAPSHOT_COMMAND_CAPACITY)
            || (state->checksum != audio_state_snapshot_checksum(
                state->command, state->count))) return 0U;
    *out_commands = state->command;
    *out_count = state->count;
    return 1U;
}
