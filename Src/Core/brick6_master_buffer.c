#include "Core/brick6_master_buffer.h"

#include <stddef.h>
#include <string.h>

#include "Audio/audio_float.h"
#include "Seq/seq_model.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Storage/memory_layout.h"
#include "ui_core.h"

#ifndef BRICK6_MASTER_BUFFER_MAX_SOURCE_TRACKS
#define BRICK6_MASTER_BUFFER_MAX_SOURCE_TRACKS UI_TRACK_COUNT
#endif

#ifndef BRICK6_MASTER_BUFFER_MAX_RECORD_LEN_STEPS
#define BRICK6_MASTER_BUFFER_MAX_RECORD_LEN_STEPS 64U
#endif

static live_recorder_t *g_buffer_recorder = NULL;
static float *g_buffer_storage = NULL;
static uint32_t g_buffer_max_frames = 0U;
static uint8_t g_source_enabled[BRICK6_MASTER_BUFFER_MAX_SOURCE_TRACKS];
static uint8_t g_record_armed = 0U;
static uint8_t g_recording = 0U;
static uint8_t g_record_waiting_boundary = 0U;
static uint8_t g_play_waiting_boundary = 0U;
static uint32_t g_record_frames_written = 0U;
static uint32_t g_record_target_frames = 0U;
static uint32_t g_record_len_steps = 16U;
static uint8_t g_quantize_record = 1U;
static uint8_t g_quantize_play = 1U;
static float g_rate = 1.0f;
static float g_xfade = 0.0f;
static uint32_t g_fade_in_amount = 0U;
static uint32_t g_fade_out_amount = 0U;
static ALIGN32 float g_capture_l[AUDIO_BLOCK_SIZE];
static ALIGN32 float g_capture_r[AUDIO_BLOCK_SIZE];

static uint32_t brick6_master_buffer_steps_to_frames(uint32_t steps)
{
    const seq_runtime_state_t *const seq_state = seq_runtime_get_state();
    uint32_t frames = 0U;

    if (steps == 0U)
    {
        steps = 1U;
    }

    if (seq_state != NULL)
    {
        frames = (uint32_t)(((uint64_t)seq_state->samples_per_step_q16 * (uint64_t)steps) >> 16);
    }

    if (frames < AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    if ((g_buffer_max_frames != 0U) && (frames > g_buffer_max_frames))
    {
        frames = g_buffer_max_frames;
    }

    return frames;
}

static uint32_t brick6_master_buffer_refresh_record_target(void)
{
    const uint32_t target_frames = brick6_master_buffer_steps_to_frames(g_record_len_steps);
    g_record_target_frames = target_frames;
    return target_frames;
}

static void brick6_master_buffer_apply_record_target_to_recorder(void)
{
    const uint32_t target_frames = brick6_master_buffer_refresh_record_target();

    if (g_buffer_recorder == NULL)
    {
        return;
    }

    live_recorder_set_loop_length(g_buffer_recorder, target_frames);
}
static uint8_t brick6_master_buffer_is_boundary_reached(void)
{
    uint32_t longest_duration = 0U;

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        if (ui_get_track_family(track) == UI_TRACK_FAMILY_OFF)
        {
            continue;
        }

        uint8_t div = 1U;
        (void)seq_runtime_get_track_div(track, &div);
        const uint32_t duration = (uint32_t)seq_model_get_track_playback_length(track) * (uint32_t)div;
        if (duration > longest_duration)
        {
            longest_duration = duration;
        }
    }

    if (longest_duration == 0U)
    {
        return 1U;
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        if (ui_get_track_family(track) == UI_TRACK_FAMILY_OFF)
        {
            continue;
        }

        uint8_t div = 1U;
        (void)seq_runtime_get_track_div(track, &div);
        const uint32_t duration = (uint32_t)seq_model_get_track_playback_length(track) * (uint32_t)div;
        if (duration != longest_duration)
        {
            continue;
        }

        seq_step_id_t playhead = 0U;
        if ((seq_runtime_get_playhead_step(track, &playhead) != 0U) && (playhead == 0U))
        {
            return 1U;
        }
    }

    return 0U;
}

static uint8_t brick6_master_buffer_is_preroll_active(void)
{
    return ((seq_runtime_is_start_pending() != 0U)
            || (seq_runtime_rec_is_pattern_pending_start() != 0U)
            || (seq_runtime_get_rec_count_in_remaining_steps() > 0U)) ? 1U : 0U;
}

