#pragma once
#include <stddef.h>
#include <stdint.h>

typedef enum {
    FX_NONE = 0,
    FX_EQ3,
    FX_SAT,
    FX_GRANULAR
} fx_type_t;

typedef struct {
    uint8_t active;
    uint8_t type;
    void* state;
} fx_slot_t;

void fx_pool_init(void);
fx_slot_t* fx_pool_get_slot(uint32_t index);

int fx_pool_activate_slot(uint32_t index, fx_type_t type);
void fx_pool_deactivate_slot(uint32_t index);
