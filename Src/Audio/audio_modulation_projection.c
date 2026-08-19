#include "Audio/audio_modulation_projection.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "stm32h7xx.h"

static AUDIO_HOT audio_modulation_topology_entry_t
    g_audio_modulation_projection_local[BRICK_ENTITY_CAPACITY];
static AUDIO_HOT audio_modulation_topology_entry_t
    g_audio_modulation_projection_candidate[BRICK_ENTITY_CAPACITY];

#define AUDIO_MODULATION_PROJECTION_CONSUME_ATTEMPTS 2U

/* The mailbox is defined by the CONTROL translation unit.  Keep the AUDIO
 * consumer dependent only on its pointer-free payload and seqlock. */
typedef struct
{
    volatile uint32_t sequence;
    audio_modulation_topology_entry_t entry[BRICK_ENTITY_CAPACITY];
} audio_modulation_projection_mailbox_t;

extern audio_modulation_projection_mailbox_t g_audio_modulation_projection;

void audio_modulation_projection_audio_init(void)
{
    memset(g_audio_modulation_projection_local, 0,
           sizeof(g_audio_modulation_projection_local));
    memset(g_audio_modulation_projection_candidate, 0,
           sizeof(g_audio_modulation_projection_candidate));
}

void audio_modulation_projection_audio_consume(void)
{
    for (uint32_t attempt = 0U;
         attempt < AUDIO_MODULATION_PROJECTION_CONSUME_ATTEMPTS;
         ++attempt)
    {
        const uint32_t before = g_audio_modulation_projection.sequence;
        __DMB();
        if ((before & 1U) != 0U)
            continue;
        memcpy(g_audio_modulation_projection_candidate,
               (const void *)g_audio_modulation_projection.entry,
               sizeof(g_audio_modulation_projection_candidate));
        __DMB();
        if (before == g_audio_modulation_projection.sequence)
        {
            memcpy(g_audio_modulation_projection_local,
                   g_audio_modulation_projection_candidate,
                   sizeof(g_audio_modulation_projection_local));
            return;
        }
    }
}

uint8_t audio_modulation_projection_audio_resolve_owner(uint8_t entity_id,
                                                        uint8_t *out_owner_id)
{
    if ((entity_id >= BRICK_ENTITY_CAPACITY) || (out_owner_id == NULL))
        return 0U;
    const audio_modulation_topology_entry_t *const entry =
        &g_audio_modulation_projection_local[entity_id];
    if ((entry->active == 0U) || (entry->mod_owner_id >= BRICK_ENTITY_CAPACITY))
        return 0U;
    *out_owner_id = entry->mod_owner_id;
    return 1U;
}

uint8_t audio_modulation_projection_audio_is_group_master(uint8_t entity_id)
{
    return (entity_id < BRICK_ENTITY_CAPACITY)
        ? g_audio_modulation_projection_local[entity_id].is_group_master : 0U;
}

uint8_t audio_modulation_projection_audio_is_active(uint8_t entity_id)
{
    return (entity_id < BRICK_ENTITY_CAPACITY)
        ? g_audio_modulation_projection_local[entity_id].active : 0U;
}
