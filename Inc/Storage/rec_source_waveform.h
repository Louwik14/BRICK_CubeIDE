#pragma once

#include <stdint.h>

#define REC_SOURCE_WAVEFORM_BINS 4096U

typedef struct
{
    uint32_t frame_count;
    uint32_t frames_per_bin;
    /* Frame domain of the bins; new REC overviews use the captured duration. */
    uint32_t bin_domain_frames;
    uint32_t generation;
    uint16_t bin_count;
    uint8_t ready;
    uint8_t reserved;
    int16_t min[REC_SOURCE_WAVEFORM_BINS];
    int16_t max[REC_SOURCE_WAVEFORM_BINS];
} rec_source_waveform_summary_t;

void rec_source_waveform_begin(uint32_t frame_limit);
void rec_source_waveform_abort(void);
void rec_source_waveform_capture_service(const int32_t *ring_interleaved,
                                         uint32_t ring_capacity_frames,
                                         uint32_t published_frames);
uint32_t rec_source_waveform_captured_frames(void);
uint8_t rec_source_waveform_finish(rec_source_waveform_summary_t *out_summary);
