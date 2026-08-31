#pragma once

#include <stdint.h>
#include "Param/param_ids.h"

typedef enum
{
    PARAM_SPEC_FLOAT,
    PARAM_SPEC_INT,
    PARAM_SPEC_ENUM,
    PARAM_SPEC_BOOL,
    PARAM_SPEC_BIPOLAR
} param_spec_type_t;

typedef struct
{
    param_id_t id;
    param_spec_type_t type;
    float min;
    float max;
    float default_value;
} param_spec_t;

extern const param_spec_t param_spec[PARAM_COUNT];

uint8_t param_spec_value_is_valid(param_id_t id, float value);
