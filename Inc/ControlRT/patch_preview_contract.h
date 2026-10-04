#ifndef PATCH_PREVIEW_CONTRACT_H
#define PATCH_PREVIEW_CONTRACT_H

#include <stdint.h>

#include "ControlRT/fm_dsp_projection.h"
#include "Param/param_ids.h"

#define PATCH_PREVIEW_PUBLICATION_SLOT_COUNT 2U

typedef enum
{
    PATCH_PREVIEW_COMMAND_PREPARE = 0U,
    PATCH_PREVIEW_COMMAND_NOTE_ON,
    PATCH_PREVIEW_COMMAND_NOTE_OFF,
    PATCH_PREVIEW_COMMAND_STOP
} patch_preview_command_t;

typedef enum
{
    PATCH_PREVIEW_ENGINE_NONE = 0U,
    PATCH_PREVIEW_ENGINE_FM,
    PATCH_PREVIEW_ENGINE_PRISM,
    PATCH_PREVIEW_ENGINE_STACK,
    PATCH_PREVIEW_ENGINE_TB303,
    PATCH_PREVIEW_ENGINE_ACID
} patch_preview_engine_t;

typedef struct
{
    float rate;
    float phase_offset;
    uint8_t shape;
    uint8_t trigger;
} patch_preview_mod_lfo_t;

typedef struct
{
    uint8_t source;
    param_id_t destination;
    float depth;
    float base_value;
    uint8_t enabled;
    uint8_t base_valid;
} patch_preview_mod_route_t;

typedef struct
{
    patch_preview_mod_lfo_t lfo[3U];
    uint8_t multi_source[2U][2U];
    uint8_t slew_source[2U];
    float slew_amount[2U];
    float env_attack;
    float env_decay;
    float env_sustain;
    float env_release;
    uint8_t env_retrigger_hard;
    patch_preview_mod_route_t route[8U];
} patch_preview_modulation_t;

typedef struct
{
    float morph, cutoff, resonance, eg_amount;
    float attack, decay, sustain, release;
    float keytrack, env_reset, env_delay, retrigger;
} patch_preview_filter_t;

typedef struct
{
    float attack, decay, sustain, release, filter_mode, retrigger;
} patch_preview_vca_t;

#define PATCH_PREVIEW_ENGINE_PARAM_CAPACITY 20U

typedef struct
{
    param_id_t parameter;
    float value;
} patch_preview_engine_param_t;

typedef struct
{
    patch_preview_engine_t engine;
    track_tone_fm_base_voice_t fm_base;
    track_tone_fm_macros_t fm_macros;
    patch_preview_engine_param_t engine_param[PATCH_PREVIEW_ENGINE_PARAM_CAPACITY];
    uint8_t engine_param_count;
    patch_preview_filter_t filter;
    patch_preview_vca_t vca;
    patch_preview_modulation_t modulation;
    uint8_t modulation_present;
    uint32_t generation;
} patch_preview_publication_t;

extern patch_preview_publication_t
    g_patch_preview_publication[PATCH_PREVIEW_PUBLICATION_SLOT_COUNT];
extern volatile uint32_t g_patch_preview_audio_consumed_generation;

#endif /* PATCH_PREVIEW_CONTRACT_H */
