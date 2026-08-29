/**
 * @file audio_io.c
 * @brief Conversion entre buffers audio communs et transport physique Board.
 */

#include "audio_io.h"
#include "Audio/audio_diag_capture.h"

#include "Audio/metronome_runtime.h"
#include "Board/board_audio.h"
#include "Platform/memory_layout.h"

static AUDIO_HOT ALIGN32 audio_physical_inputs_t g_audio_physical_inputs;

void audio_io_unpack(const int32_t *AUDIO_RESTRICT rx,
                     uint32_t frames,
                     float in_scale)
{
    board_audio_unpack_input(rx,
                             &g_audio_physical_inputs,
                             frames,
                             in_scale);
}

const audio_physical_inputs_t *audio_io_get_current_physical_inputs(void)
{
    return &g_audio_physical_inputs;
}

void audio_io_pack(int32_t *AUDIO_RESTRICT tx,
                   const float *AUDIO_RESTRICT bus_main_l,
                   const float *AUDIO_RESTRICT bus_main_r,
                   uint32_t frames,
                   float out_gain)
{
    audio_io_pack_ramped(tx,
                         bus_main_l,
                         bus_main_r,
                         frames,
                         out_gain,
                         out_gain);
}

void audio_io_pack_ramped(int32_t *AUDIO_RESTRICT tx,
                          const float *AUDIO_RESTRICT bus_main_l,
                          const float *AUDIO_RESTRICT bus_main_r,
                          uint32_t frames,
                          float out_gain_start,
                          float out_gain_end)
{
    if (frames == 0U)
    {
        return;
    }

    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    static float monitor_main_l[AUDIO_BLOCK_SIZE];
    static float monitor_main_r[AUDIO_BLOCK_SIZE];
    const float gain_step = (out_gain_end - out_gain_start) / (float)frames;
    float out_gain = out_gain_start;

    for (uint32_t n = 0; n < frames; n++)
    {
        monitor_main_l[n] = bus_main_l[n] * out_gain;
        monitor_main_r[n] = bus_main_r[n] * out_gain;
        out_gain += gain_step;
    }

    metronome_runtime_render_main_monitor(monitor_main_l, monitor_main_r, frames);
    audio_diag_capture_pre_pcm(monitor_main_l, monitor_main_r, frames);
    board_audio_pack_output(tx,
                            monitor_main_l,
                            monitor_main_r,
                            frames);
    audio_diag_capture_tx_pcm(tx, frames, BOARD_AUDIO_TDM_SLOTS);
}
