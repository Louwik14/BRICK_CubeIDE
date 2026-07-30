/**
 * @file brick6_stack_runtime.h
 * @brief Track-aware Stack runtime foundation.
 */

#pragma once

#include <stdint.h>
#include "Seq/seq_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BRICK6_STACK_MAX_INSTANCES SEQ_TRACK_COUNT
#define BRICK6_STACK_SLOT_COUNT 3U
#define BRICK6_STACK_RENDER_BLOCK_SIZE 24U

typedef enum
{
    BRICK6_STACK_MODEL_SINFD = 0,
    BRICK6_STACK_MODEL_SHAPE,
    BRICK6_STACK_MODEL_WAVETABLE,
    BRICK6_STACK_MODEL_SUB,
    BRICK6_STACK_MODEL_FM,
    BRICK6_STACK_MODEL_FEEDBACK_FM,
    BRICK6_STACK_MODEL_RING,
    BRICK6_STACK_MODEL_TRIPLE_SAW,
    BRICK6_STACK_MODEL_TRIPLE_SQUARE,
    BRICK6_STACK_MODEL_SWARM,
    BRICK6_STACK_MODEL_TRIFD,
    BRICK6_STACK_MODEL_COUNT
} brick6_stack_model_t;

typedef enum
{
    BRICK6_STACK_FAMILY_PHASE = 0,
    BRICK6_STACK_FAMILY_TABLE,
    BRICK6_STACK_FAMILY_ENSEMBLE
} brick6_stack_family_t;

typedef enum
{
    BRICK6_STACK_KERNEL_PHASE_BASIC = 0,
    BRICK6_STACK_KERNEL_PHASE_FOLD,
    BRICK6_STACK_KERNEL_WAVETABLE,
    BRICK6_STACK_KERNEL_SUB,
    BRICK6_STACK_KERNEL_FM,
    BRICK6_STACK_KERNEL_FEEDBACK_FM,
    BRICK6_STACK_KERNEL_RING,
    BRICK6_STACK_KERNEL_TRIPLE_ANALOG,
    BRICK6_STACK_KERNEL_SWARM,
    BRICK6_STACK_KERNEL_COUNT
} brick6_stack_kernel_id_t;

typedef struct
{
    uint8_t model;
    uint8_t family;
    uint8_t renderer_id;
    uint8_t level;
    int16_t tune_cents;
    uint8_t timbre;
    uint8_t color;
    uint8_t param3;
    uint8_t kernel_id;
    uint8_t kernel_state_size;
    uint16_t level_q15;
    uint16_t level_current_q15;
    uint16_t timbre_q15;
    uint16_t color_q15;
    uint16_t param3_q15;
    uint32_t phase;
    uint32_t phase_inc;
    uint32_t phase_inc_current;
    uint32_t phase2;
    uint32_t phase3;
    int16_t feedback_q15;
} stack_osc_slot_t;

typedef struct
{
    uint8_t active_note;
    uint8_t has_active_note;
    uint8_t gate;
    uint8_t trigger;
    uint16_t velocity_q15;
} brick6_stack_runtime_voice_t;

void brick6_stack_runtime_init(void);
void brick6_stack_runtime_reset_instance(uint8_t instance_id);

void brick6_stack_runtime_set_slot_level(uint8_t instance_id, uint8_t slot, float level);
void brick6_stack_runtime_set_slot_model(uint8_t instance_id, uint8_t slot, brick6_stack_model_t model);
void brick6_stack_runtime_set_slot_tune(uint8_t instance_id, uint8_t slot, float semitones);
void brick6_stack_runtime_set_slot_timbre(uint8_t instance_id, uint8_t slot, float timbre);
void brick6_stack_runtime_set_slot_color(uint8_t instance_id, uint8_t slot, float color);
void brick6_stack_runtime_set_slot_param3(uint8_t instance_id, uint8_t slot, float param3);
void brick6_stack_runtime_set_noise_level(uint8_t instance_id, float level);
void brick6_stack_runtime_set_osc_detune(uint8_t instance_id, float detune);
void brick6_stack_runtime_set_phase_reset(uint8_t instance_id, uint8_t enabled);

uint8_t brick6_stack_runtime_model_count(void);
const char *brick6_stack_runtime_model_name(brick6_stack_model_t model);
brick6_stack_family_t brick6_stack_runtime_model_family(brick6_stack_model_t model);
brick6_stack_kernel_id_t brick6_stack_runtime_model_kernel(brick6_stack_model_t model);

void brick6_stack_runtime_note_on(uint8_t instance_id, uint8_t note, uint8_t velocity);
void brick6_stack_runtime_note_off(uint8_t instance_id, uint8_t note);
void brick6_stack_runtime_all_notes_off(uint8_t instance_id);
uint8_t brick6_stack_runtime_submit_note_on(uint8_t instance_id, uint8_t note, uint8_t velocity);
uint8_t brick6_stack_runtime_submit_note_off(uint8_t instance_id, uint8_t note);
uint8_t brick6_stack_runtime_submit_all_notes_off(uint8_t instance_id);
uint8_t brick6_stack_runtime_submit_reset_instance(uint8_t instance_id);
uint8_t brick6_stack_runtime_submit_slot_model(uint8_t instance_id, uint8_t slot, brick6_stack_model_t model);
uint8_t brick6_stack_runtime_submit_slot_level(uint8_t instance_id, uint8_t slot, float level);
uint8_t brick6_stack_runtime_submit_slot_tune(uint8_t instance_id, uint8_t slot, float semitones);
uint8_t brick6_stack_runtime_submit_slot_timbre(uint8_t instance_id, uint8_t slot, float timbre);
uint8_t brick6_stack_runtime_submit_slot_color(uint8_t instance_id, uint8_t slot, float color);
uint8_t brick6_stack_runtime_submit_slot_param3(uint8_t instance_id, uint8_t slot, float param3);
uint8_t brick6_stack_runtime_submit_noise_level(uint8_t instance_id, float level);
uint8_t brick6_stack_runtime_submit_osc_detune(uint8_t instance_id, float detune);
uint8_t brick6_stack_runtime_submit_phase_reset(uint8_t instance_id, uint8_t enabled);
void brick6_stack_runtime_process_commands_from_audio(void);
void brick6_stack_runtime_clear_trigger(uint8_t instance_id);
void brick6_stack_runtime_render_instance(uint8_t instance_id, float *out_mono, uint32_t frames);

const brick6_stack_runtime_voice_t *brick6_stack_runtime_get_voice(uint8_t instance_id);

#ifdef __cplusplus
}
#endif
