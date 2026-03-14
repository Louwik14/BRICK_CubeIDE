#pragma once

#include "param_store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    PARAM_TYPE_FLOAT,
    PARAM_TYPE_INT,
    PARAM_TYPE_ENUM,
    PARAM_TYPE_BOOL,
    PARAM_TYPE_BIPOLAR
} param_type_t;

typedef struct
{
    param_id_t id;

    const char *name;

    param_type_t type;

    float min;
    float max;
    float step;

    float default_value;

    const char *unit;

    void (*apply)(float value);

} param_desc_t;

extern const param_desc_t param_registry[PARAM_COUNT];

void param_registry_init(void);

float param_get(param_id_t id);
void param_set(param_id_t id, float value);
void param_reset(param_id_t id);

#ifdef __cplusplus
}
#endif
