#include "Core/brick6_master_buffer_stretch.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "Audio/audio_float.h"

#define BRICK6_MASTER_BUFFER_STRETCH_ANALYSIS_SLICE_FRAMES 512U
#define BRICK6_MASTER_BUFFER_STRETCH_TRANSIENT_MAX 128U
#define BRICK6_MASTER_BUFFER_STRETCH_TRANSIENT_HOLDOFF_FRAMES 2048U

#define BRICK6_MASTER_BUFFER_STRETCH_ECO_GRAIN_SIZE 256U
#define BRICK6_MASTER_BUFFER_STRETCH_ECO_HOP_SIZE 128U
#define BRICK6_MASTER_BUFFER_STRETCH_STD_GRAIN_SIZE 512U
#define BRICK6_MASTER_BUFFER_STRETCH_STD_HOP_SIZE 256U
#define BRICK6_MASTER_BUFFER_STRETCH_MAX_GRAIN_SIZE BRICK6_MASTER_BUFFER_STRETCH_STD_GRAIN_SIZE
#define BRICK6_MASTER_BUFFER_STRETCH_RING_SIZE 4096U
#define BRICK6_MASTER_BUFFER_STRETCH_MAX_ANALYSIS_WINDOW 64U
#define BRICK6_MASTER_BUFFER_STRETCH_VARISPEED_LIMIT_Q16 (8U << 16)
#define BRICK6_MASTER_BUFFER_STRETCH_Q16_ONE (1U << 16)

typedef struct
{
    const float *source_interleaved;
    uint32_t transient_frames[BRICK6_MASTER_BUFFER_STRETCH_TRANSIENT_MAX];
    float window_eco[BRICK6_MASTER_BUFFER_STRETCH_ECO_GRAIN_SIZE];
    float window_std[BRICK6_MASTER_BUFFER_STRETCH_STD_GRAIN_SIZE];
    float output_ring_l[BRICK6_MASTER_BUFFER_STRETCH_RING_SIZE];
    float output_ring_r[BRICK6_MASTER_BUFFER_STRETCH_RING_SIZE];
    brick6_master_buffer_stretch_config_t config;
    brick6_master_buffer_stretch_state_t state;
    uint64_t source_pos_q16;
    uint32_t output_read_index;
    uint32_t output_write_origin;
    uint32_t output_fill;
    uint32_t last_anchor_frame;
    uint8_t last_anchor_valid;
    uint8_t window_eco_ready;
    uint8_t window_std_ready;
} brick6_master_buffer_stretch_runtime_t;

static brick6_master_buffer_stretch_runtime_t g_master_buffer_stretch;

static uint32_t brick6_master_buffer_stretch_get_grain_size(void)
{
    return (g_master_buffer_stretch.config.quality == 0U)
            ? BRICK6_MASTER_BUFFER_STRETCH_ECO_GRAIN_SIZE
            : BRICK6_MASTER_BUFFER_STRETCH_STD_GRAIN_SIZE;
}

static uint32_t brick6_master_buffer_stretch_get_hop_size(void)
{
    return (g_master_buffer_stretch.config.quality == 0U)
            ? BRICK6_MASTER_BUFFER_STRETCH_ECO_HOP_SIZE
            : BRICK6_MASTER_BUFFER_STRETCH_STD_HOP_SIZE;
}

static const float *brick6_master_buffer_stretch_get_window(void)
{
    return (g_master_buffer_stretch.config.quality == 0U)
            ? g_master_buffer_stretch.window_eco
            : g_master_buffer_stretch.window_std;
}

static uint32_t brick6_master_buffer_stretch_wrap_frame(uint32_t frame)
{
    const uint32_t source_frames = g_master_buffer_stretch.state.source_frames;
    if (source_frames == 0U)
    {
        return 0U;
    }

    if (frame >= source_frames)
    {
        frame %= source_frames;
    }

    return frame;
}

