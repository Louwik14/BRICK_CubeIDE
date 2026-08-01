#include "Core/track_topology.h"

#include <stddef.h>

#define PLAY_CAPABILITIES ((uint16_t)(TRACK_CAPABILITY_NOTES \
                                      | TRACK_CAPABILITY_AUDIO \
                                      | TRACK_CAPABILITY_MIDI \
                                      | TRACK_CAPABILITY_KEYBOARD \
                                      | TRACK_CAPABILITY_MIDI_FX \
                                      | TRACK_CAPABILITY_AUTOMATION \
                                      | TRACK_CAPABILITY_MUTE \
                                      | TRACK_CAPABILITY_INPUT_RESERVATION))
#define MASTER_CAPABILITIES ((uint16_t)(TRACK_CAPABILITY_AUDIO \
                                        | TRACK_CAPABILITY_AUTOMATION))
#define AUDIO_SPECIAL_CAPABILITIES ((uint16_t)(TRACK_CAPABILITY_AUDIO \
                                               | TRACK_CAPABILITY_AUTOMATION \
                                               | TRACK_CAPABILITY_MUTE))

#define PLAY_DESCRIPTOR(index_) \
    { (index_), TRACK_TOPOLOGY_CATEGORY_PLAY, TRACK_TOPOLOGY_ROLE_PLAY, \
      TRACK_TOPOLOGY_INPUT_NONE, PLAY_CAPABILITIES }
#define SPECIAL_DESCRIPTOR(index_, role_, input_, caps_) \
    { (index_), TRACK_TOPOLOGY_CATEGORY_SPECIAL, (role_), (input_), (caps_) }
#define UNUSED_DESCRIPTOR(index_) \
    { (index_), TRACK_TOPOLOGY_CATEGORY_UNUSED, TRACK_TOPOLOGY_ROLE_UNUSED, \
      TRACK_TOPOLOGY_INPUT_NONE, 0U }

static const track_topology_descriptor_t g_track_topology[TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY] = {
    PLAY_DESCRIPTOR(0U),
    PLAY_DESCRIPTOR(1U),
    PLAY_DESCRIPTOR(2U),
    PLAY_DESCRIPTOR(3U),
    PLAY_DESCRIPTOR(4U),
    PLAY_DESCRIPTOR(5U),
    PLAY_DESCRIPTOR(6U),
    PLAY_DESCRIPTOR(7U),
    SPECIAL_DESCRIPTOR(TRACK_TOPOLOGY_MASTER_TRACK_INDEX, TRACK_TOPOLOGY_ROLE_MASTER, TRACK_TOPOLOGY_INPUT_NONE,
                       MASTER_CAPABILITIES),
    SPECIAL_DESCRIPTOR(TRACK_TOPOLOGY_LOOPER_TRACK_INDEX, TRACK_TOPOLOGY_ROLE_LOOPER, TRACK_TOPOLOGY_INPUT_NONE,
                       AUDIO_SPECIAL_CAPABILITIES),
    SPECIAL_DESCRIPTOR(TRACK_TOPOLOGY_INPUT_FIRST_TRACK_INDEX, TRACK_TOPOLOGY_ROLE_INPUT, 0U,
                       AUDIO_SPECIAL_CAPABILITIES),
#if defined(BRICK6_VARIANT_LOWCOST)
    SPECIAL_DESCRIPTOR(TRACK_TOPOLOGY_FX_TRACK_INDEX, TRACK_TOPOLOGY_ROLE_FX, TRACK_TOPOLOGY_INPUT_NONE,
                       AUDIO_SPECIAL_CAPABILITIES),
    UNUSED_DESCRIPTOR(TRACK_TOPOLOGY_UNUSED_FIRST_TRACK_INDEX),
    UNUSED_DESCRIPTOR(TRACK_TOPOLOGY_UNUSED_SECOND_TRACK_INDEX)
#else
    SPECIAL_DESCRIPTOR(TRACK_TOPOLOGY_INPUT_SECOND_TRACK_INDEX, TRACK_TOPOLOGY_ROLE_INPUT, 1U,
                       AUDIO_SPECIAL_CAPABILITIES),
    SPECIAL_DESCRIPTOR(TRACK_TOPOLOGY_INPUT_THIRD_TRACK_INDEX, TRACK_TOPOLOGY_ROLE_INPUT, 2U,
                       AUDIO_SPECIAL_CAPABILITIES),
    SPECIAL_DESCRIPTOR(TRACK_TOPOLOGY_FX_TRACK_INDEX, TRACK_TOPOLOGY_ROLE_FX, TRACK_TOPOLOGY_INPUT_NONE,
                       AUDIO_SPECIAL_CAPABILITIES)
#endif
};

_Static_assert(TRACK_TOPOLOGY_PLAY_TRACK_COUNT + TRACK_TOPOLOGY_SPECIAL_TRACK_COUNT
                   == TRACK_TOPOLOGY_LOGICAL_TRACK_COUNT,
               "Track topology counts must add up");
_Static_assert(TRACK_TOPOLOGY_LOGICAL_TRACK_COUNT <= TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY,
               "Track topology exceeds the common storage capacity");
_Static_assert(TRACK_TOPOLOGY_PHYSICAL_INPUT_COUNT <= 3U,
               "The product has at most three physical inputs");
_Static_assert(sizeof(track_topology_descriptor_t) == 6U,
               "Track topology descriptors must remain compact");

uint8_t track_topology_get_descriptor(uint8_t track,
                                      track_topology_descriptor_t *out_descriptor)
{
    if ((track >= TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY) || (out_descriptor == NULL))
    {
        return 0U;
    }

    *out_descriptor = g_track_topology[track];
    return 1U;
}

