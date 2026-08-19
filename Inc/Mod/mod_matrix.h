#pragma once

#include <stdint.h>

#include "Audio/audio_note_engine_adapter.h"
#include "Mod/mod_ramp.h"
#include "Mod/mod_destination_catalog.h"
#include "Param/param_store.h"
#include "Seq/seq_types.h"
#include "ui_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOD_MATRIX_SLOT_COUNT 8U
#define MOD_MATRIX_SOURCE_COUNT 11U

typedef enum
{
    MOD_MATRIX_SOURCE_NONE = 0,
    MOD_MATRIX_SOURCE_LFO1,
    MOD_MATRIX_SOURCE_LFO2,
    MOD_MATRIX_SOURCE_LFO3,
    MOD_MATRIX_SOURCE_ENV_FLT,
    MOD_MATRIX_SOURCE_ENV_VCA,
    MOD_MATRIX_SOURCE_ENV3,
    MOD_MATRIX_SOURCE_MULTI1,
    MOD_MATRIX_SOURCE_MULTI2,
    MOD_MATRIX_SOURCE_SLEW1,
    MOD_MATRIX_SOURCE_SLEW2
} mod_matrix_source_t;

typedef struct
{
    uint8_t enabled;
    uint8_t source;
    mod_destination_address_t destination;
    float depth;
} track_mod_matrix_slot_t;

typedef struct
{
    track_mod_matrix_slot_t slots[MOD_MATRIX_SLOT_COUNT];
    float base_value[MOD_MATRIX_SLOT_COUNT];
    float min_value[MOD_MATRIX_SLOT_COUNT];
    float max_value[MOD_MATRIX_SLOT_COUNT];
    uint8_t multi_source[2][2];
    uint8_t slew_source[2];
    float slew_amount[2];
    uint32_t slew_generation[2];
    uint8_t drum_md_slot_count;
} mod_matrix_control_snapshot_t;

_Static_assert(sizeof(mod_matrix_control_snapshot_t) <= 256U,
               "Matrix CONTROL/AUDIO snapshot must remain bounded and pointer-free");

void mod_matrix_set_defaults(track_mod_matrix_slot_t slots[MOD_MATRIX_SLOT_COUNT], uint8_t *selected_slot);
void mod_matrix_publish_control_snapshot_track(uint8_t track);
void mod_matrix_publish_control_snapshot_all(void);
uint8_t mod_matrix_poly_route_mask(uint8_t track);

uint8_t mod_matrix_set_selected_slot(uint8_t track, float value);
uint8_t mod_matrix_get_selected_slot(uint8_t track, float *out_value);
uint8_t mod_matrix_set_selected_slot_destination_index(uint8_t track, float value);
uint8_t mod_matrix_set_selected_slot_depth(uint8_t track, float value);
uint8_t mod_matrix_set_selected_slot_source(uint8_t track, float value);
uint8_t mod_matrix_get_selected_slot_destination_index(uint8_t track, float *out_value);
uint8_t mod_matrix_get_selected_slot_depth(uint8_t track, float *out_value);
uint8_t mod_matrix_get_selected_slot_source(uint8_t track, float *out_value);
uint8_t mod_matrix_set_slot_destination_index(uint8_t track, uint8_t slot, float value);
uint8_t mod_matrix_set_slot_depth(uint8_t track, uint8_t slot, float value);
uint8_t mod_matrix_set_slot_source(uint8_t track, uint8_t slot, float value);
uint8_t mod_matrix_set_slot_state(uint8_t track,
                                  uint8_t slot,
                                  uint8_t source,
                                  mod_destination_address_t destination,
                                  float depth,
                                  uint8_t enabled);
uint8_t mod_matrix_get_slot_destination_index(uint8_t track, uint8_t slot, float *out_value);
uint8_t mod_matrix_get_slot_depth(uint8_t track, uint8_t slot, float *out_value);
uint8_t mod_matrix_get_slot_source(uint8_t track, uint8_t slot, float *out_value);
uint8_t mod_matrix_set_multi_source(uint8_t track, uint8_t op, uint8_t input, float value);
uint8_t mod_matrix_get_multi_source(uint8_t track, uint8_t op, uint8_t input, float *out_value);
uint8_t mod_matrix_set_slew_source(uint8_t track, uint8_t op, float value);
uint8_t mod_matrix_get_slew_source(uint8_t track, uint8_t op, float *out_value);
uint8_t mod_matrix_set_slew_amount(uint8_t track, uint8_t op, float value);
uint8_t mod_matrix_get_slew_amount(uint8_t track, uint8_t op, float *out_value);
uint8_t mod_matrix_has_any_configured_route(void);
uint8_t mod_matrix_track_has_configured_route(uint8_t track);
uint8_t mod_matrix_track_has_configured_source(uint8_t track, mod_matrix_source_t source);
uint8_t mod_matrix_source_has_active_route(uint8_t track,
                                           mod_matrix_source_t source,
                                           ui_track_family_t family,
                                           ui_track_type_t type,
                                           const track_audio_runtime_ctx_t *ctx);
void mod_matrix_process_track(uint8_t track,
                              const track_audio_runtime_ctx_t *ctx,
                              const float source_values[MOD_MATRIX_SOURCE_COUNT],
                              const uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT]);
void mod_matrix_process_track_ramped(uint8_t track,
                                     const track_audio_runtime_ctx_t *ctx,
                                     const float source_start[MOD_MATRIX_SOURCE_COUNT],
                                     const float source_end[MOD_MATRIX_SOURCE_COUNT],
                                     const uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT],
                                     const uint8_t source_discontinuous[MOD_MATRIX_SOURCE_COUNT],
                                     uint32_t elapsed_frames);
void mod_matrix_process_poly_voice_ramped(uint8_t track,
                                          uint8_t voice_slot,
                                          const track_audio_runtime_ctx_t *ctx,
                                          const float source_start[MOD_MATRIX_SOURCE_COUNT],
                                          const float source_end[MOD_MATRIX_SOURCE_COUNT],
                                          const uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT]);
void mod_matrix_reset_poly_voice(uint8_t track,
                                 uint8_t voice_slot,
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
uint8_t mod_matrix_get_destination_ramp(uint8_t track,
                                        param_id_t destination,
                                        mod_destination_ramp_t *out_ramp);

#ifdef __cplusplus
}
#endif
