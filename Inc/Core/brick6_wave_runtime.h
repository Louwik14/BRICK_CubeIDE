/**
 * @file brick6_wave_runtime.h
 * @brief Track-aware user wavetable runtime for Synth/Wave.
 */

#pragma once

#include <stdint.h>

#include "Seq/seq_types.h"
#include "Sampler/sample_global_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BRICK6_WAVE_MAX_INSTANCES 8U
#define BRICK6_WAVE_VOICE_INSTANCE_COUNT 16U
#define BRICK6_WAVE_OSC_COUNT     2U

typedef enum
{
    BRICK6_WAVE_WARP_OFF = 0,
    BRICK6_WAVE_WARP_BEND,
    BRICK6_WAVE_WARP_SKEW,
    BRICK6_WAVE_WARP_FOLD,
    BRICK6_WAVE_WARP_REPEAT,
    BRICK6_WAVE_WARP_QUANTIZE,
    BRICK6_WAVE_WARP_TYPE_COUNT
} brick6_wave_warp_type_t;

typedef enum
{
    BRICK6_WAVE_POS_UPDATE_FULL = 0,
    BRICK6_WAVE_POS_UPDATE_8,
    BRICK6_WAVE_POS_UPDATE_16,
    BRICK6_WAVE_POS_UPDATE_32,
    BRICK6_WAVE_POS_UPDATE_COUNT
} brick6_wave_pos_update_t;

typedef struct
{
    uint16_t table_global_slot;
    uint16_t table_wavetable_slot;
    uint32_t table_generation;
    float level;
    float level_current;
    float tune_semitones;
    float pos;
    float start;
    float end;
    float pos_smoothed;
    uint32_t phase;
    uint32_t phase_inc;
    uint32_t phase_inc_current;
    uint32_t mipmap_effective_phase_inc;
    uint8_t mipmap_band;
    uint8_t warp_type;
    float warp_amt;
} brick6_wave_runtime_osc_t;

typedef struct
{
    uint8_t active_note;
    uint8_t has_active_note;
    uint8_t gate;
    uint8_t trigger;
    float velocity;
} brick6_wave_runtime_voice_t;

typedef struct
{
    uint8_t frame_interp_enabled;
    uint8_t sample_interp_enabled;
    uint8_t pos_smooth_enabled;
    brick6_wave_pos_update_t pos_update;
} brick6_wave_runtime_quality_t;

typedef struct
{
    uint64_t cycles[2];
    uint32_t blocks[2];
    uint32_t max_cycles[2];
} brick6_wave_runtime_dwt_stats_t;

void brick6_wave_runtime_init(void);
void brick6_wave_runtime_reset_instance(uint8_t instance_id);

void brick6_wave_runtime_set_osc_table_global(uint8_t instance_id, uint8_t osc, uint16_t global_slot);
void brick6_wave_runtime_set_osc_table_wavetable(uint8_t instance_id, uint8_t osc, uint16_t wavetable_slot);
void brick6_wave_runtime_set_osc_level(uint8_t instance_id, uint8_t osc, float level);
void brick6_wave_runtime_set_osc_tune(uint8_t instance_id, uint8_t osc, float semitones);
void brick6_wave_runtime_set_osc_pos(uint8_t instance_id, uint8_t osc, float pos);
void brick6_wave_runtime_set_osc_start(uint8_t instance_id, uint8_t osc, float start);
void brick6_wave_runtime_set_osc_end(uint8_t instance_id, uint8_t osc, float end);
void brick6_wave_runtime_set_osc_warp_type(uint8_t instance_id,
                                           uint8_t osc,
                                           brick6_wave_warp_type_t type);
void brick6_wave_runtime_set_osc_warp_amt(uint8_t instance_id, uint8_t osc, float amount);
void brick6_wave_runtime_set_frame_interp(uint8_t instance_id, uint8_t enabled);
void brick6_wave_runtime_set_sample_interp(uint8_t instance_id, uint8_t enabled);
void brick6_wave_runtime_set_pos_update(uint8_t instance_id, brick6_wave_pos_update_t update);
void brick6_wave_runtime_set_pos_smooth(uint8_t instance_id, uint8_t enabled);

void brick6_wave_runtime_note_on(uint8_t instance_id, uint8_t note, uint8_t velocity);
void brick6_wave_runtime_note_off(uint8_t instance_id, uint8_t note);
void brick6_wave_runtime_all_notes_off(uint8_t instance_id);
void brick6_wave_runtime_clear_trigger(uint8_t instance_id);
uint8_t brick6_wave_runtime_prepare_block(uint8_t instance_id,
                                          uint32_t frames,
                                          uint8_t downstream_source_required);
uint8_t brick6_wave_runtime_render_instance(uint8_t instance_id, float *out_mono, uint32_t frames);

const brick6_wave_runtime_voice_t *brick6_wave_runtime_get_voice(uint8_t instance_id);
void brick6_wave_runtime_sync_voice(uint8_t track_instance, uint8_t voice_instance);
const brick6_wave_runtime_osc_t *brick6_wave_runtime_get_osc(uint8_t instance_id, uint8_t osc);
const brick6_wave_runtime_quality_t *brick6_wave_runtime_get_quality(uint8_t instance_id);
void brick6_wave_runtime_dwt_enable(uint8_t enabled);
void brick6_wave_runtime_dwt_reset(void);
void brick6_wave_runtime_dwt_read(brick6_wave_runtime_dwt_stats_t *out);

#ifdef __cplusplus
}
#endif
