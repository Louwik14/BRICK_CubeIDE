#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_format.h"

typedef enum
{
    SAMPLE_READER_BLOCK_OK = 0,
    SAMPLE_READER_BLOCK_DONE,
    SAMPLE_READER_BLOCK_UNDERRUN,
    SAMPLE_READER_BLOCK_NOT_READY
} sample_reader_block_status_t;

typedef struct
{
    const float *l;
    const float *r;
    uint32_t frames;
    uint32_t frame_stride;
    int32_t frame_step;
    sample_audio_format_t format;
    uint32_t frames_per_page;
    uint8_t is_mono;
    sample_reader_block_status_t status;
} sample_reader_block_t;
