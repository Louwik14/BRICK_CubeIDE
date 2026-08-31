#pragma once

#include "IPC/multi_sample_audio_projection.h"
#include "Sampler/multi_sample_config.h"

typedef struct
{
    volatile uint32_t sequence;
    volatile uint8_t ready;
    uint8_t reserved;
    uint16_t first_zone_id;
    uint16_t zone_count;
    uint16_t first_sample_id;
    uint16_t sample_count;
} multi_audio_instrument_t;

typedef struct
{
    uint8_t note_low;
    uint8_t note_high;
    uint8_t vel_low;
    uint8_t vel_high;
    uint8_t root_note;
    uint8_t reserved;
    uint16_t multi_sample_id;
} multi_audio_zone_t;

extern multi_audio_instrument_t
    g_multi_audio_instruments[MULTI_SAMPLE_POOL_MAX_INSTRUMENTS];
extern multi_audio_zone_t g_multi_audio_zones[MULTI_SAMPLE_POOL_MAX_ZONES];
extern multi_sample_audio_source_t
    g_multi_audio_samples[MULTI_SAMPLE_POOL_MAX_SAMPLES];
