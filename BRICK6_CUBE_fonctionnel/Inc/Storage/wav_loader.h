#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(__has_include)
#  if __has_include("ff.h")
#    include "ff.h"
#    define WAV_LOADER_HAS_FATFS 1
#  endif
#endif

#ifndef WAV_LOADER_HAS_FATFS
#define WAV_LOADER_HAS_FATFS 0
#endif

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
const float *wav_loader_get_interleaved_buffer(void);
uint32_t wav_loader_get_capacity_frames(void);

#if WAV_LOADER_HAS_FATFS
bool wav_loader_parse_info(FIL *fp, wav_info_t *info);
#endif
