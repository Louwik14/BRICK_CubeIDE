/**
 * @file multi_voice_dsp.h
 * @brief Static per-voice DSP storage for the Multi sampler.
 *
 * This pool owns the per-voice filter state and its runtime ownership data.
 * VCA state remains reserved for the later per-voice VCA step.
 */

#pragma once

#include <stdint.h>

#include "Audio/env_adsr.h"
#include "Audio/fx_biquad_filter.h"
#include "Audio/fx_deluge_filter.h"
#include "Audio/mixer.h"
#include "Core/brick6_sampler_multi_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MULTI_VOICE_DSP_SLOT_COUNT BRICK6_SAMPLER_MULTI_MAX_VOICES
#define MULTI_VOICE_DSP_SLOT_INDEX_INVALID UINT8_MAX
#define MULTI_VOICE_DSP_DEFAULT_SAMPLE_RATE (48000.0f)
#define MULTI_VOICE_DSP_SLOT_SIZE_BYTES (288U)

typedef enum
{
    MULTI_VOICE_DSP_FORMAT_MONO = 0U,
    MULTI_VOICE_DSP_FORMAT_STEREO
} multi_voice_dsp_format_t;

typedef union
{
    struct
    {
        fx_biquad_filter_t biquad;
    } stereo;
    struct
    {
        fx_biquad_filter_mono_t biquad;
    } mono;
} multi_voice_dsp_filter_state_t;

typedef struct __attribute__((aligned(32))) multi_voice_dsp_slot_t
{
    union
    {
        multi_voice_dsp_filter_state_t filter;
        fx_deluge_filter_t deluge;
    };
    env_adsr_t filter_env;
    env_adsr_t vca_env;
    uint32_t owner_generation;
    float sample_rate;
    float cutoff_hz;
    float cutoff_target_hz;
    float cutoff_mod_hz;
    float cutoff_mod_target_hz;
    float resonance;
    float resonance_target;
    float eg_amount;
    float keytrack;
    float keytrack_ratio;
    float keytrack_ratio_target;
    float morph;
    float morph_target;
    float morph_step;
    uint32_t filter_config_version;
    uint8_t owner_voice_index;
    uint8_t state;
    uint8_t format;
    uint8_t vca_gate;
    uint8_t vca_enabled;
    uint8_t filter_retrigger_hard;
    uint8_t vca_retrigger_hard;
    uint8_t current_note;
    uint8_t filter_mode;
    uint8_t morph_ramp_remaining;
} multi_voice_dsp_slot_t;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(MULTI_VOICE_DSP_SLOT_COUNT == 8U,
               "Multi DSP pool must contain exactly eight slots");
_Static_assert(sizeof(multi_voice_dsp_slot_t) == MULTI_VOICE_DSP_SLOT_SIZE_BYTES,
               "Multi DSP slot size changed; remeasure before accepting it");
_Static_assert(__alignof__(multi_voice_dsp_slot_t) >= 32U,
               "Multi DSP slot must remain 32-byte aligned");
#endif

void multi_voice_dsp_init(void);
void multi_voice_dsp_reset(void);
uint8_t multi_voice_dsp_acquire(brick6_sampler_multi_voice_handle_t handle,
                                multi_voice_dsp_format_t format,
                                float sample_rate,
                                uint8_t *out_slot_index);
uint8_t multi_voice_dsp_release(uint8_t slot_index,
                                brick6_sampler_multi_voice_handle_t handle);
uint8_t multi_voice_dsp_find_slot(brick6_sampler_multi_voice_handle_t handle,
                                  uint8_t *out_slot_index);
multi_voice_dsp_slot_t *multi_voice_dsp_get(uint8_t slot_index);
const multi_voice_dsp_slot_t *multi_voice_dsp_get_const(uint8_t slot_index);
uint8_t multi_voice_dsp_validate_ownership(void);

#ifdef __cplusplus
}
#endif
