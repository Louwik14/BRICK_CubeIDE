#include "Core/audio_input_ownership_projection.h"

#include <string.h>

#include "Core/track_input_ownership.h"
#include "Storage/memory_layout.h"
#include "stm32h7xx.h"

typedef struct
{
    volatile uint32_t sequence;
    audio_input_ownership_entry_t entry[ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT];
} audio_input_ownership_projection_mailbox_t;

D3_IPC audio_input_ownership_projection_mailbox_t
    g_audio_input_ownership_projection;

void audio_input_ownership_projection_init(void)
{
    memset((void *)&g_audio_input_ownership_projection, 0,
           sizeof(g_audio_input_ownership_projection));
}

void audio_input_ownership_projection_publish(void)
{
    audio_input_ownership_entry_t projection[
        ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT];
    for (uint8_t input = 0U;
         input < ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT;
         ++input)
    {
        uint8_t owner = TRACK_INPUT_OWNER_NONE;
        projection[input] = (audio_input_ownership_entry_t){
            .owner_entity_id = TRACK_INPUT_OWNER_NONE,
            .valid = 0U,
            .reserved = 0U
        };
        if (track_input_ownership_get_external_owner(input, &owner) != 0U)
        {
            projection[input].owner_entity_id = owner;
            projection[input].valid = 1U;
        }
    }

    uint32_t sequence = g_audio_input_ownership_projection.sequence;
    if ((sequence & 1U) != 0U)
        ++sequence;
    g_audio_input_ownership_projection.sequence = sequence + 1U;
    __DMB();
    memcpy((void *)g_audio_input_ownership_projection.entry,
           projection, sizeof(projection));
    __DMB();
    g_audio_input_ownership_projection.sequence = sequence + 2U;
    __DMB();
}
