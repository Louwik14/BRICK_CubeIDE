#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t frames_loaded;
} wav_info_t;

bool wav_loader_load_to_sdram(const char *path, wav_info_t *info);
const float *wav_loader_get_interleaved_buffer(void);
uint32_t wav_loader_get_capacity_frames(void);
