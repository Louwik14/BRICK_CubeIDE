#include "fx_comp_lab.h"
#include <math.h>

namespace {

static void update_profile(fx_comp_lab_t *comp)
{
    const float a = (float)comp->amount * 0.01f;
    if(comp->model == 1U)
    {
        comp->ratio = 4.0f;
        if(comp->deluge_saturation != 0U)
        {
            comp->attack_s = 0.0013f;
            comp->release_s = 0.055f;
            comp->threshold_db = -1.0f - 13.0f * powf(a, 1.25f);
        }
        else
        {
            comp->attack_s = 0.030f;
            comp->release_s = 0.100f;
            comp->threshold_db = -2.0f - 16.0f * powf(a, 1.25f);
        }
        comp->knee_db = 0.0f;
    }
    else
    {
        if(comp->detector_rms != 0U)
        {
            comp->ratio = 2.0f;
            comp->attack_s = 0.020f;
            comp->release_s = 0.300f;
            comp->knee_db = 9.0f;
            comp->threshold_db = -2.0f - 22.0f * powf(a, 1.25f);
        }
        else
        {
            comp->ratio = 4.0f;
            comp->attack_s = 0.003f;
            comp->release_s = 0.100f;
            comp->knee_db = 3.0f;
            comp->threshold_db = -1.0f - 13.0f * powf(a, 1.35f);
        }
    }
    comp->manual_makeup_db = fminf(4.0f,
        0.30f * a * (-comp->threshold_db) * (1.0f - 1.0f / comp->ratio));
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
    comp->threshold_db     = -18.0f;
    comp->ratio            = 2.0f;
    comp->knee_db          = 6.0f;
    comp->hpf_l = comp->hpf_r = comp->hpf_x_l = comp->hpf_x_r = 0.0f;
    comp->deluge_mean = comp->deluge_rms_log = comp->deluge_env = 0.0f;
    comp->deluge_gain = comp->brick_gain = 1.0f;
    comp->transition = 1.0f;
    comp->transition_old_gain = 1.0f;
    comp->model = 0U;
    comp->sidechain = 0U;
    comp->amount = 0U;
    comp->detector_rms = 0U;
    comp->deluge_saturation = 0U;

    comp->brick_env = 1.0f;
    for(uint32_t i = 0U; i < 64U; ++i)
        comp->feedback_l[i] = comp->feedback_r[i] = 0.0f;
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
    comp->brick_env = 1.0f;
    for(uint32_t i = 0U; i < 64U; ++i)
        comp->feedback_l[i] = comp->feedback_r[i] = 0.0f;
    update_profile(comp);
}

void fx_comp_lab_set_sidechain(fx_comp_lab_t *comp, uint8_t sidechain)
{
    if(!comp) return;
    const uint8_t next = (sidechain > 12U) ? 12U : sidechain;
    if(next != comp->sidechain)
    {
        comp->hpf_l = comp->hpf_r = comp->hpf_x_l = comp->hpf_x_r = 0.0f;
        comp->deluge_mean = comp->deluge_rms_log = comp->deluge_env = 0.0f;
        comp->deluge_gain = comp->brick_gain = comp->brick_env = 1.0f;
    }
    comp->sidechain = next;
}
uint8_t fx_comp_lab_get_sidechain(const fx_comp_lab_t *comp)
{ return comp ? comp->sidechain : 0U; }
void fx_comp_lab_set_amount(fx_comp_lab_t *comp, uint8_t amount)
{
    if(!comp) return;
    const uint8_t next = (amount > 100U) ? 100U : amount;
    if((comp->amount == 0U) != (next == 0U))
    {
        comp->deluge_mean = comp->deluge_rms_log = comp->deluge_env = 0.0f;
        comp->deluge_gain = comp->brick_gain = comp->brick_env = 1.0f;
    }
    comp->amount = next;
    update_profile(comp);
}
void fx_comp_lab_set_detector_rms(fx_comp_lab_t *comp, uint8_t rms)
{ if(comp) { comp->detector_rms = rms ? 1U : 0U; update_profile(comp); } }
void fx_comp_lab_set_deluge_saturation(fx_comp_lab_t *comp, uint8_t enabled)
{ if(comp) { comp->deluge_saturation = enabled ? 1U : 0U; update_profile(comp); } }

static float detector_hpf(fx_comp_lab_t *c, float x, bool right, float hz)
{
    const float a = expf(-6.28318530718f * hz / c->sample_rate);
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
                                 const float *key_left,
                                 const float *key_right,
                                 uint32_t frames)
{
    if((comp == nullptr) || (left == nullptr) || (right == nullptr))
        return;
    if(comp->amount == 0U)
        return;
    if(comp->model == 0U)
    {
        const uint32_t count = (frames > 64U) ? 64U : frames;
        for(uint32_t i = 0U; i < count; ++i)
        {
            comp->transition = fminf(1.0f, comp->transition + (1.0f / 128.0f));
            const float gain = comp->transition_old_gain
                + (1.0f - comp->transition_old_gain) * comp->transition;
            left[i] *= gain;
            right[i] *= gain;
        }
        return;
    }

    const float makeup = powf(10.0f, comp->manual_makeup_db * 0.05f);
    float deluge_target = comp->deluge_gain;
    float brick_target = comp->brick_gain;
    float deluge_step = 0.0f;
    float brick_step = 0.0f;
    float key_l[64];
    float key_r[64];
    const uint32_t detector_frames = (frames > 64U) ? 64U : frames;
    const uint8_t external_key = ((key_left != nullptr) && (key_right != nullptr)) ? 1U : 0U;
    const float detector_hz = (external_key != 0U) ? 20.0f : 90.0f;
    for(uint32_t i = 0U; i < detector_frames; ++i)
    {
        const float source_l = (external_key != 0U)
            ? key_left[i]
            : ((comp->model == 1U) ? comp->feedback_l[i] : left[i]);
        const float source_r = (external_key != 0U)
            ? key_right[i]
            : ((comp->model == 1U) ? comp->feedback_r[i] : right[i]);
        key_l[i] = detector_hpf(comp, source_l, false, detector_hz);
        key_r[i] = detector_hpf(comp, source_r, true, detector_hz);
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
                const float over = fmaxf(next_rms - threshold_np, 0.0f);
                const float desired = over * (1.0f - 1.0f / comp->ratio);
                const float tc = (desired > comp->deluge_env) ? comp->attack_s : comp->release_s;
                const float coeff = expf(-(float)frames / (tc * comp->sample_rate));
                comp->deluge_env = desired + coeff * (comp->deluge_env - desired);
                deluge_target = expf(-comp->deluge_env) * makeup;
                deluge_step = (deluge_target - comp->deluge_gain) / (float)frames;
                comp->deluge_rms_log = next_rms;
            }
            comp->deluge_gain += deluge_step;
            wetL = inL * comp->deluge_gain;
            wetR = inR * comp->deluge_gain;
            if(comp->deluge_saturation)
            {
                wetL = tanhf(wetL * 3.0f) * (1.0f / 3.0f);
                wetR = tanhf(wetR * 3.0f) * (1.0f / 3.0f);
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
                if(comp->detector_rms == 0U)
                {
                    detect = 0.0f;
                    const uint32_t count = ((frames - n) < 8U) ? (frames - n) : 8U;
                    for(uint32_t i = 0U; i < count; ++i)
                        detect = fmaxf(detect,
                            fmaxf(fabsf(key_l[n + i]), fabsf(key_r[n + i])));
                }
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

        const float processedL = wetL;
        const float processedR = wetR;
        if(comp->transition < 1.0f)
        {
            comp->transition = fminf(1.0f, comp->transition + (1.0f / 128.0f));
            const float old_gain = comp->transition_old_gain;
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
    for(uint32_t i = 0U; i < detector_frames; ++i)
    {
        comp->feedback_l[i] = left[i];
        comp->feedback_r[i] = right[i];
    }
}

} // extern "C"
