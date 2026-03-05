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

    /* ping pong buffers */
    float *bufferA;
    float *bufferB;

    volatile uint32_t frames_valid_A;
    volatile uint32_t frames_valid_B;

    volatile uint8_t ready_A;
    volatile uint8_t ready_B;

    /* playback state */
    volatile uint8_t active_buffer;
    volatile uint32_t read_pos;
    volatile uint8_t running;
    volatile uint8_t error;

    /* startup control */
    char pending_path[128];
    uint8_t start_pending;

    /* statistics */
    volatile uint32_t underrun_count;
    volatile uint32_t sd_read_time_max;
    volatile uint32_t buffer_switch_count;

} audio_streamer_t;

typedef struct
{
    uint32_t underrun_count;
    uint32_t sd_read_time_max;
    uint32_t buffer_switch_count;
} audio_streamer_stats_t;

bool audio_streamer_start(const char *path);
void audio_streamer_process(void);
void audio_streamer_get_frame(float *L, float *R);
void audio_streamer_get_stats(audio_streamer_stats_t *out_stats);
