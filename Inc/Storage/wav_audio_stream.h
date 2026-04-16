#pragma once

#include <stdint.h>

#include "wav_parser.h"

#if defined(__has_include)
#  if __has_include("ff.h")
#    include "ff.h"
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    FIL *fp;
    wav_info_t info;
    uint32_t target_rate;
    uint8_t stream_initialized;
    uint8_t source_exhausted;
    uint8_t stream_ended;
    uint8_t source_prev_valid;
    uint8_t source_curr_valid;
    uint8_t io_error;
    uint32_t data_remaining;
    uint32_t io_pos;
    uint32_t io_len;
    uint32_t prev_index;
    uint32_t curr_index;
    float prev_l;
    float prev_r;
    float curr_l;
    float curr_r;
    double phase;
    double phase_step;
    uint8_t io_buf[4096U];
} wav_audio_stream_t;

void wav_audio_stream_init(wav_audio_stream_t *stream,
                           FIL *fp,
                           const wav_info_t *info,
                           uint32_t target_rate);
uint8_t wav_audio_stream_start(wav_audio_stream_t *stream, uint32_t data_offset);
uint8_t wav_audio_stream_next_frame(wav_audio_stream_t *stream, float *out_left, float *out_right);

#ifdef __cplusplus
}
#endif
