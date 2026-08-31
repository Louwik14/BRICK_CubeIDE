#include "IPC/sd_preview_ring_contract.h"
#include "Platform/memory_layout.h"

AUDIO_STORAGE_SHARED_SDRAM float
    g_sd_preview_ring[SD_PREVIEW_RING_FRAMES * 2U];
D3_IPC sd_preview_ring_layout_t g_sd_preview_ring_layout;
