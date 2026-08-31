#pragma once

#include <stdint.h>

#include "IPC/audio_wave_table_projection.h"

uint8_t audio_wavetable_registry_resolve(uint16_t wavetable_slot,
                                         uint32_t generation,
                                         audio_wavetable_descriptor_t *out);
