#include "Audio/brick6_clip_shifter.h"

#include <string.h>

#define BRICK6_CLIP_SHIFTER_RATIO_MIN 0.25f
#define BRICK6_CLIP_SHIFTER_RATIO_MAX 4.0f
#define BRICK6_CLIP_SHIFTER_DELAY_MASK (BRICK6_CLIP_SHIFTER_DELAY_FRAMES - 1U)

_Static_assert((BRICK6_CLIP_SHIFTER_DELAY_FRAMES
                & (BRICK6_CLIP_SHIFTER_DELAY_FRAMES - 1U)) == 0U,
               "Clip shifter delay capacity must be a power of two");
_Static_assert(BRICK6_CLIP_SHIFTER_MAX_WINDOW_FRAMES
                   <= BRICK6_CLIP_SHIFTER_DELAY_FRAMES,
               "Clip shifter window must fit the delay line");
_Static_assert(BRICK6_CLIP_SHIFTER_DELAY_FRAMES <= (UINT16_MAX + 1UL),
               "Clip shifter write index type is too narrow");

#include "brick6_clip_shifter_sine_lut.inc"

/*
 * Local C port of the two-tap crossfaded delay pitch-shifter used by Mutable
 * Instruments Clouds (MIT License, Copyright 2014 Emilie Gillet).
 * Only the algorithmic core is retained; Clouds/FxEngine are not imported.
 */

