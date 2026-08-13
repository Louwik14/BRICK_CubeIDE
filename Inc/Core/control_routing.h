#ifndef CONTROL_ROUTING_H
#define CONTROL_ROUTING_H
#include <stdint.h>
#include "Core/entity_topology.h"
void control_routing_init(void);
uint8_t control_routing_get_looper_source(brick_entity_id_t looper,brick_entity_id_t source);
uint8_t control_routing_set_looper_source(brick_entity_id_t looper,brick_entity_id_t source,uint8_t enabled);
#endif
