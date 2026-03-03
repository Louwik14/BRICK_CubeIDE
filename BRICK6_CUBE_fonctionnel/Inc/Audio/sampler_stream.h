#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STREAM_BUFFER_FRAMES 4096U

extern volatile uint32_t g_stream_write_pos;
extern volatile uint32_t g_stream_read_pos;
extern volatile uint32_t g_stream_underrun_count;
extern float stream_buffer[STREAM_BUFFER_FRAMES * 2U];

void sampler_stream_init(void);
void sampler_stream_update(void);
uint32_t sampler_stream_fill_samples(void);

#ifdef __cplusplus
}
#endif
