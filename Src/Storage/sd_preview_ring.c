#include "Contracts/sd_preview_ring_contract.h"
#include "Platform/memory_layout.h"

AUDIO_M7_PRIVATE_SDRAM float
    g_sd_preview_ring[SD_PREVIEW_RING_FRAMES * 2U];
STREAM_LOCAL_D2 sd_preview_ring_layout_t g_sd_preview_ring_layout;
