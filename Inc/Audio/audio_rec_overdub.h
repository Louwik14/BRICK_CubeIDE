#pragma once

#include <stdint.h>

void audio_rec_overdub_init(void);
uint8_t audio_rec_overdub_mix(uint8_t enabled,
                              float *left,
                              float *right,
                              uint32_t frames);
