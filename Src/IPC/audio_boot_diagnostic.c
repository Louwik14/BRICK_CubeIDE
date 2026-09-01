#include "IPC/audio_boot_diagnostic_layout.h"
#include "Platform/memory_layout.h"

D3_IPC audio_boot_diag_layout_t g_audio_boot_diag_layout;

_Static_assert(sizeof(audio_boot_diag_snapshot_t) == 6U,
               "Boot diagnostic snapshot ABI changed");
_Static_assert(sizeof(audio_boot_diag_layout_t) == 12U,
               "Boot diagnostic ABI changed");
