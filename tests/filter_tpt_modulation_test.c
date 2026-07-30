#include "Audio/fx_biquad_filter.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define TEST_SAMPLE_RATE       48000.0f
#define TEST_BLOCK_FRAMES      64U
#define TEST_CHUNK_FRAMES      8U
#define TEST_MIN_CUTOFF_HZ     20.0f
#define TEST_MAX_CUTOFF_HZ     16000.0f
#define TEST_PI                3.14159265358979323846f

typedef enum
{
    TEST_LFO_SINE = 0,
    TEST_LFO_TRIANGLE,
    TEST_LFO_SAW,
    TEST_LFO_SQUARE
} test_lfo_shape_t;

static float test_clampf(float value, float lo, float hi)
{
    if(value < lo) return lo;
    if(value > hi) return hi;
    return value;
}

static float test_lfo_value(test_lfo_shape_t shape, float phase)
{
    phase -= floorf(phase);
    switch(shape)
    {
        case TEST_LFO_TRIANGLE:
            return 1.0f - (4.0f * fabsf(phase - 0.5f));
        case TEST_LFO_SAW:
            return (2.0f * phase) - 1.0f;
        case TEST_LFO_SQUARE:
            return (phase < 0.5f) ? 1.0f : -1.0f;
        case TEST_LFO_SINE:
        default:
            return sinf(2.0f * TEST_PI * phase);
    }
}

static float test_ui_to_cutoff(float value)
{
    const float unit = test_clampf(value, 0.0f, 127.0f) * (1.0f / 127.0f);
    return TEST_MIN_CUTOFF_HZ * powf(2.0f, 9.6438561897747247f * unit);
}

static uint8_t test_finite_coeffs(const fx_biquad_filter_coeffs_t *coeffs)
{
    return (uint8_t)(isfinite(coeffs->a1)
                  && isfinite(coeffs->a2)
                  && isfinite(coeffs->a3)
                  && isfinite(coeffs->k)
                  && isfinite(coeffs->lp_gain)
                  && isfinite(coeffs->hp_gain)
                  && isfinite(coeffs->bp_gain));
}

static uint8_t test_coherent_coeffs(const fx_biquad_filter_coeffs_t *coeffs)
{
    if((test_finite_coeffs(coeffs) == 0U)
            || (fabsf(coeffs->a1) < 1.0e-20f)
            || (fabsf(coeffs->a2) < 1.0e-20f))
        return 0U;

    const float g_from_a2 = coeffs->a2 / coeffs->a1;
    const float g_from_a3 = coeffs->a3 / coeffs->a2;
    const float tolerance = 2.0e-5f * fmaxf(1.0f, fabsf(g_from_a2));
    return (fabsf(g_from_a2 - g_from_a3) <= tolerance) ? 1U : 0U;
}

static void test_fill_input(float *buffer, uint32_t absolute_frame)
{
    for(uint32_t i = 0U; i < TEST_CHUNK_FRAMES; ++i)
    {
        const float frame = (float)(absolute_frame + i);
        buffer[i] = (0.09f * sinf(2.0f * TEST_PI * 997.0f * frame / TEST_SAMPLE_RATE))
                  + (0.04f * sinf(2.0f * TEST_PI * 7111.0f * frame / TEST_SAMPLE_RATE));
    }
}

