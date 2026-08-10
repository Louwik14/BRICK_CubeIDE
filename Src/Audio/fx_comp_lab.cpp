#include "fx_comp_lab.h"
#include <math.h>

namespace {
static float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}
}

extern "C" {

fx_comp_lab_t *fx_comp_lab_get_instance(void)
{
    static fx_comp_lab_t s_comp;
    return &s_comp;
}

void fx_comp_lab_init(fx_comp_lab_t *comp, float sample_rate)
{
    if((comp == nullptr) || (sample_rate <= 0.0f)) return;
    comp->sample_rate = sample_rate;
    comp->attack_s = 0.01f;
    comp->release_s = 0.1f;
    comp->manual_makeup_db = 0.0f;
    comp->mix = 1.0f;
    comp->threshold_db = -18.0f;
    comp->ratio = 2.0f;
    comp->sc_hpf_hz = 0.0f;
    comp->knee_db = 6.0f;
    comp->hpf_l = comp->hpf_r = comp->hpf_x_l = comp->hpf_x_r = 0.0f;
    comp->deluge_mean = comp->deluge_rms_log = comp->deluge_env = 0.0f;
    comp->deluge_gain = comp->brick_gain = 1.0f;
    comp->brick_env = 0.0f;
    comp->transition = 1.0f;
    comp->transition_old_gain = 1.0f;
    comp->cached_makeup = 1.0f;
    comp->cached_hpf_a = 0.0f;
    comp->cached_coeff_frames = 0U;
    comp->coeff_dirty = 1U;
    comp->model = 0U;
    comp->detector_rms = 0U;
    comp->deluge_saturation = 0U;
}

void fx_comp_lab_set_threshold_db(fx_comp_lab_t *c, float v) { if(c) c->threshold_db = clampf(v, -48.0f, 0.0f); }
void fx_comp_lab_set_ratio(fx_comp_lab_t *c, float v) { if(c) c->ratio = clampf(v, 1.0f, 20.0f); }
void fx_comp_lab_set_attack_s(fx_comp_lab_t *c, float v) { if(c) { const float n = clampf(v, 0.0001f, 0.1f); if(n != c->attack_s) { c->attack_s = n; c->coeff_dirty = 1U; } } }
void fx_comp_lab_set_release_s(fx_comp_lab_t *c, float v) { if(c) { const float n = clampf(v, 0.02f, 1.0f); if(n != c->release_s) { c->release_s = n; c->coeff_dirty = 1U; } } }
void fx_comp_lab_set_makeup_db(fx_comp_lab_t *c, float v) { if(c) { const float n = clampf(v, 0.0f, 18.0f); if(n != c->manual_makeup_db) { c->manual_makeup_db = n; c->coeff_dirty = 1U; } } }
void fx_comp_lab_set_mix(fx_comp_lab_t *c, float v) { if(c) c->mix = clampf(v, 0.0f, 1.0f); }

void fx_comp_lab_set_model(fx_comp_lab_t *c, uint8_t model)
{
    if(!c) return;
    model = (model > 2U) ? 2U : model;
    if(model == c->model) return;
    c->transition_old_gain = (c->model == 1U) ? c->deluge_gain
        : ((c->model == 2U) ? c->brick_gain : 1.0f);
    c->model = model;
    c->transition = 0.0f;
    c->hpf_l = c->hpf_r = c->hpf_x_l = c->hpf_x_r = 0.0f;
    c->deluge_mean = c->deluge_rms_log = c->deluge_env = 0.0f;
    c->deluge_gain = c->brick_gain = 1.0f;
    c->brick_env = 0.0f;
}

void fx_comp_lab_set_sc_hpf_hz(fx_comp_lab_t *c, float hz)
{ if(c) { const float n = (hz < 40.0f) ? 0.0f : clampf(hz, 40.0f, 200.0f); if(n != c->sc_hpf_hz) { c->sc_hpf_hz = n; c->coeff_dirty = 1U; } } }
void fx_comp_lab_set_detector_rms(fx_comp_lab_t *c, uint8_t rms) { if(c) c->detector_rms = rms ? 1U : 0U; }
void fx_comp_lab_set_knee_db(fx_comp_lab_t *c, float db) { if(c) c->knee_db = clampf(db, 0.0f, 12.0f); }
void fx_comp_lab_set_deluge_saturation(fx_comp_lab_t *c, uint8_t enabled) { if(c) c->deluge_saturation = enabled ? 1U : 0U; }

static float detector_hpf(fx_comp_lab_t *c, float x, bool right)
{
    if(c->sc_hpf_hz < 40.0f) return x;
    const float a = c->cached_hpf_a;
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

void fx_comp_lab_process_block(fx_comp_lab_t *c, float *left, float *right, uint32_t frames)
{
    if(!c || !left || !right) return;
    /* Model 0 is a true bypass once its model-change crossfade has settled. */
    if((c->model == 0U) && (c->transition >= 1.0f)) return;
    const float mix = c->mix;
    const float imix = 1.0f - mix;
    const uint32_t count = (frames > 64U) ? 64U : frames;
    if ((c->coeff_dirty != 0U) || (c->cached_coeff_frames != count))
    {
        c->cached_makeup = powf(10.0f, c->manual_makeup_db * 0.05f);
        c->cached_hpf_a = (c->sc_hpf_hz >= 40.0f)
            ? expf(-6.28318530718f * c->sc_hpf_hz / c->sample_rate) : 0.0f;
        c->cached_deluge_attack_coeff = expf(-(float)count / (c->attack_s * c->sample_rate));
        c->cached_deluge_release_coeff = expf(-(float)count / (c->release_s * c->sample_rate));
        c->cached_brick_attack_coeff = expf(-8.0f / (c->attack_s * c->sample_rate));
        c->cached_brick_release_coeff = expf(-8.0f / (c->release_s * c->sample_rate));
        c->cached_coeff_frames = count;
        c->coeff_dirty = 0U;
    }
    const float makeup = c->cached_makeup;
    float deluge_target = c->deluge_gain;
    float brick_target = c->brick_gain;
    float deluge_step = 0.0f;
    float brick_step = 0.0f;
    float key_l[64], key_r[64];
    for(uint32_t i = 0U; i < count; ++i)
    {
        key_l[i] = detector_hpf(c, left[i], false);
        key_r[i] = detector_hpf(c, right[i], true);
    }
    for(uint32_t n = 0U; n < count; ++n)
    {
        const float inL = left[n], inR = right[n];
        const float keyL = key_l[n], keyR = key_r[n];
        float wetL = inL, wetR = inR;
        if(c->model == 1U)
        {
            if(n == 0U)
            {
                const float old_mean = c->deluge_mean;
                float sum = 0.0f;
                for(uint32_t i = 0U; i < count; ++i)
                {
                    const float s = fmaxf(fabsf(key_l[i]), fabsf(key_r[i]));
                    sum += s * s;
                }
                const float ns = (float)count * 2.0f;
                c->deluge_mean = ((sum / ns) * ns + old_mean) / (1.0f + ns);
                const float next_rms = logf(fmaxf(sqrtf(c->deluge_mean), 1e-20f));
                const float threshold_np = c->threshold_db * 0.11512925465f;
                const float over = fmaxf(c->deluge_rms_log - threshold_np, 0.0f);
                const float desired = over * (1.0f - 1.0f / c->ratio);
                const float coeff = (desired > c->deluge_env)
                    ? c->cached_deluge_attack_coeff : c->cached_deluge_release_coeff;
                c->deluge_env = desired + coeff * (c->deluge_env - desired);
                deluge_target = expf(-c->deluge_env) * makeup;
                deluge_step = (deluge_target - c->deluge_gain) / (float)count;
                c->deluge_rms_log = next_rms;
            }
            c->deluge_gain += deluge_step;
            wetL = inL * c->deluge_gain;
            wetR = inR * c->deluge_gain;
            if(c->deluge_saturation)
            {
                wetL = tanhf(wetL * 3.0f) / tanhf(3.0f);
                wetR = tanhf(wetR * 3.0f) / tanhf(3.0f);
            }
        }
        else if(c->model == 2U)
        {
            if((n & 7U) == 0U)
            {
                float detect;
                if(c->detector_rms)
                {
                    float sum = 0.0f;
                    const uint32_t window = ((count - n) < 8U) ? (count - n) : 8U;
                    for(uint32_t i = 0U; i < window; ++i)
                    {
                        const float s = fmaxf(fabsf(key_l[n + i]), fabsf(key_r[n + i]));
                        sum += s * s;
                    }
                    detect = sqrtf(sum / (float)window);
                }
                else detect = fmaxf(fabsf(keyL), fabsf(keyR));
                const float level_db = 20.0f * log10f(detect + 1e-20f);
                const float target_db = brick_curve_db(level_db, c->threshold_db, c->ratio, c->knee_db);
                const float target = powf(10.0f, target_db * 0.05f) * makeup;
                const float coeff = (target < c->brick_env)
                    ? c->cached_brick_attack_coeff : c->cached_brick_release_coeff;
                c->brick_env = target + coeff * (c->brick_env - target);
                brick_target = c->brick_env;
                brick_step = (brick_target - c->brick_gain) * 0.125f;
            }
            c->brick_gain += brick_step;
            wetL = inL * c->brick_gain;
            wetR = inR * c->brick_gain;
        }
        const float processedL = inL * imix + wetL * mix;
        const float processedR = inR * imix + wetR * mix;
        if(c->transition < 1.0f)
        {
            c->transition = fminf(1.0f, c->transition + (1.0f / 128.0f));
            const float old_gain = imix + c->transition_old_gain * mix;
            left[n] = inL * old_gain + (processedL - inL * old_gain) * c->transition;
            right[n] = inR * old_gain + (processedR - inR * old_gain) * c->transition;
        }
        else { left[n] = processedL; right[n] = processedR; }
    }
    c->deluge_gain = deluge_target;
    c->brick_gain = brick_target;
}

}
