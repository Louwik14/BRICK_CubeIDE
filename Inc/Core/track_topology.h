#ifndef TRACK_TOPOLOGY_H
#define TRACK_TOPOLOGY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The track index (0..7) is the sole runtime identity on every product. */
#define TRACK_TOPOLOGY_TRACK_COUNT 8U
#define TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY TRACK_TOPOLOGY_TRACK_COUNT
#define TRACK_TOPOLOGY_PLAY_TRACK_COUNT TRACK_TOPOLOGY_TRACK_COUNT
#define TRACK_TOPOLOGY_LOGICAL_TRACK_COUNT TRACK_TOPOLOGY_TRACK_COUNT
#define TRACK_TOPOLOGY_INPUT_NONE 0xFFU

#if defined(BRICK6_VARIANT_LOWCOST)
#define TRACK_TOPOLOGY_PHYSICAL_INPUT_COUNT 1U
#else
#define TRACK_TOPOLOGY_PHYSICAL_INPUT_COUNT 3U
#endif

typedef enum
{
    TRACK_CAPABILITY_NOTES = (1U << 0),
    TRACK_CAPABILITY_AUDIO = (1U << 1),
    TRACK_CAPABILITY_MIDI = (1U << 2),
    TRACK_CAPABILITY_KEYBOARD = (1U << 3),
    TRACK_CAPABILITY_MIDI_FX = (1U << 4),
    TRACK_CAPABILITY_AUTOMATION = (1U << 5),
    TRACK_CAPABILITY_MUTE = (1U << 6),
    TRACK_CAPABILITY_INPUT_RESERVATION = (1U << 7)
} track_capability_t;

typedef struct
{
    uint8_t track_index;
    uint8_t category;
    uint8_t role;
    uint8_t physical_input_index;
    uint16_t capabilities;
} track_topology_descriptor_t;

/* Transitional queries for consumers removed by the later UI/audio stages.
 * They cannot create identities outside the eight-slot domain. */
typedef enum
{
    TRACK_TOPOLOGY_ROLE_PLAY = 0,
    TRACK_TOPOLOGY_ROLE_LOOPER,
    TRACK_TOPOLOGY_ROLE_INPUT,
    TRACK_TOPOLOGY_ROLE_UNUSED
} track_topology_role_t;
typedef enum
{
    TRACK_TOPOLOGY_CATEGORY_PLAY = 0,
    TRACK_TOPOLOGY_CATEGORY_SPECIAL,
    TRACK_TOPOLOGY_CATEGORY_UNUSED
} track_topology_category_t;

uint8_t track_topology_get_descriptor(uint8_t track,
                                      track_topology_descriptor_t *out_descriptor);
uint8_t track_topology_has_capability(uint8_t track, track_capability_t capability);
uint8_t track_topology_is_active(uint8_t track);
uint8_t track_topology_is_play(uint8_t track);
uint8_t track_topology_is_special(uint8_t track);
uint8_t track_topology_is_role(uint8_t track, track_topology_role_t role);
uint8_t track_topology_find_special(track_topology_role_t role, uint8_t ordinal, uint8_t *out_track);
uint8_t track_topology_get_logical_track_count(void);
uint8_t track_topology_get_play_track_count(void);
uint8_t track_topology_get_physical_input_count(void);
uint8_t track_topology_get_special_track_count(void);

#ifdef __cplusplus
}
#endif

#endif /* TRACK_TOPOLOGY_H */
