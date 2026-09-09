#pragma once

#include "IPC/audio_wave_table_projection.h"
#include "Sampler/wavetable_pool.h"

void audio_wave_table_projection_init(void);
uint8_t audio_wave_table_projection_build_descriptor(
    uint16_t wavetable_slot,
    const wavetable_slot_t *slot,
    audio_wavetable_descriptor_t *out);
void audio_wave_table_projection_install_prepared(
    const audio_wavetable_descriptor_t *descriptor);
uint8_t audio_wave_table_projection_publish_track(
    uint8_t track, uint8_t osc, uint16_t logical_slot);
uint8_t audio_wave_table_projection_clear_track(uint8_t track, uint8_t osc);
void audio_wave_table_projection_withdraw_slot(uint16_t wavetable_slot,
                                               uint32_t generation);
uint8_t audio_wave_table_projection_install_descriptor(
    const audio_wavetable_descriptor_t *descriptor);
