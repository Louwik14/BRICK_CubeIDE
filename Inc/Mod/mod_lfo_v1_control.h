#pragma once

#include <stdint.h>

#include "Mod/mod_lfo_v1.h"
#include "Param/param_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

void mod_lfo_v1_init(void);
uint8_t mod_lfo_v1_reset_track(uint8_t track);
typedef struct
{
    float rate;
    float shape;
    float trigger;
    float phase;
} mod_lfo_control_value_t;

typedef struct
{
    mod_lfo_control_value_t lfo[MOD_LFO_COUNT_PER_TRACK];
} mod_lfo_control_bank_t;

uint8_t mod_lfo_v1_prepare_track_param(uint8_t track, uint8_t lfo_index,
                                       mod_lfo_param_t param, float value,
                                       uint8_t *out_owner,
                                       float *out_canonical_value);
uint8_t mod_lfo_v1_install_prepared_track_param(uint8_t owner,
                                                uint8_t lfo_index,
                                                mod_lfo_param_t param,
                                                float canonical_value);
uint8_t mod_lfo_v1_get_track_param(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param, float *out_value);
uint8_t mod_lfo_v1_restore_track(uint8_t track,
                                 const mod_lfo_control_bank_t *state);
uint8_t mod_lfo_v1_prepare_bank(const mod_lfo_control_bank_t *state,
                                mod_lfo_control_bank_t *out_canonical);

uint16_t mod_lfo_v1_dest_count(uint8_t track);
uint8_t mod_lfo_v1_dest_param_at(uint8_t track, uint16_t dest_index, param_id_t *out_param);
uint8_t mod_lfo_v1_dest_label(uint8_t track, uint16_t dest_index, char *out, uint32_t out_len);
uint8_t mod_lfo_v1_dest_short_label(uint8_t track, uint16_t dest_index, char *out, uint32_t out_len);
void mod_lfo_v1_invalidate_dest_cache_track(uint8_t track);
void mod_lfo_v1_invalidate_dest_cache_all(void);

uint8_t mod_lfo_v1_shape_is_random(uint8_t track, uint8_t lfo_index);
uint8_t mod_lfo_v1_waveform_point(uint8_t track, uint8_t lfo_index, uint8_t x, uint8_t width, int8_t *out_y_q7);

#ifdef __cplusplus
}
#endif
