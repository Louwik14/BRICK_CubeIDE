#include "Track/control_routing.h"
#include <string.h>

static uint8_t g_looper_sources[BRICK_ENTITY_CAPACITY][BRICK_ENTITY_CAPACITY];

void control_routing_init(void)
{
    memset(g_looper_sources, 0, sizeof(g_looper_sources));
}
uint8_t control_routing_get_looper_source(brick_entity_id_t looper,brick_entity_id_t source){return(looper<BRICK_ENTITY_CAPACITY&&source<BRICK_ENTITY_CAPACITY)?g_looper_sources[looper][source]:0U;}
uint8_t control_routing_set_looper_source(brick_entity_id_t looper,brick_entity_id_t source,uint8_t enabled){if(looper>=BRICK_ENTITY_CAPACITY||source>=BRICK_ENTITY_CAPACITY||looper==source)return 0U;g_looper_sources[looper][source]=(enabled!=0U)?1U:0U;return 1U;}

uint8_t control_routing_apply_bulk(
    const uint8_t sources[BRICK_ENTITY_CAPACITY][BRICK_ENTITY_CAPACITY])
{
    if (sources == NULL) return 0U;
    for (uint8_t looper = 0U; looper < BRICK_ENTITY_CAPACITY; ++looper)
        for (uint8_t source = 0U; source < BRICK_ENTITY_CAPACITY; ++source)
            g_looper_sources[looper][source] = (uint8_t)(
                (source != looper) && (sources[looper][source] != 0U));
    return 1U;
}

uint8_t control_routing_clear_entity(brick_entity_id_t entity)
{
    if (entity >= BRICK_ENTITY_CAPACITY) return 0U;
    uint8_t next[BRICK_ENTITY_CAPACITY][BRICK_ENTITY_CAPACITY];
    memcpy(next, g_looper_sources, sizeof(next));
    for (uint8_t other = 0U; other < BRICK_ENTITY_CAPACITY; ++other)
    {
        next[entity][other] = 0U;
        next[other][entity] = 0U;
    }
    return control_routing_apply_bulk(next);
}
