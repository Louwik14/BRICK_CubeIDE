#ifndef AUDIO_STATE_TRANSACTION_H
#define AUDIO_STATE_TRANSACTION_H

#include <stdint.h>

#include "ControlRT/control_audio_command.h"

/* Patch remains on the legacy command snapshot.  Its full final projection is
 * bounded by 1922 unique keys and rollback may add 256 distinct temp clears. */
#define AUDIO_STATE_PATCH_FINAL_KEY_BOUND 1922U
#define AUDIO_STATE_PATCH_TEMP_CLEAR_BOUND 256U
#define AUDIO_STATE_TRANSACTION_COMMAND_CAPACITY 2304U
_Static_assert(AUDIO_STATE_TRANSACTION_COMMAND_CAPACITY
                   >= AUDIO_STATE_PATCH_FINAL_KEY_BOUND
                        + AUDIO_STATE_PATCH_TEMP_CLEAR_BOUND,
               "Patch AUDIO transaction no longer covers its unique keys");
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
