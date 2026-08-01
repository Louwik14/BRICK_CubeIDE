#ifndef STORAGE_TRACK_IDENTITY_H
#define STORAGE_TRACK_IDENTITY_H

#include <stdint.h>

#include "Core/track_topology.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_TRACK_IDENTITY_UNMAPPED 0xFFU

uint8_t storage_track_identity_build_remap(
    const track_topology_identity_t identities[TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY],
    uint8_t stored_track_count,
    uint8_t out_stored_to_current[TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY]);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_TRACK_IDENTITY_H */
