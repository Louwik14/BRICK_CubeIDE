/**
 * @file audio_io.c
 * @brief Conversion entre buffers audio communs et transport physique Board.
 */

#include "audio_io.h"

#include "Audio/audio_track_diag.h"
#include "Audio/metronome_runtime.h"
#include "Board/board_audio.h"

void audio_io_unpack(const int32_t *AUDIO_RESTRICT rx,
                     StereoTrack *AUDIO_RESTRICT track_buf,
                     uint32_t frames,
                     float in_scale)
{
    board_audio_unpack_input(rx, track_buf, frames, in_scale);
}

void audio_io_pack(int32_t *AUDIO_RESTRICT tx,
                   const float *AUDIO_RESTRICT bus_main_l,
                   const float *AUDIO_RESTRICT bus_main_r,
                   const float *AUDIO_RESTRICT bus_cue_l,
                   const float *AUDIO_RESTRICT bus_cue_r,
                   uint32_t frames,
                   float out_gain)
{
    audio_io_pack_ramped(tx,
                         bus_main_l,
                         bus_main_r,
                         bus_cue_l,
                         bus_cue_r,
                         frames,
                         out_gain,
                         out_gain);
}

void audio_io_pack_ramped(int32_t *AUDIO_RESTRICT tx,
                          const float *AUDIO_RESTRICT bus_main_l,
                          const float *AUDIO_RESTRICT bus_main_r,
                          const float *AUDIO_RESTRICT bus_cue_l,
                          const float *AUDIO_RESTRICT bus_cue_r,
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
    const uint8_t diag_enabled = audio_track_diag_is_enabled();

    for (uint32_t n = 0; n < frames; n++)
    {
        monitor_main_l[n] = bus_main_l[n] * out_gain;
        monitor_main_r[n] = bus_main_r[n] * out_gain;
        if (diag_enabled != 0U)
        {
            audio_global_diag_measure_sample(AUDIO_GLOBAL_DIAG_POST_MASTER_GAIN,
                                             monitor_main_l[n],
                                             monitor_main_r[n]);
        }
        out_gain += gain_step;
    }

    metronome_runtime_render_main_monitor(monitor_main_l, monitor_main_r, frames);
    if (diag_enabled != 0U)
    {
        audio_global_diag_measure_stereo(AUDIO_GLOBAL_DIAG_PRE_PCM24,
                                         monitor_main_l, monitor_main_r, frames);
    }

    board_audio_pack_output(tx,
                            monitor_main_l,
                            monitor_main_r,
                            bus_cue_l,
                            bus_cue_r,
                            frames,
                            out_gain_start,
                            out_gain_end);
    if (diag_enabled != 0U)
    {
        audio_global_diag_end_block(frames);
    }
}
