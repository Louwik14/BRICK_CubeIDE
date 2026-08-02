#include "fx_revb_model.h"

#include <cmath>
#include <cstdio>
#include <vector>

static float damping_to_lp(float damping)
{
    const float clamped = (damping < 0.0f) ? 0.0f : ((damping > 1.0f) ? 1.0f : damping);
    if(clamped <= 0.0f)
        return 1.0f;
    const float arg = ((1.0f - clamped) * 50.0f) + 1.0f;
    const float curve = std::log2(arg) / 5.7f;
    const float bounded = (curve < 0.0f) ? 0.0f : ((curve > 1.0f) ? 1.0f : curve);
    return 1.0f - bounded;
}

static int run_case(float damping)
{
    constexpr size_t kFrames = 131072U;
    std::vector<float> delay(32768U, 0.0f);
    std::vector<float> input(kFrames, 0.0f);
    std::vector<float> left(kFrames, 0.0f);
    std::vector<float> right(kFrames, 0.0f);
    input[0] = 1.0f;

    mifx::Reverb reverb;
    reverb.Init(delay.data());
    reverb.set_amount(1.0f);
    reverb.set_input_gain(1.0f);
    reverb.set_diffusion(0.9f);
    reverb.set_time(1.0f);
    reverb.set_lp(damping_to_lp(damping));
    reverb.set_output_filters(0.0f, 1.0f);
    reverb.Process(input.data(), left.data(), right.data(), kFrames);

    float peak = 0.0f;
    double energy = 0.0;
    for(size_t i = 0U; i < kFrames; ++i)
    {
        const float sample = 0.5f * (left[i] + right[i]);
        if(!std::isfinite(sample))
            return 1;
        const float magnitude = std::fabs(sample);
        peak = (magnitude > peak) ? magnitude : peak;
        energy += (double)sample * (double)sample;
    }

    if(!std::isfinite(peak) || !std::isfinite(energy) || peak > 4.0f)
        return 2;
    return 0;
}

int main()
{
    if(run_case(1.0f) != 0)
    {
        std::fprintf(stderr, "Mutable reverb exceeded finite bounded response at max DAMP\n");
        return 1;
    }
    if(run_case(0.0f) != 0)
    {
        std::fprintf(stderr, "Mutable reverb failed at minimum DAMP\n");
        return 1;
    }
    return 0;
}
