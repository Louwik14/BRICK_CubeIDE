#include "IPC/audio_boot_diagnostic_reader.h"
#include "IPC/audio_boot_diagnostic_layout.h"
#include "stm32h7xx.h"

void audio_boot_diag_read(audio_boot_diag_snapshot_t *out_diag)
{
    if (out_diag == NULL) return;
    for (uint8_t attempt = 0U; attempt < 3U; ++attempt)
    {
        const uint32_t before = g_audio_boot_diag_layout.sequence;
        if ((before & 1U) != 0U) continue;
        __DMB();
        const audio_boot_diag_snapshot_t snapshot = g_audio_boot_diag_layout.snapshot;
        __DMB();
        if ((before == g_audio_boot_diag_layout.sequence) && ((before & 1U) == 0U))
        {
            *out_diag = snapshot;
            return;
        }
    }
}
