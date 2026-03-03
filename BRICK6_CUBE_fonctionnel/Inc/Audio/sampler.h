#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Ce module représente une source audio simple (sampler),
 * destinée à être utilisée dans l’IRQ audio sans accès externe.
 */
typedef struct
{
    bool active;
    float gainL;
    float gainR;
    uint32_t pos;
    uint32_t length;
    const float *data;
    bool loop;
    uint32_t loop_start;
    uint32_t loop_end;
} sample_voice_t;

void sample_voice_init(sample_voice_t *v);
void sample_voice_trigger(sample_voice_t *v, const float *data, uint32_t length);
void sample_voice_process(sample_voice_t *v, float *outL, float *outR, uint32_t nframes);
