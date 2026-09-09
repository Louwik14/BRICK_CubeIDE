#ifndef AUDIO_STATE_SNAPSHOT_H
#define AUDIO_STATE_SNAPSHOT_H

#include <stdint.h>

#include "IPC/control_audio_command.h"

#define AUDIO_STATE_SNAPSHOT_COMMAND_CAPACITY 4618U
#define AUDIO_STATE_SNAPSHOT_VALID_MAGIC      0x4155534EU

typedef struct
{
    uint32_t generation;
    uint16_t count;
    uint16_t reserved0;
    uint32_t checksum;
    uint32_t valid_magic;
    uint32_t reserved[4];
    control_audio_command_t command[AUDIO_STATE_SNAPSHOT_COMMAND_CAPACITY];
} audio_prepared_state_t;

_Static_assert(sizeof(control_audio_command_t) == 16U,
               "snapshot command projection ABI changed");
_Static_assert(sizeof(audio_prepared_state_t) == 73920U,
               "prepared AUDIO state size changed");

extern audio_prepared_state_t g_audio_prepared_state;

uint8_t audio_state_snapshot_publish(
    const control_audio_command_t *commands, uint16_t count,
    uint32_t *out_generation);
uint8_t audio_state_snapshot_resolve(uint32_t generation,
    const control_audio_command_t **out_commands, uint16_t *out_count);

#endif
