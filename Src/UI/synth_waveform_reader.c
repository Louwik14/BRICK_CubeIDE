#include "IPC/synth_waveform_reader.h"
#include "stm32h7xx.h"

uint8_t synth_waveform_control_read(synth_waveform_snapshot_t *out)
{
    if (out == NULL) return 0U;
    const uint32_t before = g_synth_waveform_layout.sequence;
    if ((before & 1U) != 0U) return 0U;
    __DMB();
    const synth_waveform_snapshot_t snapshot = g_synth_waveform_layout.snapshot;
    __DMB();
    if ((before != g_synth_waveform_layout.sequence) || ((before & 1U) != 0U)) return 0U;
    *out = snapshot;
    return (uint8_t)(snapshot.osc_mask != 0U);
}
