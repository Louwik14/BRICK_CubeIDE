#include "IPC/synth_waveform_contract.h"
#include "Platform/memory_layout.h"

D3_IPC synth_waveform_layout_t g_synth_waveform_layout;

_Static_assert(sizeof(synth_waveform_snapshot_t) == 104U,
               "Synth waveform snapshot ABI changed");
_Static_assert(sizeof(synth_waveform_layout_t) == 108U,
               "Synth waveform layout ABI changed");