uint8_t track_topology_has_capability(uint8_t track, track_capability_t capability)
{
    track_topology_descriptor_t descriptor;
    if (track_topology_get_descriptor(track, &descriptor) == 0U)
    {
        return 0U;
    }

    return ((descriptor.capabilities & (uint16_t)capability) != 0U) ? 1U : 0U;
}

uint8_t track_topology_is_active(uint8_t track)
{
    track_topology_descriptor_t descriptor;
    return (uint8_t)((track_topology_get_descriptor(track, &descriptor) != 0U)
            && (descriptor.category != (uint8_t)TRACK_TOPOLOGY_CATEGORY_UNUSED));
}

uint8_t track_topology_is_play(uint8_t track)
{
    track_topology_descriptor_t descriptor;
    return (uint8_t)((track_topology_get_descriptor(track, &descriptor) != 0U)
            && (descriptor.category == (uint8_t)TRACK_TOPOLOGY_CATEGORY_PLAY));
}

uint8_t track_topology_is_special(uint8_t track)
{
    track_topology_descriptor_t descriptor;
    return (uint8_t)((track_topology_get_descriptor(track, &descriptor) != 0U)
            && (descriptor.category == (uint8_t)TRACK_TOPOLOGY_CATEGORY_SPECIAL));
}

uint8_t track_topology_is_role(uint8_t track, track_topology_role_t role)
{
    track_topology_descriptor_t descriptor;
    return (uint8_t)((track_topology_get_descriptor(track, &descriptor) != 0U)
            && (descriptor.role == (uint8_t)role));
}

uint8_t track_topology_find_special(track_topology_role_t role,
                                    uint8_t ordinal,
                                    uint8_t *out_track)
{
    if ((out_track == NULL) || (role == TRACK_TOPOLOGY_ROLE_PLAY)
            || (role == TRACK_TOPOLOGY_ROLE_UNUSED))
    {
        return 0U;
    }

    uint8_t match = 0U;
    for (uint8_t track = TRACK_TOPOLOGY_PLAY_TRACK_COUNT;
         track < TRACK_TOPOLOGY_LOGICAL_TRACK_COUNT;
         ++track)
    {
        if (g_track_topology[track].role != (uint8_t)role)
        {
            continue;
        }
        if (match == ordinal)
        {
            *out_track = track;
            return 1U;
        }
        match++;
    }

    return 0U;
}

uint8_t track_topology_get_logical_track_count(void)
{
    return TRACK_TOPOLOGY_LOGICAL_TRACK_COUNT;
}

uint8_t track_topology_get_play_track_count(void)
{
    return TRACK_TOPOLOGY_PLAY_TRACK_COUNT;
}

uint8_t track_topology_get_special_track_count(void)
{
    return TRACK_TOPOLOGY_SPECIAL_TRACK_COUNT;
}

uint8_t track_topology_get_physical_input_count(void)
{
    return TRACK_TOPOLOGY_PHYSICAL_INPUT_COUNT;
}

uint8_t track_topology_get_identity(uint8_t track,
                                    track_topology_identity_t *out_identity)
{
    track_topology_descriptor_t descriptor;
    if ((out_identity == NULL)
            || (track_topology_get_descriptor(track, &descriptor) == 0U)
            || (descriptor.category == (uint8_t)TRACK_TOPOLOGY_CATEGORY_UNUSED))
    {
        return 0U;
    }

    out_identity->role = descriptor.role;
    out_identity->ordinal = (descriptor.role == (uint8_t)TRACK_TOPOLOGY_ROLE_PLAY)
        ? track
        : ((descriptor.role == (uint8_t)TRACK_TOPOLOGY_ROLE_INPUT)
            ? descriptor.physical_input_index : 0U);
    return 1U;
}

uint8_t track_topology_resolve_identity(const track_topology_identity_t *identity,
                                        uint8_t *out_track)
{
    if ((identity == NULL) || (out_track == NULL))
    {
        return 0U;
    }
    if (identity->role == (uint8_t)TRACK_TOPOLOGY_ROLE_PLAY)
    {
        if ((identity->ordinal >= TRACK_TOPOLOGY_PLAY_TRACK_COUNT)
                || (track_topology_is_play(identity->ordinal) == 0U))
        {
            return 0U;
        }
        *out_track = identity->ordinal;
        return 1U;
    }
    if (identity->role == (uint8_t)TRACK_TOPOLOGY_ROLE_INPUT)
    {
        for (uint8_t track = TRACK_TOPOLOGY_PLAY_TRACK_COUNT;
             track < TRACK_TOPOLOGY_LOGICAL_TRACK_COUNT;
             ++track)
        {
            if ((g_track_topology[track].role == (uint8_t)TRACK_TOPOLOGY_ROLE_INPUT)
                    && (g_track_topology[track].physical_input_index == identity->ordinal))
            {
                *out_track = track;
                return 1U;
            }
        }
        return 0U;
    }
    return track_topology_find_special((track_topology_role_t)identity->role,
                                       identity->ordinal,
                                       out_track);
}

uint8_t track_topology_identity_is_compatible(uint8_t track,
                                              const track_topology_identity_t *identity)
{
    track_topology_identity_t target;
    uint8_t source_track = 0U;
    if ((identity == NULL)
            || (track_topology_get_identity(track, &target) == 0U)
            || (track_topology_resolve_identity(identity, &source_track) == 0U))
    {
        return 0U;
    }
    if (target.role == (uint8_t)TRACK_TOPOLOGY_ROLE_PLAY)
    {
        return (uint8_t)(identity->role == (uint8_t)TRACK_TOPOLOGY_ROLE_PLAY);
    }
    return (uint8_t)(source_track == track);
}
