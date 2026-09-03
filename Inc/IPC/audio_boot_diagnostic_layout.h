#pragma once

#include <stddef.h>
#include "IPC/audio_boot_diagnostic.h"

typedef struct
{
    volatile uint32_t sequence;
    audio_boot_diag_snapshot_t snapshot;
} audio_boot_diag_layout_t;

_Static_assert(sizeof(audio_boot_diag_layout_t) == 12U,
               "Boot diagnostic layout ABI changed");
_Static_assert(offsetof(audio_boot_diag_layout_t, snapshot) == 4U,
               "Boot diagnostic snapshot offset changed");

extern audio_boot_diag_layout_t g_audio_boot_diag_layout;
