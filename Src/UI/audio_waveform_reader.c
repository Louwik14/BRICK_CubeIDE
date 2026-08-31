#include "IPC/audio_waveform_reader.h"

#include "stm32h7xx.h"
#include <string.h>

uint8_t audio_waveform_capture_read(audio_waveform_capture_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) return 0U;
    for (uint32_t attempt = 0U; attempt < 3U; ++attempt)
    {
        const uint32_t before = g_audio_waveform_layout.sequence;
        if ((before & 1U) != 0U) continue;
        __DMB();
        const uint8_t buffer = g_audio_waveform_layout.buffer;
        out_snapshot->generation = g_audio_waveform_layout.generation;
        out_snapshot->entity_id = g_audio_waveform_layout.entity_id;
        out_snapshot->valid = g_audio_waveform_layout.valid;
        out_snapshot->triggered = g_audio_waveform_layout.triggered;
        out_snapshot->trigger_fraction_q8 = g_audio_waveform_layout.trigger_fraction_q8;
        memcpy(out_snapshot->samples, g_audio_waveform_buffers[buffer], sizeof(out_snapshot->samples));
        __DMB();
        const uint32_t after = g_audio_waveform_layout.sequence;
        if ((before == after) && ((after & 1U) == 0U)) return 1U;
    }
    return 0U;
}