static uint32_t brick6_master_buffer_stretch_wrap_frame_signed(int32_t frame)
{
    const uint32_t source_frames = g_master_buffer_stretch.state.source_frames;
    int32_t wrapped = frame;

    if (source_frames == 0U)
    {
        return 0U;
    }

    while (wrapped < 0)
    {
        wrapped += (int32_t)source_frames;
    }
    while (wrapped >= (int32_t)source_frames)
    {
        wrapped -= (int32_t)source_frames;
    }

    return (uint32_t)wrapped;
}

static uint32_t brick6_master_buffer_stretch_clamp_ratio_q16(uint32_t ratio_q16)
{
    if (ratio_q16 < 16384U)
    {
        return 16384U;
    }
    if (ratio_q16 > 262144U)
    {
        return 262144U;
    }
    return ratio_q16;
}

static void brick6_master_buffer_stretch_prepare_windows(void)
{
    if (g_master_buffer_stretch.window_eco_ready == 0U)
    {
        for (uint32_t i = 0U; i < BRICK6_MASTER_BUFFER_STRETCH_ECO_GRAIN_SIZE; ++i)
        {
            const float phase = (2.0f * 3.14159265358979323846f * (float)i)
                    / (float)BRICK6_MASTER_BUFFER_STRETCH_ECO_GRAIN_SIZE;
            g_master_buffer_stretch.window_eco[i] = 0.5f - (0.5f * cosf(phase));
        }
        g_master_buffer_stretch.window_eco_ready = 1U;
    }

    if (g_master_buffer_stretch.window_std_ready == 0U)
    {
        for (uint32_t i = 0U; i < BRICK6_MASTER_BUFFER_STRETCH_STD_GRAIN_SIZE; ++i)
        {
            const float phase = (2.0f * 3.14159265358979323846f * (float)i)
                    / (float)BRICK6_MASTER_BUFFER_STRETCH_STD_GRAIN_SIZE;
            g_master_buffer_stretch.window_std[i] = 0.5f - (0.5f * cosf(phase));
        }
        g_master_buffer_stretch.window_std_ready = 1U;
    }
}

static void brick6_master_buffer_stretch_reset_dsp_state(void)
{
    g_master_buffer_stretch.source_pos_q16 = 0U;
    g_master_buffer_stretch.output_read_index = 0U;
    g_master_buffer_stretch.output_write_origin = 0U;
    g_master_buffer_stretch.output_fill = 0U;
    g_master_buffer_stretch.last_anchor_frame = 0U;
    g_master_buffer_stretch.last_anchor_valid = 0U;
    memset(g_master_buffer_stretch.output_ring_l, 0, sizeof(g_master_buffer_stretch.output_ring_l));
    memset(g_master_buffer_stretch.output_ring_r, 0, sizeof(g_master_buffer_stretch.output_ring_r));
}

static void brick6_master_buffer_stretch_reset_runtime(void)
{
    memset(&g_master_buffer_stretch, 0, sizeof(g_master_buffer_stretch));
    g_master_buffer_stretch.config.ratio_q16 = BRICK6_MASTER_BUFFER_STRETCH_Q16_ONE;
    g_master_buffer_stretch.config.transient_sensitivity = 64U;
    g_master_buffer_stretch.config.preserve_pitch = 1U;
    g_master_buffer_stretch.config.source_bpm_milli = 120000U;
    g_master_buffer_stretch.state.status = BRICK6_MASTER_BUFFER_STRETCH_STATUS_BYPASS;
}

static uint8_t brick6_master_buffer_stretch_config_requests_processing(void)
{
    return (g_master_buffer_stretch.config.stretch_mode != 0U) ? 1U : 0U;
}

static void brick6_master_buffer_stretch_refresh_status(void)
{
    const uint32_t grain_size = brick6_master_buffer_stretch_get_grain_size();

    if ((g_master_buffer_stretch.source_interleaved == NULL)
            || (g_master_buffer_stretch.state.source_frames < grain_size)
            || (brick6_master_buffer_stretch_config_requests_processing() == 0U))
    {
        g_master_buffer_stretch.state.mode_active = 0U;
        g_master_buffer_stretch.state.status = BRICK6_MASTER_BUFFER_STRETCH_STATUS_BYPASS;
        return;
    }

    g_master_buffer_stretch.state.mode_active = 1U;
    g_master_buffer_stretch.state.status = BRICK6_MASTER_BUFFER_STRETCH_STATUS_READY;
}