void brick6_master_buffer_init(live_recorder_t *rec,
                               float *storage,
                               uint32_t max_frames)
{
    g_buffer_recorder = rec;
    g_buffer_storage = storage;
    g_buffer_max_frames = max_frames;
    g_record_armed = 0U;
    g_recording = 0U;
    g_record_waiting_boundary = 0U;
    g_play_waiting_boundary = 0U;
    g_record_frames_written = 0U;
    g_record_target_frames = 0U;
    g_record_len_steps = 16U;
    g_quantize_record = 1U;
    g_quantize_play = 1U;
    g_rate = 1.0f;
    g_xfade = 0.0f;
    g_fade_in_amount = 0U;
    g_fade_out_amount = 0U;
    memset(g_source_enabled, 0, sizeof(g_source_enabled));
    for (uint8_t track = 0U; track < BRICK6_MASTER_BUFFER_MAX_SOURCE_TRACKS; ++track)
    {
        g_source_enabled[track] = 1U;
    }
    memset(g_capture_l, 0, sizeof(g_capture_l));
    memset(g_capture_r, 0, sizeof(g_capture_r));

    if (g_buffer_recorder == NULL)
    {
        return;
    }

    live_recorder_init(g_buffer_recorder);
    live_recorder_set_buffer(g_buffer_recorder, g_buffer_storage, g_buffer_max_frames);
    (void)brick6_master_buffer_refresh_record_target();
    live_recorder_set_play_rate(g_buffer_recorder, g_rate);
    live_recorder_start_play(g_buffer_recorder);
    live_recorder_clear(g_buffer_recorder);
}

void brick6_master_buffer_reset(void)
{
    brick6_master_buffer_clear();
    g_record_armed = 0U;
    g_recording = 0U;
    g_record_waiting_boundary = 0U;
    g_play_waiting_boundary = 0U;
    g_record_frames_written = 0U;
    g_record_target_frames = 0U;
    g_quantize_record = 1U;
    g_quantize_play = 1U;
    g_rate = 1.0f;
    g_xfade = 0.0f;
    g_fade_in_amount = 0U;
    g_fade_out_amount = 0U;
    memset(g_source_enabled, 0, sizeof(g_source_enabled));
    for (uint8_t track = 0U; track < BRICK6_MASTER_BUFFER_MAX_SOURCE_TRACKS; ++track)
    {
        g_source_enabled[track] = 1U;
    }
    if (g_buffer_recorder != NULL)
    {
        live_recorder_set_play_rate(g_buffer_recorder, g_rate);
        (void)brick6_master_buffer_refresh_record_target();
    }
}

void brick6_master_buffer_clear(void)
{
    g_record_armed = 0U;
    g_recording = 0U;
    g_record_waiting_boundary = 0U;
    g_play_waiting_boundary = 0U;
    g_record_frames_written = 0U;
    g_record_target_frames = 0U;
    memset(g_capture_l, 0, sizeof(g_capture_l));
    memset(g_capture_r, 0, sizeof(g_capture_r));

    if (g_buffer_recorder != NULL)
    {
        live_recorder_clear(g_buffer_recorder);
        live_recorder_start_play(g_buffer_recorder);
    }
}

void brick6_master_buffer_set_record_len(uint32_t steps)
{
    if (steps == 0U)
    {
        steps = 1U;
    }
    else if (steps > BRICK6_MASTER_BUFFER_MAX_RECORD_LEN_STEPS)
    {
        steps = BRICK6_MASTER_BUFFER_MAX_RECORD_LEN_STEPS;
    }

    g_record_len_steps = steps;
    (void)brick6_master_buffer_refresh_record_target();
}

void brick6_master_buffer_set_quantize_record(uint8_t enabled)
{
    g_quantize_record = (enabled != 0U) ? 1U : 0U;
}

void brick6_master_buffer_set_quantize_play(uint8_t enabled)
{
    g_quantize_play = (enabled != 0U) ? 1U : 0U;
}

void brick6_master_buffer_set_rate(float rate)
{
    g_rate = rate;
    if (g_buffer_recorder != NULL)
    {
        live_recorder_set_play_rate(g_buffer_recorder, rate);
    }
}

void brick6_master_buffer_set_xfade(float xfade)
{
    if (xfade < 0.0f)
    {
        xfade = 0.0f;
    }
    else if (xfade > 1.0f)
    {
        xfade = 1.0f;
    }

    g_xfade = xfade;
}

float brick6_master_buffer_get_xfade(void)
{
    return g_xfade;
}

void brick6_master_buffer_set_fade_in(uint32_t frames)
{
    g_fade_in_amount = frames;
}

void brick6_master_buffer_set_fade_out(uint32_t frames)
{
    g_fade_out_amount = frames;
}

