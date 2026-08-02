#include "Audio/spectral_window.h"

#include <math.h>

#define SPECTRAL_WINDOW_MIN_HZ 20.0f
#define SPECTRAL_WINDOW_MAX_HZ 20000.0f
#define SPECTRAL_WINDOW_MIN_SPAN 0.05f

static float spectral_window_clampf(float value, float lo, float hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static spectral_window_limits_t spectral_window_normalize_limits(spectral_window_limits_t limits)
{
    if ((limits.min_hz < SPECTRAL_WINDOW_MIN_HZ)
            || (limits.max_hz <= limits.min_hz))
    {
        limits.min_hz = SPECTRAL_WINDOW_MIN_HZ;
        limits.max_hz = SPECTRAL_WINDOW_MAX_HZ;
    }
    limits.min_width = spectral_window_clampf(limits.min_width,
                                              SPECTRAL_WINDOW_MIN_SPAN,
                                              1.0f);
    return limits;
}

spectral_window_limits_t spectral_window_reverb_limits(void)
{
    const spectral_window_limits_t limits = {
        .min_hz = SPECTRAL_WINDOW_MIN_HZ,
        .max_hz = SPECTRAL_WINDOW_MAX_HZ,
        .min_width = SPECTRAL_WINDOW_MIN_SPAN,
    };
    return limits;
}

spectral_window_limits_t spectral_window_delay_limits(void)
{
    const spectral_window_limits_t limits = {
        .min_hz = SPECTRAL_WINDOW_MIN_HZ,
        .max_hz = SPECTRAL_WINDOW_MAX_HZ,
        .min_width = SPECTRAL_WINDOW_MIN_SPAN,
    };
    return limits;
}

void spectral_window_calculate(float position,
                               float width,
                               spectral_window_limits_t limits,
                               spectral_window_result_t *out)
{
    if (out == NULL)
    {
        return;
    }

    limits = spectral_window_normalize_limits(limits);
    const float p = spectral_window_clampf(position, 0.0f, 1.0f);
    const float w = spectral_window_clampf(width, 0.0f, 1.0f);
    const float span = limits.min_width + ((1.0f - limits.min_width) * w);
    float left = p - (0.5f * span);
    float right = p + (0.5f * span);

    if (left < 0.0f)
    {
        right -= left;
        left = 0.0f;
    }
    if (right > 1.0f)
    {
        left -= right - 1.0f;
        right = 1.0f;
    }
    left = spectral_window_clampf(left, 0.0f, 1.0f - limits.min_width);
    right = spectral_window_clampf(right, limits.min_width, 1.0f);

    const float log_min = logf(limits.min_hz);
    const float log_span = logf(limits.max_hz) - log_min;
    out->low_cut_hz = expf(log_min + (left * log_span));
    out->high_cut_hz = expf(log_min + (right * log_span));
    if (out->high_cut_hz <= out->low_cut_hz)
    {
        out->high_cut_hz = nextafterf(out->low_cut_hz, limits.max_hz);
    }
    out->low_cut_hz = spectral_window_clampf(out->low_cut_hz,
                                             limits.min_hz,
                                             limits.max_hz);
    out->high_cut_hz = spectral_window_clampf(out->high_cut_hz,
                                              limits.min_hz,
                                              limits.max_hz);
}

float spectral_window_log_position(float frequency_hz,
                                   spectral_window_limits_t limits)
{
    limits = spectral_window_normalize_limits(limits);
    const float clamped = spectral_window_clampf(frequency_hz,
                                                 limits.min_hz,
                                                 limits.max_hz);
    return spectral_window_clampf((logf(clamped) - logf(limits.min_hz))
                                      / (logf(limits.max_hz) - logf(limits.min_hz)),
                                  0.0f,
                                  1.0f);
}
