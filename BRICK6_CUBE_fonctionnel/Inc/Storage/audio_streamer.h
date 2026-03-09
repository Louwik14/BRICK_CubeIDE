#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(__has_include)
#  if __has_include("ff.h")
#    include "ff.h"
#    define AUDIO_STREAMER_HAS_FATFS 1
#  endif
#endif

#ifndef AUDIO_STREAMER_HAS_FATFS
#define AUDIO_STREAMER_HAS_FATFS 0
#endif

#define AUDIO_STREAMER_MAX_STREAMERS (24U)

typedef struct
{
    /* file system */
#if AUDIO_STREAMER_HAS_FATFS
    FATFS fs;
    FIL fp;
#endif
    uint8_t fs_mounted;
    uint8_t file_open;

    /* wav metadata */
    uint16_t bits_per_sample;
    uint32_t bytes_per_frame;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t file_data_pos;

    /* playback state */
    /* read_pos/write_pos are ring positions in STEREO FRAMES (not samples). */
    volatile uint32_t read_pos;
    volatile uint32_t write_pos;
    volatile uint8_t running;
    volatile uint8_t error;

    /* startup control */
    char pending_path[128];
    uint8_t start_pending;
    uint32_t start_frame;

    /* per-stream continuity */
    float last_out_l;
    float last_out_r;

    /* statistics */
    volatile uint32_t underrun_count;
    volatile uint32_t ring_level_min_frames;
    volatile uint32_t ring_level_max_frames;
    volatile uint32_t total_frames_read_from_ring;
    volatile uint32_t total_frames_filled_from_sd;
    volatile uint32_t total_bytes_read_from_sd;
    volatile uint32_t sd_read_time_max_ms;
    volatile uint32_t refill_time_max_ms;
    volatile uint32_t total_refills;
    volatile uint32_t total_refill_time_ms;
    volatile uint32_t file_restart_count;
    volatile uint32_t partial_read_count;
    volatile uint32_t ring_overflow_detect_count;
    volatile uint32_t ring_underflow_logic_count;
    volatile uint32_t ring_incoherence_count;
    volatile uint32_t pos_oob_count;
    volatile uint32_t last_refill_bytes;
    volatile uint32_t last_refill_frames;
    volatile uint32_t refill_fail_count;

} audio_streamer_t;

typedef struct
{
    uint32_t underrun_count;
    uint32_t ring_level_min_frames;
    uint32_t ring_level_max_frames;
    uint32_t ring_used_frames;
    uint32_t total_frames_read_from_ring;
    uint32_t total_frames_filled_from_sd;
    uint32_t total_bytes_read_from_sd;
    uint32_t sd_read_time_max_ms;
    uint32_t refill_time_max_ms;
    uint32_t total_refills;
    uint32_t total_refill_time_ms;
    uint32_t file_restart_count;
    uint32_t partial_read_count;
    uint32_t ring_overflow_detect_count;
    uint32_t ring_underflow_logic_count;
    uint32_t ring_incoherence_count;
    uint32_t pos_oob_count;
    uint32_t last_refill_bytes;
    uint32_t last_refill_frames;
    uint32_t refill_fail_count;
} audio_streamer_stats_t;

bool audio_streamer_start(uint8_t streamer_id, const char *path, uint32_t start_frame);
bool audio_streamer_seek_frame(uint8_t streamer_id, uint32_t frame);
void audio_streamer_stop(uint8_t streamer_id);
void audio_streamer_process(uint8_t streamer_id);
void audio_streamer_get_frame(uint8_t streamer_id, float *L, float *R);
void audio_streamer_get_stats(uint8_t streamer_id, audio_streamer_stats_t *out_stats);
bool audio_streamer_is_healthy(uint8_t streamer_id);
bool audio_streamer_is_running(uint8_t streamer_id);
