#pragma once

#include <stdint.h>

#include "ControlRT/patch_preview_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

void preview_voice_dsp_reset(void);
uint8_t preview_voice_dsp_prepare(
    const patch_preview_filter_t *filter,
    const patch_preview_vca_t *vca);
void preview_voice_dsp_note_on(uint8_t note, uint8_t velocity);
void preview_voice_dsp_note_off(uint8_t note);
void preview_voice_dsp_prepare_mod_sources(uint32_t frames,
                                            float *filter_envelope,
                                            float *vca_envelope);
uint8_t preview_voice_dsp_apply_param(param_id_t parameter, float value);
uint8_t preview_voice_dsp_process(float *mono, uint32_t frames);
uint8_t preview_voice_dsp_process_stereo(float *left, float *right,
                                         uint32_t frames);

#ifdef __cplusplus
}
#endif
