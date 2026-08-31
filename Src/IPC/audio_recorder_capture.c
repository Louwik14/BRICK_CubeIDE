#include "IPC/audio_recorder_capture_contract.h"
#include "Platform/memory_layout.h"

SDRAM_RECORDER int32_t g_audio_recorder_capture_ring
    [AUDIO_RECORDER_CAPTURE_RING_FRAMES * AUDIO_RECORDER_CHANNELS];
D3_IPC audio_recorder_capture_transport_t g_audio_recorder_capture;
