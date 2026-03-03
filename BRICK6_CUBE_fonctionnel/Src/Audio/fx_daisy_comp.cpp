#include "fx_daisy_comp.h"
#include <math.h>

namespace {

static float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

} // namespace

extern "C" {

fx_daisy_comp_t *fx_daisy_comp_get_instance(void)
{
    static fx_daisy_comp_t s_comp;
    return &s_comp;
}

void fx_daisy_comp_init(fx_daisy_comp_t *comp, float sample_rate)
{
    if((comp == nullptr) || (sample_rate <= 0.0f))
        return;

    comp->core.Init(sample_rate);

    comp->attack_s         = 0.1f;
    comp->release_s        = 0.1f;
    comp->auto_makeup      = 1U;
    comp->manual_makeup_db = 0.0f;
    comp->mix              = 1.0f; // NEW

    comp->core.SetAttack(comp->attack_s);
    comp->core.SetRelease(comp->release_s);
    comp->core.AutoMakeup(true);
}

void fx_daisy_comp_set_threshold_db(fx_daisy_comp_t *comp, float threshold_db)
{
    if(comp == nullptr)
        return;

    comp->core.SetThreshold(clampf(threshold_db, -80.0f, 0.0f));
}

void fx_daisy_comp_set_ratio(fx_daisy_comp_t *comp, float ratio)
{
    if(comp == nullptr)
        return;

    comp->core.SetRatio(clampf(ratio, 1.0f, 40.0f));
}

void fx_daisy_comp_set_attack_s(fx_daisy_comp_t *comp, float attack_s)
{
    if(comp == nullptr)
        return;

    const float attack = clampf(attack_s, 0.001f, 10.0f);
    comp->attack_s = attack;
    comp->core.SetAttack(attack);
}

void fx_daisy_comp_set_release_s(fx_daisy_comp_t *comp, float release_s)
{
    if(comp == nullptr)
        return;

    const float release = clampf(release_s, 0.001f, 10.0f);
    comp->release_s = release;
    comp->core.SetRelease(release);
}

void fx_daisy_comp_set_makeup_db(fx_daisy_comp_t *comp, float makeup_db)
{
    if(comp == nullptr)
        return;

    comp->manual_makeup_db = clampf(makeup_db, 0.0f, 24.0f);

    if(comp->auto_makeup == 0U)
        comp->core.SetMakeup(comp->manual_makeup_db);
}

void fx_daisy_comp_set_auto_makeup(fx_daisy_comp_t *comp, uint8_t enabled)
{
    if(comp == nullptr)
        return;

    comp->auto_makeup = enabled ? 1U : 0U;

    comp->core.AutoMakeup(comp->auto_makeup != 0U);

    if(comp->auto_makeup == 0U)
        comp->core.SetMakeup(comp->manual_makeup_db);
}

// NEW
void fx_daisy_comp_set_mix(fx_daisy_comp_t *comp, float mix)
{
    if(comp == nullptr)
        return;

    comp->mix = clampf(mix, 0.0f, 1.0f);
}

void fx_daisy_comp_process_block(fx_daisy_comp_t *comp,
                                 float *left,
                                 float *right,
                                 uint32_t frames)
{
    if((comp == nullptr) || (left == nullptr) || (right == nullptr))
        return;

    const float mix  = comp->mix;
    const float imix = 1.0f - mix;

    for(uint32_t n = 0U; n < frames; ++n)
    {
        float inL = left[n];
        float inR = right[n];

        float key = fmaxf(fabsf(inL), fabsf(inR)) + 1e-20f;

        comp->core.Process(key);

        float wetL = comp->core.Apply(inL);
        float wetR = comp->core.Apply(inR);

        float gain_comp = 1.0f / (1.0f + mix);

        left[n]  = (inL * imix + wetL * mix) * gain_comp;
        right[n] = (inR * imix + wetR * mix) * gain_comp;
    }
}

} // extern "C"