static uint8_t brick6_master_buffer_stretch_analysis_needs_work(void)
{
    return ((g_master_buffer_stretch.state.analysis_pending != 0U)
            && (g_master_buffer_stretch.source_interleaved != NULL)
            && (g_master_buffer_stretch.state.source_frames >= 2U)
            && (brick6_master_buffer_stretch_config_requests_processing() != 0U)) ? 1U : 0U;
}

static float brick6_master_buffer_stretch_get_mono_energy(uint32_t frame_index)
{
    const uint32_t idx = frame_index * 2U;
    const float left = g_master_buffer_stretch.source_interleaved[idx];
    const float right = g_master_buffer_stretch.source_interleaved[idx + 1U];
    const float mono = 0.5f * (left + right);
    return mono * mono;
}

static uint8_t brick6_master_buffer_stretch_should_flag_transient(float prev_energy,
                                                                  float curr_energy,
                                                                  uint32_t frame_index)
{
    const uint8_t tsns = g_master_buffer_stretch.config.transient_sensitivity;
    const float threshold = 0.0025f + (((float)tsns / 127.0f) * 0.08f);
    const float delta = curr_energy - prev_energy;

    if (delta <= threshold)
    {
        return 0U;
    }

    if (g_master_buffer_stretch.state.transient_count == 0U)
    {
        return 1U;
    }

    {
        const uint32_t last_index = (uint32_t)g_master_buffer_stretch.state.transient_count - 1U;
        const uint32_t last_frame = g_master_buffer_stretch.transient_frames[last_index];
        return ((frame_index - last_frame) >= BRICK6_MASTER_BUFFER_STRETCH_TRANSIENT_HOLDOFF_FRAMES) ? 1U : 0U;
    }
}

static float brick6_master_buffer_stretch_sample_source(uint32_t channel, uint64_t frame_pos_q16)
{
    const uint32_t source_frames = g_master_buffer_stretch.state.source_frames;
    const uint64_t loop_q16 = ((uint64_t)source_frames << 16);
    uint64_t pos_q16 = frame_pos_q16;
    uint32_t base = 0U;
    uint32_t next = 0U;
    float frac = 0.0f;

    if ((g_master_buffer_stretch.source_interleaved == NULL) || (source_frames < 2U))
    {
        return 0.0f;
    }

    while ((loop_q16 != 0U) && (pos_q16 >= loop_q16))
    {
        pos_q16 -= loop_q16;
    }

    base = (uint32_t)(pos_q16 >> 16);
    next = base + 1U;
    if (next >= source_frames)
    {
        next = 0U;
    }
    frac = (float)(pos_q16 & 0xFFFFU) * (1.0f / 65536.0f);

    {
        const uint32_t idx0 = (base * 2U) + channel;
        const uint32_t idx1 = (next * 2U) + channel;
        const float s0 = g_master_buffer_stretch.source_interleaved[idx0];
        const float s1 = g_master_buffer_stretch.source_interleaved[idx1];
        return s0 + ((s1 - s0) * frac);
    }
}

static void brick6_master_buffer_stretch_emit_grain(uint64_t source_start_q16)
{
    const uint32_t grain_size = brick6_master_buffer_stretch_get_grain_size();
    const float *const window = brick6_master_buffer_stretch_get_window();
    const uint32_t hop_size = brick6_master_buffer_stretch_get_hop_size();

    for (uint32_t i = 0U; i < grain_size; ++i)
    {
        const uint64_t sample_pos_q16 = source_start_q16 + ((uint64_t)i << 16);
        const uint32_t ring_index = (g_master_buffer_stretch.output_write_origin + i)
                % BRICK6_MASTER_BUFFER_STRETCH_RING_SIZE;
        const float gain = window[i];
        const float sample_l = brick6_master_buffer_stretch_sample_source(0U, sample_pos_q16) * gain;
        const float sample_r = brick6_master_buffer_stretch_sample_source(1U, sample_pos_q16) * gain;
        g_master_buffer_stretch.output_ring_l[ring_index] += sample_l;
        g_master_buffer_stretch.output_ring_r[ring_index] += sample_r;
    }

    g_master_buffer_stretch.output_write_origin =
            (g_master_buffer_stretch.output_write_origin + hop_size)
            % BRICK6_MASTER_BUFFER_STRETCH_RING_SIZE;
    g_master_buffer_stretch.output_fill += hop_size;
    if (g_master_buffer_stretch.output_fill > BRICK6_MASTER_BUFFER_STRETCH_RING_SIZE)
    {
        g_master_buffer_stretch.output_fill = BRICK6_MASTER_BUFFER_STRETCH_RING_SIZE;
    }
}

