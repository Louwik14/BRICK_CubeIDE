#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "Sampler/sample_pool.h"
#include "Storage/audio_streamer.h"

typedef struct
{
    uint32_t process_call_count;
    uint32_t process_dt_max_ms;
    uint32_t process_dt_acc_ms;
    uint32_t process_dt_samples;
    uint32_t process_watchdog_count;
} stream_manager_stats_t;

bool stream_manager_start(const char *path);
void stream_manager_process(void);
void stream_manager_get_frame(float *L, float *R);
bool stream_manager_start_stream(const sample_desc_t *sample_desc,
                                 uint32_t start_frame,
                                 uint8_t *out_streamer_id);
bool stream_manager_get_stream_frame(uint8_t streamer_id, float *L, float *R);
void stream_manager_stop_stream(uint8_t streamer_id);
void stream_manager_get_stats(stream_manager_stats_t *out_stats);
