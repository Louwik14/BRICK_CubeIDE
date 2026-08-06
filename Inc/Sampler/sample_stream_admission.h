#pragma once

#include <stdint.h>

#include "Sampler/sample_page_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_STREAM_ADMISSION_MAX_VOICES (8U)

typedef enum
{
    SAMPLE_STREAM_ADMISSION_OK = 0,
    SAMPLE_STREAM_ADMISSION_INVALID,
    SAMPLE_STREAM_ADMISSION_VOICE_LIMIT,
    SAMPLE_STREAM_ADMISSION_BANDWIDTH,
    SAMPLE_STREAM_ADMISSION_LATENCY
} sample_stream_admission_result_t;

typedef struct
{
    uint32_t measured_bytes_per_second;
    uint32_t per_distinct_file_overhead_bytes_per_second;
    uint32_t worst_read_latency_audio_frames;
    uint16_t utilization_permille;
    uint8_t max_voices;
    uint8_t reserved;
} sample_stream_admission_config_t;

typedef struct
{
    sample_audio_key_t key;
    uint32_t step_q16;
    uint32_t owner_generation;
    uint32_t horizon_frames;
    uint16_t block_align;
    uint8_t owner_kind;
    uint8_t owner_id;
} sample_stream_admission_demand_t;

typedef struct
{
    uint64_t admitted_bytes_per_second;
    uint64_t capacity_bytes_per_second;
    uint32_t rejection_count;
    uint8_t active_voices;
    uint8_t distinct_files;
    uint8_t last_result;
    uint8_t reserved;
} sample_stream_admission_stats_t;

void sample_stream_admission_init(const sample_stream_admission_config_t *config);
sample_stream_admission_result_t sample_stream_admission_try_reserve(
    const sample_stream_admission_demand_t *demand);
void sample_stream_admission_release_owner(uint8_t owner_kind,
                                           uint8_t owner_id,
                                           uint32_t owner_generation);
void sample_stream_admission_get_stats(sample_stream_admission_stats_t *out_stats);

#ifdef __cplusplus
}
#endif