static uint8_t brick6_master_buffer_stretch_find_nearby_transient(uint32_t source_frame,
                                                                  uint32_t *out_transient_frame)
{
    if ((out_transient_frame == NULL) || (g_master_buffer_stretch.state.transient_count == 0U))
    {
        return 0U;
    }

    {
        const uint32_t max_distance = 32U + (((uint32_t)g_master_buffer_stretch.config.transient_sensitivity * 3U) >> 1U);
        uint32_t best_frame = 0U;
        uint32_t best_distance = 0xFFFFFFFFU;

        for (uint32_t i = 0U; i < g_master_buffer_stretch.state.transient_count; ++i)
        {
            const uint32_t candidate = g_master_buffer_stretch.transient_frames[i];
            const uint32_t distance = (candidate > source_frame)
                    ? (candidate - source_frame)
                    : (source_frame - candidate);
            if ((distance <= max_distance) && (distance < best_distance))
            {
                best_distance = distance;
                best_frame = candidate;
            }
        }

        if (best_distance == 0xFFFFFFFFU)
        {
            return 0U;
        }

        *out_transient_frame = best_frame;
        return 1U;
    }
}

static uint64_t brick6_master_buffer_stretch_resolve_grain_start_q16(void)
{
    uint64_t source_start_q16 = g_master_buffer_stretch.source_pos_q16;

    if ((g_master_buffer_stretch.config.stretch_mode == 2U)
            && (g_master_buffer_stretch.state.analysis_ready != 0U)
            && (g_master_buffer_stretch.state.transient_count > 0U))
    {
        const uint32_t grain_size = brick6_master_buffer_stretch_get_grain_size();
        const uint32_t source_frame = (uint32_t)(g_master_buffer_stretch.source_pos_q16 >> 16);
        uint32_t transient_frame = 0U;

        if ((brick6_master_buffer_stretch_find_nearby_transient(source_frame, &transient_frame) != 0U)
                && ((g_master_buffer_stretch.last_anchor_valid == 0U)
                    || (transient_frame != g_master_buffer_stretch.last_anchor_frame)))
        {
            const int32_t pre_roll = (int32_t)(grain_size >> 2U);
            const uint32_t anchor_start = brick6_master_buffer_stretch_wrap_frame_signed((int32_t)transient_frame - pre_roll);
            source_start_q16 = ((uint64_t)anchor_start << 16);
            g_master_buffer_stretch.source_pos_q16 = source_start_q16;
            g_master_buffer_stretch.last_anchor_frame = transient_frame;
            g_master_buffer_stretch.last_anchor_valid = 1U;
        }
    }

    return source_start_q16;
}

static uint8_t brick6_master_buffer_stretch_process_frame(void)
{
    const uint32_t grain_size = brick6_master_buffer_stretch_get_grain_size();
    const uint32_t hop_size = brick6_master_buffer_stretch_get_hop_size();
    const uint32_t source_frames = g_master_buffer_stretch.state.source_frames;
    const uint64_t loop_q16 = ((uint64_t)source_frames << 16);
    uint64_t analysis_hop_q16 = 0U;

    if ((g_master_buffer_stretch.source_interleaved == NULL) || (source_frames < grain_size))
    {
        return 0U;
    }

    if (g_master_buffer_stretch.output_fill > (BRICK6_MASTER_BUFFER_STRETCH_RING_SIZE - grain_size - hop_size))
    {
        return 0U;
    }

    brick6_master_buffer_stretch_prepare_windows();
    brick6_master_buffer_stretch_emit_grain(brick6_master_buffer_stretch_resolve_grain_start_q16());

    analysis_hop_q16 = g_master_buffer_stretch.config.ratio_q16 * hop_size;
    if (analysis_hop_q16 < BRICK6_MASTER_BUFFER_STRETCH_Q16_ONE)
    {
        analysis_hop_q16 = BRICK6_MASTER_BUFFER_STRETCH_Q16_ONE;
    }

    g_master_buffer_stretch.source_pos_q16 += analysis_hop_q16;
    while ((loop_q16 != 0U) && (g_master_buffer_stretch.source_pos_q16 >= loop_q16))
    {
        g_master_buffer_stretch.source_pos_q16 -= loop_q16;
        g_master_buffer_stretch.last_anchor_valid = 0U;
    }

    return 1U;
}

