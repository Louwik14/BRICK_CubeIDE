#include "Core/audio_input_ownership_projection.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "stm32h7xx.h"

static AUDIO_HOT audio_input_ownership_entry_t
    g_audio_input_ownership_local[ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT];
static AUDIO_HOT audio_input_ownership_entry_t
    g_audio_input_ownership_candidate[ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT];

#define AUDIO_INPUT_OWNERSHIP_CONSUME_ATTEMPTS 2U

typedef struct
{
    volatile uint32_t sequence;
    audio_input_ownership_entry_t entry[ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT];
} audio_input_ownership_projection_mailbox_t;

extern audio_input_ownership_projection_mailbox_t
    g_audio_input_ownership_projection;

void audio_input_ownership_projection_audio_init(void)
{
    memset(g_audio_input_ownership_local, 0,
           sizeof(g_audio_input_ownership_local));
    memset(g_audio_input_ownership_candidate, 0,
           sizeof(g_audio_input_ownership_candidate));
}

void audio_input_ownership_projection_audio_consume(void)
{
    for (uint32_t attempt = 0U;
         attempt < AUDIO_INPUT_OWNERSHIP_CONSUME_ATTEMPTS;
         ++attempt)
    {
        const uint32_t before = g_audio_input_ownership_projection.sequence;
        __DMB();
        if ((before & 1U) != 0U)
            continue;
        memcpy(g_audio_input_ownership_candidate,
               (const void *)g_audio_input_ownership_projection.entry,
               sizeof(g_audio_input_ownership_candidate));
        __DMB();
        if (before == g_audio_input_ownership_projection.sequence)
        {
            memcpy(g_audio_input_ownership_local,
                   g_audio_input_ownership_candidate,
                   sizeof(g_audio_input_ownership_local));
            return;
        }
    }
}

uint8_t audio_input_ownership_projection_audio_get_owner(
    uint8_t input, uint8_t *out_owner_entity_id)
{
    if ((input >= ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT)
            || (out_owner_entity_id == NULL))
    {
        return 0U;
    }
    const audio_input_ownership_entry_t *const entry =
        &g_audio_input_ownership_local[input];
    if ((entry->valid == 0U) || (entry->owner_entity_id >= BRICK_ENTITY_CAPACITY))
        return 0U;
    *out_owner_entity_id = entry->owner_entity_id;
    return 1U;
}
