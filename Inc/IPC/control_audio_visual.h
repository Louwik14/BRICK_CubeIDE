#pragma once

#include <stdint.h>
#include "IPC/synth_waveform_contract.h"
#include "Track/entity_types.h"

uint8_t control_audio_visual_waveform_request(brick_entity_id_t entity,
                                               uint8_t enabled,
                                               uint8_t fast_refresh);
uint8_t control_audio_visual_synth_request(uint8_t enabled,
                                           brick_entity_id_t entity,
                                           synth_waveform_engine_t engine,
                                           uint8_t osc_mask);
