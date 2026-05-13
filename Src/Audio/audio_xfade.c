#include "Audio/audio_xfade.h"

static float g_audio_xfade = 0.0f;

void audio_xfade_set(float xfade)
{
    if (xfade < 0.0f)
    {
        xfade = 0.0f;
    }
    else if (xfade > 1.0f)
    {
        xfade = 1.0f;
    }

    g_audio_xfade = xfade;
}

float audio_xfade_get(void)
{
    return g_audio_xfade;
}

float audio_xfade_smooth_next(float target, float *smoothed)
{
    float value = (smoothed != 0) ? *smoothed : target;

    value += (target - value) * 0.25f;

    if (value < 0.0f)
    {
        value = 0.0f;
    }
    else if (value > 1.0f)
    {
        value = 1.0f;
    }

    if (smoothed != 0)
    {
        *smoothed = value;
    }

    return value;
}

float audio_xfade_frame(float xfade_start,
                        float xfade_end,
                        uint32_t frame,
                        uint32_t frames)
{
    float xfade;

    if (frames > 1U)
    {
        const float t = (float)frame / (float)(frames - 1U);
        xfade = xfade_start + ((xfade_end - xfade_start) * t);
    }
    else
    {
        xfade = xfade_end;
    }

    if (xfade < 0.0f)
    {
        xfade = 0.0f;
    }
    else if (xfade > 1.0f)
    {
        xfade = 1.0f;
    }

    return xfade;
}
