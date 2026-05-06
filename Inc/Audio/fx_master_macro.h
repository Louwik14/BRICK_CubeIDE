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
    FX_MASTER_MACRO_ECHO = 5,
    FX_MASTER_MACRO_WOBBLE = 6,
    FX_MASTER_MACRO_COMB = 7,
    FX_MASTER_MACRO_RING = 8,
    FX_MASTER_MACRO_PITCH = 9,
    FX_MASTER_MACRO_TALK = 10,
    FX_MASTER_MACRO_STUTTER = 11,
    FX_MASTER_MACRO_FREEZE = 12
} fx_master_macro_type_t;

void fx_master_macro_init(float sample_rate);
void fx_master_macro_process_block(float *left, float *right, uint32_t frames);

#ifdef __cplusplus
}
#endif
