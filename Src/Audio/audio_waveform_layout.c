#include "Audio/Publications/audio_waveform_contract.h"
#include "Platform/memory_layout.h"

STREAM_LOCAL_D2 int8_t
    g_audio_waveform_buffers[2][AUDIO_WAVEFORM_CAPTURE_FRAME_SAMPLES];
STREAM_LOCAL_D2 audio_waveform_layout_t g_audio_waveform_layout;

_Static_assert(sizeof(g_audio_waveform_buffers) == 3008U,
               "scope double buffer size changed");
_Static_assert(sizeof(audio_waveform_layout_t) == 16U,
               "Audio waveform layout ABI changed");
