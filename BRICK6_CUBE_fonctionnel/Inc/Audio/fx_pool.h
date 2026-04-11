#pragma once
#include <stddef.h>
#include <stdint.h>

typedef enum {
    FX_NONE = 0,
    FX_EQ3,
    FX_SAT,
    FX_GRANULAR,
    FX_DAISY_COMP
} fx_type_t;

typedef struct {
    uint8_t active;
    uint8_t type;
    void* state;
} fx_slot_t;

void fx_pool_init(void);
fx_slot_t* fx_pool_get_slot(uint32_t index);
void* fx_pool_get_sat_state_for_track(uint32_t track);

int fx_pool_activate_slot(uint32_t index, fx_type_t type);
void fx_pool_deactivate_slot(uint32_t index);
