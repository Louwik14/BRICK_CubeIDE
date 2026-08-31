#pragma once

#include "IPC/audio_waveform_contract.h"
#include "Track/entity_types.h"

void audio_waveform_capture_init(void);
void audio_waveform_capture_audio_apply_control(brick_entity_id_t entity_id,
                                                uint8_t enabled,
                                                uint8_t fast_refresh);
brick_entity_id_t audio_waveform_capture_get_entity(void);
uint8_t audio_waveform_capture_needs_final_samples(void);
void audio_waveform_capture_begin_block(brick_entity_id_t observed_entity);
void audio_waveform_capture_tap_reference_mono_block(const float *mono, uint32_t frames);
void audio_waveform_capture_tap_reference_stereo_block(const float *left, const float *right, uint32_t frames);
void audio_waveform_capture_tap_stereo_sample(float left, float right);
void audio_waveform_capture_tap_stereo_block(const float *left, const float *right, uint32_t frames);