static int test_static_sound_and_bypass(void)
{
    fx_biquad_filter_mono_t reference;
    fx_biquad_filter_mono_t chunked;
    float reference_buffer[TEST_BLOCK_FRAMES];
    float chunked_buffer[TEST_BLOCK_FRAMES];

    fx_biquad_filter_mono_init(&reference, TEST_SAMPLE_RATE);
    fx_biquad_filter_mono_init(&chunked, TEST_SAMPLE_RATE);
    fx_biquad_filter_mono_set_bypass(&reference, 0U);
    fx_biquad_filter_mono_set_bypass(&chunked, 0U);
    fx_biquad_filter_mono_set_params(&reference, 2400.0f, 6.0f);
    fx_biquad_filter_mono_set_params(&chunked, 2400.0f, 6.0f);

    for(uint32_t i = 0U; i < TEST_BLOCK_FRAMES; ++i)
    {
        reference_buffer[i] = 0.125f * sinf(2.0f * TEST_PI * 997.0f
                                           * (float)i / TEST_SAMPLE_RATE);
        chunked_buffer[i] = reference_buffer[i];
    }

    fx_biquad_filter_mono_process_block(&reference, reference_buffer, TEST_BLOCK_FRAMES);
    for(uint32_t i = 0U; i < TEST_BLOCK_FRAMES; i += TEST_CHUNK_FRAMES)
    {
        fx_biquad_filter_mono_set_params(&chunked, 2400.0f, 6.0f);
        fx_biquad_filter_mono_process_block(&chunked, &chunked_buffer[i], TEST_CHUNK_FRAMES);
    }

    if(memcmp(reference_buffer, chunked_buffer, sizeof(reference_buffer)) != 0)
        return 1;

    fx_biquad_filter_mono_set_bypass(&chunked, 1U);
    for(uint32_t block = 0U; block < 4U; ++block)
    {
        memcpy(reference_buffer, chunked_buffer, sizeof(reference_buffer));
        fx_biquad_filter_mono_process_block(&chunked, chunked_buffer, TEST_BLOCK_FRAMES);
    }
    if((chunked.bypass_mix != 1.0f) || (chunked.ic1eq != 0.0f)
            || (chunked.ic2eq != 0.0f))
        return 2;
    memcpy(reference_buffer, chunked_buffer, sizeof(reference_buffer));
    fx_biquad_filter_mono_process_block(&chunked, chunked_buffer, TEST_BLOCK_FRAMES);
    if(memcmp(reference_buffer, chunked_buffer, sizeof(reference_buffer)) != 0)
        return 3;

    fx_biquad_filter_mono_set_bypass(&chunked, 0U);
    if((chunked.bypass_xfade_remaining != 256U)
            || (chunked.ic1eq != 0.0f) || (chunked.ic2eq != 0.0f))
        return 4;
    return 0;
}

static int test_short_segments(void)
{
    static const uint32_t segment_sizes[] = { 1U, 7U, 8U, 15U, 31U, 64U };
    for(uint32_t size_index = 0U;
            size_index < sizeof(segment_sizes) / sizeof(segment_sizes[0]);
            ++size_index)
    {
        const uint32_t frames = segment_sizes[size_index];
        fx_biquad_filter_mono_t filter;
        float samples[TEST_BLOCK_FRAMES] = {0.0f};
        fx_biquad_filter_mono_init(&filter, TEST_SAMPLE_RATE);
        fx_biquad_filter_mono_set_bypass(&filter, 0U);
        for(uint32_t offset = 0U; offset < frames; offset += TEST_CHUNK_FRAMES)
        {
            uint32_t chunk = frames - offset;
            if(chunk > TEST_CHUNK_FRAMES) chunk = TEST_CHUNK_FRAMES;
            const float progress = (float)(offset + chunk) / (float)frames;
            fx_biquad_filter_mono_set_params(
                &filter,
                80.0f + ((15000.0f - 80.0f) * progress),
                0.70710678f + ((6.5f - 0.70710678f) * progress));
            if(test_coherent_coeffs(&filter.current) == 0U) return 1;
            fx_biquad_filter_mono_process_block(&filter, &samples[offset], chunk);
        }
        for(uint32_t i = 0U; i < frames; ++i)
        {
            if(!isfinite(samples[i])) return 2;
        }
    }
    return 0;
}

