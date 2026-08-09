#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_key.h"
#include "Sampler/sample_stream_io.h"
#include "Sampler/sample_stream_scheduler.h"

#ifndef BRICK6_STREAM_CALIBRATION
#define BRICK6_STREAM_CALIBRATION (0)
#endif

#define BRICK6_STREAM_CALIBRATION_MAGIC       (0x5343414CUL)
#define BRICK6_STREAM_CALIBRATION_ABI_VERSION (2U)
#define BRICK6_STREAM_CALIBRATION_CASES_PER_BUILD (15U)
#define BRICK6_STREAM_CALIBRATION_MAX_RESULTS (30U)

typedef struct
{
    uint16_t page_kib;
    uint8_t passes;
    uint8_t advance_pages;
    uint8_t passed;
    uint8_t first_fault_voice;
    uint16_t reserved0;
    uint32_t first_fault_page;
    uint32_t underruns;
    uint32_t minimum_margin_frames;
    uint32_t minimum_margin_per_voice[8];
    uint32_t max_voice_service_gap_frames;
    uint32_t round_cycles_average;
    uint32_t round_cycles_max;
    uint32_t sd_read_cycles_average;
    uint32_t sd_read_cycles_p99_upper;
    uint32_t sd_read_cycles_max;
    uint64_t source_bytes;
    uint64_t read_bytes;
    uint64_t read_cycles_total;
    uint64_t service_cycles_total;
    uint32_t elapsed_audio_frames;
    uint32_t pages_loaded;
    uint32_t physical_reads;
    uint32_t fatfs_ops;
    uint32_t contiguous_reads;
    uint32_t fatfs_reads;
    uint32_t seeks;
    uint32_t file_opens;
    uint32_t io_errors;
    uint32_t pages_per_second_q16;
    uint32_t sd_bytes_per_second;
    uint32_t audio_irq_overruns;
} brick6_stream_calibration_result_t;

typedef struct
{
    uint32_t magic;
    uint16_t abi_version;
    uint16_t header_size;
    uint16_t record_size;
    uint16_t result_count;
    uint32_t firmware_page_kib;
    uint32_t sample_rate;
    uint32_t case_duration_frames;
    uint32_t grid_signature;
    uint32_t payload_crc32;
    brick6_stream_calibration_result_t results[BRICK6_STREAM_CALIBRATION_MAX_RESULTS];
} brick6_stream_calibration_file_t;

_Static_assert(sizeof(brick6_stream_calibration_result_t) == 160U,
               "stream calibration record ABI changed");

#if BRICK6_STREAM_CALIBRATION
void brick6_stream_calibration_init(void);
void brick6_stream_calibration_process(void);
void brick6_stream_calibration_select_case(uint8_t case_index);
void brick6_stream_calibration_note_select(
    const sample_stream_scheduler_candidate_t *candidate);
void brick6_stream_calibration_note_io(const sample_stream_io_result_t *result,
                                       uint32_t read_cycles);
void brick6_stream_calibration_note_underrun(sample_audio_key_t key,
                                             uint32_t page_index);
void brick6_stream_calibration_note_round_begin(void);
void brick6_stream_calibration_note_round_end(void);
uint16_t brick6_stream_calibration_case_index(void);
uint16_t brick6_stream_calibration_case_count(void);
uint8_t brick6_stream_calibration_current_passes(void);
uint8_t brick6_stream_calibration_current_advance(void);
#endif
