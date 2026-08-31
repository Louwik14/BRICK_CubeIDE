#pragma once

#include <stddef.h>
#include <stdint.h>
#include "Param/param_ids.h"
#include "Track/entity_types.h"

typedef uint16_t mod_destination_address_t;
typedef struct { uint8_t audio_fx_model[2]; uint8_t prism_model[2];
    uint8_t stack_model[3]; uint8_t drum_md_slot_count;
} mod_destination_audio_models_t;
typedef enum {
    MOD_DEST_APPLY_NONE=0, MOD_DEST_APPLY_LFO_RATE, MOD_DEST_APPLY_MIX_LEVEL,
    MOD_DEST_APPLY_MIX_PAN, MOD_DEST_APPLY_MIX_SEND,
    MOD_DEST_APPLY_FILTER_CUTOFF, MOD_DEST_APPLY_FILTER_RESONANCE,
    MOD_DEST_APPLY_FILTER_EG_AMOUNT, MOD_DEST_APPLY_FILTER_ATTACK,
    MOD_DEST_APPLY_FILTER_DECAY, MOD_DEST_APPLY_FILTER_SUSTAIN,
    MOD_DEST_APPLY_FILTER_RELEASE, MOD_DEST_APPLY_VCA_ATTACK,
    MOD_DEST_APPLY_VCA_DECAY, MOD_DEST_APPLY_VCA_SUSTAIN,
    MOD_DEST_APPLY_VCA_RELEASE, MOD_DEST_APPLY_ENV3,
    MOD_DEST_APPLY_SAMPLER_GAIN, MOD_DEST_APPLY_SAMPLER_START,
    MOD_DEST_APPLY_SAMPLER_LENGTH, MOD_DEST_APPLY_SAMPLER_LOOP_START,
    MOD_DEST_APPLY_SAMPLER_TUNE, MOD_DEST_APPLY_LOOPER_XFADE,
    MOD_DEST_APPLY_PRISM_TUNE, MOD_DEST_APPLY_PRISM_DETUNE,
    MOD_DEST_APPLY_PRISM_DRIFT, MOD_DEST_APPLY_PRISM_PITCH_MOD,
    MOD_DEST_APPLY_PRISM_TIMBRE, MOD_DEST_APPLY_PRISM_MODULATION,
    MOD_DEST_APPLY_PRISM_COLOR, MOD_DEST_APPLY_PRISM_BALANCE,
    MOD_DEST_APPLY_FM_RATIO, MOD_DEST_APPLY_FM_BRIGHT,
    MOD_DEST_APPLY_FM_BODY, MOD_DEST_APPLY_FM_DETAIL,
    MOD_DEST_APPLY_FM_METAL, MOD_DEST_APPLY_FM_ENV,
    MOD_DEST_APPLY_STACK_LEVEL, MOD_DEST_APPLY_STACK_TUNE,
    MOD_DEST_APPLY_STACK_TIMBRE, MOD_DEST_APPLY_STACK_COLOR,
    MOD_DEST_APPLY_STACK_NOISE, MOD_DEST_APPLY_WAVE_POSITION,
    MOD_DEST_APPLY_WAVE_START, MOD_DEST_APPLY_WAVE_LENGTH,
    MOD_DEST_APPLY_WAVE_VOLUME, MOD_DEST_APPLY_WAVE_BALANCE,
    MOD_DEST_APPLY_WAVE_TUNE, MOD_DEST_APPLY_WAVE_DETUNE,
    MOD_DEST_APPLY_DRUM_PARAM, MOD_DEST_APPLY_AUDIO_FX_DELAY,
    MOD_DEST_APPLY_GENERIC
} mod_destination_apply_opcode_t;
#define MOD_DEST_PREPARED_RAMP_CONTINUOUS (1U << 0)
#define MOD_DEST_PREPARED_RAMP_SEGMENT (1U << 1)
typedef struct { uint16_t param; uint8_t opcode; uint8_t target;
    uint8_t endpoint; uint8_t subindex; uint8_t aux; uint8_t flags;
} mod_destination_prepared_t;
#define MOD_DESTINATION_NONE ((mod_destination_address_t)UINT16_MAX)
#define MOD_DESTINATION_PARAM_BITS 9U
#define MOD_DESTINATION_PARAM_MASK ((1U << MOD_DESTINATION_PARAM_BITS) - 1U)
static inline mod_destination_address_t mod_destination_address_make(
    uint8_t entity_id, param_id_t param)
{
    if ((entity_id >= BRICK_ENTITY_CAPACITY) || (param >= PARAM_COUNT)
        || ((uint16_t)param > MOD_DESTINATION_PARAM_MASK))
        return MOD_DESTINATION_NONE;
    return (mod_destination_address_t)(((uint16_t)entity_id
        << MOD_DESTINATION_PARAM_BITS) | (uint16_t)param);
}
static inline uint8_t mod_destination_address_resolve(
    mod_destination_address_t address, uint8_t *out_entity_id,
    param_id_t *out_param)
{
    const uint8_t entity=(uint8_t)(address >> MOD_DESTINATION_PARAM_BITS);
    const param_id_t param=(param_id_t)(address & MOD_DESTINATION_PARAM_MASK);
    if ((address == MOD_DESTINATION_NONE) || (entity >= BRICK_ENTITY_CAPACITY)
        || (param >= PARAM_COUNT) || (out_entity_id == NULL)
        || (out_param == NULL)) return 0U;
    *out_entity_id=entity; *out_param=param; return 1U;
}
