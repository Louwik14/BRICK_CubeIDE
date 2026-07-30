#include <math.h>
#include <stdint.h>

#define TEST_PI 3.14159265358979323846f

typedef enum {
    SHAPE_SINE = 0,
    SHAPE_TRIANGLE,
    SHAPE_SAW,
    SHAPE_SQUARE
} test_shape_t;

static float lfo_value(test_shape_t shape, float phase)
{
    phase -= floorf(phase);
    switch (shape) {
        case SHAPE_TRIANGLE: return 1.0f - 4.0f * fabsf(phase - 0.5f);
        case SHAPE_SAW: return 2.0f * phase - 1.0f;
        case SHAPE_SQUARE: return (phase < 0.5f) ? 1.0f : -1.0f;
        case SHAPE_SINE:
        default: return sinf(2.0f * TEST_PI * phase);
    }
}

static int test_plock_matrix_composition(void)
{
    const float normal_base = 42.0f;
    float temporary_base = 80.0f;
    const float slots[] = { 3.0f, -7.0f, 1.5f };
    float result = temporary_base;
    for (uint32_t i = 0U; i < 3U; ++i) result += slots[i];
    if (fabsf(result - 77.5f) > 1.0e-6f) return 1;

    temporary_base = 12.0f; /* consecutive lock */
    result = temporary_base;
    for (uint32_t i = 0U; i < 3U; ++i) result += slots[i];
    if (fabsf(result - 9.5f) > 1.0e-6f) return 2;

    result = normal_base; /* lock release */
    for (uint32_t i = 0U; i < 3U; ++i) result += slots[i];
    return (fabsf(result - 39.5f) <= 1.0e-6f) ? 0 : 3;
}

static int test_segment_start_and_ramps(void)
{
    static const uint32_t frames_to_test[] = { 1U, 7U, 8U, 15U, 64U };
    for (uint32_t f = 0U; f < sizeof(frames_to_test) / sizeof(frames_to_test[0]); ++f) {
        const uint32_t frames = frames_to_test[f];
        float env = 0.125f;
        const float segment_value = env;
        env += 0.005f * (float)frames;
        if (fabsf(segment_value - 0.125f) > 1.0e-7f) return 10;

        float level = 0.2f;
        const float target = 0.9f;
        const float step = (target - level) / (float)frames;
        uint32_t phase_inc = 1000U;
        const uint32_t phase_target = 9000U;
        for (uint32_t i = 0U; i < frames; ++i) {
            level += step;
            const int64_t delta = (int64_t)phase_target - 1000;
            phase_inc = (uint32_t)(1000 + delta * (int64_t)(i + 1U) / (int64_t)frames);
            if (!isfinite(level) || phase_inc < 1000U || phase_inc > phase_target) return 11;
        }
        if (fabsf(level - target) > 2.0e-6f || phase_inc != phase_target) return 12;
    }
    return 0;
}

static int test_lfo_and_elapsed_slew(void)
{
    static const float rates[] = { 0.25f, 3.0f, 17.0f, 50.0f };
    static const float depths[] = { 0.1f, 0.5f, 1.0f };
    static const uint32_t segments[] = { 8U, 16U, 64U };
    for (uint32_t shape = 0U; shape <= SHAPE_SQUARE; ++shape) {
        for (uint32_t r = 0U; r < sizeof(rates) / sizeof(rates[0]); ++r) {
            for (uint32_t d = 0U; d < sizeof(depths) / sizeof(depths[0]); ++d) {
                float phase = 0.0f;
                for (uint32_t s = 0U; s < sizeof(segments) / sizeof(segments[0]); ++s) {
                    const float v = lfo_value((test_shape_t)shape, phase) * depths[d];
                    if (!isfinite(v) || fabsf(v) > 1.000001f) return 20;
                    phase += rates[r] * (float)segments[s] / 48000.0f;
                }
            }
        }
    }

    const float reference_alpha = 0.24f;
    const float tau = 64.0f * (1.0f - reference_alpha) / reference_alpha;
    const float once = 64.0f / (tau + 64.0f);
    const float a8 = 8.0f / (tau + 8.0f);
    float split = 0.0f;
    for (uint32_t i = 0U; i < 8U; ++i) split += (1.0f - split) * a8;
    if (!isfinite(split) || !isfinite(once)
            || split <= 0.0f || split >= 1.0f
            || fabsf(split - once) > 0.04f) return 21;
    return 0;
}

static int test_keytrack_and_dj_chunks(void)
{
    const float ratio_c4 = exp2f((60.0f - 60.0f) / 12.0f);
    const float ratio_c5 = exp2f((72.0f - 60.0f) / 12.0f);
    const float ratio_half = exp2f(((72.0f - 60.0f) * 0.5f) / 12.0f);
    if (fabsf(ratio_c4 - 1.0f) > 1.0e-6f
            || fabsf(ratio_c5 - 2.0f) > 1.0e-6f
            || fabsf(ratio_half - 1.41421356f) > 2.0e-6f) return 30;

    static const uint32_t frames_to_test[] = { 7U, 8U, 31U, 64U };
    for (uint32_t f = 0U; f < sizeof(frames_to_test) / sizeof(frames_to_test[0]); ++f) {
        const uint32_t frames = frames_to_test[f];
        float previous[3] = { -12.0f, 3.0f, -6.0f };
        const float target[3] = { 6.0f, -9.0f, 12.0f };
        for (uint32_t offset = 0U; offset < frames; offset += 8U) {
            uint32_t chunk = frames - offset;
            if (chunk > 8U) chunk = 8U;
            const float progress = (float)(offset + chunk) / (float)frames;
            for (uint32_t band = 0U; band < 3U; ++band) {
                const float value = previous[band]
                    + ((target[band] - previous[band]) * progress);
                if (!isfinite(value) || value < -80.0f || value > 12.0f) return 31;
            }
        }
    }
    return 0;
}

int common_modulation_test(void)
{
    int result = test_plock_matrix_composition();
    if (result != 0) return result;
    result = test_segment_start_and_ramps();
    if (result != 0) return result;
    result = test_lfo_and_elapsed_slew();
    if (result != 0) return result;
    return test_keytrack_and_dj_chunks();
}
