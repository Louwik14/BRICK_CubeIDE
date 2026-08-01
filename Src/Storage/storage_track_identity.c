#include "Storage/storage_track_identity.h"

#include <stddef.h>
#include <string.h>

uint8_t storage_track_identity_build_remap(
    const track_topology_identity_t identities[TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY],
    uint8_t stored_track_count,
    uint8_t out_stored_to_current[TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY])
{
    if ((identities == NULL) || (out_stored_to_current == NULL)
            || (stored_track_count != track_topology_get_logical_track_count())
            || (stored_track_count > TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY))
    {
        return 0U;
    }

    uint8_t current_seen[TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY];
    memset(current_seen, 0, sizeof(current_seen));
    memset(out_stored_to_current,
           STORAGE_TRACK_IDENTITY_UNMAPPED,
           TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY);

    for (uint8_t stored = stored_track_count;
         stored < TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY;
         ++stored)
    {
        if ((identities[stored].role != 0U) || (identities[stored].ordinal != 0U))
        {
            return 0U;
        }
    }

    for (uint8_t stored = 0U; stored < stored_track_count; ++stored)
    {
        uint8_t current = STORAGE_TRACK_IDENTITY_UNMAPPED;
        if ((track_topology_resolve_identity(&identities[stored], &current) == 0U)
                || (current >= stored_track_count)
                || (track_topology_is_active(current) == 0U)
                || (current_seen[current] != 0U))
        {
            return 0U;
        }
        out_stored_to_current[stored] = current;
        current_seen[current] = 1U;
    }

    for (uint8_t current = 0U; current < stored_track_count; ++current)
    {
        if (current_seen[current] == 0U)
        {
            return 0U;
        }
    }
    return 1U;
}
