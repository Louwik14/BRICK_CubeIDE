#pragma once

#include <stdint.h>
#include "Param/engine_model_catalog.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    const char *name;
    const char *slot_labels[8];
    uint8_t defaults[8];
    uint8_t slot_count;
} md_model_profile_t;

const md_model_profile_t *md_model_profile_get(uint8_t model);
uint8_t md_model_validate(float value);

#ifdef __cplusplus
}
#endif
