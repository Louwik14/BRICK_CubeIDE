#pragma once

#include "IPC/sampler_ram_audio_projection.h"

uint8_t sampler_ram_audio_projection_resolve(
    uint16_t global_slot,
    sampler_ram_audio_descriptor_t *out);
