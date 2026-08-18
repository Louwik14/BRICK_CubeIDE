#pragma once

#include <stdint.h>

#include "Audio/mixer.h"

typedef struct
{
    uint32_t generation;
    uint8_t route_master[MIXER_MAX_TRACKS];
    int8_t insert_slot[MIXER_MAX_TRACKS][MIXER_INSERTS_PER_TRACK];
} mixer_routing_snapshot_t;

_Static_assert(sizeof(mixer_routing_snapshot_t) == 56U,
               "Mixer routing publication ABI changed");

/* CONTROL owner. */
void mixer_routing_control_init(void);
uint8_t mixer_routing_control_set_route(uint32_t track_id,
                                        mixer_route_t route);
uint8_t mixer_routing_control_set_insert_slot(uint32_t track_id,
                                              uint32_t insert_idx,
                                              int8_t slot);

/* AUDIO consumer. Copies a coherent, pointer-free snapshot. */
uint8_t mixer_routing_publication_audio_read(mixer_routing_snapshot_t *out);
