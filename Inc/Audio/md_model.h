#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    MD_MODEL_TRX_BD = 0,
    MD_MODEL_TRX_SD,
    MD_MODEL_TRX_CH,
    MD_MODEL_EFM_BD,
    MD_MODEL_EFM_SD,
    MD_MODEL_EFM_CB,
    MD_MODEL_COUNT
} md_model_t;

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