void brick6_master_buffer_set_source_enabled(uint8_t track, uint8_t enabled)
{
    if (track >= BRICK6_MASTER_BUFFER_MAX_SOURCE_TRACKS)
    {
        return;
    }

    g_source_enabled[track] = (enabled != 0U) ? 1U : 0U;
}

void brick6_master_buffer_set_all_sources(uint8_t enabled)
{
    const uint8_t next = (enabled != 0U) ? 1U : 0U;
    for (uint8_t track = 0U; track < BRICK6_MASTER_BUFFER_MAX_SOURCE_TRACKS; ++track)
    {
        g_source_enabled[track] = next;
    }
}

uint8_t brick6_master_buffer_get_source_enabled(uint8_t track)
{
    if (track >= BRICK6_MASTER_BUFFER_MAX_SOURCE_TRACKS)
    {
        return 0U;
    }

    return g_source_enabled[track];
}

void brick6_master_buffer_request_record(void)
{
    if (g_recording != 0U)
    {
        if (g_buffer_recorder != NULL)
        {
            live_recorder_stop_record(g_buffer_recorder);
            if (g_buffer_recorder->recorded_frames >= 2U)
            {
                if ((g_quantize_play != 0U) && (seq_runtime_is_running() != 0U))
                {
                    g_play_waiting_boundary = 1U;
                }
                else
                {
                    live_recorder_start_play(g_buffer_recorder);
                    g_play_waiting_boundary = 0U;
                }
            }
            else
            {
                g_play_waiting_boundary = 0U;
            }
        }
        g_recording = 0U;
        g_record_armed = 0U;
        g_record_waiting_boundary = 0U;
        g_record_frames_written = 0U;
        (void)brick6_master_buffer_refresh_record_target();
        return;
    }

    if (g_record_armed != 0U)
    {
        g_record_armed = 0U;
        return;
    }

    g_record_armed = 1U;
    g_record_waiting_boundary = (((seq_runtime_is_running() != 0U) && (g_quantize_record != 0U))
                                 || (brick6_master_buffer_is_preroll_active() != 0U)) ? 1U : 0U;
    g_record_frames_written = 0U;
}

void brick6_master_buffer_request_clear(void)
{
    brick6_master_buffer_clear();
}

brick6_master_buffer_state_t brick6_master_buffer_get_state(void)
{
    if (g_recording != 0U)
    {
        return BRICK6_MASTER_BUFFER_STATE_RECORDING;
    }

    if (g_record_armed != 0U)
    {
        return BRICK6_MASTER_BUFFER_STATE_ARMED;
    }

    return BRICK6_MASTER_BUFFER_STATE_IDLE;
}

uint8_t brick6_master_buffer_is_recording(void)
{
    return g_recording;
}

uint8_t brick6_master_buffer_is_armed(void)
{
    return g_record_armed;
}

uint8_t brick6_master_buffer_is_waiting_start(void)
{
    if ((g_record_armed == 0U) || (g_recording != 0U))
    {
        return 0U;
    }

    return ((g_record_waiting_boundary != 0U)
            || (brick6_master_buffer_is_preroll_active() != 0U)) ? 1U : 0U;
}

uint32_t brick6_master_buffer_get_recorded_frames(void)
{
    if (g_buffer_recorder == NULL)
    {
        return 0U;
    }

    return g_buffer_recorder->recorded_frames;
}

uint32_t brick6_master_buffer_get_record_target_frames(void)
{
    return g_record_target_frames;
}

void brick6_master_buffer_begin_block(uint32_t frames)
{
    (void)frames;

    memset(g_capture_l, 0, sizeof(g_capture_l));
    memset(g_capture_r, 0, sizeof(g_capture_r));

    if ((g_record_armed != 0U) && (g_recording == 0U) && (g_buffer_recorder != NULL))
    {
        const uint8_t seq_running = (seq_runtime_is_running() != 0U) ? 1U : 0U;
        const uint8_t should_start_now = ((g_record_waiting_boundary == 0U)
                                          || ((seq_running != 0U)
                                              && (brick6_master_buffer_is_boundary_reached() != 0U)))
                                                 ? 1U
                                                 : 0U;
        if (should_start_now != 0U)
        {
            brick6_master_buffer_apply_record_target_to_recorder();
            live_recorder_stop_play(g_buffer_recorder);
            g_play_waiting_boundary = 0U;
            live_recorder_start_record(g_buffer_recorder);
            g_recording = 1U;
            g_record_armed = 0U;
            g_record_waiting_boundary = 0U;
            g_record_frames_written = 0U;
        }
    }

    if ((g_play_waiting_boundary != 0U)
            && (g_buffer_recorder != NULL)
            && (seq_runtime_is_running() != 0U)
            && (brick6_master_buffer_is_boundary_reached() != 0U)
            && (g_buffer_recorder->recorded_frames >= 2U))
    {
        live_recorder_start_play(g_buffer_recorder);
        g_play_waiting_boundary = 0U;
    }
}

