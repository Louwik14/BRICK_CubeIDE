#pragma once

#include <stdint.h>

#include "Audio/audio_note_engine_adapter.h"
#include "Mod/mod_ramp.h"
#include "Param/param_store.h"
#include "ui_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t mod_destination_address_t;

typedef enum
{
    MOD_DEST_APPLY_NONE = 0,
    MOD_DEST_APPLY_LFO_RATE,
    MOD_DEST_APPLY_MIX_LEVEL,
    MOD_DEST_APPLY_MIX_PAN,
    MOD_DEST_APPLY_MIX_SEND,
    MOD_DEST_APPLY_FILTER_CUTOFF,
    MOD_DEST_APPLY_FILTER_RESONANCE,
    MOD_DEST_APPLY_FILTER_EG_AMOUNT,
    MOD_DEST_APPLY_FILTER_ATTACK,
    MOD_DEST_APPLY_FILTER_DECAY,
    MOD_DEST_APPLY_FILTER_SUSTAIN,
    MOD_DEST_APPLY_FILTER_RELEASE,
    MOD_DEST_APPLY_VCA_ATTACK,
    MOD_DEST_APPLY_VCA_DECAY,
    MOD_DEST_APPLY_VCA_SUSTAIN,
    MOD_DEST_APPLY_VCA_RELEASE,
    MOD_DEST_APPLY_ENV3,
    MOD_DEST_APPLY_SAMPLER_GAIN,
    MOD_DEST_APPLY_SAMPLER_START,
    MOD_DEST_APPLY_SAMPLER_END,
    MOD_DEST_APPLY_SAMPLER_LOOP_START,
    MOD_DEST_APPLY_SAMPLER_TUNE,
    MOD_DEST_APPLY_LOOPER_XFADE,
    MOD_DEST_APPLY_PRISM_TUNE,
    MOD_DEST_APPLY_PRISM_DETUNE,
    MOD_DEST_APPLY_PRISM_PITCH_MOD,
    MOD_DEST_APPLY_PRISM_TIMBRE,
    MOD_DEST_APPLY_PRISM_MODULATION,
    MOD_DEST_APPLY_PRISM_COLOR,
    MOD_DEST_APPLY_PRISM_BALANCE,
    MOD_DEST_APPLY_FM_RATIO,
    MOD_DEST_APPLY_FM_BRIGHT,
    MOD_DEST_APPLY_FM_BODY,
    MOD_DEST_APPLY_FM_DETAIL,
    MOD_DEST_APPLY_FM_METAL,
    MOD_DEST_APPLY_FM_ENV,
    MOD_DEST_APPLY_STACK_LEVEL,
    MOD_DEST_APPLY_STACK_TUNE,
    MOD_DEST_APPLY_STACK_TIMBRE,
    MOD_DEST_APPLY_STACK_COLOR,
    MOD_DEST_APPLY_STACK_NOISE,
    MOD_DEST_APPLY_WAVE_POSITION,
    MOD_DEST_APPLY_WAVE_VOLUME,
    MOD_DEST_APPLY_WAVE_BALANCE,
    MOD_DEST_APPLY_WAVE_TUNE,
    MOD_DEST_APPLY_WAVE_DETUNE,
    MOD_DEST_APPLY_DRUM_PARAM,
    MOD_DEST_APPLY_MIDI_CC,
    MOD_DEST_APPLY_AUDIO_FX_DELAY,
    MOD_DEST_APPLY_GENERIC
} mod_destination_apply_opcode_t;

#define MOD_DEST_PREPARED_RAMP_CONTINUOUS (1U << 0)
#define MOD_DEST_PREPARED_RAMP_SEGMENT    (1U << 1)

typedef struct
{
    uint16_t param;
    uint8_t opcode;
    uint8_t target;
    uint8_t endpoint;
    uint8_t subindex;
    uint8_t aux;
    uint8_t flags;
} mod_destination_prepared_t;

#define MOD_DESTINATION_NONE ((mod_destination_address_t)UINT16_MAX)
#define MOD_DESTINATION_PARAM_BITS 9U
#define MOD_DESTINATION_PARAM_MASK ((1U << MOD_DESTINATION_PARAM_BITS) - 1U)

mod_destination_address_t mod_destination_address_make(uint8_t entity_id,
                                                       param_id_t param);
uint8_t mod_destination_address_resolve(mod_destination_address_t address,
                                        uint8_t *out_entity_id,
                                        param_id_t *out_param);

void mod_destination_catalog_init(void);
void audio_mod_destination_catalog_reset_runtime(void);

uint8_t mod_destination_catalog_prepare(uint8_t target,
                                        param_id_t dest,
                                        const track_audio_runtime_ctx_t *ctx,
                                        mod_destination_prepared_t *out);
uint8_t mod_destination_catalog_apply_prepared(
    const mod_destination_prepared_t *prepared, float value);
uint8_t mod_destination_catalog_apply_ramp_prepared(
    const mod_destination_prepared_t *prepared,
    const mod_destination_ramp_t *ramp);
uint8_t mod_destination_catalog_apply_poly_prepared(
    const mod_destination_prepared_t *prepared,
    uint8_t voice_slot, float value);

uint8_t mod_destination_catalog_apply_rt(uint8_t track,
                                         param_id_t dest,
                                         const track_audio_runtime_ctx_t *ctx,
                                         float value);
uint8_t mod_destination_catalog_apply_ramp_rt(uint8_t track,
                                              param_id_t dest,
                                              const track_audio_runtime_ctx_t *ctx,
                                              const mod_destination_ramp_t *ramp);
uint8_t mod_destination_catalog_supported_fast(uint8_t track,
                                               param_id_t dest,
                                               ui_track_family_t family,
                                               ui_track_type_t type,
                                               const track_audio_runtime_ctx_t *ctx);
uint8_t mod_destination_catalog_supported_audio(uint8_t track,
                                                param_id_t dest,
                                                ui_track_family_t family,
                                                ui_track_type_t type,
                                                const track_audio_runtime_ctx_t *ctx,
                                                uint8_t drum_md_slot_count);
uint8_t mod_destination_catalog_apply_poly_voice_rt(uint8_t track,
                                                    uint8_t voice_slot,
                                                    param_id_t dest,
                                                    const track_audio_runtime_ctx_t *ctx,
                                                    float value);
uint8_t mod_destination_catalog_poly_voice_supported(param_id_t dest,
                                                      const track_audio_runtime_ctx_t *ctx);

uint16_t mod_destination_catalog_count(uint8_t track);
param_id_t mod_destination_catalog_param_from_index(uint8_t track, uint16_t dest_index);
uint16_t mod_destination_catalog_index_from_param(uint8_t track, param_id_t dest);
mod_destination_address_t mod_destination_catalog_address_from_index(uint8_t owner,
                                                                     uint16_t dest_index);
uint16_t mod_destination_catalog_index_from_address(uint8_t owner,
                                                    mod_destination_address_t address);
uint8_t mod_destination_catalog_label(uint8_t track, uint16_t dest_index, char *out, uint32_t out_len);
uint8_t mod_destination_catalog_short_label(uint8_t track, uint16_t dest_index, char *out, uint32_t out_len);
void mod_destination_catalog_invalidate_track(uint8_t track);
void mod_destination_catalog_invalidate_all(void);
void audio_mod_destination_catalog_invalidate_runtime_value(uint8_t track, param_id_t id);

#ifdef __cplusplus
}
#endif
