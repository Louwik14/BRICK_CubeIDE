#pragma once

#include <stdbool.h>
#include <stdint.h>

bool audio_init(void);
bool audio_start(void);
void audio_get_stats(uint32_t *half, uint32_t *full);
