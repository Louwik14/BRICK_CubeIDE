#pragma once

#include <stdint.h>
#include "IPC/control_audio_command.h"

#define CONTROL_AUDIO_FIFO_MAX_PARAM_BURST   1024U
#define CONTROL_AUDIO_FIFO_MAX_NOTE_BURST     722U
#define CONTROL_AUDIO_FIFO_MAX_GENERAL_BURST   35U
#define CONTROL_AUDIO_FIFO_CONTRACT_BURST \
    (CONTROL_AUDIO_FIFO_MAX_PARAM_BURST + CONTROL_AUDIO_FIFO_MAX_NOTE_BURST \
     + CONTROL_AUDIO_FIFO_MAX_GENERAL_BURST)
#define CONTROL_AUDIO_FIFO_CAPACITY 2048U

_Static_assert((CONTROL_AUDIO_FIFO_CAPACITY
                & (CONTROL_AUDIO_FIFO_CAPACITY - 1U)) == 0U,
               "functional FIFO capacity must be a power of two");
_Static_assert(CONTROL_AUDIO_FIFO_CAPACITY >= CONTROL_AUDIO_FIFO_CONTRACT_BURST,
               "functional FIFO cannot contain the contractual worst burst");

typedef struct
{
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t overflow_count;
    volatile uint32_t invariant_failure_count;
} control_audio_fifo_layout_t;

extern control_audio_fifo_layout_t g_control_audio_fifo_layout;
extern control_audio_command_t
    g_control_audio_fifo_commands[CONTROL_AUDIO_FIFO_CAPACITY];
