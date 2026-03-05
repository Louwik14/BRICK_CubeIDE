#pragma once

#include <stdbool.h>
#include <stdint.h>

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
