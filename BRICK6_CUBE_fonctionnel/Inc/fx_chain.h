#pragma once
#include <stdint.h>

void fx_chain_process_track0(
    float* in_l,
    float* in_r,
    uint32_t frames
);

void fx_chain_process_slot(
    uint32_t slot,
    float* in_l,
    float* in_r,
    uint32_t frames
);
