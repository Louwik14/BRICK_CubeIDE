#ifndef AUDIO_WAVEFORM_CAPTURE_H
#define AUDIO_WAVEFORM_CAPTURE_H

#include <stdint.h>
#include "Core/entity_topology.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_WAVEFORM_CAPTURE_GRAPH_X 1U
#define AUDIO_WAVEFORM_CAPTURE_GRAPH_WIDTH 94U
#define AUDIO_WAVEFORM_CAPTURE_COLUMNS AUDIO_WAVEFORM_CAPTURE_GRAPH_WIDTH
#define AUDIO_WAVEFORM_CAPTURE_FRAME_SAMPLES 1504U
#define AUDIO_WAVEFORM_CAPTURE_FRAME_AUDIO_SAMPLES 1504U
#define AUDIO_WAVEFORM_CAPTURE_FREE_RUN_SAMPLES (AUDIO_WAVEFORM_CAPTURE_FRAME_AUDIO_SAMPLES * 2U)

typedef struct
{
    uint32_t generation;
    brick_entity_id_t entity_id;
    uint8_t valid;
    uint8_t triggered;
    uint8_t trigger_fraction_q8;
    int8_t samples[AUDIO_WAVEFORM_CAPTURE_FRAME_SAMPLES];
} audio_waveform_capture_snapshot_t;

void audio_waveform_capture_init(void);
void audio_waveform_capture_set_entity(brick_entity_id_t entity_id);
void audio_waveform_capture_disable(void);
void audio_waveform_capture_set_fast_refresh(uint8_t fast);
brick_entity_id_t audio_waveform_capture_get_entity(void);
uint8_t audio_waveform_capture_needs_final_samples(void);
uint32_t audio_waveform_capture_get_generation(void);
void audio_waveform_capture_begin_block(brick_entity_id_t observed_entity);
void audio_waveform_capture_tap_reference_mono_block(const float *mono, uint32_t frames);
void audio_waveform_capture_tap_reference_stereo_block(const float *left, const float *right, uint32_t frames);
void audio_waveform_capture_tap_stereo_sample(float left, float right);
void audio_waveform_capture_tap_stereo_block(const float *left, const float *right, uint32_t frames);
uint8_t audio_waveform_capture_read(audio_waveform_capture_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
#endif
