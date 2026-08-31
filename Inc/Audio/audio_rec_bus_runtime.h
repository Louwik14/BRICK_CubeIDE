#pragma once

#include <stdint.h>
#include "IPC/audio_rec_bus_contract.h"

typedef struct
{
    uint16_t source_entity_mask;
    uint8_t arm;
    uint8_t source_flags;
} audio_rec_bus_runtime_t;

void audio_rec_bus_runtime_init(void);
uint8_t audio_rec_bus_runtime_apply(uint32_t packed);
const audio_rec_bus_runtime_t *audio_rec_bus_runtime_get(void);
