#pragma once

#include "IPC/audio_boot_diagnostic.h"

typedef struct
{
    volatile uint32_t sequence;
    audio_boot_diag_snapshot_t snapshot;
} audio_boot_diag_layout_t;

extern audio_boot_diag_layout_t g_audio_boot_diag_layout;
