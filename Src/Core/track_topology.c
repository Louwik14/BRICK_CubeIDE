#include "Core/track_topology.h"

#include <stddef.h>

#define TRACK_CAPABILITIES ((uint16_t)(TRACK_CAPABILITY_NOTES \
                                      | TRACK_CAPABILITY_AUDIO \
                                      | TRACK_CAPABILITY_MIDI \
                                      | TRACK_CAPABILITY_KEYBOARD \
                                      | TRACK_CAPABILITY_MIDI_FX \
                                      | TRACK_CAPABILITY_AUTOMATION \
                                      | TRACK_CAPABILITY_MUTE \
                                      | TRACK_CAPABILITY_INPUT_RESERVATION))

static const track_topology_descriptor_t g_track_topology[TRACK_TOPOLOGY_TRACK_COUNT] = {
    { 0U, TRACK_CAPABILITIES },
    { 1U, TRACK_CAPABILITIES },
    { 2U, TRACK_CAPABILITIES },
    { 3U, TRACK_CAPABILITIES },
    { 4U, TRACK_CAPABILITIES },
    { 5U, TRACK_CAPABILITIES },
    { 6U, TRACK_CAPABILITIES },
    { 7U, TRACK_CAPABILITIES }
};

uint8_t track_topology_get_descriptor(uint8_t track, track_topology_descriptor_t *out_descriptor)
{
    if ((track >= TRACK_TOPOLOGY_TRACK_COUNT) || (out_descriptor == NULL)) return 0U;
    *out_descriptor = g_track_topology[track];
    return 1U;
}

uint8_t track_topology_has_capability(uint8_t track, track_capability_t capability)
{
    track_topology_descriptor_t descriptor;
    return (uint8_t)((track_topology_get_descriptor(track, &descriptor) != 0U)
        && ((descriptor.capabilities & (uint16_t)capability) != 0U));
}

uint8_t track_topology_is_active(uint8_t track) { return (track < TRACK_TOPOLOGY_TRACK_COUNT) ? 1U : 0U; }
uint8_t track_topology_get_logical_track_count(void) { return TRACK_TOPOLOGY_TRACK_COUNT; }
uint8_t track_topology_get_physical_input_count(void) { return TRACK_TOPOLOGY_PHYSICAL_INPUT_COUNT; }
