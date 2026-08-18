#pragma once

#include <stdint.h>

#include "Param/param_store.h"
#include "Seq/seq_types.h"
#include "Mod/modulation_publication.h"

#ifdef __cplusplus
extern "C" {
#endif

struct track_audio_runtime_ctx_s;

typedef enum
{
    MOD_LFO_SHAPE_SINE = 0,
    MOD_LFO_SHAPE_TRIANGLE,
    MOD_LFO_SHAPE_SAW,
    MOD_LFO_SHAPE_SQUARE,
    MOD_LFO_SHAPE_RANDOM_SH,
    MOD_LFO_SHAPE_SINE_POS,
    MOD_LFO_SHAPE_TRIANGLE_POS,
    MOD_LFO_SHAPE_SQUARE_POS,
    MOD_LFO_SHAPE_REVERSE_SAW,
    MOD_LFO_SHAPE_COUNT
} mod_lfo_shape_t;

typedef enum
{
    MOD_LFO_TRIG_FREE = 0,
    MOD_LFO_TRIG_TRIG,
    MOD_LFO_TRIG_HOLD,
    MOD_LFO_TRIG_ONE,
    MOD_LFO_TRIG_POLY_TRIG,
    MOD_LFO_TRIG_POLY_HOLD,
    MOD_LFO_TRIG_POLY_ONE,
    MOD_LFO_TRIG_COUNT
} mod_lfo_trig_mode_t;

typedef enum
{
    MOD_LFO_PARAM_RATE = 0,
    MOD_LFO_PARAM_SHAPE,
    MOD_LFO_PARAM_TRIG,
    MOD_LFO_PARAM_PHASE,
    MOD_LFO_PARAM_COUNT
} mod_lfo_param_t;

#define MOD_LFO_COUNT_PER_TRACK 3U
#define MOD_LFO_POLY_SYNTH_SLOT_COUNT 16U
#define MOD_LFO_POLY_MULTI_SLOT_COUNT 8U
#define MOD_LFO_POLY_SLOT_COUNT (MOD_LFO_POLY_SYNTH_SLOT_COUNT + MOD_LFO_POLY_MULTI_SLOT_COUNT)
#define LFO_FREE_MAX_HZ 80.0f
#define MOD_LFO_SYNC_RATE_COUNT 16U

void mod_lfo_v1_init(void);
void mod_lfo_v1_reset_runtime(void);
uint8_t mod_lfo_v1_audio_config_get(uint8_t track,
                                    uint8_t lfo_index,
                                    modulation_lfo_publication_t *out);
void mod_lfo_v1_audio_apply_config(uint8_t track,
                                   uint8_t lfo_index,
                                   const modulation_lfo_publication_t *config);

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
void mod_lfo_v1_note_trigger(uint8_t track);
void mod_lfo_v1_poly_note_trigger(uint8_t track, uint8_t voice_slot);
void mod_lfo_v1_poly_voice_reset(uint8_t voice_slot);
void mod_lfo_v1_process_poly_voice(uint8_t track,
                                   uint8_t voice_slot,
                                   const struct track_audio_runtime_ctx_s *ctx,
                                   uint32_t frames);
void mod_lfo_v1_note_release(uint8_t track);
void mod_lfo_v1_all_notes_off(uint8_t track);
uint8_t mod_lfo_v1_shape_is_random(uint8_t track, uint8_t lfo_index);
mod_lfo_trig_mode_t mod_lfo_v1_effective_trig(uint8_t track, uint8_t lfo_index);
uint8_t mod_lfo_v1_waveform_point(uint8_t track, uint8_t lfo_index, uint8_t x, uint8_t width, int8_t *out_y_q7);

#ifdef __cplusplus
}
#endif
