#pragma once

#include <stdint.h>

void control_routing_audio_init(void);
uint8_t control_routing_audio_set_mask(uint8_t looper, uint16_t source_mask);
uint8_t control_routing_audio_get_looper_source(uint8_t looper,
                                                uint8_t source);
