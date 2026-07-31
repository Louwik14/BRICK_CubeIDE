#ifndef TRACK_TOPOLOGY_H
#define TRACK_TOPOLOGY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Common Low-Cost/Premium storage capacity; selectable tracks use LOGICAL_TRACK_COUNT. */
#define TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY 14U
#define TRACK_TOPOLOGY_PLAY_TRACK_COUNT 8U
#define TRACK_TOPOLOGY_INPUT_NONE 0xFFU
#define TRACK_TOPOLOGY_MASTER_TRACK_INDEX 8U
#define TRACK_TOPOLOGY_LOOPER_TRACK_INDEX 9U
#define TRACK_TOPOLOGY_INPUT_FIRST_TRACK_INDEX 10U
#define TRACK_TOPOLOGY_INPUT_SECOND_TRACK_INDEX (TRACK_TOPOLOGY_INPUT_FIRST_TRACK_INDEX + 1U)
#define TRACK_TOPOLOGY_INPUT_THIRD_TRACK_INDEX (TRACK_TOPOLOGY_INPUT_FIRST_TRACK_INDEX + 2U)

#if defined(BRICK6_VARIANT_LOWCOST)
#define TRACK_TOPOLOGY_SPECIAL_TRACK_COUNT 4U
#define TRACK_TOPOLOGY_LOGICAL_TRACK_COUNT 12U
#define TRACK_TOPOLOGY_PHYSICAL_INPUT_COUNT 1U
#define TRACK_TOPOLOGY_FX_TRACK_INDEX 11U
#define TRACK_TOPOLOGY_UNUSED_FIRST_TRACK_INDEX TRACK_TOPOLOGY_INPUT_SECOND_TRACK_INDEX
#define TRACK_TOPOLOGY_UNUSED_SECOND_TRACK_INDEX TRACK_TOPOLOGY_INPUT_THIRD_TRACK_INDEX
#else
#define TRACK_TOPOLOGY_SPECIAL_TRACK_COUNT 6U
#define TRACK_TOPOLOGY_LOGICAL_TRACK_COUNT 14U
#define TRACK_TOPOLOGY_PHYSICAL_INPUT_COUNT 3U
#define TRACK_TOPOLOGY_FX_TRACK_INDEX 13U
#endif

typedef enum
{
    TRACK_TOPOLOGY_CATEGORY_PLAY = 0,
    TRACK_TOPOLOGY_CATEGORY_SPECIAL,
    TRACK_TOPOLOGY_CATEGORY_UNUSED
} track_topology_category_t;

typedef enum
{
    TRACK_TOPOLOGY_ROLE_PLAY = 0,
    TRACK_TOPOLOGY_ROLE_MASTER,
    TRACK_TOPOLOGY_ROLE_LOOPER,
    TRACK_TOPOLOGY_ROLE_INPUT,
    TRACK_TOPOLOGY_ROLE_FX,
    TRACK_TOPOLOGY_ROLE_UNUSED
} track_topology_role_t;

typedef enum
{
    TRACK_CAPABILITY_NOTES = (1U << 0),
    TRACK_CAPABILITY_AUDIO = (1U << 1),
    TRACK_CAPABILITY_MIDI = (1U << 2),
    TRACK_CAPABILITY_KEYBOARD = (1U << 3),
    TRACK_CAPABILITY_ARPEGGIATOR = (1U << 4),
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

typedef struct
{
    uint8_t role;
    uint8_t ordinal;
} track_topology_identity_t;

uint8_t track_topology_get_descriptor(uint8_t track,
                                      track_topology_descriptor_t *out_descriptor);
uint8_t track_topology_has_capability(uint8_t track, track_capability_t capability);
uint8_t track_topology_is_active(uint8_t track);
uint8_t track_topology_is_play(uint8_t track);
uint8_t track_topology_is_special(uint8_t track);
uint8_t track_topology_is_role(uint8_t track, track_topology_role_t role);
uint8_t track_topology_find_special(track_topology_role_t role,
                                    uint8_t ordinal,
                                    uint8_t *out_track);
uint8_t track_topology_get_logical_track_count(void);
uint8_t track_topology_get_play_track_count(void);
uint8_t track_topology_get_special_track_count(void);
uint8_t track_topology_get_physical_input_count(void);
uint8_t track_topology_get_identity(uint8_t track,
                                    track_topology_identity_t *out_identity);
uint8_t track_topology_resolve_identity(const track_topology_identity_t *identity,
                                        uint8_t *out_track);
uint8_t track_topology_identity_is_compatible(uint8_t track,
                                              const track_topology_identity_t *identity);

#ifdef __cplusplus
}
#endif

#endif /* TRACK_TOPOLOGY_H */
