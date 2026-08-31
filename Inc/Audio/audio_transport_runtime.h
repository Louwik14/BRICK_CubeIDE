#pragma once

#include <stdint.h>

typedef struct
{
    uint32_t tempo_effective_bpm_milli;
    uint32_t samples_per_step_q16;
    uint8_t running;
} audio_transport_runtime_t;

void audio_transport_runtime_init(void);
const audio_transport_runtime_t *audio_transport_runtime_get(void);
uint8_t audio_transport_runtime_set_running(uint8_t running);
uint8_t audio_transport_runtime_set_tempo(uint32_t tempo_milli);
uint8_t audio_transport_runtime_set_step_q16(uint32_t samples_per_step_q16);
