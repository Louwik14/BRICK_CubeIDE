#include "fx_onepole.h"
#include <math.h>

#define FX_ONEPOLE_PI_F 3.14159265358979323846f

void fx_onepole_init(fx_onepole_t *f)
{
    if(!f)
        return;

    f->g = 0.0f;
    f->gi = 1.0f;
    f->state = 0.0f;
    f->mode = 0U;
}

void fx_onepole_set_freq(fx_onepole_t *f, float freq_norm)
{
    if(!f)
        return;

    if(freq_norm < 0.0f)
        freq_norm = 0.0f;
    if(freq_norm > 0.497f)
        freq_norm = 0.497f;

    const float g = tanf(FX_ONEPOLE_PI_F * freq_norm);
    f->g = g;
    f->gi = 1.0f / (1.0f + g);
}

void fx_onepole_set_mode(fx_onepole_t *f, uint8_t mode)
{
    if(!f)
        return;

    f->mode = (mode == 0U) ? 0U : 1U;
}

float fx_onepole_process(fx_onepole_t *f, float in)
{
    const float lp = (f->g * in + f->state) * f->gi;
    f->state = f->g * (in - lp) + lp;

    if(f->mode == 0U)
        return lp;

    return in - lp;
}
