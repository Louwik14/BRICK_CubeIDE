#include "fx_filter_ladder_moog.h"

#include <math.h>
#include <string.h>

#define FX_FILTER_LADDER_MOOG_PI 3.14159265358979323846
#define FX_FILTER_LADDER_MOOG_VT 0.312f
#define FX_FILTER_LADDER_MOOG_MIN_CUTOFF_HZ 20.0f
#define FX_FILTER_LADDER_MOOG_MIN_SAMPLE_RATE 1000.0f
#define FX_FILTER_LADDER_MOOG_MAX_RESONANCE 4.0f
#define FX_FILTER_LADDER_MOOG_DEFAULT_DRIVE 1.0f

static float fx_filter_ladder_moog_clampf(float value, float min_value, float max_value)
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

void fx_filter_ladder_moog_reset(fx_filter_ladder_moog_t *filter)
{
    if (filter == NULL)
    {
        return;
    }

    (void)memset(filter->stage, 0, sizeof(filter->stage));
    (void)memset(filter->delta, 0, sizeof(filter->delta));
    (void)memset(filter->tanh_stage, 0, sizeof(filter->tanh_stage));
}

void fx_filter_ladder_moog_set_sample_rate(fx_filter_ladder_moog_t *filter, float sample_rate)
{
    if (filter == NULL)
    {
        return;
    }

    filter->sample_rate = (sample_rate > FX_FILTER_LADDER_MOOG_MIN_SAMPLE_RATE)
                        ? sample_rate
                        : FX_FILTER_LADDER_MOOG_MIN_SAMPLE_RATE;
    fx_filter_ladder_moog_set_cutoff(filter, filter->cutoff_hz);
}

void fx_filter_ladder_moog_set_cutoff(fx_filter_ladder_moog_t *filter, float cutoff_hz)
{
    if (filter == NULL)
    {
        return;
    }

    const float nyquist_margin_hz = 0.45f * filter->sample_rate;
    const float clamped_cutoff_hz = fx_filter_ladder_moog_clampf(cutoff_hz,
                                                                 FX_FILTER_LADDER_MOOG_MIN_CUTOFF_HZ,
                                                                 nyquist_margin_hz);
    const double x = (FX_FILTER_LADDER_MOOG_PI * (double)clamped_cutoff_hz) / (double)filter->sample_rate;

    filter->cutoff_hz = clamped_cutoff_hz;
    filter->g = 4.0 * FX_FILTER_LADDER_MOOG_PI * FX_FILTER_LADDER_MOOG_VT * (double)clamped_cutoff_hz * (1.0 - x) / (1.0 + x);
}

void fx_filter_ladder_moog_set_resonance(fx_filter_ladder_moog_t *filter, float resonance)
{
    if (filter == NULL)
    {
        return;
    }

    filter->resonance = fx_filter_ladder_moog_clampf(resonance, 0.0f, FX_FILTER_LADDER_MOOG_MAX_RESONANCE);
}

void fx_filter_ladder_moog_set_drive(fx_filter_ladder_moog_t *filter, float drive)
{
    if (filter == NULL)
    {
        return;
    }

    filter->drive = (drive > 0.0f) ? drive : FX_FILTER_LADDER_MOOG_DEFAULT_DRIVE;
}

void fx_filter_ladder_moog_init(fx_filter_ladder_moog_t *filter, float sample_rate)
{
    if (filter == NULL)
    {
        return;
    }

    (void)memset(filter, 0, sizeof(*filter));
    filter->cutoff_hz = 1000.0f;
    filter->drive = FX_FILTER_LADDER_MOOG_DEFAULT_DRIVE;
    fx_filter_ladder_moog_set_sample_rate(filter, sample_rate);
    fx_filter_ladder_moog_set_resonance(filter, 0.0f);
    fx_filter_ladder_moog_reset(filter);
}

float fx_filter_ladder_moog_process_sample(fx_filter_ladder_moog_t *filter, float input)
{
    if (filter == NULL)
    {
        return input;
    }

    const double half_vt = 2.0 * FX_FILTER_LADDER_MOOG_VT;
    const double inv_two_fs = 1.0 / (2.0 * (double)filter->sample_rate);

    const double delta0 = -filter->g * (tanh((((double)filter->drive * (double)input) + ((double)filter->resonance * filter->stage[3])) / half_vt) + filter->tanh_stage[0]);
    filter->stage[0] += (delta0 + filter->delta[0]) * inv_two_fs;
    filter->delta[0] = delta0;
    filter->tanh_stage[0] = tanh(filter->stage[0] / half_vt);

    const double delta1 = filter->g * (filter->tanh_stage[0] - filter->tanh_stage[1]);
    filter->stage[1] += (delta1 + filter->delta[1]) * inv_two_fs;
    filter->delta[1] = delta1;
    filter->tanh_stage[1] = tanh(filter->stage[1] / half_vt);

    const double delta2 = filter->g * (filter->tanh_stage[1] - filter->tanh_stage[2]);
    filter->stage[2] += (delta2 + filter->delta[2]) * inv_two_fs;
    filter->delta[2] = delta2;
    filter->tanh_stage[2] = tanh(filter->stage[2] / half_vt);

    const double delta3 = filter->g * (filter->tanh_stage[2] - filter->tanh_stage[3]);
    filter->stage[3] += (delta3 + filter->delta[3]) * inv_two_fs;
    filter->delta[3] = delta3;
    filter->tanh_stage[3] = tanh(filter->stage[3] / half_vt);

    return (float)filter->stage[3];
}

void fx_filter_ladder_moog_process_block(fx_filter_ladder_moog_t *filter, float *samples, uint32_t frames)
{
    if ((filter == NULL) || (samples == NULL))
    {
        return;
    }

    for (uint32_t i = 0U; i < frames; ++i)
    {
        samples[i] = fx_filter_ladder_moog_process_sample(filter, samples[i]);
    }
}
