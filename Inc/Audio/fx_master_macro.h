#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    FX_MASTER_MACRO_OFF = 0,
    FX_MASTER_MACRO_DRIVE = 1,
    FX_MASTER_MACRO_CRUSH = 2,
    FX_MASTER_MACRO_PUMP = 3,
    FX_MASTER_MACRO_CHOP = 4,
    FX_MASTER_MACRO_WOBBLE = 5,
    FX_MASTER_MACRO_COMB = 6,
    FX_MASTER_MACRO_RING = 7,
    FX_MASTER_MACRO_STUTTER = 8,
    FX_MASTER_MACRO_FREEZE = 9,
    FX_MASTER_MACRO_COLOR = 10,
    FX_MASTER_MACRO_TYPE_COUNT
} fx_master_macro_type_t;

void fx_master_macro_init(float sample_rate);
void fx_master_macro_process_block(float *left, float *right, uint32_t frames);

#ifdef __cplusplus
}
#endif
