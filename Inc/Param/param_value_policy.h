#pragma once

#include <stdint.h>

#include "Param/param_store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef float (*param_value_transform_fn)(float value, float min_value, float max_value);

typedef enum
{
    PARAM_AUTOMATION_DISCRETE_STEP = 0,
    PARAM_AUTOMATION_LINEAR_U16
} param_automation_policy_t;

typedef struct
{
    param_value_transform_fn canonical_to_display;
    param_value_transform_fn display_to_canonical;
    float normal_step_display;
    float fine_step_display;
    param_automation_policy_t automation;
} param_value_policy_t;

struct param_desc;

float param_value_identity(float value, float min_value, float max_value);
float param_value_percent127_to_display(float value, float min_value, float max_value);
float param_value_percent127_to_canonical(float value, float min_value, float max_value);
float param_value_seconds_to_milliseconds(float value, float min_value, float max_value);
float param_value_milliseconds_to_seconds(float value, float min_value, float max_value);
float param_value_prism_tune_to_display(float value, float min_value, float max_value);
float param_value_prism_tune_to_canonical(float value, float min_value, float max_value);

param_value_policy_t param_value_policy_resolve(param_id_t id, uint8_t track);
const char *param_value_policy_display_unit(param_id_t id, uint8_t track);
float param_value_policy_canonical_to_display(param_id_t id, uint8_t track, float value);
float param_value_policy_display_to_canonical(param_id_t id, uint8_t track, float value);
float param_value_policy_canonicalize(param_id_t id, uint8_t track, float value);
float param_value_policy_apply_delta(param_id_t id,
                                     uint8_t track,
                                     float canonical_value,
                                     int16_t delta,
                                     uint8_t fine,
                                     float min_value,
                                     float max_value);
uint16_t param_value_policy_encode_u16(const struct param_desc *desc, float value);
float param_value_policy_decode_u16(const struct param_desc *desc, uint16_t value);

#ifdef __cplusplus
}
#endif
