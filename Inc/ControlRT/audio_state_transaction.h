#ifndef AUDIO_STATE_TRANSACTION_H
#define AUDIO_STATE_TRANSACTION_H

#include <stdint.h>

#include "ControlRT/control_audio_command.h"

#define AUDIO_STATE_TRANSACTION_COMMAND_CAPACITY 4618U
typedef struct
{
    uint16_t count;
    control_audio_command_t command[AUDIO_STATE_TRANSACTION_COMMAND_CAPACITY];
} audio_state_transaction_t;

_Static_assert(sizeof(control_audio_command_t) == 16U,
               "prepared AUDIO command size changed");
_Static_assert(sizeof(((audio_state_transaction_t *)0)->command)
                   == AUDIO_STATE_TRANSACTION_COMMAND_CAPACITY * 16U,
               "prepared AUDIO state capacity changed");

extern audio_state_transaction_t g_audio_state_transaction;

#endif
