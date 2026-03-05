#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ff.h"

typedef struct
{
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint32_t data_offset;
    uint32_t data_size;
} wav_parser_info_t;

bool wav_parser_find_first_wav(char *out_path, uint32_t max_len);
bool wav_parser_read_header(FIL *fp, wav_parser_info_t *info);

