#pragma once

#include <stdint.h>

#include "Param/param_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t param_stack_model_param_resolve(uint8_t model,
                                        uint8_t param_index,
                                        const char **out_label);
uint8_t param_stack_dynamic_param_info(param_id_t id,
                                       uint8_t *out_slot,
                                       uint8_t *out_param_index);
uint8_t param_stack_label_for_track_param(uint8_t track,
                                          param_id_t id,
                                          const char **out_label);

#ifdef __cplusplus
}
#endif
