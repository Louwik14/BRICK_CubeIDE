#pragma once

#include <stdint.h>

#define SD_PREVIEW_RING_FRAMES 2048U

typedef struct
{
    volatile uint32_t write_count;
    volatile uint32_t read_count;
} sd_preview_ring_layout_t;

extern float g_sd_preview_ring[SD_PREVIEW_RING_FRAMES * 2U];
extern sd_preview_ring_layout_t g_sd_preview_ring_layout;