static float brick6_clip_shifter_clampf(float value, float min_value, float max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

static float brick6_clip_shifter_read_interp(const float *buffer, uint16_t write_index, float offset)
{
    uint32_t offset_integral;
    float offset_fractional;
    uint32_t index_a;
    uint32_t index_b;
    float a;
    float b;

    if (offset < 0.0f)
    {
        offset = 0.0f;
    }
    if (offset > (float)BRICK6_CLIP_SHIFTER_MAX_WINDOW_FRAMES)
    {
        offset = (float)BRICK6_CLIP_SHIFTER_MAX_WINDOW_FRAMES;
    }

    offset_integral = (uint32_t)offset;
    offset_fractional = offset - (float)offset_integral;
    index_a = ((uint32_t)write_index + offset_integral) & BRICK6_CLIP_SHIFTER_DELAY_MASK;
    index_b = (index_a + 1U) & BRICK6_CLIP_SHIFTER_DELAY_MASK;
    a = buffer[index_a];
    b = buffer[index_b];
    return a + ((b - a) * offset_fractional);
}

static void brick6_clip_shifter_advance_phase(brick6_clip_shifter_t *shifter,
                                               float *out_tri,
                                               float *out_phase,
                                               float *out_half)
{
    shifter->phase += (1.0f - shifter->ratio) / shifter->window_frames;
    if (shifter->phase >= 1.0f)
    {
        shifter->phase -= 1.0f;
    }
    if (shifter->phase <= 0.0f)
    {
        shifter->phase += 1.0f;
    }

    *out_tri = 2.0f * ((shifter->phase >= 0.5f) ? (1.0f - shifter->phase) : shifter->phase);
    *out_phase = shifter->phase * shifter->window_frames;
    *out_half = *out_phase + (shifter->window_frames * 0.5f);
    if (*out_half >= shifter->window_frames)
    {
        *out_half -= shifter->window_frames;
    }
}

static float brick6_clip_shifter_render_channel(const float *buffer,
                                                uint16_t write_index,
                                                float phase,
                                                float half,
                                                float tri)
{
    return (brick6_clip_shifter_read_interp(buffer, write_index, phase) * tri)
           + (brick6_clip_shifter_read_interp(buffer, write_index, half) * (1.0f - tri));
}

static float brick6_clip_shifter_wrap_phase(float phase)
{
    if (phase >= 1.0f) phase -= 1.0f;
    if (phase < 0.0f) phase += 1.0f;
    return phase;
}

static float brick6_clip_shifter_sine_window(float phase)
{
    const float scaled = phase * 128.0f;
    const uint32_t index = (uint32_t)scaled;
    if (index >= 128U) return 0.0f;
    const float fraction = scaled - (float)index;
    const float a = g_brick6_clip_shifter_sine_lut[index];
    return a + ((g_brick6_clip_shifter_sine_lut[index + 1U] - a) * fraction);
}

static uint32_t brick6_clip_shifter_random(brick6_clip_shifter_t *shifter)
{
    uint32_t value = shifter->random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    shifter->random_state = value;
    return value;
}

static float brick6_clip_shifter_new_dispersion(brick6_clip_shifter_t *shifter)
{
    const float maximum = shifter->window_frames * ((float)shifter->dispersion_percent * 0.01f);
    if ((maximum <= 0.0f) || (shifter->dispersion_percent == 0U)) return 0.0f;
    return maximum * (float)(brick6_clip_shifter_random(shifter) & 0x00FFFFFFU)
           * (1.0f / 16777215.0f);
}

static float brick6_clip_shifter_sine_normalization(uint8_t heads)
{
    switch (heads)
    {
        case 3U: return 0.816496581f;
        case 4U: return 0.707106781f;
        case 6U: return 0.577350269f;
        case 8U: return 0.5f;
        default: return 1.0f;
    }
}

void brick6_clip_shifter_init(brick6_clip_shifter_t *shifter,
                              float *buffer_l,
                              float *buffer_r)
{
    if ((shifter == NULL) || (buffer_l == NULL) || (buffer_r == NULL))
    {
        return;
    }

    shifter->buffer_l = buffer_l;
    shifter->buffer_r = buffer_r;
    brick6_clip_shifter_reset(shifter);
    shifter->ratio = 1.0f;
    shifter->window_frames = (float)BRICK6_CLIP_SHIFTER_MAX_WINDOW_FRAMES;
    shifter->heads = 2U;
    shifter->window = (uint8_t)BRICK6_CLIP_SHIFTER_WINDOW_TRI;
    shifter->dispersion_percent = 0U;
}

void brick6_clip_shifter_reset(brick6_clip_shifter_t *shifter)
{
    if (shifter == NULL)
    {
        return;
    }

    memset(shifter->buffer_l, 0,
           BRICK6_CLIP_SHIFTER_DELAY_FRAMES * sizeof(shifter->buffer_l[0]));
    memset(shifter->buffer_r, 0,
           BRICK6_CLIP_SHIFTER_DELAY_FRAMES * sizeof(shifter->buffer_r[0]));
    shifter->phase = 0.0f;
    shifter->write_index = 0U;
    shifter->random_state = 0x6D2B79F5U;
    shifter->heads = 0U;
    shifter->window = UINT8_MAX;
    shifter->dispersion_percent = UINT8_MAX;
    for (uint32_t head = 0U; head < BRICK6_CLIP_SHIFTER_MAX_HEADS; ++head)
    {
        shifter->head_previous_phase[head] = 0.0f;
        shifter->head_dispersion_frames[head] = 0.0f;
    }
}

void brick6_clip_shifter_set_window_frames(brick6_clip_shifter_t *shifter, uint16_t window_frames)
{
    if (shifter == NULL)
    {
        return;
    }

    if (window_frames < BRICK6_CLIP_SHIFTER_MIN_WINDOW_FRAMES)
    {
        window_frames = BRICK6_CLIP_SHIFTER_MIN_WINDOW_FRAMES;
    }
    else if (window_frames > BRICK6_CLIP_SHIFTER_MAX_WINDOW_FRAMES)
    {
        window_frames = BRICK6_CLIP_SHIFTER_MAX_WINDOW_FRAMES;
    }

    shifter->window_frames = (float)window_frames;
}

void brick6_clip_shifter_set_pitch_correction(brick6_clip_shifter_t *shifter, float pitch_correction)
{
    if (shifter == NULL)
    {
        return;
    }

    shifter->ratio = brick6_clip_shifter_clampf(pitch_correction,
                                                BRICK6_CLIP_SHIFTER_RATIO_MIN,
                                                BRICK6_CLIP_SHIFTER_RATIO_MAX);
}

void brick6_clip_shifter_set_experiment(brick6_clip_shifter_t *shifter,
                                        uint8_t heads,
                                        uint8_t window,
                                        uint8_t dispersion_percent)
{
    if (shifter == NULL) return;
    if ((heads != 2U) && (heads != 3U) && (heads != 4U) && (heads != 6U) && (heads != 8U)) heads = 2U;
    if (window > (uint8_t)BRICK6_CLIP_SHIFTER_WINDOW_SINE) window = (uint8_t)BRICK6_CLIP_SHIFTER_WINDOW_TRI;
    if (dispersion_percent > 100U) dispersion_percent = 100U;
    if ((shifter->heads == heads) && (shifter->window == window)
            && (shifter->dispersion_percent == dispersion_percent)) return;

    shifter->heads = heads;
    shifter->window = window;
    shifter->dispersion_percent = dispersion_percent;
    for (uint32_t head = 0U; head < heads; ++head)
    {
        shifter->head_previous_phase[head] = brick6_clip_shifter_wrap_phase(
            shifter->phase + ((float)head / (float)heads));
        shifter->head_dispersion_frames[head] = brick6_clip_shifter_new_dispersion(shifter);
    }
}

static void brick6_clip_shifter_process_experimental(brick6_clip_shifter_t *shifter,
                                                      float *left,
                                                      float *right,
                                                      uint32_t frames,
                                                      uint8_t stereo)
{
    const uint8_t heads = shifter->heads;
    const float phase_step = (1.0f - shifter->ratio) / shifter->window_frames;
    const float sine_normalization = brick6_clip_shifter_sine_normalization(heads);
    const float hann_normalization = 2.0f / (float)heads;

    for (uint32_t i = 0U; i < frames; ++i)
    {
        shifter->write_index = (uint16_t)((shifter->write_index - 1U) & BRICK6_CLIP_SHIFTER_DELAY_MASK);
        shifter->buffer_l[shifter->write_index] = left[i];
        if (stereo != 0U) shifter->buffer_r[shifter->write_index] = right[i];
        shifter->phase = brick6_clip_shifter_wrap_phase(shifter->phase + phase_step);

        float mixed_l = 0.0f;
        float mixed_r = 0.0f;
        float tri_sum = 0.0f;
        for (uint32_t head = 0U; head < heads; ++head)
        {
            const float local_phase = brick6_clip_shifter_wrap_phase(
                shifter->phase + ((float)head / (float)heads));
            const float previous = shifter->head_previous_phase[head];
            const uint8_t renewed = (phase_step >= 0.0f)
                                        ? (uint8_t)(local_phase < previous)
                                        : (uint8_t)(local_phase > previous);
            if (renewed != 0U)
                shifter->head_dispersion_frames[head] = brick6_clip_shifter_new_dispersion(shifter);
            shifter->head_previous_phase[head] = local_phase;

            const float sine = brick6_clip_shifter_sine_window(local_phase);
            float weight;
            if (shifter->window == (uint8_t)BRICK6_CLIP_SHIFTER_WINDOW_HANN)
                weight = sine * sine * hann_normalization;
            else if (shifter->window == (uint8_t)BRICK6_CLIP_SHIFTER_WINDOW_SINE)
                weight = sine * sine_normalization;
            else
            {
                weight = 1.0f - ((local_phase >= 0.5f)
                                      ? ((local_phase - 0.5f) * 2.0f)
                                      : ((0.5f - local_phase) * 2.0f));
                tri_sum += weight;
            }

            float offset = (local_phase * shifter->window_frames)
                           + shifter->head_dispersion_frames[head];
            if (offset >= shifter->window_frames) offset -= shifter->window_frames;
            mixed_l += brick6_clip_shifter_read_interp(shifter->buffer_l,
                                                       shifter->write_index, offset) * weight;
            if (stereo != 0U)
                mixed_r += brick6_clip_shifter_read_interp(shifter->buffer_r,
                                                           shifter->write_index, offset) * weight;
        }
        if ((shifter->window == (uint8_t)BRICK6_CLIP_SHIFTER_WINDOW_TRI) && (tri_sum > 0.0f))
        {
            mixed_l /= tri_sum;
            if (stereo != 0U) mixed_r /= tri_sum;
        }
        left[i] = mixed_l;
        if (stereo != 0U) right[i] = mixed_r;
    }
}

void brick6_clip_shifter_process_mono(brick6_clip_shifter_t *shifter,
                                      float *mono,
                                      uint32_t frames)
{
    if ((shifter == NULL) || (mono == NULL) || (frames == 0U))
    {
        return;
    }

    if ((shifter->heads != 2U)
            || (shifter->window != (uint8_t)BRICK6_CLIP_SHIFTER_WINDOW_TRI)
            || (shifter->dispersion_percent != 0U))
    {
        brick6_clip_shifter_process_experimental(shifter, mono, NULL, frames, 0U);
        return;
    }

    /* buffer_r remains reserved for the unchanged stereo path. */
    for (uint32_t i = 0U; i < frames; ++i)
    {
        shifter->write_index = (uint16_t)((shifter->write_index - 1U) & BRICK6_CLIP_SHIFTER_DELAY_MASK);
        shifter->buffer_l[shifter->write_index] = mono[i];

        float tri;
        float phase;
        float half;
        brick6_clip_shifter_advance_phase(shifter, &tri, &phase, &half);
        mono[i] = brick6_clip_shifter_render_channel(shifter->buffer_l,
                                                     shifter->write_index,
                                                     phase,
                                                     half,
                                                     tri);
    }
}

void brick6_clip_shifter_process_stereo(brick6_clip_shifter_t *shifter,
                                        float *left,
                                        float *right,
                                        uint32_t frames)
{
    if ((shifter == NULL) || (left == NULL) || (right == NULL) || (frames == 0U))
    {
        return;
    }

    if ((shifter->heads != 2U)
            || (shifter->window != (uint8_t)BRICK6_CLIP_SHIFTER_WINDOW_TRI)
            || (shifter->dispersion_percent != 0U))
    {
        brick6_clip_shifter_process_experimental(shifter, left, right, frames, 1U);
        return;
    }

    for (uint32_t i = 0U; i < frames; ++i)
    {
        shifter->write_index = (uint16_t)((shifter->write_index - 1U) & BRICK6_CLIP_SHIFTER_DELAY_MASK);
        shifter->buffer_l[shifter->write_index] = left[i];
        shifter->buffer_r[shifter->write_index] = right[i];

        shifter->phase += (1.0f - shifter->ratio) / shifter->window_frames;
        if (shifter->phase >= 1.0f)
        {
            shifter->phase -= 1.0f;
        }
        if (shifter->phase <= 0.0f)
        {
            shifter->phase += 1.0f;
        }

        const float tri = 2.0f * ((shifter->phase >= 0.5f) ? (1.0f - shifter->phase) : shifter->phase);
        float phase = shifter->phase * shifter->window_frames;
        float half = phase + (shifter->window_frames * 0.5f);
        if (half >= shifter->window_frames)
        {
            half -= shifter->window_frames;
        }

        left[i] = (brick6_clip_shifter_read_interp(shifter->buffer_l, shifter->write_index, phase) * tri)
                  + (brick6_clip_shifter_read_interp(shifter->buffer_l, shifter->write_index, half) * (1.0f - tri));
        right[i] = (brick6_clip_shifter_read_interp(shifter->buffer_r, shifter->write_index, phase) * tri)
                   + (brick6_clip_shifter_read_interp(shifter->buffer_r, shifter->write_index, half) * (1.0f - tri));
    }
}
