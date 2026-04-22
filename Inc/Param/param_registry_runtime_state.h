#pragma once

#include "param_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

void param_registry_runtime_state_init(void);
uint8_t param_registry_runtime_cache_get(uint8_t track, param_id_t id, float *out_value);
void param_registry_runtime_cache_set(uint8_t track, param_id_t id, float value);
uint8_t param_registry_runtime_get_or_default(const param_desc_t *registry,
                                              param_id_t id,
                                              uint8_t track,
                                              float *out_value);
void param_registry_runtime_resync_lfo(uint8_t track, param_id_t id, float value);

#ifdef __cplusplus
}
#endif
