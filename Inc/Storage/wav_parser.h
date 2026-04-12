#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(__has_include)
#  if __has_include("ff.h")
#    include "ff.h"
#  endif
#endif

typedef struct
{
    uint16_t audio_format;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t channels;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint32_t data_offset;
    uint32_t data_size;
} wav_info_t;

bool wav_parser_parse_info(FIL *fp, wav_info_t *info);
