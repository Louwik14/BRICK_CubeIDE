#pragma once
#include <stdint.h>
#include "audio_float.h"

void mixer_init(void);

void mixer_set_master(float gain);
float mixer_get_master(void);
void mixer_set_output_gain(uint32_t ch, float gain);

void mixer_process(StereoTrack *tracks,
                   uint32_t track_count,
                   uint32_t frames);
