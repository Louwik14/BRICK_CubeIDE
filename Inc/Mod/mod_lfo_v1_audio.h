#pragma once

#include <stdint.h>

#include "Mod/mod_lfo_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

struct track_audio_runtime_ctx_s;

/* Must run during audio bootstrap; the IRQ path never performs lazy init. */
void mod_lfo_v1_audio_init(void);

uint8_t mod_lfo_v1_set_track_param_audio(uint8_t track, uint8_t lfo_index,
                                         mod_lfo_param_t param, float value);
uint8_t mod_lfo_v1_apply_track_param_temp(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param, float value);
uint8_t mod_lfo_v1_clear_track_param_temp_audio(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param);

void mod_lfo_v1_process_sample_all(void);
void mod_lfo_v1_process_block(uint32_t frames);
void mod_lfo_v1_note_trigger(uint8_t track);
void mod_lfo_v1_poly_note_trigger(uint8_t track, uint8_t voice_slot);
void mod_lfo_v1_poly_voice_reset(uint8_t voice_slot);
void mod_lfo_v1_process_poly_voice(uint8_t track,
                                   uint8_t voice_slot,
                                   const struct track_audio_runtime_ctx_s *ctx,
                                   uint32_t frames);
void mod_lfo_v1_note_release(uint8_t track);
mod_lfo_trig_mode_t mod_lfo_v1_effective_trig(uint8_t track, uint8_t lfo_index);

#ifdef __cplusplus
}
#endif
