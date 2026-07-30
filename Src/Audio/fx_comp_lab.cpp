#include "fx_comp_lab.h"
#include <math.h>

namespace {

static float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

} // namespace

extern "C" {

fx_comp_lab_t *fx_comp_lab_get_instance(void)
{
    static fx_comp_lab_t s_comp;
    return &s_comp;
}

void fx_comp_lab_init(fx_comp_lab_t *comp, float sample_rate)
{
    if((comp == nullptr) || (sample_rate <= 0.0f))
        return;

    comp->sample_rate      = sample_rate;
    comp->attack_s         = 0.01f;
    comp->release_s        = 0.1f;
    comp->manual_makeup_db = 0.0f;
    comp->mix              = 1.0f;
    comp->threshold_db     = -18.0f;
    comp->ratio            = 2.0f;
    comp->sc_hpf_hz        = 0.0f;
    comp->knee_db          = 6.0f;
    comp->hpf_l = comp->hpf_r = comp->hpf_x_l = comp->hpf_x_r = 0.0f;
    comp->deluge_mean = comp->deluge_rms_log = comp->deluge_env = 0.0f;
    comp->deluge_gain = comp->brick_gain = 1.0f;
    comp->brick_env = 0.0f;
    comp->transition = 1.0f;
    comp->transition_old_gain = 1.0f;
    comp->model = 0U;
    comp->detector_rms = 0U;
    comp->deluge_saturation = 0U;

}

void fx_comp_lab_set_threshold_db(fx_comp_lab_t *comp, float threshold_db)
{
    if(comp == nullptr)
        return;

    comp->threshold_db = clampf(threshold_db, -48.0f, 0.0f);
}

void fx_comp_lab_set_ratio(fx_comp_lab_t *comp, float ratio)
{
    if(comp == nullptr)
        return;

    comp->ratio = clampf(ratio, 1.0f, 20.0f);
}

void fx_comp_lab_set_attack_s(fx_comp_lab_t *comp, float attack_s)
{
    if(comp == nullptr)
        return;

    const float attack = clampf(attack_s, 0.0001f, 0.1f);
    comp->attack_s = attack;
}

void fx_comp_lab_set_release_s(fx_comp_lab_t *comp, float release_s)
{
    if(comp == nullptr)
        return;

    const float release = clampf(release_s, 0.02f, 1.0f);
    comp->release_s = release;
}

void fx_comp_lab_set_makeup_db(fx_comp_lab_t *comp, float makeup_db)
{
    if(comp == nullptr)
        return;

    comp->manual_makeup_db = clampf(makeup_db, 0.0f, 18.0f);
}

void fx_comp_lab_set_mix(fx_comp_lab_t *comp, float mix)
{
    if(comp == nullptr)
        return;

    comp->mix = clampf(mix, 0.0f, 1.0f);
}

void fx_comp_lab_set_model(fx_comp_lab_t *comp, uint8_t model)
{
    if(comp == nullptr) return;
    model = (model > 2U) ? 2U : model;
    if(model == comp->model) return;
    if(comp->model == 1U)
        comp->transition_old_gain = comp->deluge_gain;
    else if(comp->model == 2U)
        comp->transition_old_gain = comp->brick_gain;
    else
        comp->transition_old_gain = 1.0f;
    comp->model = model;
    comp->transition = 0.0f;
    comp->hpf_l = comp->hpf_r = comp->hpf_x_l = comp->hpf_x_r = 0.0f;
    comp->deluge_mean = comp->deluge_rms_log = comp->deluge_env = 0.0f;
    comp->deluge_gain = comp->brick_gain = 1.0f;
    comp->brick_env = 0.0f;
}

void fx_comp_lab_set_sc_hpf_hz(fx_comp_lab_t *comp, float hz)
{ if(comp) comp->sc_hpf_hz = (hz < 40.0f) ? 0.0f : clampf(hz, 40.0f, 200.0f); }
void fx_comp_lab_set_detector_rms(fx_comp_lab_t *comp, uint8_t rms)
{ if(comp) comp->detector_rms = rms ? 1U : 0U; }
void fx_comp_lab_set_knee_db(fx_comp_lab_t *comp, float db)
{ if(comp) comp->knee_db = clampf(db, 0.0f, 12.0f); }
void fx_comp_lab_set_deluge_saturation(fx_comp_lab_t *comp, uint8_t enabled)
{ if(comp) comp->deluge_saturation = enabled ? 1U : 0U; }

static float detector_hpf(fx_comp_lab_t *c, float x, bool right)
{
    if(c->sc_hpf_hz < 40.0f) return x;
    const float a = expf(-6.28318530718f * c->sc_hpf_hz / c->sample_rate);
    float& y = right ? c->hpf_r : c->hpf_l;
    float& px = right ? c->hpf_x_r : c->hpf_x_l;
    y = a * (y + x - px);
    px = x;
    return y;
}

static float brick_curve_db(float level, float threshold, float ratio, float knee)
{
    const float over = level - threshold;
    const float slope = (1.0f / ratio) - 1.0f;
    if(knee <= 0.0f) return (over > 0.0f) ? slope * over : 0.0f;
    if(over <= -0.5f * knee) return 0.0f;
    if(over >= 0.5f * knee) return slope * over;
    const float x = over + 0.5f * knee;
    return slope * x * x / (2.0f * knee);
}

void fx_comp_lab_process_block(fx_comp_lab_t *comp,
                                 float *left,
                                 float *right,
                                 uint32_t frames)
{
    if((comp == nullptr) || (left == nullptr) || (right == nullptr))
        return;

    const float mix  = comp->mix;
    const float imix = 1.0f - mix;
    const float makeup = powf(10.0f, comp->manual_makeup_db * 0.05f);
    float deluge_target = comp->deluge_gain;
    float brick_target = comp->brick_gain;
    float deluge_step = 0.0f;
    float brick_step = 0.0f;
    float key_l[64];
    float key_r[64];
    const uint32_t detector_frames = (frames > 64U) ? 64U : frames;
    for(uint32_t i = 0U; i < detector_frames; ++i)
    {
        key_l[i] = detector_hpf(comp, left[i], false);
        key_r[i] = detector_hpf(comp, right[i], true);
    }

    for(uint32_t n = 0U; n < detector_frames; ++n)
    {
        float inL = left[n];
        float inR = right[n];
        const float keyL = key_l[n];
        const float keyR = key_r[n];
        float wetL = inL;
        float wetR = inR;

        if(comp->model == 1U)
        {
            if(n == 0U)
            {
                const float old_mean = comp->deluge_mean;
                float sum = 0.0f;
                for(uint32_t i = 0U; i < frames; ++i)
                {
                    const float dl = key_l[i];
                    const float dr = key_r[i];
                    const float s = fmaxf(fabsf(dl), fabsf(dr));
                    sum += s * s;
                }
                const float ns = (float)frames * 2.0f;
                comp->deluge_mean = ((sum / ns) * ns + old_mean) / (1.0f + ns);
                const float next_rms = logf(fmaxf(sqrtf(comp->deluge_mean), 1e-20f));
                const float threshold_np = comp->threshold_db * 0.11512925465f;
                const float over = fmaxf(comp->deluge_rms_log - threshold_np, 0.0f);
                const float desired = over * (1.0f - 1.0f / comp->ratio);
                const float tc = (desired > comp->deluge_env) ? comp->attack_s : comp->release_s;
                const float coeff = expf(-(float)frames / (tc * comp->sample_rate));
                comp->deluge_env = desired + coeff * (comp->deluge_env - desired);
                deluge_target = expf(-comp->deluge_env) * makeup;
                deluge_step = (deluge_target - comp->deluge_gain) / (float)frames;
                comp->deluge_rms_log = next_rms; /* feedback: next block sees post-window RMS */
            }
            comp->deluge_gain += deluge_step;
            wetL = inL * comp->deluge_gain;
            wetR = inR * comp->deluge_gain;
            if(comp->deluge_saturation)
            {
                wetL = tanhf(wetL * 3.0f) / tanhf(3.0f);
                wetR = tanhf(wetR * 3.0f) / tanhf(3.0f);
            }
        }
        else if(comp->model == 2U)
        {
            if((n & 7U) == 0U)
            {
                float detect;
                if(comp->detector_rms)
                {
                    float sum = 0.0f;
                    const uint32_t count = ((frames - n) < 8U) ? (frames - n) : 8U;
                    for(uint32_t i = 0U; i < count; ++i)
                    {
                        const float s = fmaxf(fabsf(key_l[n + i]), fabsf(key_r[n + i]));
                        sum += s * s;
                    }
                    detect = sqrtf(sum / (float)count);
                }
                else detect = fmaxf(fabsf(keyL), fabsf(keyR));
                const float level_db = 20.0f * log10f(detect + 1e-20f);
                const float target_db = brick_curve_db(level_db, comp->threshold_db,
                                                       comp->ratio, comp->knee_db);
                const float target = powf(10.0f, target_db * 0.05f) * makeup;
                const float tc = (target < comp->brick_env) ? comp->attack_s : comp->release_s;
                const float coeff = expf(-8.0f / (tc * comp->sample_rate));
                comp->brick_env = target + coeff * (comp->brick_env - target);
                brick_target = comp->brick_env;
                brick_step = (brick_target - comp->brick_gain) * 0.125f;
            }
            comp->brick_gain += brick_step;
            wetL = inL * comp->brick_gain;
            wetR = inR * comp->brick_gain;
        }

        const float processedL = inL * imix + wetL * mix;
        const float processedR = inR * imix + wetR * mix;
        if(comp->transition < 1.0f)
        {
            comp->transition = fminf(1.0f, comp->transition + (1.0f / 128.0f));
            const float old_gain = imix + comp->transition_old_gain * mix;
            const float oldL = inL * old_gain;
            const float oldR = inR * old_gain;
            left[n] = oldL + (processedL - oldL) * comp->transition;
            right[n] = oldR + (processedR - oldR) * comp->transition;
        }
        else
        {
            left[n] = processedL;
            right[n] = processedR;
        }
    }
    comp->deluge_gain = deluge_target;
    comp->brick_gain = brick_target;
}

} // extern "C"
