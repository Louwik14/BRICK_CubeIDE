#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    LIVE_RECORDER_TAP_POST_MIX = 0,
    LIVE_RECORDER_TAP_PRE_MIX
} live_recorder_tap_mode_t;

typedef struct
{
    float *buffer;
    uint32_t max_frames;
    uint32_t loop_frames;

    uint32_t write_pos;
    uint32_t read_pos;

    uint8_t recording;
    uint8_t playing;

    uint32_t latency_offset_frames;

    uint8_t tap_mode;
} live_recorder_t;

void live_recorder_init(live_recorder_t *rec);

void live_recorder_set_buffer(live_recorder_t *rec,
                              float *buffer,
                              uint32_t max_frames);

void live_recorder_set_loop_length(live_recorder_t *rec,
                                   uint32_t loop_frames);

void live_recorder_start_record(live_recorder_t *rec);
void live_recorder_stop_record(live_recorder_t *rec);

void live_recorder_start_play(live_recorder_t *rec);
void live_recorder_stop_play(live_recorder_t *rec);

void live_recorder_write(live_recorder_t *rec,
                         const float *L,
                         const float *R,
                         uint32_t frames);


#ifdef __cplusplus
}
#endif
