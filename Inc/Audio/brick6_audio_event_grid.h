#pragma once

#include <stdint.h>

#define BRICK6_AUDIO_EVENT_GRID_FRAMES 64U

static inline uint32_t brick6_audio_event_grid_frames(void)
{
    return (uint32_t)BRICK6_AUDIO_EVENT_GRID_FRAMES;
}

static inline uint32_t brick6_audio_event_grid_q16_u32(void)
{
    return (uint32_t)(BRICK6_AUDIO_EVENT_GRID_FRAMES << 16U);
}

static inline uint64_t brick6_audio_event_grid_q16_u64(void)
{
    return ((uint64_t)BRICK6_AUDIO_EVENT_GRID_FRAMES) << 16U;
}
