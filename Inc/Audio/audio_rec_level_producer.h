#pragma once

#include <stdint.h>

void audio_rec_level_producer_init(void);
void audio_rec_level_producer_publish(uint32_t peak_abs_pcm24);
