#include "Core/brick6_clip_stretch.h"

#include <math.h>
#include <string.h>

#define BRICK6_CLIP_STRETCH_RATIO_Q16_ONE (1U << 16)
#define BRICK6_CLIP_STRETCH_RATIO_Q16_MIN (1U << 15)
#define BRICK6_CLIP_STRETCH_RATIO_Q16_MAX (2U << 16)

enum
{
    BRICK6_CLIP_STRETCH_ERROR_NONE = 0,
    BRICK6_CLIP_STRETCH_ERROR_GUARD_NULL = 1,
    BRICK6_CLIP_STRETCH_ERROR_GUARD_CHANNEL = 2,
    BRICK6_CLIP_STRETCH_ERROR_GUARD_FIFO_RANGE = 3,
    BRICK6_CLIP_STRETCH_ERROR_GUARD_OUTPUT_RANGE = 4,
    BRICK6_CLIP_STRETCH_ERROR_GUARD_INPUT_RANGE = 5,
    BRICK6_CLIP_STRETCH_ERROR_GUARD_RENDER_ZERO = 6,
    BRICK6_CLIP_STRETCH_ERROR_FALLBACK_BYPASS = 7
};

volatile brick6_clip_stretch_diag_snapshot_t g_brick6_clip_stretch_diag_snapshot;

static void brick6_clip_stretch_diag_guard_fail(uint32_t error_code)
{
    g_brick6_clip_stretch_diag_snapshot.guard_fail_count++;
    g_brick6_clip_stretch_diag_snapshot.last_error_code = error_code;
}

static uint16_t brick6_clip_stretch_sanitize_grain_size(uint16_t grain_size)
{
    switch (grain_size)
    {
        case 32U:
        case 64U:
        case 96U:
        case 128U:
        case 256U:
        case 512U:
            return grain_size;
        default:
            return BRICK6_CLIP_STRETCH_DEFAULT_GRAIN_FRAMES;
    }
}

static uint16_t brick6_clip_stretch_sanitize_hop_size(uint16_t hop_size, uint16_t grain_size)
{
    uint16_t sanitized = BRICK6_CLIP_STRETCH_DEFAULT_HOP_FRAMES;

    switch (hop_size)
    {
        case 32U:
        case 64U:
        case 96U:
        case 128U:
        case 256U:
        case 512U:
            sanitized = hop_size;
            break;
        default:
            break;
    }

    if (sanitized > grain_size)
    {
        sanitized = grain_size;
    }

    return sanitized;
}

static uint16_t brick6_clip_stretch_sanitize_search_frames(uint16_t search_frames)
{
    switch (search_frames)
    {
        case 0U:
        case 4U:
        case 8U:
        case 12U:
        case 16U:
            return search_frames;
        default:
            return BRICK6_CLIP_STRETCH_DEFAULT_SEARCH_FRAMES;
    }
}

static uint16_t brick6_clip_stretch_grain_size(const brick6_clip_stretch_t *stretch)
{
    if (stretch == NULL)
    {
        return BRICK6_CLIP_STRETCH_DEFAULT_GRAIN_FRAMES;
    }

    return brick6_clip_stretch_sanitize_grain_size(stretch->config.grain_size);
}

static uint16_t brick6_clip_stretch_hop_size(const brick6_clip_stretch_t *stretch)
{
    const uint16_t grain_size = brick6_clip_stretch_grain_size(stretch);

    if (stretch == NULL)
    {
        return brick6_clip_stretch_sanitize_hop_size(BRICK6_CLIP_STRETCH_DEFAULT_HOP_FRAMES, grain_size);
    }

    return brick6_clip_stretch_sanitize_hop_size(stretch->config.hop_size, grain_size);
}

static uint16_t brick6_clip_stretch_search_frames(const brick6_clip_stretch_t *stretch)
{
    if (stretch == NULL)
    {
        return BRICK6_CLIP_STRETCH_DEFAULT_SEARCH_FRAMES;
    }

    return brick6_clip_stretch_sanitize_search_frames(stretch->config.search_frames);
}

