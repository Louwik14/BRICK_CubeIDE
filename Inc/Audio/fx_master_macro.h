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
    FX_MASTER_MACRO_CHORUS_MICRO = 11,
    FX_MASTER_MACRO_CHORUS_DAISY = 12,
    FX_MASTER_MACRO_CHORUS_JUNO = 13,
    FX_MASTER_MACRO_TYPE_COUNT
} fx_master_macro_type_t;

#define FX_MASTER_MACRO_DIAG_SLOT_COUNT 4U

typedef struct
{
    uint8_t active_mask;
    uint8_t type[FX_MASTER_MACRO_DIAG_SLOT_COUNT];
    uint8_t level[FX_MASTER_MACRO_DIAG_SLOT_COUNT];
} fx_master_macro_diag_state_t;

void fx_master_macro_init(float sample_rate);
void fx_master_macro_set_mute(uint8_t muted);
void fx_master_macro_process_block(float *left, float *right, uint32_t frames);
void fx_master_macro_get_diag_state(fx_master_macro_diag_state_t *out);

#ifdef __cplusplus
}
#endif
