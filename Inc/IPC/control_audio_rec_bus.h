#pragma once

#include <stdint.h>
#include "IPC/audio_rec_bus_contract.h"

void control_audio_rec_bus_init(void);
uint8_t control_audio_rec_bus_publish(uint16_t source_entity_mask,
                                      audio_rec_bus_arm_t arm,
                                      uint8_t source_flags);
uint8_t control_audio_rec_bus_publish_at(uint16_t source_entity_mask,
                                         audio_rec_bus_arm_t arm,
                                         uint8_t source_flags,
                                         uint64_t sample_time);
