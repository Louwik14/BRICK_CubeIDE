#ifndef CONTROL_ROUTING_H
#define CONTROL_ROUTING_H
#include <stdint.h>
#include "Track/entity_topology.h"
void control_routing_init(void);
uint8_t control_routing_get_looper_source(brick_entity_id_t looper,brick_entity_id_t source);
uint8_t control_routing_set_looper_source(brick_entity_id_t looper,brick_entity_id_t source,uint8_t enabled);
uint8_t control_routing_apply_bulk(
    const uint8_t sources[BRICK_ENTITY_CAPACITY][BRICK_ENTITY_CAPACITY]);
uint8_t control_routing_clear_entity(brick_entity_id_t entity);
#endif
