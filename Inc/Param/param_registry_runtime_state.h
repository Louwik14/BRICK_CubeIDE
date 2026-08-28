#pragma once

#include "param_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

void param_registry_control_values_init(void);
uint8_t param_registry_control_value_get(uint8_t track, param_id_t id,
                                         float *out_value);
void param_registry_control_value_set(uint8_t track, param_id_t id, float value);
void param_registry_control_values_reset_track(uint8_t track);

#ifdef __cplusplus
}
#endif
