/**
 * @file audio_io.c
 * @brief Conversion entre buffers audio communs et transport physique Board.
 */

#include "audio_io.h"

#include <string.h>

#include "Audio/metronome_runtime.h"
#include "Board/board_audio.h"
#include "usb_audio.h"
#include "Platform/memory_layout.h"

static AUDIO_HOT ALIGN32 audio_physical_inputs_t g_audio_physical_inputs;
static AUDIO_HOT ALIGN32 int32_t g_usb_audio_input[AUDIO_BLOCK_SIZE * 2U];

static inline float usb_audio_pcm24_to_float(int32_t sample, float gain)
{
    const int32_t signed_sample = (int32_t)((uint32_t)sample << 8U) >> 8U;
    return (float)signed_sample * gain;
}

void audio_io_unpack(const int32_t *AUDIO_RESTRICT rx,
                     uint32_t frames,
                     float in_scale)
{
    board_audio_unpack_input(rx,
                             &g_audio_physical_inputs,
                             frames,
                             in_scale);

    memset(g_audio_physical_inputs.usb.left, 0,
           frames * sizeof(float));
    memset(g_audio_physical_inputs.usb.right, 0,
           frames * sizeof(float));
    if (usb_audio_audio_input_active() != 0U)
    {
        const uint32_t usb_frames = usb_audio_audio_read(g_usb_audio_input,
                                                         frames);
        if (usb_frames == frames)
        {
            for (uint32_t n = 0U; n < frames; ++n)
            {
                g_audio_physical_inputs.usb.left[n] =
                    usb_audio_pcm24_to_float(g_usb_audio_input[n * 2U],
                                             in_scale);
                g_audio_physical_inputs.usb.right[n] =
                    usb_audio_pcm24_to_float(g_usb_audio_input[n * 2U + 1U],
                                             in_scale);
            }
        }
    }
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
    board_audio_pack_output(tx,
                            monitor_main_l,
                            monitor_main_r,
                            frames);
    (void)usb_audio_audio_write(tx, frames);
}
