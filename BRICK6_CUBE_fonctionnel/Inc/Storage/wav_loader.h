#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "wav_parser.h"

#if defined(__has_include)
#  if __has_include("ff.h")
#    include "ff.h"
#    define WAV_LOADER_HAS_FATFS 1
#  endif
#endif

#ifndef WAV_LOADER_HAS_FATFS
#define WAV_LOADER_HAS_FATFS 0
#endif

bool wav_loader_load_to_sdram(const char *path, wav_info_t *info);
bool wav_loader_find_first_wav(char *out_path, uint32_t max_len);
const float *wav_loader_get_interleaved_buffer(void);
uint32_t wav_loader_get_capacity_frames(void);

