#pragma once

#include <stdint.h>

void sample_classic_audio_projection_init(void);
uint8_t sample_classic_audio_projection_publish(uint16_t sample_id);
void sample_classic_audio_projection_withdraw(uint16_t sample_id);
