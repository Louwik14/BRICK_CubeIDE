#pragma once
#include "Mod/mod_destination_contract.h"
#include "Mod/mod_ramp.h"
#include "Track/track_types.h"
typedef struct track_audio_runtime_ctx_s track_audio_runtime_ctx_t;
void audio_mod_destination_catalog_reset_runtime(void);
uint8_t mod_destination_catalog_prepare(uint8_t target, param_id_t dest, const track_audio_runtime_ctx_t *ctx, const mod_destination_audio_models_t *models, mod_destination_prepared_t *out);
uint8_t mod_destination_catalog_apply_prepared(const mod_destination_prepared_t *prepared, float value);
uint8_t mod_destination_catalog_apply_ramp_prepared(const mod_destination_prepared_t *prepared, const mod_destination_ramp_t *ramp);
uint8_t mod_destination_catalog_apply_poly_prepared(const mod_destination_prepared_t *prepared, uint8_t voice_slot, float value);
uint8_t mod_destination_catalog_apply_rt(uint8_t track, param_id_t dest, const track_audio_runtime_ctx_t *ctx, float value);
uint8_t mod_destination_catalog_apply_ramp_rt(uint8_t track, param_id_t dest, const track_audio_runtime_ctx_t *ctx, const mod_destination_ramp_t *ramp);
uint8_t mod_destination_catalog_supported_audio(uint8_t track, param_id_t dest, track_family_t family, track_type_t type, const track_audio_runtime_ctx_t *ctx, const mod_destination_audio_models_t *models);
uint8_t mod_destination_catalog_apply_poly_voice_rt(uint8_t track, uint8_t voice_slot, param_id_t dest, const track_audio_runtime_ctx_t *ctx, float value);
uint8_t mod_destination_catalog_poly_voice_supported(param_id_t dest, const track_audio_runtime_ctx_t *ctx);
void audio_mod_destination_catalog_invalidate_runtime_value(uint8_t track, param_id_t id);
