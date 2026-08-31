#pragma once

#include "IPC/multi_sample_audio_projection.h"

uint8_t multi_sample_audio_projection_is_ready(uint16_t instrument_id);
uint8_t multi_sample_audio_projection_resolve(
    uint16_t instrument_id,
    uint8_t note,
    uint8_t velocity,
    multi_sample_audio_source_t *out_source);
