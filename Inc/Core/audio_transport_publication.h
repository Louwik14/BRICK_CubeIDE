#pragma once

#include <stdint.h>

typedef struct
{
    uint8_t running;
    uint8_t reserved[3];
    uint32_t tempo_effective_bpm_milli;
    uint32_t samples_per_step_q16;
    uint32_t transport_epoch;
} audio_transport_publication_t;

void audio_transport_publication_init(void);
void audio_transport_publication_publish(uint8_t running,
                                         uint32_t tempo_effective_bpm_milli,
                                         uint32_t samples_per_step_q16);
void audio_transport_publication_set_running(uint8_t running);
void audio_transport_publication_set_tempo(uint32_t tempo_effective_bpm_milli,
                                           uint32_t samples_per_step_q16);
void audio_transport_publication_get(audio_transport_publication_t *out);

