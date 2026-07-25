#pragma once

#include <stdint.h>

#include "Core/track_runtime.h"
#include "Param/param_store.h"
#include "Seq/seq_types.h"
#include "ui_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOD_MATRIX_SLOT_COUNT 8U
#define MOD_MATRIX_SOURCE_COUNT 6U

typedef enum
{
    MOD_MATRIX_SOURCE_NONE = 0,
    MOD_MATRIX_SOURCE_LFO1,
    MOD_MATRIX_SOURCE_LFO2,
    MOD_MATRIX_SOURCE_ENV_FLT,
    MOD_MATRIX_SOURCE_ENV_VCA,
    MOD_MATRIX_SOURCE_ENV3
} mod_matrix_source_t;

typedef struct
{
    uint8_t enabled;
    uint8_t source;
    uint16_t destination;
    float depth;
} track_mod_matrix_slot_t;

void mod_matrix_init(void);
void mod_matrix_reset_runtime(void);
void mod_matrix_set_defaults(track_mod_matrix_slot_t slots[MOD_MATRIX_SLOT_COUNT], uint8_t *selected_slot);

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
uint8_t mod_matrix_get_slot_destination_index(uint8_t track, uint8_t slot, float *out_value);
uint8_t mod_matrix_get_slot_depth(uint8_t track, uint8_t slot, float *out_value);
uint8_t mod_matrix_get_slot_source(uint8_t track, uint8_t slot, float *out_value);
uint8_t mod_matrix_source_has_active_route(uint8_t track,
                                           mod_matrix_source_t source,
                                           ui_track_family_t family,
                                           ui_track_type_t type,
                                           const track_runtime_ctx_t *ctx);
void mod_matrix_process_track(uint8_t track,
                              const track_runtime_ctx_t *ctx,
                              const float source_values[MOD_MATRIX_SOURCE_COUNT],
                              const uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT]);
void mod_matrix_release_track(uint8_t track,
                              ui_track_family_t family,
                              ui_track_type_t type,
                              const track_runtime_ctx_t *ctx);
void mod_matrix_resync_base_on_authoritative_write(uint8_t track, param_id_t id, float value);

#ifdef __cplusplus
}
#endif
