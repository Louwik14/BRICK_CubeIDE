#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_key.h"
#include "Sampler/sample_audio_format.h"

typedef enum
{
    SAMPLE_PAGE_FREE = 0,
    SAMPLE_PAGE_RESERVED,
    SAMPLE_PAGE_LOADING,
    SAMPLE_PAGE_READY,
    SAMPLE_PAGE_EVICTING,
    SAMPLE_PAGE_FAILED
} sample_page_state_t;

typedef struct
{
    const float *frames_interleaved;
    uint32_t frame_count;
    uint32_t start_frame;
    uint32_t page_index;
    uint32_t page_generation;
    sample_audio_key_t key;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    uint32_t slot_index;
} sample_page_span_t;

typedef struct
{
    sample_audio_key_t key;
    uint32_t page_index;
    uint32_t page_generation;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    uint32_t slot_index;
} sample_page_ref_t;