static uint16_t brick6_clip_stretch_correlation_frames(const brick6_clip_stretch_t *stretch)
{
    uint16_t grain_size;
    uint16_t hop_size;
    uint16_t search_frames;
    uint16_t overlap_frames;
    uint16_t correlation_frames;
    uint16_t search_limited_frames;

    if (stretch == NULL)
    {
        return 0U;
    }

    grain_size = brick6_clip_stretch_grain_size(stretch);
    hop_size = brick6_clip_stretch_hop_size(stretch);
    search_frames = brick6_clip_stretch_search_frames(stretch);

    if ((search_frames == 0U) || (hop_size >= grain_size))
    {
        return 0U;
    }

    overlap_frames = grain_size - hop_size;
    correlation_frames = BRICK6_CLIP_STRETCH_CORRELATION_FRAMES;
    if (correlation_frames > hop_size)
    {
        correlation_frames = hop_size;
    }
    if (correlation_frames > overlap_frames)
    {
        correlation_frames = overlap_frames;
    }

    search_limited_frames = (uint16_t)(16U + (search_frames * 2U));
    if (correlation_frames > search_limited_frames)
    {
        correlation_frames = search_limited_frames;
    }

    return correlation_frames;
}

static brick6_clip_stretch_config_t brick6_clip_stretch_default_config(void)
{
    brick6_clip_stretch_config_t config;
    config.ratio_q16 = BRICK6_CLIP_STRETCH_RATIO_Q16_ONE;
    config.grain_size = BRICK6_CLIP_STRETCH_DEFAULT_GRAIN_FRAMES;
    config.hop_size = BRICK6_CLIP_STRETCH_DEFAULT_HOP_FRAMES;
    config.search_frames = BRICK6_CLIP_STRETCH_DEFAULT_SEARCH_FRAMES;
    config.mode = BRICK6_CLIP_STRETCH_MODE_BYPASS;
    config.sync_len = BRICK6_CLIP_STRETCH_SYNC_LEN_OFF;
    return config;
}

static brick6_clip_stretch_config_t brick6_clip_stretch_sanitize_config(const brick6_clip_stretch_config_t *config)
{
    brick6_clip_stretch_config_t sanitized = brick6_clip_stretch_default_config();

    if (config == NULL)
    {
        return sanitized;
    }

    if (config->ratio_q16 != 0U)
    {
        sanitized.ratio_q16 = config->ratio_q16;
    }
    if (sanitized.ratio_q16 < BRICK6_CLIP_STRETCH_RATIO_Q16_MIN)
    {
        sanitized.ratio_q16 = BRICK6_CLIP_STRETCH_RATIO_Q16_MIN;
    }
    else if (sanitized.ratio_q16 > BRICK6_CLIP_STRETCH_RATIO_Q16_MAX)
    {
        sanitized.ratio_q16 = BRICK6_CLIP_STRETCH_RATIO_Q16_MAX;
    }

    switch (config->mode)
    {
        case BRICK6_CLIP_STRETCH_MODE_BYPASS:
        case BRICK6_CLIP_STRETCH_MODE_PRESERVE_PITCH:
            sanitized.mode = config->mode;
            break;
        default:
            break;
    }

    switch (config->sync_len)
    {
        case BRICK6_CLIP_STRETCH_SYNC_LEN_OFF:
        case BRICK6_CLIP_STRETCH_SYNC_LEN_HALF:
        case BRICK6_CLIP_STRETCH_SYNC_LEN_QUARTER:
        case BRICK6_CLIP_STRETCH_SYNC_LEN_AUTO:
            sanitized.sync_len = config->sync_len;
            break;
        default:
            break;
    }

    sanitized.grain_size = brick6_clip_stretch_sanitize_grain_size(config->grain_size);
    sanitized.hop_size = brick6_clip_stretch_sanitize_hop_size(config->hop_size, sanitized.grain_size);
    sanitized.search_frames = brick6_clip_stretch_sanitize_search_frames(config->search_frames);

    return sanitized;
}