static int test_mono_stereo_equivalence_and_transitions(void)
{
    fx_biquad_filter_t stereo;
    fx_biquad_filter_mono_t mono;
    float left[TEST_BLOCK_FRAMES];
    float right[TEST_BLOCK_FRAMES];
    float mono_samples[TEST_BLOCK_FRAMES];
    static const int modes[] = {
        FX_BIQUAD_FILTER_MODE_LP,
        FX_BIQUAD_FILTER_MODE_HP,
        FX_BIQUAD_FILTER_MODE_BP,
        FX_BIQUAD_FILTER_MODE_LP
    };

    fx_biquad_filter_init(&stereo, TEST_SAMPLE_RATE);
    fx_biquad_filter_mono_init(&mono, TEST_SAMPLE_RATE);
    fx_biquad_filter_set_params(&stereo, 1800.0f, 6.5f);
    fx_biquad_filter_mono_set_params(&mono, 1800.0f, 6.5f);
    fx_biquad_filter_set_bypass(&stereo, 0U);
    fx_biquad_filter_mono_set_bypass(&mono, 0U);

    uint32_t absolute = 0U;
    for(uint32_t mode_index = 0U;
            mode_index < sizeof(modes) / sizeof(modes[0]);
            ++mode_index)
    {
        fx_biquad_filter_set_mode(&stereo, (fx_biquad_filter_mode_t)modes[mode_index]);
        fx_biquad_filter_mono_set_mode(&mono, (fx_biquad_filter_mode_t)modes[mode_index]);
        for(uint32_t block = 0U; block < 6U; ++block)
        {
            for(uint32_t i = 0U; i < TEST_BLOCK_FRAMES; ++i)
            {
                const float x = 0.2f * sinf(TEST_PI * 2.0f * 733.0f
                                           * (float)(absolute + i) / TEST_SAMPLE_RATE);
                left[i] = x;
                right[i] = x;
                mono_samples[i] = x;
            }
            fx_biquad_filter_process_block(&stereo, left, right, TEST_BLOCK_FRAMES);
            fx_biquad_filter_mono_process_block(&mono, mono_samples, TEST_BLOCK_FRAMES);
            for(uint32_t i = 0U; i < TEST_BLOCK_FRAMES; ++i)
            {
                if((fabsf(left[i] - mono_samples[i]) > 2.0e-6f)
                        || (fabsf(right[i] - mono_samples[i]) > 2.0e-6f))
                    return 1;
            }
            absolute += TEST_BLOCK_FRAMES;
        }
    }

    fx_biquad_filter_set_bypass(&stereo, 1U);
    fx_biquad_filter_mono_set_bypass(&mono, 1U);
    for(uint32_t block = 0U; block < 4U; ++block)
    {
        memset(left, 0, sizeof(left));
        memset(right, 0, sizeof(right));
        memset(mono_samples, 0, sizeof(mono_samples));
        fx_biquad_filter_process_block(&stereo, left, right, TEST_BLOCK_FRAMES);
        fx_biquad_filter_mono_process_block(&mono, mono_samples, TEST_BLOCK_FRAMES);
    }
    if((stereo.ic1eq_l != 0.0f) || (stereo.ic2eq_l != 0.0f)
            || (stereo.ic1eq_r != 0.0f) || (stereo.ic2eq_r != 0.0f)
            || (mono.ic1eq != 0.0f) || (mono.ic2eq != 0.0f))
        return 2;

    fx_biquad_filter_set_bypass(&stereo, 0U);
    fx_biquad_filter_mono_set_bypass(&mono, 0U);
    return ((stereo.ic1eq_l == 0.0f) && (stereo.ic2eq_l == 0.0f)
            && (mono.ic1eq == 0.0f) && (mono.ic2eq == 0.0f)) ? 0 : 3;
}

