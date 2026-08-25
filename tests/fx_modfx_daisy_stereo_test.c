#include "Audio/fx_modfx_global.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

const int16_t sineWaveSmall[257] = {0};

#define BLOCK 64U

static void set_parameters(float rate_a, float rate_b,
                           float delay_a, float delay_b,
                           float depth_a, float depth_b, float feedback_x)
{
    fx_modfx_global_set_rate(rate_a);
    fx_modfx_global_set_rate_b(rate_b);
    fx_modfx_global_set_offset(delay_a);
    fx_modfx_global_set_offset_b(delay_b);
    fx_modfx_global_set_depth(depth_a);
    fx_modfx_global_set_depth_b(depth_b);
    fx_modfx_global_set_feedback(feedback_x);
    fx_modfx_global_set_width(0.5f);
}

static void assert_pair(float a, float b, float expected_a, float expected_b)
{
    assert(fabsf(a - expected_a) < 1.0e-5f);
    assert(fabsf(b - expected_b) < 1.0e-5f);
}

static float render_difference(void)
{
    float left[BLOCK];
    float right[BLOCK];
    float difference = 0.0f;
    for (uint32_t block = 0U; block < 32U; ++block) {
        memset(left, 0, sizeof left);
        memset(right, 0, sizeof right);
        if (block == 0U) left[0] = right[0] = 1.0f;
        fx_modfx_global_process_block(left, right, BLOCK);
        for (uint32_t i = 0U; i < BLOCK; ++i) {
            assert(isfinite(left[i]));
            assert(isfinite(right[i]));
            difference += fabsf(left[i] - right[i]);
        }
    }
    return difference;
}

static void reset_stereo(void)
{
    fx_modfx_global_init();
    fx_modfx_global_set_model(FX_MODFX_DAISY_STEREO);
    set_parameters(0.3f, 0.3f, 0.75f, 0.75f, 0.9f, 0.9f, 0.0f);
}

static void test_independent_parameters(void)
{
    reset_stereo();
    assert(render_difference() < 1.0e-6f);
    reset_stereo();
    fx_modfx_global_set_rate_b(1.2f);
    assert(render_difference() > 1.0e-5f);
    reset_stereo();
    fx_modfx_global_set_offset_b(0.25f);
    assert(render_difference() > 1.0e-5f);
    reset_stereo();
    fx_modfx_global_set_depth_b(0.35f);
    assert(render_difference() > 1.0e-5f);
    reset_stereo();
    set_parameters(0.2f, 1.2f, 0.2f, 0.9f, 0.3f, 0.93f, 1.0f);
    assert(render_difference() > 1.0e-5f);
}

static void test_feedback_mapping(void)
{
    static const float position[5] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
    static const float expected_a[5] = {0.95f, 0.7125f, 0.475f, 0.2375f, 0.0f};
    static const float expected_b[5] = {0.0f, 0.2375f, 0.475f, 0.7125f, 0.95f};
    fx_modfx_daisy_stereo_debug_t p;
    reset_stereo();
    for (uint32_t i = 0U; i < 5U; ++i) {
        fx_modfx_global_set_feedback(position[i]);
        fx_modfx_global_daisy_stereo_debug(&p);
        assert_pair(p.feedback[0], p.feedback[1], expected_a[i], expected_b[i]);
        assert(p.feedback[0] < 1.0f);
        assert(p.feedback[1] < 1.0f);
        assert(fabsf(p.feedback[0] + p.feedback[1] - 0.95f) < 1.0e-6f);
    }
}

static float render_width(float width)
{
    float left[BLOCK] = {0};
    float right[BLOCK] = {0};
    reset_stereo();
    fx_modfx_global_set_rate_b(1.2f);
    fx_modfx_global_set_width(width);
    fx_modfx_global_process_block(left, right, BLOCK);
    return render_difference();
}

static void test_width_mapping(void)
{
    const float mono = render_width(0.0f);
    const float daisy = render_width(0.5f);
    const float split = render_width(1.0f);
    assert(mono < 1.0e-6f);
    assert(daisy > 1.0e-5f);
    assert(split > daisy);
}

static void test_live_modulation_and_tails(void)
{
    float left[BLOCK];
    float right[BLOCK];
    float phase = 0.0f;
    reset_stereo();
    for (uint32_t block = 0U; block < 32U; ++block) {
        const float x = (block & 1U) ? 1.0f : -1.0f;
        set_parameters((x > 0.0f) ? 1.2f : 0.02f,
                       (x > 0.0f) ? 0.02f : 1.2f,
                       (x + 1.0f) * 0.5f, (1.0f - x) * 0.5f,
                       (x > 0.0f) ? 0.93f : 0.1f,
                       (x > 0.0f) ? 0.1f : 0.93f, x);
        for (uint32_t i = 0U; i < BLOCK; ++i) {
            const float sample = 0.2f * sinf(phase);
            phase += 0.03125f;
            left[i] = right[i] = sample;
        }
        fx_modfx_global_process_block(left, right, BLOCK);
        for (uint32_t i = 0U; i < BLOCK; ++i) {
            assert(isfinite(left[i]));
            assert(isfinite(right[i]));
            assert(fabsf(left[i]) < 4.0f);
            assert(fabsf(right[i]) < 4.0f);
        }
    }

    fx_modfx_global_set_feedback(-1.0f);
    memset(left, 0, sizeof left);
    memset(right, 0, sizeof right);
    left[0] = right[0] = 1.0f;
    fx_modfx_global_process_block(left, right, BLOCK);
    for (uint32_t block = 0U; block < 4U; ++block) {
        memset(left, 0, sizeof left);
        memset(right, 0, sizeof right);
        fx_modfx_global_process_block(left, right, BLOCK);
    }
    float tail_energy = 0.0f;
    fx_modfx_global_set_model(FX_MODFX_OFF);
    for (uint32_t block = 0U; block < 8U; ++block) {
        memset(left, 0, sizeof left);
        memset(right, 0, sizeof right);
        fx_modfx_global_process_block(left, right, BLOCK);
        for (uint32_t i = 0U; i < BLOCK; ++i)
            tail_energy += fabsf(left[i]) + fabsf(right[i]);
    }
    fx_modfx_global_set_model(FX_MODFX_DAISY_STEREO);
    for (uint32_t block = 0U; block < 8U; ++block) {
        memset(left, 0, sizeof left);
        memset(right, 0, sizeof right);
        fx_modfx_global_process_block(left, right, BLOCK);
        for (uint32_t i = 0U; i < BLOCK; ++i)
            tail_energy += fabsf(left[i]) + fabsf(right[i]);
    }
    assert(tail_energy > 0.0f);
}

int main(void)
{
    test_independent_parameters();
    test_feedback_mapping();
    test_width_mapping();
    test_live_modulation_and_tails();
    return 0;
}
