#pragma once

#include <stdint.h>

void multi_sample_audio_projection_init(void);
uint8_t multi_sample_audio_projection_publish(uint16_t instrument_id);
void multi_sample_audio_projection_withdraw(uint16_t instrument_id);
