#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    volatile uint32_t underrun_count;
    volatile uint8_t buffer_A_full;
    volatile uint8_t buffer_B_full;
    volatile uint32_t max_sd_read_time;
} audio_streamer_stats_t;

bool audio_streamer_start_first_wav(void);
void audio_streamer_stop(void);
void audio_streamer_task(void);
void audio_streamer_get_frame(float *l, float *r);
const audio_streamer_stats_t *audio_streamer_get_stats(void);

