#include "Core/control_routing.h"
#include <string.h>
static uint8_t g_looper_sources[BRICK_ENTITY_CAPACITY][BRICK_ENTITY_CAPACITY];
void control_routing_init(void){memset(g_looper_sources,0,sizeof(g_looper_sources));}
uint8_t control_routing_get_looper_source(brick_entity_id_t looper,brick_entity_id_t source){return(looper<BRICK_ENTITY_CAPACITY&&source<BRICK_ENTITY_CAPACITY)?g_looper_sources[looper][source]:0U;}
uint8_t control_routing_set_looper_source(brick_entity_id_t looper,brick_entity_id_t source,uint8_t enabled){if(looper>=BRICK_ENTITY_CAPACITY||source>=BRICK_ENTITY_CAPACITY||looper==source)return 0U;g_looper_sources[looper][source]=(enabled!=0U)?1U:0U;return 1U;}
