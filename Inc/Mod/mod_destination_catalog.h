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

#define MOD_DESTINATION_NONE ((mod_destination_address_t)UINT16_MAX)
#define MOD_DESTINATION_PARAM_BITS 9U
#define MOD_DESTINATION_PARAM_MASK ((1U << MOD_DESTINATION_PARAM_BITS) - 1U)

mod_destination_address_t mod_destination_address_make(uint8_t entity_id,
                                                       param_id_t param);
uint8_t mod_destination_address_resolve(mod_destination_address_t address,
                                        uint8_t *out_entity_id,
                                        param_id_t *out_param);

void mod_destination_catalog_init(void);
void mod_destination_catalog_reset_runtime(void);

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
void mod_destination_catalog_invalidate_runtime_value(uint8_t track, param_id_t id);

#ifdef __cplusplus
}
#endif
