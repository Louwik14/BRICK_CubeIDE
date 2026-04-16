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

#define WAV_LOADER_CATALOG_MAX (64U)

typedef enum
{
    WAV_LOADER_CATALOG_INVALID = 0,
    WAV_LOADER_CATALOG_READY
} wav_loader_catalog_state_t;

typedef struct
{
    char path[64];
    char name[32];
    wav_loader_catalog_state_t state;
} wav_loader_catalog_entry_t;

bool wav_loader_load_to_sdram(const char *path, wav_info_t *info);
bool wav_loader_find_first_wav(char *out_path, uint32_t max_len);
void wav_loader_catalog_refresh(void);
uint8_t wav_loader_catalog_count(void);
const wav_loader_catalog_entry_t *wav_loader_catalog_get(uint8_t index);
const float *wav_loader_get_interleaved_buffer(void);
uint32_t wav_loader_get_capacity_frames(void);
