#pragma once

#include <stdint.h>

#include "Sampler/sample_stream_io.h"
#include "Sampler/sample_stream_time.h"

#ifndef BRICK6_STREAM_BENCH
#define BRICK6_STREAM_BENCH (0)
#endif

#define SAMPLE_STREAM_BENCH_HISTOGRAM_BUCKETS (32U)

typedef struct
{
    uint32_t magic;
    uint32_t abi_version;
    uint32_t enabled;
    uint32_t read_chunk_kib;
    uint64_t start_audio_frame;
    uint64_t selected_pages;
    uint64_t ready_pages;
    uint64_t source_bytes;
    uint64_t read_bytes;
    uint64_t read_cycles_total;
    uint64_t wait_frames_total;
    uint64_t service_cycles_total;
    uint32_t read_cycles_max;
    uint32_t wait_frames_max;
    uint32_t service_cycles_max;
    uint32_t backlog_max;
    uint32_t pages_per_service_max;
    uint32_t deadline_misses;
    uint32_t source_changes;
    uint32_t physical_reads;
    uint32_t fatfs_ops;
    uint32_t file_opens;
    uint32_t seeks;
    uint32_t read_cache_hits;
    uint32_t service_calls;
    uint32_t blocked_service_polls;
    uint32_t io_errors;
    uint32_t read_cycles_histogram[SAMPLE_STREAM_BENCH_HISTOGRAM_BUCKETS];
    uint32_t wait_frames_histogram[SAMPLE_STREAM_BENCH_HISTOGRAM_BUCKETS];
} sample_stream_benchmark_snapshot_t;

#if BRICK6_STREAM_BENCH
extern volatile sample_stream_benchmark_snapshot_t g_sample_stream_benchmark;
void sample_stream_benchmark_reset(void);
void sample_stream_benchmark_note_io(sample_audio_key_t key,
                                     const sample_stream_io_result_t *result,
                                     uint32_t read_cycles,
                                     uint32_t wait_frames,
                                     uint32_t backlog,
                                     uint8_t deadline_missed);
void sample_stream_benchmark_note_service(uint32_t pages, uint32_t service_cycles);
void sample_stream_benchmark_note_blocked_poll(void);
#endif
