#pragma once

#include <stdint.h>

#include "Param/param_store.h"
#include "Seq/seq_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    MOD_LFO_SHAPE_SINE = 0,
    MOD_LFO_SHAPE_TRIANGLE,
    MOD_LFO_SHAPE_SAW,
    MOD_LFO_SHAPE_SQUARE,
    MOD_LFO_SHAPE_RANDOM_SH,
    MOD_LFO_SHAPE_COUNT
} mod_lfo_shape_t;

typedef enum
{
    MOD_LFO_PARAM_DEST = 0,
    MOD_LFO_PARAM_RATE,
    MOD_LFO_PARAM_DEPTH,
    MOD_LFO_PARAM_SHAPE,
    MOD_LFO_PARAM_COUNT
} mod_lfo_param_t;

void mod_lfo_v1_init(void);
void mod_lfo_v1_reset_runtime(void);

uint8_t mod_lfo_v1_set_track_param(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param, float value);
uint8_t mod_lfo_v1_get_track_param(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param, float *out_value);
uint8_t mod_lfo_v1_apply_track_param_temp(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param, float value);
void mod_lfo_v1_clear_track_param_temp(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param);
void mod_lfo_v1_resync_base_on_authoritative_write(uint8_t track, param_id_t id, float value);

uint16_t mod_lfo_v1_dest_count(uint8_t track);
uint8_t mod_lfo_v1_dest_param_at(uint8_t track, uint16_t dest_index, param_id_t *out_param);
uint8_t mod_lfo_v1_dest_label(uint8_t track, uint16_t dest_index, char *out, uint32_t out_len);
uint8_t mod_lfo_v1_dest_short_label(uint8_t track, uint16_t dest_index, char *out, uint32_t out_len);
void mod_lfo_v1_invalidate_dest_cache_track(uint8_t track);
void mod_lfo_v1_invalidate_dest_cache_all(void);

void mod_lfo_v1_process_sample_all(void);
void mod_lfo_v1_process_block(uint32_t frames);

#ifdef __cplusplus
}
#endif
