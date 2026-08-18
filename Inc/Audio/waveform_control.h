#ifndef WAVEFORM_CONTROL_H
#define WAVEFORM_CONTROL_H

#include <stdint.h>

#include "Core/entity_topology.h"

typedef struct
{
    brick_entity_id_t entity_id;
    uint8_t enabled;
    uint8_t fast_refresh;
} waveform_control_command_t;

_Static_assert(sizeof(waveform_control_command_t) == 3U,
               "waveform CONTROL/AUDIO command must remain bounded and pointer-free");

/* CONTROL producer: the most recent complete state supersedes older states. */
void waveform_control_publish(brick_entity_id_t entity_id,
                              uint8_t enabled,
                              uint8_t fast_refresh);

/* AUDIO consumer: returns one only when a newer complete state is available. */
uint8_t waveform_control_audio_consume(waveform_control_command_t *out_command);

#endif /* WAVEFORM_CONTROL_H */
