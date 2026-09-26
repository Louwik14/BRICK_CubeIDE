#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(__has_include)
#  if __has_include("ff.h")
#    include "ff.h"
#  endif
#endif

typedef enum
{
    WAV_SAMPLE_ENCODING_INVALID = 0,
    WAV_SAMPLE_ENCODING_PCM_INTEGER,
    WAV_SAMPLE_ENCODING_IEEE_FLOAT
} wav_sample_encoding_t;

typedef struct
{
    uint16_t audio_format;
    wav_sample_encoding_t encoding;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t channels;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint16_t valid_bits_per_sample;
    uint32_t fmt_chunk_size;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t fact_sample_length;
    uint8_t has_fact;
} wav_info_t;

uint8_t wav_parser_format_supported(const wav_info_t *info);
uint8_t wav_parser_is_canonical_brick_float(const wav_info_t *info);

bool wav_parser_parse_info(FIL *fp, wav_info_t *info);
uint8_t wav_parser_crc32_file(FIL *fp, uint32_t *out_crc32);
