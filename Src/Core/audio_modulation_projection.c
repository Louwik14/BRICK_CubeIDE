#include "Core/audio_modulation_projection.h"

#include <string.h>

#include "Core/entity_topology.h"
#include "Storage/memory_layout.h"
#include "stm32h7xx.h"

typedef struct
{
    volatile uint32_t sequence;
    audio_modulation_topology_entry_t entry[BRICK_ENTITY_CAPACITY];
} audio_modulation_projection_mailbox_t;

D3_IPC audio_modulation_projection_mailbox_t
    g_audio_modulation_projection;

void audio_modulation_projection_init(void)
{
    memset((void *)&g_audio_modulation_projection, 0,
           sizeof(g_audio_modulation_projection));
}

void audio_modulation_projection_publish(void)
{
    audio_modulation_topology_entry_t projection[BRICK_ENTITY_CAPACITY];
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        entity_topology_descriptor_t topology;
        projection[entity] = (audio_modulation_topology_entry_t){
            .mod_owner_id = entity,
            .is_group_master = 0U,
            .active = 0U,
            .reserved = 0U
        };
        if (entity_topology_get((brick_entity_id_t)entity, &topology) != 0U)
        {
            brick_entity_id_t owner = (brick_entity_id_t)entity;
            if (entity_topology_mod_owner((brick_entity_id_t)entity, &owner) != 0U)
                projection[entity].mod_owner_id = owner;
            projection[entity].is_group_master =
                (topology.role == ENTITY_ROLE_GROUP_MASTER) ? 1U : 0U;
            projection[entity].active = topology.active;
        }
    }

    uint32_t sequence = g_audio_modulation_projection.sequence;
    if ((sequence & 1U) != 0U)
        ++sequence;
    g_audio_modulation_projection.sequence = sequence + 1U;
    __DMB();
    memcpy((void *)g_audio_modulation_projection.entry,
           projection, sizeof(projection));
    __DMB();
    g_audio_modulation_projection.sequence = sequence + 2U;
    __DMB();
}
