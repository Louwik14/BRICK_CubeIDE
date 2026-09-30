#include "Recorder/audio_recorder_ring.h"

#include "Platform/memory_layout.h"

SDRAM_RECORDER_RING float g_audio_recorder_ring
    [AUDIO_RECORDER_CAPTURE_RING_FRAMES * AUDIO_RECORDER_CHANNELS];
AUDIO_HOT ALIGN32 audio_recorder_ring_state_t g_audio_recorder_ring_state;