static void brick6_clip_stretch_prepare_window(brick6_clip_stretch_t *stretch)
{
    uint16_t grain_size;
    float denom;

    if (stretch == NULL)
    {
        return;
    }

    grain_size = brick6_clip_stretch_grain_size(stretch);
    if (stretch->window_grain_size == grain_size)
    {
        return;
    }

    /* Fixed WSOLA window keeps the overlap-add path bounded in IRQ. */
    denom = (grain_size > 1U) ? (float)(grain_size - 1U) : 1.0f;
    for (uint32_t i = 0U; i < grain_size; ++i)
    {
        const float phase = (2.0f * 3.14159265358979323846f * (float)i) / denom;
        stretch->ola_window[i] = 0.5f - (0.5f * cosf(phase));
    }

    stretch->window_grain_size = grain_size;
}

static uint32_t brick6_clip_stretch_fifo_frame_count(const brick6_clip_stretch_t *stretch)
{
    return (stretch == NULL) ? 0U : stretch->queued_frames;
}

static uint32_t brick6_clip_stretch_render_bypass(brick6_clip_stretch_t *stretch,
                                                  float *out_left,
                                                  float *out_right,
                                                  uint32_t frames)
{
    uint32_t produced = 0U;

    if ((stretch == NULL) || (out_left == NULL) || (out_right == NULL))
    {
        brick6_clip_stretch_diag_guard_fail(BRICK6_CLIP_STRETCH_ERROR_GUARD_NULL);
        return 0U;
    }

    if (stretch->queued_frames > BRICK6_CLIP_STRETCH_INPUT_CAPACITY_FRAMES)
    {
        stretch->queued_frames = BRICK6_CLIP_STRETCH_INPUT_CAPACITY_FRAMES;
        brick6_clip_stretch_diag_guard_fail(BRICK6_CLIP_STRETCH_ERROR_GUARD_INPUT_RANGE);
    }

    produced = frames;
    if (stretch->queued_frames < produced)
    {
        produced = stretch->queued_frames;
    }

    for (uint32_t i = 0U; i < produced; ++i)
    {
        const uint32_t src = stretch->read_index * 2U;
        out_left[i] = stretch->input_interleaved[src];
        out_right[i] = stretch->input_interleaved[src + 1U];
        stretch->read_index++;
        if (stretch->read_index >= BRICK6_CLIP_STRETCH_INPUT_CAPACITY_FRAMES)
        {
            stretch->read_index = 0U;
        }
    }

    stretch->queued_frames -= produced;
    return produced;
}

static void brick6_clip_stretch_discard_input_frames(brick6_clip_stretch_t *stretch, uint32_t frames)
{
    if ((stretch == NULL) || (frames == 0U))
    {
        return;
    }

    if (frames > stretch->queued_frames)
    {
        frames = stretch->queued_frames;
    }

    stretch->read_index = (stretch->read_index + frames) % BRICK6_CLIP_STRETCH_INPUT_CAPACITY_FRAMES;
    stretch->queued_frames -= frames;
}

static float brick6_clip_stretch_sample_fifo(const brick6_clip_stretch_t *stretch,
                                             uint32_t channel,
                                             uint32_t base_frame,
                                             float frac)
{
    if ((stretch == NULL) || (channel > 1U))
    {
        brick6_clip_stretch_diag_guard_fail((stretch == NULL)
                                                ? BRICK6_CLIP_STRETCH_ERROR_GUARD_NULL
                                                : BRICK6_CLIP_STRETCH_ERROR_GUARD_CHANNEL);
        return 0.0f;
    }

    if ((base_frame + 1U) >= stretch->queued_frames)
    {
        brick6_clip_stretch_diag_guard_fail(BRICK6_CLIP_STRETCH_ERROR_GUARD_FIFO_RANGE);
        return 0.0f;
    }

    const uint32_t next_frame = base_frame + 1U;
    const uint32_t index0 =
            ((stretch->read_index + base_frame) % BRICK6_CLIP_STRETCH_INPUT_CAPACITY_FRAMES) * 2U;
    const uint32_t index1 =
            ((stretch->read_index + next_frame) % BRICK6_CLIP_STRETCH_INPUT_CAPACITY_FRAMES) * 2U;
    const float s0 = stretch->input_interleaved[index0 + channel];
    const float s1 = stretch->input_interleaved[index1 + channel];
    return s0 + ((s1 - s0) * frac);
}

