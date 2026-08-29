#include "Audio/brick6_clip_shifter.h"

#include <string.h>

#define BRICK6_CLIP_SHIFTER_RATIO_MIN 0.25f
#define BRICK6_CLIP_SHIFTER_RATIO_MAX 4.0f
#define BRICK6_CLIP_SHIFTER_DELAY_MASK (BRICK6_CLIP_SHIFTER_DELAY_FRAMES - 1U)

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

void brick6_clip_shifter_init(brick6_clip_shifter_t *shifter)
{
    if (shifter == NULL)
    {
        return;
    }

    brick6_clip_shifter_reset(shifter);
    shifter->ratio = 1.0f;
    shifter->window_frames = (float)BRICK6_CLIP_SHIFTER_MAX_WINDOW_FRAMES;
}

void brick6_clip_shifter_reset(brick6_clip_shifter_t *shifter)
{
    if (shifter == NULL)
    {
        return;
    }

    memset(shifter->buffer_l, 0, sizeof(shifter->buffer_l));
    memset(shifter->buffer_r, 0, sizeof(shifter->buffer_r));
    shifter->phase = 0.0f;
    shifter->write_index = 0U;
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

void brick6_clip_shifter_process_mono(brick6_clip_shifter_t *shifter,
                                      float *mono,
                                      uint32_t frames)
{
    if ((shifter == NULL) || (mono == NULL) || (frames == 0U))
    {
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
