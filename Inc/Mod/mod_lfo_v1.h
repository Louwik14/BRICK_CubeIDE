#pragma once

#include <stdint.h>

#include "Mod/mod_lfo_types.h"
#include "Param/param_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

struct track_audio_runtime_ctx_s;

void mod_lfo_v1_init(void);

uint8_t mod_lfo_v1_set_track_param(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param, float value);
uint8_t mod_lfo_v1_set_track_param_audio(uint8_t track, uint8_t lfo_index,
                                         mod_lfo_param_t param, float value);
uint8_t mod_lfo_v1_get_track_param(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param, float *out_value);
uint8_t mod_lfo_v1_apply_track_param_temp(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param, float value);
uint8_t mod_lfo_v1_clear_track_param_temp_audio(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param);

uint16_t mod_lfo_v1_dest_count(uint8_t track);
uint8_t mod_lfo_v1_dest_param_at(uint8_t track, uint16_t dest_index, param_id_t *out_param);
uint8_t mod_lfo_v1_dest_label(uint8_t track, uint16_t dest_index, char *out, uint32_t out_len);
uint8_t mod_lfo_v1_dest_short_label(uint8_t track, uint16_t dest_index, char *out, uint32_t out_len);
void mod_lfo_v1_invalidate_dest_cache_track(uint8_t track);
void mod_lfo_v1_invalidate_dest_cache_all(void);

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
uint8_t mod_lfo_v1_shape_is_random(uint8_t track, uint8_t lfo_index);
mod_lfo_trig_mode_t mod_lfo_v1_effective_trig(uint8_t track, uint8_t lfo_index);
uint8_t mod_lfo_v1_waveform_point(uint8_t track, uint8_t lfo_index, uint8_t x, uint8_t width, int8_t *out_y_q7);

#ifdef __cplusplus
}
#endif
