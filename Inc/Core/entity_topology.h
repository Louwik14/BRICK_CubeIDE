#ifndef ENTITY_TOPOLOGY_H
#define ENTITY_TOPOLOGY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t brick_entity_id_t;

#define BRICK_ENTITY_TOP_LEVEL_COUNT 8U
#define BRICK_ENTITY_GROUP_CHILD_COUNT 8U
#define BRICK_ENTITY_CAPACITY \
    (BRICK_ENTITY_TOP_LEVEL_COUNT + BRICK_ENTITY_GROUP_CHILD_COUNT)
#define BRICK_ENTITY_INVALID_ID UINT8_MAX
#define BRICK_ENTITY_GROUP_MASTER_ID (BRICK_ENTITY_TOP_LEVEL_COUNT - 1U)
#define BRICK_ENTITY_FIRST_GROUP_CHILD_ID BRICK_ENTITY_TOP_LEVEL_COUNT
#define BRICK_ENTITY_LAST_GROUP_CHILD_ID (BRICK_ENTITY_CAPACITY - 1U)

#define ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT 1U

typedef enum
{
    ENTITY_ROLE_MAIN = 0,
    ENTITY_ROLE_GROUP_MASTER,
    ENTITY_ROLE_GROUP_CHILD
} entity_role_t;

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
    brick_entity_id_t entity_id;
    brick_entity_id_t parent_entity_id;
    uint8_t member_index;
    entity_role_t role;
    uint8_t active;
} entity_topology_descriptor_t;

/* Pure resolver for prospective snapshots which are not live yet. */
uint8_t entity_topology_resolve(uint8_t group_active,
                                brick_entity_id_t entity_id,
                                entity_topology_descriptor_t *out_descriptor);

/* Live topology authority. */
uint8_t entity_topology_group_is_active(void);
uint8_t entity_topology_get(brick_entity_id_t entity_id,
                            entity_topology_descriptor_t *out_descriptor);
uint8_t entity_topology_is_active(brick_entity_id_t entity_id);

/* Role-derived properties: never stored as parallel state. */
uint8_t entity_topology_can_sequence(const entity_topology_descriptor_t *descriptor);
uint8_t entity_topology_can_emit_notes(const entity_topology_descriptor_t *descriptor);
uint8_t entity_topology_has_audio_source(const entity_topology_descriptor_t *descriptor);
uint16_t entity_topology_get_capabilities(const entity_topology_descriptor_t *descriptor);
uint8_t entity_topology_has_capability(brick_entity_id_t entity_id,
                                       track_capability_t capability);

uint8_t entity_topology_get_top_level_count(void);
uint8_t entity_topology_get_physical_input_count(void);

#ifdef __cplusplus
}
#endif

#endif /* ENTITY_TOPOLOGY_H */