void brick6_master_buffer_submit_track_post_fader(uint32_t track_id,
                                                  const float *left,
                                                  const float *right,
                                                  uint32_t frames)
{
    if ((g_recording == 0U)
            || (left == NULL)
            || (right == NULL)
            || (track_id >= BRICK6_MASTER_BUFFER_MAX_SOURCE_TRACKS)
            || (g_source_enabled[track_id] == 0U))
    {
        return;
    }

    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    for (uint32_t i = 0U; i < frames; ++i)
    {
        g_capture_l[i] += left[i];
        g_capture_r[i] += right[i];
    }
}

void brick6_master_buffer_commit_block(uint32_t frames)
{
    if ((g_recording == 0U) || (g_buffer_recorder == NULL))
    {
        return;
    }

    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    live_recorder_write(g_buffer_recorder, g_capture_l, g_capture_r, frames);
    g_record_frames_written += frames;
    if ((g_record_target_frames != 0U) && (g_record_frames_written >= g_record_target_frames))
    {
        live_recorder_stop_record(g_buffer_recorder);
        if (g_buffer_recorder->recorded_frames >= 2U)
        {
            if ((g_quantize_play != 0U) && (seq_runtime_is_running() != 0U))
            {
                g_play_waiting_boundary = 1U;
            }
            else
            {
                live_recorder_start_play(g_buffer_recorder);
                g_play_waiting_boundary = 0U;
            }
        }
        else
        {
            g_play_waiting_boundary = 0U;
        }
        g_recording = 0U;
        g_record_armed = 0U;
        g_record_waiting_boundary = 0U;
        g_record_frames_written = 0U;
        (void)brick6_master_buffer_refresh_record_target();
    }
}

void brick6_master_buffer_read_playback(float *left, float *right, uint32_t frames)
{
    if ((g_buffer_recorder == NULL) || (left == NULL) || (right == NULL) || (frames == 0U))
    {
        return;
    }

    const uint32_t loop_frames = g_buffer_recorder->recorded_frames;
    if (loop_frames < 2U)
    {
        memset(left, 0, sizeof(float) * frames);
        memset(right, 0, sizeof(float) * frames);
        return;
    }
    uint32_t sample_pos = g_buffer_recorder->read_pos;
    uint32_t sample_frac_q16 = g_buffer_recorder->read_pos_q16 & 0xFFFFU;
    const uint32_t read_step_q16 = (g_buffer_recorder->read_step_q16 == 0U) ? (1U << 16) : g_buffer_recorder->read_step_q16;
    live_recorder_read(g_buffer_recorder, left, right, frames);

    const uint32_t fade_in_frames = (g_fade_in_amount == 0U)
            ? 0U
            : (uint32_t)(((uint64_t)loop_frames * (uint64_t)g_fade_in_amount) / 127U);
    const uint32_t fade_out_frames = (g_fade_out_amount == 0U)
            ? 0U
            : (uint32_t)(((uint64_t)loop_frames * (uint64_t)g_fade_out_amount) / 127U);

    if ((loop_frames != 0U) && ((fade_in_frames != 0U) || (fade_out_frames != 0U)))
    {
        if (sample_pos >= loop_frames)
        {
            sample_pos %= loop_frames;
        }
        for (uint32_t i = 0U; i < frames; ++i)
        {
            const uint32_t sample_index = sample_pos;
            float gain = 1.0f;

            if (fade_in_frames != 0U)
            {
                if (sample_index < fade_in_frames)
                {
                    gain *= ((float)sample_index) / (float)fade_in_frames;
                }
            }

            if (fade_out_frames != 0U)
            {
                if (fade_out_frames < loop_frames)
                {
                    const uint32_t fade_out_start = loop_frames - fade_out_frames;
                    if (sample_index >= fade_out_start)
                    {
                        const uint32_t remaining = loop_frames - sample_index;
                        gain *= ((float)remaining) / (float)fade_out_frames;
                    }
                }
            }

            if (gain < 0.0f)
            {
                gain = 0.0f;
            }
            else if (gain > 1.0f)
            {
                gain = 1.0f;
            }

            left[i] *= gain;
            right[i] *= gain;

            sample_frac_q16 += read_step_q16;
            sample_pos += (sample_frac_q16 >> 16);
            sample_frac_q16 &= 0xFFFFU;
            while (sample_pos >= loop_frames)
            {
                sample_pos -= loop_frames;
            }
        }
    }
}
