#pragma once

#include <stdint.h>

#include "Mod/mod_matrix.h"
#include "Mod/mod_ramp.h"
#include "Param/param_ids.h"

typedef struct track_audio_runtime_ctx_s track_audio_runtime_ctx_t;

#ifdef __cplusplus
extern "C" {
#endif

uint8_t mod_matrix_poly_route_mask(uint8_t track);
uint8_t mod_matrix_has_any_configured_route(void);
uint8_t mod_matrix_track_has_configured_route(uint8_t track);
uint16_t mod_matrix_required_source_mask(uint8_t track);
void mod_matrix_process_track(uint8_t track, const track_audio_runtime_ctx_t *ctx,
                              const float source_values[MOD_MATRIX_SOURCE_COUNT],
                              const uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT]);
void mod_matrix_process_track_ramped(uint8_t track, const track_audio_runtime_ctx_t *ctx,
                                     const float source_start[MOD_MATRIX_SOURCE_COUNT],
                                     const float source_end[MOD_MATRIX_SOURCE_COUNT],
                                     const uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT],
                                     const uint8_t source_discontinuous[MOD_MATRIX_SOURCE_COUNT],
                                     uint32_t elapsed_frames);
void mod_matrix_process_poly_voice_ramped(uint8_t track, uint8_t voice_slot,
                                          const track_audio_runtime_ctx_t *ctx,
                                          const float source_start[MOD_MATRIX_SOURCE_COUNT],
                                          const float source_end[MOD_MATRIX_SOURCE_COUNT],
                                          const uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT]);
void mod_matrix_reset_poly_voice(uint8_t track, uint8_t voice_slot,
                                 const track_audio_runtime_ctx_t *ctx);
void mod_matrix_process_operators(uint8_t track,
                                  float source_values[MOD_MATRIX_SOURCE_COUNT],
                                  uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT],
                                  uint32_t elapsed_frames);
void mod_matrix_process_operators_ramped(uint8_t track,
                                         float source_start[MOD_MATRIX_SOURCE_COUNT],
                                         float source_end[MOD_MATRIX_SOURCE_COUNT],
                                         uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT],
                                         uint8_t source_discontinuous[MOD_MATRIX_SOURCE_COUNT],
                                         uint32_t elapsed_frames);
uint8_t mod_matrix_get_destination_ramp(uint8_t track, param_id_t destination,
                                        mod_destination_ramp_t *out_ramp);

#ifdef __cplusplus
}
#endif
