#include "Track/control_routing.h"
#include "IPC/control_audio_command.h"
#include "ControlRT/control_rt_publication.h"
#include <string.h>

static uint8_t g_looper_sources[BRICK_ENTITY_CAPACITY][BRICK_ENTITY_CAPACITY];

static uint8_t control_routing_publish(brick_entity_id_t looper)
{
    uint16_t mask = 0U;
    for (uint8_t source = 0U; source < BRICK_ENTITY_CAPACITY; ++source)
        if (g_looper_sources[looper][source] != 0U)
            mask |= (uint16_t)(1U << source);
    return control_rt_publish_param_now(looper,
        CONTROL_AUDIO_PARAM_LOOPER_ROUTE,
        mask, 0U);
}

uint8_t control_routing_get_looper_source(brick_entity_id_t looper,brick_entity_id_t source){return(looper<BRICK_ENTITY_CAPACITY&&source<BRICK_ENTITY_CAPACITY)?g_looper_sources[looper][source]:0U;}
uint8_t control_routing_set_looper_source(brick_entity_id_t looper,brick_entity_id_t source,uint8_t enabled){if(looper>=BRICK_ENTITY_CAPACITY||source>=BRICK_ENTITY_CAPACITY||looper==source)return 0U;const uint8_t next=(enabled!=0U)?1U:0U;if(g_looper_sources[looper][source]==next)return 1U;const uint8_t old=g_looper_sources[looper][source];g_looper_sources[looper][source]=next;if(control_routing_publish(looper)==0U){g_looper_sources[looper][source]=old;return 0U;}return 1U;}

uint8_t control_routing_apply_bulk(
    const uint8_t sources[BRICK_ENTITY_CAPACITY][BRICK_ENTITY_CAPACITY])
{
    if (sources == NULL) return 0U;
    control_audio_command_t commands[BRICK_ENTITY_CAPACITY];
    uint8_t command_count = 0U;
    for (uint8_t looper = 0U; looper < BRICK_ENTITY_CAPACITY; ++looper)
    {
        uint16_t mask = 0U;
        for (uint8_t source = 0U; source < BRICK_ENTITY_CAPACITY; ++source)
        {
            if ((source != looper) && (sources[looper][source] != 0U))
                mask |= (uint16_t)(1U << source);
        }
        uint16_t old_mask = 0U;
        for (uint8_t source = 0U; source < BRICK_ENTITY_CAPACITY; ++source)
            if (g_looper_sources[looper][source] != 0U)
                old_mask |= (uint16_t)(1U << source);
        if (old_mask == mask)
            continue;
        commands[command_count++] = (control_audio_command_t){
            .value = mask,
            .id = CONTROL_AUDIO_PARAM_LOOPER_ROUTE,
            .entity = looper,
            .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(
                CONTROL_AUDIO_COMMAND_PARAM, 0U)
        };
    }
    if ((command_count != 0U)
            && (control_rt_publish_batch_now(commands, command_count) == 0U))
        return 0U;
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
