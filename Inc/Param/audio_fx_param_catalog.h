#pragma once

#include <stdint.h>

#include "Param/param_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_FX_PARAM_CATALOG_MAX_PARAMS 3U

uint8_t audio_fx_param_catalog_param_info(param_id_t id,
                                          uint8_t *out_slot,
                                          uint8_t *out_param_index);
uint8_t audio_fx_param_catalog_resolve(uint8_t model,
                                       uint8_t param_index,
                                       const char **out_name);
uint8_t audio_fx_param_catalog_count(uint8_t model);

#ifdef __cplusplus
}
#endif