static float brick6_clip_stretch_sample_fifo_mono(const brick6_clip_stretch_t *stretch,
                                                  uint32_t base_frame,
                                                  float frac)
{
    const float left = brick6_clip_stretch_sample_fifo(stretch, 0U, base_frame, frac);
    const float right = brick6_clip_stretch_sample_fifo(stretch, 1U, base_frame, frac);
    return 0.5f * (left + right);
}

static uint32_t brick6_clip_stretch_select_wsola_base(const brick6_clip_stretch_t *stretch,
                                                      uint32_t expected_base_frame,
                                                      float frac,
                                                      uint32_t available)
{
    const uint32_t needed_frames = (uint32_t)brick6_clip_stretch_grain_size(stretch) + 1U;
    const uint32_t correlation_frames = (uint32_t)brick6_clip_stretch_correlation_frames(stretch);
    const uint32_t search_frames = (uint32_t)brick6_clip_stretch_search_frames(stretch);
    uint32_t max_base_frame;
    uint32_t best_base_frame;
    float best_score;

    if ((stretch == NULL) || (available < needed_frames))
    {
        return 0U;
    }

    max_base_frame = available - needed_frames;
    best_base_frame = (expected_base_frame <= max_base_frame) ? expected_base_frame : max_base_frame;

    if ((stretch->prev_overlap_valid == 0U) || (correlation_frames == 0U) || (search_frames == 0U))
    {
        return best_base_frame;
    }

    best_score = -3.402823466e38f;

    {
        int32_t start_offset = -((int32_t)search_frames);
        int32_t end_offset = (int32_t)search_frames;
        for (int32_t offset = start_offset; offset <= end_offset; ++offset)
        {
            int32_t candidate_frame = (int32_t)expected_base_frame + offset;
            float score = 0.0f;

            if (candidate_frame < 0)
            {
                continue;
            }

            if ((uint32_t)candidate_frame > max_base_frame)
            {
                continue;
            }

            for (uint32_t i = 0U; i < correlation_frames; ++i)
            {
                const float current =
                    brick6_clip_stretch_sample_fifo_mono(stretch, (uint32_t)candidate_frame + i, frac);
                score += stretch->prev_overlap_mono[i] * current;
            }

            if (score > best_score)
            {
                best_score = score;
                best_base_frame = (uint32_t)candidate_frame;
            }
        }
    }

    return best_base_frame;
}

static void brick6_clip_stretch_store_prev_overlap(brick6_clip_stretch_t *stretch,
                                                   uint32_t base_frame,
                                                   float frac)
{
    const uint32_t correlation_frames = (uint32_t)brick6_clip_stretch_correlation_frames(stretch);
    const uint32_t hop_size = (uint32_t)brick6_clip_stretch_hop_size(stretch);

    if (stretch == NULL)
    {
        return;
    }

    if (correlation_frames == 0U)
    {
        stretch->prev_overlap_valid = 0U;
        return;
    }

    for (uint32_t i = 0U; i < correlation_frames; ++i)
    {
        stretch->prev_overlap_mono[i] =
            brick6_clip_stretch_sample_fifo_mono(stretch,
                                                 base_frame + hop_size + i,
                                                 frac);
    }

    for (uint32_t i = correlation_frames; i < BRICK6_CLIP_STRETCH_CORRELATION_FRAMES; ++i)
    {
        stretch->prev_overlap_mono[i] = 0.0f;
    }

    stretch->prev_overlap_valid = 1U;
}

