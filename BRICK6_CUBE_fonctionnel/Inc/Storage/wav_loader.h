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
bool wav_loader_find_first_wav(char *out_path, uint32_t max_len);
bool wav_get_physical_location(const char *path, uint32_t *first_block, uint32_t *block_count);
const float *wav_loader_get_interleaved_buffer(void);
uint32_t wav_loader_get_capacity_frames(void);
