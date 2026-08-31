#pragma once

#include "IPC/synth_waveform_contract.h"
#include "Track/entity_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SYNTH_WAVEFORM_CAPTURE_IDLE = 0,
    SYNTH_WAVEFORM_CAPTURE_ARMED,
    SYNTH_WAVEFORM_CAPTURE_ACTIVE,
    SYNTH_WAVEFORM_CAPTURE_READY
} synth_waveform_capture_state_t;

void synth_waveform_init(void);
uint8_t synth_waveform_audio_apply_request(brick_entity_id_t entity_id,
                                           synth_waveform_engine_t engine,
                                           uint8_t osc_mask);
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
