#pragma once

#include "Sampler/sample_play_plan.h"

uint8_t sample_classic_audio_projection_is_ready(uint16_t sample_id);
uint8_t sample_classic_audio_projection_resolve(
    uint16_t sample_id,
    sample_resolved_source_t *out_source);