static uint8_t brick6_clip_stretch_emit_preserve_pitch_grain(brick6_clip_stretch_t *stretch)
{
    uint32_t needed_frames;
    uint32_t available;
    uint32_t expected_base_frame;
    uint32_t selected_base_frame;
    uint32_t discard_frames;
    uint32_t grain_size;
    uint32_t hop_size;
    uint32_t search_frames;
    float frac;

    g_brick6_clip_stretch_diag_snapshot.last_enter_emit_grain++;

    if (stretch == NULL)
    {
        brick6_clip_stretch_diag_guard_fail(BRICK6_CLIP_STRETCH_ERROR_GUARD_NULL);
        return 0U;
    }

    grain_size = (uint32_t)brick6_clip_stretch_grain_size(stretch);
    hop_size = (uint32_t)brick6_clip_stretch_hop_size(stretch);
    search_frames = (uint32_t)brick6_clip_stretch_search_frames(stretch);
    needed_frames = grain_size + 1U;
    available = brick6_clip_stretch_fifo_frame_count(stretch);
    expected_base_frame = stretch->source_pos_q16 >> 16;
    frac = (float)(stretch->source_pos_q16 & 0xFFFFU) * (1.0f / 65536.0f);

    if (stretch->output_fill > (BRICK6_CLIP_STRETCH_OUTPUT_RING_FRAMES
                                - grain_size
                                - hop_size))
    {
        return 0U;
    }

    selected_base_frame = brick6_clip_stretch_select_wsola_base(stretch, expected_base_frame, frac, available);
    if ((selected_base_frame + needed_frames) > available)
    {
        return 0U;
    }

    brick6_clip_stretch_prepare_window(stretch);

    for (uint32_t i = 0U; i < grain_size; ++i)
    {
        const uint32_t ring_index =
                (stretch->output_write_origin + i) % BRICK6_CLIP_STRETCH_OUTPUT_RING_FRAMES;
        const float window = stretch->ola_window[i];
        const float left = brick6_clip_stretch_sample_fifo(stretch, 0U, selected_base_frame + i, frac);
        const float right = brick6_clip_stretch_sample_fifo(stretch, 1U, selected_base_frame + i, frac);
        stretch->output_ring_l[ring_index] += left * window;
        stretch->output_ring_r[ring_index] += right * window;
        stretch->output_ring_gain[ring_index] += window;
    }

    brick6_clip_stretch_store_prev_overlap(stretch, selected_base_frame, frac);

    stretch->output_write_origin =
            (stretch->output_write_origin + hop_size)
            % BRICK6_CLIP_STRETCH_OUTPUT_RING_FRAMES;
    stretch->output_fill += hop_size;
    if (stretch->output_fill > BRICK6_CLIP_STRETCH_OUTPUT_RING_FRAMES)
    {
        stretch->output_fill = BRICK6_CLIP_STRETCH_OUTPUT_RING_FRAMES;
    }

    /* The timing contract stays anchored here: source_pos_q16 follows ratio_q16 with the configured hop,
       and the bounded WSOLA search only nudges the local grain alignment around that schedule. */
    stretch->source_pos_q16 += stretch->config.ratio_q16 * hop_size;
    discard_frames = stretch->source_pos_q16 >> 16;
    if (discard_frames > (search_frames + 1U))
    {
        discard_frames -= (search_frames + 1U);
        brick6_clip_stretch_discard_input_frames(stretch, discard_frames);
        stretch->source_pos_q16 -= (discard_frames << 16);
    }

    g_brick6_clip_stretch_diag_snapshot.last_exit_emit_grain++;
    return 1U;
}

void brick6_clip_stretch_init(brick6_clip_stretch_t *stretch)
{
    if (stretch == NULL)
    {
        return;
    }

    memset(stretch, 0, sizeof(*stretch));
    stretch->config = brick6_clip_stretch_default_config();
}

void brick6_clip_stretch_reset(brick6_clip_stretch_t *stretch)
{
    brick6_clip_stretch_init(stretch);
}

void brick6_clip_stretch_set_config(brick6_clip_stretch_t *stretch, const brick6_clip_stretch_config_t *config)
{
    if (stretch == NULL)
    {
        return;
    }

    stretch->config = brick6_clip_stretch_sanitize_config(config);
}

uint32_t brick6_clip_stretch_input_capacity(const brick6_clip_stretch_t *stretch)
{
    if (stretch == NULL)
    {
        brick6_clip_stretch_diag_guard_fail(BRICK6_CLIP_STRETCH_ERROR_GUARD_NULL);
        return 0U;
    }

    if (stretch->queued_frames >= BRICK6_CLIP_STRETCH_INPUT_CAPACITY_FRAMES)
    {
        return 0U;
    }

    return BRICK6_CLIP_STRETCH_INPUT_CAPACITY_FRAMES - stretch->queued_frames;
}