int filter_tpt_modulation_test(void)
{
    static const float rates_hz[] = {0.1f, 1.0f, 10.0f, 40.0f, 80.0f};
    static const float depths[] = {32.0f, 64.0f, 127.0f};
    static const float q_values[] = {0.70710678f, 2.0f, 4.0f, 6.5f};
    const int static_result = test_static_sound_and_bypass();
    if(static_result != 0)
        return 100 + static_result;
    const int short_result = test_short_segments();
    if(short_result != 0)
        return 120 + short_result;
    const int transition_result = test_mono_stereo_equivalence_and_transitions();
    if(transition_result != 0)
        return 140 + transition_result;

    for(uint32_t shape = 0U; shape <= (uint32_t)TEST_LFO_SQUARE; ++shape)
    {
        for(uint32_t rate_index = 0U;
                rate_index < (sizeof(rates_hz) / sizeof(rates_hz[0]));
                ++rate_index)
        {
            for(uint32_t depth_index = 0U;
                    depth_index < (sizeof(depths) / sizeof(depths[0]));
                    ++depth_index)
            {
                for(uint32_t q_index = 0U;
                        q_index < (sizeof(q_values) / sizeof(q_values[0]));
                        ++q_index)
                {
                    fx_biquad_filter_mono_t filter;
                    float cutoff_current = test_ui_to_cutoff(63.5f);
                    float q_current = q_values[0];
                    float output_previous = 0.0f;
                    uint8_t output_valid = 0U;
                    uint32_t absolute_frame = 0U;

                    fx_biquad_filter_mono_init(&filter, TEST_SAMPLE_RATE);
                    fx_biquad_filter_mono_set_bypass(&filter, 0U);
                    fx_biquad_filter_mono_set_params(&filter, cutoff_current,
                                                     q_current);

                    for(uint32_t block = 0U; block < 256U; ++block)
                    {
                        const float phase = rates_hz[rate_index]
                                          * (float)absolute_frame / TEST_SAMPLE_RATE;
                        const float lfo =
                                test_lfo_value((test_lfo_shape_t)shape, phase);
                        const float ui = 63.5f + (depths[depth_index] * lfo);
                        const float cutoff_target = test_ui_to_cutoff(ui);
                        const float cutoff_smoothed =
                                cutoff_current + (0.25f * (cutoff_target - cutoff_current));
                        const float q_target = q_values[0]
                                             + ((q_values[q_index] - q_values[0])
                                                * ((lfo + 1.0f) * 0.5f));
                        const float q_smoothed =
                                q_current + (0.25f * (q_target - q_current));

                        for(uint32_t chunk = 0U; chunk < 8U; ++chunk)
                        {
                            float samples[TEST_CHUNK_FRAMES];
                            const float progress = (float)(chunk + 1U) * (1.0f / 8.0f);
                            const float cutoff = cutoff_current
                                               + ((cutoff_smoothed - cutoff_current) * progress);
                            const float q = q_current
                                          + ((q_smoothed - q_current) * progress);
                            test_fill_input(samples, absolute_frame + (chunk * TEST_CHUNK_FRAMES));
                            fx_biquad_filter_mono_set_params(&filter, cutoff, q);

                            if(test_coherent_coeffs(&filter.current) == 0U)
                                return 200;

                            const fx_biquad_filter_coeffs_t held = filter.current;
                            fx_biquad_filter_mono_process_block(&filter, samples,
                                                               TEST_CHUNK_FRAMES);
                            if(memcmp(&held, &filter.current, sizeof(held)) != 0)
                                return 201;

                            for(uint32_t i = 0U; i < TEST_CHUNK_FRAMES; ++i)
                            {
                                if(!isfinite(samples[i]))
                                    return 202;
                                if(output_valid != 0U)
                                {
                                    const float delta = samples[i] - output_previous;
                                    if(!isfinite(delta))
                                        return 203;
                                }
                                output_previous = samples[i];
                                output_valid = 1U;
                            }
                        }

                        cutoff_current = cutoff_smoothed;
                        q_current = q_smoothed;
                        absolute_frame += TEST_BLOCK_FRAMES;
                    }
                }
            }
        }
    }

    return 0;
}
