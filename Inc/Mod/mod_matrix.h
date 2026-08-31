#pragma once

#include <stdint.h>

#include "Mod/mod_destination_contract.h"

#define MOD_MATRIX_SLOT_COUNT 8U
#define MOD_MATRIX_SOURCE_COUNT 11U

typedef enum
{
    MOD_MATRIX_SOURCE_NONE = 0,
    MOD_MATRIX_SOURCE_LFO1,
    MOD_MATRIX_SOURCE_LFO2,
    MOD_MATRIX_SOURCE_LFO3,
    MOD_MATRIX_SOURCE_ENV_FLT,
    MOD_MATRIX_SOURCE_ENV_VCA,
    MOD_MATRIX_SOURCE_ENV3,
    MOD_MATRIX_SOURCE_MULTI1,
    MOD_MATRIX_SOURCE_MULTI2,
    MOD_MATRIX_SOURCE_SLEW1,
    MOD_MATRIX_SOURCE_SLEW2
} mod_matrix_source_t;

typedef struct
{
    uint8_t enabled;
    uint8_t source;
    mod_destination_address_t destination;
    float depth;
} track_mod_matrix_slot_t;
