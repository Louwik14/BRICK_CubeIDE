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

typedef struct
{
    uint16_t table_global_slot;
    uint16_t table_wavetable_slot;
    uint32_t table_generation;
    float balance_gain;
    float balance_gain_current;
    float pos;
    float pos_smoothed;
    uint32_t phase;
    uint32_t phase_inc;
    uint32_t phase_inc_current;
    uint32_t mipmap_phase_inc;
    uint8_t mipmap_band;
} brick6_wave_runtime_osc_t;

typedef struct
{
    uint8_t active_note;
    uint8_t has_active_note;
    uint8_t gate;
    uint8_t trigger;
    float velocity;
} brick6_wave_runtime_voice_t;

void brick6_wave_runtime_init(void);
void brick6_wave_runtime_reset_instance(uint8_t instance_id);

void brick6_wave_runtime_set_osc_table_wavetable(uint8_t instance_id, uint8_t osc, uint16_t wavetable_slot);
void brick6_wave_runtime_set_osc_table_wavetable_generation(
    uint8_t instance_id, uint8_t osc, uint16_t wavetable_slot,
    uint32_t generation);
void brick6_wave_runtime_set_osc_pos(uint8_t instance_id, uint8_t osc, float pos);
void brick6_wave_runtime_set_volume(uint8_t instance_id, float volume);
void brick6_wave_runtime_set_balance(uint8_t instance_id, float balance);
void brick6_wave_runtime_set_tune(uint8_t instance_id, float semitones);
void brick6_wave_runtime_set_detune(uint8_t instance_id, float semitones);

void brick6_wave_runtime_note_on(uint8_t instance_id, uint8_t note, uint8_t velocity);
void brick6_wave_runtime_note_off(uint8_t instance_id, uint8_t note);
void brick6_wave_runtime_all_notes_off(uint8_t instance_id);
void brick6_wave_runtime_stop_wavetable_slot(uint16_t wavetable_slot,
                                            uint32_t generation);
void brick6_wave_runtime_clear_trigger(uint8_t instance_id);
uint8_t brick6_wave_runtime_prepare_block(uint8_t instance_id,
                                          uint32_t frames,
                                          uint8_t downstream_source_required);
uint8_t brick6_wave_runtime_render_instance(uint8_t instance_id, float *out_mono, uint32_t frames);

const brick6_wave_runtime_voice_t *brick6_wave_runtime_get_voice(uint8_t instance_id);
void brick6_wave_runtime_sync_voice(uint8_t track_instance, uint8_t voice_instance);
const brick6_wave_runtime_osc_t *brick6_wave_runtime_get_osc(uint8_t instance_id, uint8_t osc);

#ifdef __cplusplus
}
#endif
