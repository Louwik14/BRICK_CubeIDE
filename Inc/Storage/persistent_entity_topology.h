#ifndef PERSISTENT_ENTITY_TOPOLOGY_H
#define PERSISTENT_ENTITY_TOPOLOGY_H

#include <stdint.h>

#include "Track/entity_topology.h"
#include "Storage/persistent_control_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    persist_control_entity_id_t entity_id;
    persist_control_entity_id_t parent_entity_id;
    persist_control_entity_role_t role;
    uint8_t exists;
    uint8_t active;
    uint8_t persistable;
    uint8_t sequence_owner;
    uint8_t modulation_owner;
    uint8_t audio_fx_owner;
    uint8_t note_fx_owner;
    uint8_t input_owner;
    uint8_t play_limit;
} persist_entity_caps_t;

uint8_t persist_entity_caps_resolve(uint8_t group_active,
                                    persist_control_entity_id_t entity_id,
                                    persist_entity_caps_t *out_caps);
uint8_t persist_entity_mod_destination_allowed(
    uint8_t group_active,
    persist_control_entity_id_t owner_id,
    persist_control_entity_id_t destination_id);

#ifdef __cplusplus
}
#endif

#endif /* PERSISTENT_ENTITY_TOPOLOGY_H */
