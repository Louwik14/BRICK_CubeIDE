#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "Audio/md_dsp.h"

int main(void)
{
    md_phase_t osc = {0U, md_phase_increment_from_hz(12000.0f, 48000.0f)};
    assert(fabsf(md_phase_sine_next(&osc)) < 0.001f);
    assert(md_phase_sine_next(&osc) > 0.99f);
    assert(fabsf(md_phase_sine_next(&osc)) < 0.001f);
    assert(md_phase_sine_next(&osc) < -0.99f);

    md_decay_env_t env = {0};
    md_decay_env_prepare(&env, 0.001f, 48000.0f);
    md_decay_env_trigger(&env, 1.0f);
    for (uint32_t i = 0U; i < 1024U; ++i)
    {
        const float value = md_decay_env_process(&env);
        assert(isfinite(value));
        assert(value >= 0.0f);
        assert(value <= 1.0f);
    }
    assert(env.active == 0U);

    md_rng_t a;
    md_rng_t b;
    md_rng_seed(&a, 1234U);
    md_rng_seed(&b, 1234U);
    for (uint8_t i = 0U; i < 32U; ++i)
    {
        assert(md_rng_next_u32(&a) == md_rng_next_u32(&b));
    }

    md_hpf_t hpf = {0};
    md_lpf_t lpf = {0};
    md_hpf_prepare(&hpf, 0.95f);
    md_lpf_prepare(&lpf, 0.2f);
    for (uint16_t i = 0U; i < 512U; ++i)
    {
        assert(isfinite(md_hpf_process(&hpf, 1.0f)));
        assert(isfinite(md_lpf_process(&lpf, 1.0f)));
    }
    assert(fabsf(hpf.y1) < 0.001f);
    assert(fabsf(lpf.state - 1.0f) < 0.001f);

    assert(md_clip(1000.0f, 16.0f) == 1.0f);
    assert(md_clip(-1000.0f, 16.0f) == -1.0f);
    assert(md_mix2(0.0f, 1.0f, 0.25f) == 0.25f);
    return 0;
}
