#pragma once

#include "IPC/audio_wave_table_projection.h"

void audio_wave_table_projection_init(void);
uint8_t audio_wave_table_projection_publish_track(
    uint8_t track, uint8_t osc, uint16_t logical_slot);
void audio_wave_table_projection_withdraw_slot(uint16_t wavetable_slot,
                                               uint32_t generation);
uint8_t audio_wave_table_projection_install_descriptor(
    const audio_wavetable_descriptor_t *descriptor);