uint32_t brick6_clip_stretch_push_interleaved(brick6_clip_stretch_t *stretch,
                                              const float *interleaved_stereo,
                                              uint32_t frames)
{
    if ((stretch == NULL) || (interleaved_stereo == NULL) || (frames == 0U))
    {
        g_brick6_clip_stretch_diag_snapshot.last_enter_push++;
        brick6_clip_stretch_diag_guard_fail((frames == 0U)
                                                ? BRICK6_CLIP_STRETCH_ERROR_GUARD_RENDER_ZERO
                                                : BRICK6_CLIP_STRETCH_ERROR_GUARD_NULL);
        return 0U;
    }

    return brick6_clip_stretch_push_stereo_stride(stretch,
                                                  interleaved_stereo,
                                                  interleaved_stereo + 1U,
                                                  frames,
                                                  2U,
                                                  0U);
}

uint32_t brick6_clip_stretch_push_stereo_stride(brick6_clip_stretch_t *stretch,
                                                const float *left,
                                                const float *right,
                                                uint32_t frames,
                                                uint32_t frame_stride,
                                                uint8_t right_matches_left)
{
    uint32_t accepted;

    if ((stretch == NULL) || (left == NULL) || (right == NULL) || (frames == 0U) || (frame_stride == 0U))
    {
        g_brick6_clip_stretch_diag_snapshot.last_enter_push++;
        brick6_clip_stretch_diag_guard_fail((frames == 0U)
                                                ? BRICK6_CLIP_STRETCH_ERROR_GUARD_RENDER_ZERO
                                                : BRICK6_CLIP_STRETCH_ERROR_GUARD_NULL);
        return 0U;
    }

    g_brick6_clip_stretch_diag_snapshot.last_enter_push++;

    if (stretch->queued_frames > BRICK6_CLIP_STRETCH_INPUT_CAPACITY_FRAMES)
    {
        stretch->queued_frames = BRICK6_CLIP_STRETCH_INPUT_CAPACITY_FRAMES;
        brick6_clip_stretch_diag_guard_fail(BRICK6_CLIP_STRETCH_ERROR_GUARD_INPUT_RANGE);
    }

    accepted = brick6_clip_stretch_input_capacity(stretch);
    if (frames < accepted)
    {
        accepted = frames;
    }

    for (uint32_t i = 0U; i < accepted; ++i)
    {
        const uint32_t dst = stretch->write_index * 2U;
        stretch->input_interleaved[dst] = left[i * frame_stride];
        stretch->input_interleaved[dst + 1U] = (right_matches_left != 0U)
                ? left[i * frame_stride]
                : right[i * frame_stride];
        stretch->write_index++;
        if (stretch->write_index >= BRICK6_CLIP_STRETCH_INPUT_CAPACITY_FRAMES)
        {
            stretch->write_index = 0U;
        }
    }

    stretch->queued_frames += accepted;
    g_brick6_clip_stretch_diag_snapshot.last_push_frames = accepted;
    g_brick6_clip_stretch_diag_snapshot.last_fifo_level = stretch->queued_frames;
    g_brick6_clip_stretch_diag_snapshot.last_exit_push++;
    return accepted;
}

