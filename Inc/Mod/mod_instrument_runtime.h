#pragma once

#include <stdint.h>

#include "Audio/env_adsr.h"
#include "ControlRT/patch_preview_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t (*mod_instrument_apply_fn)(void *context,
                                           param_id_t parameter,
                                           float value);

typedef struct
{
    uint32_t phase;
    uint32_t rng;
    float sample_hold;
    float effective_rate;
    uint8_t active;
    uint8_t one_done;
} mod_instrument_lfo_runtime_t;

typedef struct
{
    param_id_t parameter;
    float sum;
    uint8_t valid;
} mod_instrument_destination_t;

typedef struct
{
    patch_preview_modulation_t config;
    mod_instrument_lfo_runtime_t lfo[3U];
    env_adsr_t envelope;
    float slew[2U];
    float route_base[8U];
    uint8_t slew_valid[2U];
    uint8_t route_base_valid[8U];
    uint8_t prepared;
} mod_instrument_runtime_t;

void mod_instrument_runtime_reset(mod_instrument_runtime_t *runtime);
uint8_t mod_instrument_runtime_prepare(mod_instrument_runtime_t *runtime,
    const patch_preview_modulation_t *configuration);
void mod_instrument_runtime_note_on(mod_instrument_runtime_t *runtime);
void mod_instrument_runtime_note_off(mod_instrument_runtime_t *runtime);
void mod_instrument_runtime_process(mod_instrument_runtime_t *runtime,
    uint32_t frames, float filter_envelope, float vca_envelope,
    mod_instrument_apply_fn apply, void *context);

#ifdef __cplusplus
}
#endif
