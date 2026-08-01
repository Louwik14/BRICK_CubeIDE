#ifndef NOTE_FX_PIPELINE_H
#define NOTE_FX_PIPELINE_H

#include <stdint.h>

void note_fx_pipeline_init(void);
uint8_t note_fx_pipeline_submit(uint8_t track, uint8_t note, uint8_t velocity,
                                uint8_t is_note_on, uint64_t sample_time);
void note_fx_pipeline_process(uint64_t block_start, uint16_t frames,
                              uint32_t samples_per_step_q16);
uint16_t note_fx_pipeline_frames_until_deadline(uint64_t block_start,
                                                uint16_t max_frames);
void note_fx_pipeline_cleanup_track(uint8_t track);
void note_fx_pipeline_cleanup_all(void);
void note_fx_pipeline_suspend_track(uint8_t track, uint8_t suspended);
void note_fx_pipeline_before_model_change(uint8_t track);
void note_fx_pipeline_on_base_param_change(uint8_t track);
void note_fx_pipeline_reset_runtime_overrides(uint8_t track);
void note_fx_pipeline_reset_all_runtime_overrides(void);
uint8_t note_fx_pipeline_apply_runtime_param(uint8_t track, uint8_t slot,
                                             uint8_t param, uint8_t value);
uint8_t note_fx_pipeline_release_runtime_param(uint8_t track, uint8_t slot,
                                               uint8_t param);

#endif
