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

typedef struct
{
    const float *data;
    uint32_t length;
} sample_buffer_t;

bool wav_loader_load_to_sdram(const char *path, wav_info_t *info);
bool wav_loader_is_ready(void);
bool wav_loader_get_sample_buffer(sample_buffer_t *out);
uint32_t wav_loader_get_capacity_frames(void);
