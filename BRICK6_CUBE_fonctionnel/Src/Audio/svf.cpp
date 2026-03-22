#include "svf.h"

#include <math.h>

#ifndef SVF_PI_F
#define SVF_PI_F 3.14159265358979323846f
#endif

static inline float svf_clamp(float x, float lo, float hi)
{
    if(x < lo)
        return lo;
    if(x > hi)
        return hi;
    return x;
}

static inline float svf_min(float a, float b)
{
    return (a < b) ? a : b;
}

extern "C" {

void svf_init(svf_t *svf, float sample_rate)
{
    if((svf == nullptr) || (sample_rate <= 0.0f))
        return;

    svf->sr        = sample_rate;
    svf->fc        = 200.0f;
    svf->res       = 0.5f;
    svf->drive     = 0.5f;
    svf->pre_drive = 0.5f;
    svf->freq      = 0.25f;
    svf->damp      = 0.0f;

    svf->low       = 0.0f;
    svf->band      = 0.0f;

    svf->fc_max    = svf->sr / 3.0f;
}

static inline float svf_process_pass(svf_t *svf, float in, svf_mode_t mode)
{
    const float notch = in - svf->damp * svf->band;
    const float low = svf->low + svf->freq * svf->band;
    const float high = notch - low;
    const float band_sq = svf->band * svf->band;
    const float band = svf->freq * high + svf->band - svf->drive * band_sq * svf->band;

    svf->low = low;
    svf->band = band;

    switch(mode)
    {
        case SVF_MODE_HP:
            return high;

        case SVF_MODE_BP:
            return band;

        case SVF_MODE_LP:
        default:
            return low;
    }
}

float svf_process_mode(svf_t *svf, float in, svf_mode_t mode)
{
    if(svf == nullptr)
        return 0.0f;

    const float pass1 = svf_process_pass(svf, in, mode);
    const float pass2 = svf_process_pass(svf, in, mode);

    return 0.5f * (pass1 + pass2);
}

void svf_set_freq(svf_t *svf, float cutoff_hz)
{
    if(svf == nullptr)
        return;

    svf->fc = svf_clamp(cutoff_hz, 1.0e-6f, svf->fc_max);
    svf->freq = 2.0f * sinf(SVF_PI_F * svf_min(0.25f, svf->fc / (svf->sr * 2.0f)));
    svf->damp = svf_min(2.0f * (1.0f - powf(svf->res, 0.25f)),
                        svf_min(2.0f, 2.0f / svf->freq - svf->freq * 0.5f));
}

void svf_set_res(svf_t *svf, float resonance_0_1)
{
    if(svf == nullptr)
        return;

    svf->res = svf_clamp(resonance_0_1, 0.0f, 1.0f);
    svf->damp = svf_min(2.0f * (1.0f - powf(svf->res, 0.25f)),
                        svf_min(2.0f, 2.0f / svf->freq - svf->freq * 0.5f));
    svf->drive = svf->pre_drive * svf->res;
}

void svf_set_drive(svf_t *svf, float drive)
{
    if(svf == nullptr)
        return;

    svf->pre_drive = svf_clamp(drive * 0.1f, 0.0f, 1.0f);
    svf->drive = svf->pre_drive * svf->res;
}

} // extern "C"
