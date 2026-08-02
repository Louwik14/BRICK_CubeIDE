#pragma once

#include <stdint.h>

/* Temporary FX-track listening/IRQ comparison bench. */
typedef enum
{
    FX_CHORUS_BENCH_NONE = 0,
    FX_CHORUS_BENCH_MICRO = 1,
    FX_CHORUS_BENCH_DAISY = 2,
    FX_CHORUS_BENCH_JUNO = 3
} fx_chorus_bench_model_t;

void fx_chorus_bench_init(float sample_rate);
uint8_t fx_chorus_bench_is_model(uint8_t model);
void fx_chorus_bench_process(float *left,
                             float *right,
                             uint32_t frames,
                             fx_chorus_bench_model_t model,
                             float wet,
                             float depth,
                             float rate);