static void brick6_master_buffer_stretch_render_varispeed(float *left, float *right, uint32_t frames)
{
    const uint32_t source_frames = g_master_buffer_stretch.state.source_frames;
    const uint64_t loop_q16 = ((uint64_t)source_frames << 16);
    uint64_t step_q16 = g_master_buffer_stretch.config.ratio_q16;

    if ((source_frames < 2U) || (loop_q16 == 0U))
    {
        memset(left, 0, sizeof(float) * frames);
        memset(right, 0, sizeof(float) * frames);
        return;
    }

    if (step_q16 == 0U)
    {
        step_q16 = BRICK6_MASTER_BUFFER_STRETCH_Q16_ONE;
    }
    if (step_q16 > BRICK6_MASTER_BUFFER_STRETCH_VARISPEED_LIMIT_Q16)
    {
        step_q16 = BRICK6_MASTER_BUFFER_STRETCH_VARISPEED_LIMIT_Q16;
    }

    for (uint32_t i = 0U; i < frames; ++i)
    {
        left[i] = brick6_master_buffer_stretch_sample_source(0U, g_master_buffer_stretch.source_pos_q16);
        right[i] = brick6_master_buffer_stretch_sample_source(1U, g_master_buffer_stretch.source_pos_q16);
        g_master_buffer_stretch.source_pos_q16 += step_q16;
        while (g_master_buffer_stretch.source_pos_q16 >= loop_q16)
        {
            g_master_buffer_stretch.source_pos_q16 -= loop_q16;
            g_master_buffer_stretch.last_anchor_valid = 0U;
        }
    }
}

void brick6_master_buffer_stretch_init(uint32_t max_frames)
{
    brick6_master_buffer_stretch_reset_runtime();
    g_master_buffer_stretch.state.max_frames = max_frames;
}

void brick6_master_buffer_stretch_reset(void)
{
    const uint32_t max_frames = g_master_buffer_stretch.state.max_frames;
    brick6_master_buffer_stretch_reset_runtime();
    g_master_buffer_stretch.state.max_frames = max_frames;
}

void brick6_master_buffer_stretch_clear(void)
{
    g_master_buffer_stretch.source_interleaved = NULL;
    g_master_buffer_stretch.state.source_generation++;
    g_master_buffer_stretch.state.source_frames = 0U;
    g_master_buffer_stretch.state.analysis_cursor = 0U;
    g_master_buffer_stretch.state.transient_count = 0U;
    g_master_buffer_stretch.state.analysis_pending = 0U;
    g_master_buffer_stretch.state.analysis_ready = 0U;
    memset(g_master_buffer_stretch.transient_frames, 0, sizeof(g_master_buffer_stretch.transient_frames));
    brick6_master_buffer_stretch_reset_dsp_state();
    brick6_master_buffer_stretch_refresh_status();
}

