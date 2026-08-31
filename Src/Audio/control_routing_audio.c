#include "Audio/control_routing_audio.h"

#include <string.h>

#include "Track/entity_types.h"

static uint16_t g_audio_looper_source_mask[BRICK_ENTITY_CAPACITY];

void control_routing_audio_init(void)
{
    memset(g_audio_looper_source_mask, 0, sizeof(g_audio_looper_source_mask));
}

uint8_t control_routing_audio_set_mask(uint8_t looper, uint16_t source_mask)
{
    if (looper >= BRICK_ENTITY_CAPACITY) return 0U;
    g_audio_looper_source_mask[looper] = source_mask;
    return 1U;
}

uint8_t control_routing_audio_get_looper_source(uint8_t looper,
                                                uint8_t source)
{
    if ((looper >= BRICK_ENTITY_CAPACITY) || (source >= BRICK_ENTITY_CAPACITY))
        return 0U;
    return (uint8_t)((g_audio_looper_source_mask[looper]
                      & (uint16_t)(1U << source)) != 0U);
}
