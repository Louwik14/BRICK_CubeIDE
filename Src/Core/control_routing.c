#include "Core/control_routing.h"
#include "Storage/memory_layout.h"
#include "stm32h7xx_hal.h"
#include <string.h>

typedef struct
{
    volatile uint32_t sequence;
    volatile uint16_t source_mask[BRICK_ENTITY_CAPACITY];
} control_routing_mailbox_t;

_Static_assert(sizeof(control_routing_mailbox_t) == 36U,
               "Looper routing mailbox ABI changed");

static uint8_t g_looper_sources[BRICK_ENTITY_CAPACITY][BRICK_ENTITY_CAPACITY];
D3_IPC static control_routing_mailbox_t g_control_routing_mailbox;
AUDIO_HOT static uint16_t g_audio_looper_source_mask[BRICK_ENTITY_CAPACITY];
AUDIO_HOT static uint32_t g_audio_looper_source_generation;

static void control_routing_publish(void)
{
    uint32_t sequence = g_control_routing_mailbox.sequence;
    if ((sequence & 1U) != 0U) ++sequence;
    g_control_routing_mailbox.sequence = sequence + 1U;
    __DMB();
    for (uint32_t looper = 0U; looper < BRICK_ENTITY_CAPACITY; ++looper)
    {
        uint16_t mask = 0U;
        for (uint32_t source = 0U; source < BRICK_ENTITY_CAPACITY; ++source)
        {
            if (g_looper_sources[looper][source] != 0U)
                mask |= (uint16_t)(1U << source);
        }
        g_control_routing_mailbox.source_mask[looper] = mask;
    }
    __DMB();
    g_control_routing_mailbox.sequence = sequence + 2U;
    if (g_control_routing_mailbox.sequence == 0U)
        g_control_routing_mailbox.sequence = 2U;
    __DMB();
}

void control_routing_init(void)
{
    memset(g_looper_sources, 0, sizeof(g_looper_sources));
    memset(g_audio_looper_source_mask, 0, sizeof(g_audio_looper_source_mask));
    g_audio_looper_source_generation = 0U;
    g_control_routing_mailbox.sequence = 0U;
    control_routing_publish();
}
uint8_t control_routing_get_looper_source(brick_entity_id_t looper,brick_entity_id_t source){return(looper<BRICK_ENTITY_CAPACITY&&source<BRICK_ENTITY_CAPACITY)?g_looper_sources[looper][source]:0U;}
uint8_t control_routing_set_looper_source(brick_entity_id_t looper,brick_entity_id_t source,uint8_t enabled){if(looper>=BRICK_ENTITY_CAPACITY||source>=BRICK_ENTITY_CAPACITY||looper==source)return 0U;g_looper_sources[looper][source]=(enabled!=0U)?1U:0U;control_routing_publish();return 1U;}

void control_routing_audio_apply_publication(void)
{
    for (uint8_t attempt = 0U; attempt < 2U; ++attempt)
    {
        const uint32_t before = g_control_routing_mailbox.sequence;
        if ((before == 0U) || (before == g_audio_looper_source_generation)
                || ((before & 1U) != 0U))
            return;
        __DMB();
        uint16_t next[BRICK_ENTITY_CAPACITY];
        for (uint32_t i = 0U; i < BRICK_ENTITY_CAPACITY; ++i)
            next[i] = g_control_routing_mailbox.source_mask[i];
        __DMB();
        const uint32_t after = g_control_routing_mailbox.sequence;
        if ((before == after) && ((after & 1U) == 0U))
        {
            memcpy(g_audio_looper_source_mask, next, sizeof(next));
            g_audio_looper_source_generation = after;
            return;
        }
    }
}

uint8_t control_routing_audio_get_looper_source(brick_entity_id_t looper,
                                                brick_entity_id_t source)
{
    if ((looper >= BRICK_ENTITY_CAPACITY) || (source >= BRICK_ENTITY_CAPACITY))
        return 0U;
    return (uint8_t)((g_audio_looper_source_mask[looper]
                      & (uint16_t)(1U << source)) != 0U);
}