uint32_t brick6_clip_stretch_render(brick6_clip_stretch_t *stretch,
                                    float *out_left,
                                    float *out_right,
                                    uint32_t frames)
{
    uint32_t produced;

    g_brick6_clip_stretch_diag_snapshot.last_enter_clip_stretch_render++;

    if ((stretch == NULL) || (out_left == NULL) || (out_right == NULL) || (frames == 0U))
    {
        brick6_clip_stretch_diag_guard_fail((frames == 0U)
                                                ? BRICK6_CLIP_STRETCH_ERROR_GUARD_RENDER_ZERO
                                                : BRICK6_CLIP_STRETCH_ERROR_GUARD_NULL);
        return 0U;
    }

    g_brick6_clip_stretch_diag_snapshot.last_render_requested = frames;
    g_brick6_clip_stretch_diag_snapshot.last_fifo_level = stretch->queued_frames;
    g_brick6_clip_stretch_diag_snapshot.last_ring_level = stretch->output_fill;

    if (stretch->output_fill > BRICK6_CLIP_STRETCH_OUTPUT_RING_FRAMES)
    {
        stretch->output_fill = BRICK6_CLIP_STRETCH_OUTPUT_RING_FRAMES;
        brick6_clip_stretch_diag_guard_fail(BRICK6_CLIP_STRETCH_ERROR_GUARD_OUTPUT_RANGE);
    }

    if (stretch->config.mode == BRICK6_CLIP_STRETCH_MODE_BYPASS)
    {
        produced = brick6_clip_stretch_render_bypass(stretch, out_left, out_right, frames);
    }
    else
    {
#if BRICK6_CLIP_STRETCH_PRESERVE_PITCH_ENABLED
        uint32_t loop_guard = 0U;
        while (stretch->output_fill < frames)
        {
            if (loop_guard++ >= BRICK6_CLIP_STRETCH_OUTPUT_RING_FRAMES)
            {
                g_brick6_clip_stretch_diag_snapshot.loop_guard_break_count++;
                break;
            }
            if (brick6_clip_stretch_emit_preserve_pitch_grain(stretch) == 0U)
            {
                break;
            }
        }

        produced = frames;
        if (stretch->output_fill < produced)
        {
            produced = stretch->output_fill;
        }

        for (uint32_t i = 0U; i < produced; ++i)
        {
            const float gain = stretch->output_ring_gain[stretch->output_read_index];
            const float inv_gain = (gain > 1.0e-6f) ? (1.0f / gain) : 0.0f;
            out_left[i] = stretch->output_ring_l[stretch->output_read_index] * inv_gain;
            out_right[i] = stretch->output_ring_r[stretch->output_read_index] * inv_gain;
            stretch->output_ring_l[stretch->output_read_index] = 0.0f;
            stretch->output_ring_r[stretch->output_read_index] = 0.0f;
            stretch->output_ring_gain[stretch->output_read_index] = 0.0f;
            stretch->output_read_index =
                    (stretch->output_read_index + 1U) % BRICK6_CLIP_STRETCH_OUTPUT_RING_FRAMES;
            stretch->output_fill--;
        }
#else
        g_brick6_clip_stretch_diag_snapshot.last_error_code = BRICK6_CLIP_STRETCH_ERROR_FALLBACK_BYPASS;
        produced = brick6_clip_stretch_render_bypass(stretch, out_left, out_right, frames);
#endif
    }

    for (uint32_t i = produced; i < frames; ++i)
    {
        out_left[i] = 0.0f;
        out_right[i] = 0.0f;
    }

    stretch->starved = (produced < frames) ? 1U : 0U;
    if (stretch->starved != 0U)
    {
        stretch->starve_count++;
        g_brick6_clip_stretch_diag_snapshot.starved_count++;
    }

    g_brick6_clip_stretch_diag_snapshot.last_render_produced = produced;
    g_brick6_clip_stretch_diag_snapshot.last_fifo_level = stretch->queued_frames;
    g_brick6_clip_stretch_diag_snapshot.last_ring_level = stretch->output_fill;
    g_brick6_clip_stretch_diag_snapshot.last_exit_clip_stretch_render++;
    return produced;
}

uint8_t brick6_clip_stretch_is_starved(const brick6_clip_stretch_t *stretch)
{
    if (stretch == NULL)
    {
        return 1U;
    }

    return stretch->starved;
}

brick6_clip_stretch_status_t brick6_clip_stretch_get_status(const brick6_clip_stretch_t *stretch)
{
    if (stretch == NULL)
    {
        return BRICK6_CLIP_STRETCH_STATUS_STARVED;
    }

    if (stretch->starved != 0U)
    {
        return BRICK6_CLIP_STRETCH_STATUS_STARVED;
    }

    return BRICK6_CLIP_STRETCH_STATUS_OK;
}
