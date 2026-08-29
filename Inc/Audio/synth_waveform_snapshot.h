#pragma once

#include <stdint.h>

#include "Track/entity_topology.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_WAVEFORM_POINT_COUNT 48U
#define SYNTH_WAVEFORM_OSC_COUNT   2U

typedef enum
{
    SYNTH_WAVEFORM_ENGINE_NONE = 0,
    SYNTH_WAVEFORM_ENGINE_WAVE,
    SYNTH_WAVEFORM_ENGINE_PRISM
} synth_waveform_engine_t;

typedef enum
{
    SYNTH_WAVEFORM_CAPTURE_IDLE = 0,
    SYNTH_WAVEFORM_CAPTURE_ARMED,
    SYNTH_WAVEFORM_CAPTURE_ACTIVE,
    SYNTH_WAVEFORM_CAPTURE_READY
} synth_waveform_capture_state_t;

typedef struct
{
    uint32_t generation;
    brick_entity_id_t entity_id;
    uint8_t engine;
    uint8_t osc_mask;
    uint8_t voice_instance;
    int8_t points[SYNTH_WAVEFORM_OSC_COUNT][SYNTH_WAVEFORM_POINT_COUNT];
} synth_waveform_snapshot_t;

void synth_waveform_init(void);
void synth_waveform_control_request(uint8_t enabled,
                                    brick_entity_id_t entity_id,
                                    synth_waveform_engine_t engine,
                                    uint8_t osc_mask);
uint8_t synth_waveform_control_read(synth_waveform_snapshot_t *out);

void synth_waveform_audio_begin_block(uint32_t frames);
uint8_t synth_waveform_audio_target_is(brick_entity_id_t entity_id,
                                       synth_waveform_engine_t engine);
void synth_waveform_audio_select_instance(brick_entity_id_t entity_id,
                                          synth_waveform_engine_t engine,
                                          uint8_t instance_id);
uint8_t synth_waveform_audio_instance_mask(uint8_t instance_id);
void synth_waveform_audio_restart_instance(uint8_t instance_id);
void synth_waveform_audio_capture_sample(uint8_t instance_id,
                                         uint8_t osc,
                                         uint32_t carrier_phase,
                                         float sample);

#ifdef __cplusplus
}
#endif
