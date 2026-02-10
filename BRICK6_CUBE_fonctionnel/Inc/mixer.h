#pragma once
#include <stdint.h>

#define MIXER_INPUTS   8
#define MIXER_OUTPUTS  8

void mixer_init(void);

void mixer_set_master(float gain);
void mixer_set_output_gain(uint32_t ch, float gain);

void mixer_process(float **in,
                   float **out,
                   uint32_t frames);