void brick6_master_buffer_stretch_set_config(const brick6_master_buffer_stretch_config_t *config)
{
    brick6_master_buffer_stretch_config_t next_config;
    uint8_t mode_changed;
    uint8_t quality_changed;
    uint8_t preserve_pitch_changed;

    if (config == NULL)
    {
        return;
    }

    next_config = *config;
    next_config.ratio_q16 = brick6_master_buffer_stretch_clamp_ratio_q16(next_config.ratio_q16);

    if (memcmp(&g_master_buffer_stretch.config, &next_config, sizeof(next_config)) == 0)
    {
        brick6_master_buffer_stretch_refresh_status();
        return;
    }

    mode_changed = (g_master_buffer_stretch.config.stretch_mode != next_config.stretch_mode) ? 1U : 0U;
    quality_changed = (g_master_buffer_stretch.config.quality != next_config.quality) ? 1U : 0U;
    preserve_pitch_changed = (g_master_buffer_stretch.config.preserve_pitch != next_config.preserve_pitch) ? 1U : 0U;

    g_master_buffer_stretch.config = next_config;
    if ((mode_changed != 0U) || (quality_changed != 0U) || (preserve_pitch_changed != 0U))
    {
        g_master_buffer_stretch.state.config_generation++;
        brick6_master_buffer_stretch_reset_dsp_state();
    }
    brick6_master_buffer_stretch_refresh_status();
}

void brick6_master_buffer_stretch_notify_record_started(uint32_t source_generation)
{
    g_master_buffer_stretch.source_interleaved = NULL;
    g_master_buffer_stretch.state.source_generation = source_generation;
    g_master_buffer_stretch.state.source_frames = 0U;
    g_master_buffer_stretch.state.analysis_cursor = 0U;
    g_master_buffer_stretch.state.transient_count = 0U;
    g_master_buffer_stretch.state.analysis_pending = 0U;
    g_master_buffer_stretch.state.analysis_ready = 0U;
    memset(g_master_buffer_stretch.transient_frames, 0, sizeof(g_master_buffer_stretch.transient_frames));
    brick6_master_buffer_stretch_reset_dsp_state();
    brick6_master_buffer_stretch_refresh_status();
}

void brick6_master_buffer_stretch_notify_record_finished(const float *interleaved_stereo,
                                                         uint32_t recorded_frames,
                                                         uint32_t max_frames,
                                                         uint32_t source_generation)
{
    brick6_master_buffer_stretch_set_source(interleaved_stereo,
                                            recorded_frames,
                                            max_frames,
                                            source_generation);
}

void brick6_master_buffer_stretch_notify_record_stopped(const float *interleaved_stereo,
                                                        uint32_t recorded_frames,
                                                        uint32_t max_frames,
                                                        uint32_t source_generation)
{
    brick6_master_buffer_stretch_set_source(interleaved_stereo,
                                            recorded_frames,
                                            max_frames,
                                            source_generation);
}

void brick6_master_buffer_stretch_set_source(const float *interleaved_stereo,
                                             uint32_t recorded_frames,
                                             uint32_t max_frames,
                                             uint32_t source_generation)
{
    g_master_buffer_stretch.source_interleaved = interleaved_stereo;
    g_master_buffer_stretch.state.source_frames = recorded_frames;
    g_master_buffer_stretch.state.max_frames = max_frames;
    g_master_buffer_stretch.state.source_generation = source_generation;
    g_master_buffer_stretch.state.analysis_cursor = 0U;
    g_master_buffer_stretch.state.transient_count = 0U;
    g_master_buffer_stretch.state.analysis_pending = (recorded_frames >= 2U) ? 1U : 0U;
    g_master_buffer_stretch.state.analysis_ready = 0U;
    memset(g_master_buffer_stretch.transient_frames, 0, sizeof(g_master_buffer_stretch.transient_frames));
    brick6_master_buffer_stretch_reset_dsp_state();
    brick6_master_buffer_stretch_refresh_status();
}

void brick6_master_buffer_stretch_mark_analysis_ready(uint32_t source_generation)
{
    if (source_generation != g_master_buffer_stretch.state.source_generation)
    {
        return;
    }

    g_master_buffer_stretch.state.analysis_ready = 1U;
}

void brick6_master_buffer_stretch_get_state(brick6_master_buffer_stretch_state_t *out_state)
{
    if (out_state == NULL)
    {
        return;
    }

    *out_state = g_master_buffer_stretch.state;
}

uint8_t brick6_master_buffer_stretch_is_ready(void)
{
    return (g_master_buffer_stretch.state.status == BRICK6_MASTER_BUFFER_STRETCH_STATUS_READY) ? 1U : 0U;
}

