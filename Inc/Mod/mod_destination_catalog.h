#pragma once

#include <stdint.h>

#include "Core/track_runtime.h"
#include "Param/param_store.h"
#include "ui_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOD_DESTINATION_NONE ((param_id_t)PARAM_COUNT)

void mod_destination_catalog_init(void);
void mod_destination_catalog_reset_runtime(void);

uint8_t mod_destination_catalog_apply_rt(uint8_t track,
                                         param_id_t dest,
                                         const track_runtime_ctx_t *ctx,
                                         float value);
uint8_t mod_destination_catalog_supported_fast(uint8_t track,
                                               param_id_t dest,
                                               ui_track_family_t family,
                                               ui_track_type_t type,
                                               const track_runtime_ctx_t *ctx);

uint16_t mod_destination_catalog_count(uint8_t track);
param_id_t mod_destination_catalog_param_from_index(uint8_t track, uint16_t dest_index);
uint16_t mod_destination_catalog_index_from_param(uint8_t track, param_id_t dest);
uint8_t mod_destination_catalog_label(uint8_t track, uint16_t dest_index, char *out, uint32_t out_len);
uint8_t mod_destination_catalog_short_label(uint8_t track, uint16_t dest_index, char *out, uint32_t out_len);
void mod_destination_catalog_invalidate_track(uint8_t track);
void mod_destination_catalog_invalidate_all(void);
void mod_destination_catalog_invalidate_runtime_value(uint8_t track, param_id_t id);

#ifdef __cplusplus
}
#endif
