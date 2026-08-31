#ifndef ENTITY_TOPOLOGY_H
#define ENTITY_TOPOLOGY_H

#include "Track/entity_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    ENTITY_ROLE_MAIN = 0,
    ENTITY_ROLE_GROUP_MASTER,
    ENTITY_ROLE_GROUP_CHILD
} entity_role_t;

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
uint8_t entity_topology_mod_owner(brick_entity_id_t entity_id,
                                  brick_entity_id_t *out_owner_id);
uint8_t entity_topology_group_child(brick_entity_id_t parent_entity_id,
                                    uint8_t member_index,
                                    brick_entity_id_t *out_child_id);

uint8_t entity_topology_get_top_level_count(void);
uint8_t entity_topology_get_physical_input_count(void);

#ifdef __cplusplus
}
#endif

#endif /* ENTITY_TOPOLOGY_H */