uint8_t brick6_master_buffer_stretch_is_active(void)
{
    return g_master_buffer_stretch.state.mode_active;
}

void brick6_master_buffer_stretch_service_analysis(void)
{
    if (brick6_master_buffer_stretch_analysis_needs_work() == 0U)
    {
        return;
    }

    {
        const uint32_t source_frames = g_master_buffer_stretch.state.source_frames;
        uint32_t cursor = g_master_buffer_stretch.state.analysis_cursor;
        uint32_t end = cursor + BRICK6_MASTER_BUFFER_STRETCH_ANALYSIS_SLICE_FRAMES;

        if (end > source_frames)
        {
            end = source_frames;
        }

        if (cursor == 0U)
        {
            cursor = 1U;
        }

        for (; cursor < end; ++cursor)
        {
            float prev_avg = 0.0f;
            float curr_avg = 0.0f;
            uint32_t count = 0U;
            const uint32_t window = BRICK6_MASTER_BUFFER_STRETCH_MAX_ANALYSIS_WINDOW;

            for (uint32_t w = 0U; w < window; ++w)
            {
                const uint32_t prev_index = brick6_master_buffer_stretch_wrap_frame_signed((int32_t)cursor - (int32_t)(w + 1U));
                const uint32_t curr_index = brick6_master_buffer_stretch_wrap_frame(cursor + w);
                prev_avg += brick6_master_buffer_stretch_get_mono_energy(prev_index);
                curr_avg += brick6_master_buffer_stretch_get_mono_energy(curr_index);
                count++;
            }

            if (count != 0U)
            {
                prev_avg /= (float)count;
                curr_avg /= (float)count;
            }

            if ((g_master_buffer_stretch.state.transient_count < BRICK6_MASTER_BUFFER_STRETCH_TRANSIENT_MAX)
                    && (brick6_master_buffer_stretch_should_flag_transient(prev_avg, curr_avg, cursor) != 0U))
            {
                g_master_buffer_stretch.transient_frames[g_master_buffer_stretch.state.transient_count] = cursor;
                g_master_buffer_stretch.state.transient_count++;
            }
        }

        g_master_buffer_stretch.state.analysis_cursor = end;
        if (end >= source_frames)
        {
            g_master_buffer_stretch.state.analysis_pending = 0U;
            brick6_master_buffer_stretch_mark_analysis_ready(g_master_buffer_stretch.state.source_generation);
        }
    }
}

void brick6_master_buffer_stretch_render_block(float *left, float *right, uint32_t frames)
{
    if ((left == NULL) || (right == NULL))
    {
        return;
    }

    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    if (g_master_buffer_stretch.state.status != BRICK6_MASTER_BUFFER_STRETCH_STATUS_READY)
    {
        memset(left, 0, sizeof(float) * frames);
        memset(right, 0, sizeof(float) * frames);
        return;
    }

    if (g_master_buffer_stretch.config.preserve_pitch == 0U)
    {
        brick6_master_buffer_stretch_render_varispeed(left, right, frames);
        return;
    }

    while (g_master_buffer_stretch.output_fill < frames)
    {
        if (brick6_master_buffer_stretch_process_frame() == 0U)
        {
            break;
        }
    }

    for (uint32_t i = 0U; i < frames; ++i)
    {
        if (g_master_buffer_stretch.output_fill == 0U)
        {
            left[i] = 0.0f;
            right[i] = 0.0f;
            continue;
        }

        left[i] = g_master_buffer_stretch.output_ring_l[g_master_buffer_stretch.output_read_index];
        right[i] = g_master_buffer_stretch.output_ring_r[g_master_buffer_stretch.output_read_index];
        g_master_buffer_stretch.output_ring_l[g_master_buffer_stretch.output_read_index] = 0.0f;
        g_master_buffer_stretch.output_ring_r[g_master_buffer_stretch.output_read_index] = 0.0f;
        g_master_buffer_stretch.output_read_index =
                (g_master_buffer_stretch.output_read_index + 1U) % BRICK6_MASTER_BUFFER_STRETCH_RING_SIZE;
        g_master_buffer_stretch.output_fill--;
    }
}
