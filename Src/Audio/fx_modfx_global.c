#include "Audio/fx_modfx_global.h"

#include <math.h>
#include <string.h>

#include "Platform/memory_layout.h"

#define DAISY_RING_SIZE 1024U
#define DAISY_RING_MASK (DAISY_RING_SIZE - 1U)
#define JUNO_RING_SIZE 512U
#define JUNO_RING_MASK (JUNO_RING_SIZE - 1U)
#define DAISY_STEREO_FEEDBACK_TOTAL 0.95f

/* At the exposed maxima: centre = 8 ms = 384 samples and
 * modulation = 0.93 * 384 = 357.12 samples.  Linear interpolation
 * therefore reads at most logical offsets 741 and 742. */
_Static_assert(743U <= DAISY_RING_SIZE, "Daisy history cannot cover its maximum delay");

typedef struct { float l, r; } stereo_float_t;

typedef union {
    float daisy[2][DAISY_RING_SIZE];
    float juno[JUNO_RING_SIZE];
} modfx_history_t;

typedef struct { float ff, fb, z; } juno_filter_t;

typedef struct {
    float phase, step, delay, amplitude, feedback;
    uint32_t write;
} daisy_stereo_engine_t;

typedef struct {
    float rate_hz, depth, delay, feedback;
} daisy_stereo_parameters_t;

typedef struct {
    uint32_t write, juno_phase[2];
    int32_t offset;
    daisy_stereo_engine_t daisy_stereo[2];
    daisy_stereo_parameters_t daisy_stereo_target[2];
    float daisy_stereo_matrix_main, daisy_stereo_matrix_main_target;
    float juno_gain[2];
    juno_filter_t juno_pre, juno_post[2];
    uint8_t model;
} modfx_state_t;

AUDIO_WARM static modfx_history_t history;
AUDIO_WARM static modfx_state_t state;

static inline __attribute__((always_inline)) float lerp(float a, float b, float fraction)
{
    return a + (b - a) * fraction;
}

static inline __attribute__((always_inline)) float daisy_stereo_sample(
    daisy_stereo_engine_t *engine, float *ring, float input,
    float delay_increment, float amplitude_increment, float feedback_increment)
{
    float phase = engine->phase + engine->step;
    float step = engine->step;
    if (phase > 1.0f) {
        phase = 2.0f - phase;
        step = -step;
    } else if (phase < -1.0f) {
        phase = -2.0f - phase;
        step = -step;
    }
    engine->phase = phase;
    engine->step = step;
    engine->delay += delay_increment;
    engine->amplitude += amplitude_increment;
    engine->feedback += feedback_increment;

    const float delay = engine->delay + phase * engine->amplitude;
    const uint32_t whole = (uint32_t)delay;
    const uint32_t position = (engine->write + whole) & DAISY_RING_MASK;
    const float output = lerp(ring[position],
                              ring[(position + 1U) & DAISY_RING_MASK],
                              delay - (float)whole);
    ring[engine->write] = input + output * engine->feedback;
    engine->write = (engine->write - 1U) & DAISY_RING_MASK;
    return output;
}

static void daisy_stereo_set_feedback_target(float x)
{
    if (x < -1.0f) x = -1.0f;
    if (x > 1.0f) x = 1.0f;
    state.daisy_stereo_target[0].feedback =
        0.5f * DAISY_STEREO_FEEDBACK_TOTAL * (1.0f - x);
    state.daisy_stereo_target[1].feedback =
        0.5f * DAISY_STEREO_FEEDBACK_TOTAL * (1.0f + x);
}

static void daisy_stereo_set_defaults(void)
{
    for (uint8_t engine = 0U; engine < 2U; ++engine) {
        state.daisy_stereo_target[engine].rate_hz = 0.3f;
        state.daisy_stereo_target[engine].depth = 0.9f;
        state.daisy_stereo_target[engine].delay = 6.025f * 48.0f;
    }
    daisy_stereo_set_feedback_target(0.0f);
    state.daisy_stereo_matrix_main = 0.75f;
    state.daisy_stereo_matrix_main_target = 0.75f;
    for (uint8_t engine = 0U; engine < 2U; ++engine) {
        daisy_stereo_engine_t *const current = &state.daisy_stereo[engine];
        const daisy_stereo_parameters_t *const target = &state.daisy_stereo_target[engine];
        current->phase = 0.0f;
        current->step = 4.0f * target->rate_hz / 48000.0f;
        current->delay = target->delay;
        current->amplitude = target->depth * target->delay;
        current->feedback = target->feedback;
        current->write = 0U;
    }
}

static void juno_filter_coefficients(juno_filter_t *filter, float frequency)
{
    const float k = tanf(3.14159265358979323846f * frequency / 48000.0f);
    const float denominator = k + 1.0f;
    filter->ff = k / denominator;
    filter->fb = (k - 1.0f) / denominator;
}

static inline __attribute__((always_inline)) float juno_filter(juno_filter_t *filter, float input)
{
    const float output = filter->ff * input + filter->z;
    filter->z = filter->ff * input - filter->fb * output;
    return output;
}

static void juno_cut(float value)
{
    const float squared = value * value;
    juno_filter_coefficients(&state.juno_pre, 2000.0f + (23500.0f - 2000.0f) * squared);
    juno_filter_coefficients(&state.juno_post[0], 6000.0f + (23500.0f - 6000.0f) * squared);
    state.juno_post[1].ff = state.juno_post[0].ff;
    state.juno_post[1].fb = state.juno_post[0].fb;
}

static void juno_mode(float value)
{
    const float scaled = value * 2.9999f;
    const unsigned mode = (unsigned)scaled;
    const float fraction = scaled - (float)mode;
    if ((fraction < 0.1f) && (mode > 0U)) {
        const float x = 0.5f + fraction * 5.0f;
        const float a0 = (mode == 1U) ? 1.0f : 0.7071067811865475f;
        const float b0 = (mode == 1U) ? 0.0f : 0.7071067811865475f;
        const float a1 = (mode == 1U) ? 0.7071067811865475f : 0.0f;
        const float b1 = (mode == 1U) ? 0.7071067811865475f : 1.0f;
        state.juno_gain[0] = a0 + (a1 - a0) * x;
        state.juno_gain[1] = b0 + (b1 - b0) * x;
        juno_cut(1.0f - x);
    } else if ((fraction > 0.9f) && (mode < 2U)) {
        const float x = (fraction - 0.9f) * 5.0f;
        const float a0 = (mode == 0U) ? 1.0f : 0.7071067811865475f;
        const float b0 = (mode == 0U) ? 0.0f : 0.7071067811865475f;
        float a1 = 0.7071067811865475f;
        float b1 = 0.7071067811865475f;
        if (mode == 1U) { a1 = 0.0f; b1 = 1.0f; }
        state.juno_gain[0] = a0 + (a1 - a0) * x;
        state.juno_gain[1] = b0 + (b1 - b0) * x;
        juno_cut(1.0f - x);
    } else {
        state.juno_gain[0] = (mode == 0U) ? 1.0f
            : ((mode == 1U) ? 0.7071067811865475f : 0.0f);
        state.juno_gain[1] = (mode == 0U) ? 0.0f
            : ((mode == 1U) ? 0.7071067811865475f : 1.0f);
        float x = (fraction - 0.1f) * 1.25f;
        if (x < 0.0f) x = 0.0f;
        if (x > 1.0f) x = 1.0f;
        juno_cut(x);
    }
}

static inline __attribute__((always_inline)) float juno_read(float phase, float low, float high)
{
    const float delay = (low + (high - low) * phase) * 48000.0f;
    const uint32_t whole = (uint32_t)delay;
    return lerp(history.juno[(state.write + whole) & JUNO_RING_MASK],
                history.juno[(state.write + whole + 1U) & JUNO_RING_MASK],
                delay - (float)whole);
}

static inline __attribute__((always_inline)) stereo_float_t juno_sample(stereo_float_t input)
{
    float mono = input.l + input.r;
    const float squared = mono * mono;
    mono = mono * (27.0f + squared) / (27.0f + 9.0f * squared);
    history.juno[state.write] = juno_filter(&state.juno_pre, mono);
    state.write = (state.write - 1U) & JUNO_RING_MASK;
    state.juno_phase[0] += 45902U;
    state.juno_phase[1] += 77219U;
    const float phase0 = fabsf((float)(int32_t)state.juno_phase[0]
                               * (1.0f / 2147483648.0f));
    const float phase1 = fabsf((float)(int32_t)state.juno_phase[1]
                               * (1.0f / 2147483648.0f));
    const float left = state.juno_gain[0] * juno_read(phase0, 0.00154f, 0.00515f)
        + state.juno_gain[1] * juno_read(phase1, 0.00154f, 0.00515f);
    const float right = state.juno_gain[0] * juno_read(1.0f - phase0, 0.00151f, 0.00540f)
        + state.juno_gain[1] * juno_read(1.0f - phase1, 0.00151f, 0.00540f);
    return (stereo_float_t){juno_filter(&state.juno_post[0], left),
                            juno_filter(&state.juno_post[1], right)};
}

static void refresh(void)
{
    if (state.model == FX_MODFX_JUNOLOGUE) {
        juno_mode((state.offset * (1.0f / 2147483648.0f) + 1.0f) * 0.5f);
    }
}

void fx_modfx_global_init(void)
{
    memset(&state, 0, sizeof state);
    memset(&history, 0, sizeof history);
    daisy_stereo_set_defaults();
}

void fx_modfx_global_set_model(uint8_t model)
{
    if ((model != FX_MODFX_DAISY_STEREO) && (model != FX_MODFX_JUNOLOGUE))
        model = FX_MODFX_OFF;
    if (state.model == model) return;
    state.write = 0U;
    memset(&history, 0, sizeof history);
    memset(&state.juno_pre, 0, sizeof state.juno_pre);
    memset(state.juno_post, 0, sizeof state.juno_post);
    state.juno_phase[0] = state.juno_phase[1] = 0x80000000U;
    if (model == FX_MODFX_DAISY_STEREO) daisy_stereo_set_defaults();
    state.model = model;
    refresh();
}

void fx_modfx_global_set_rate(float value)
{
    if (value < 0.01f) value = 0.01f;
    if (value > 12.0f) value = 12.0f;
    state.daisy_stereo_target[0].rate_hz = value;
}

void fx_modfx_global_set_rate_b(float value)
{
    if (value < 0.01f) value = 0.01f;
    if (value > 12.0f) value = 12.0f;
    state.daisy_stereo_target[1].rate_hz = value;
}

void fx_modfx_global_set_depth(float value)
{
    if (value < 0.0f) value = 0.0f;
    if (value > 0.93f) value = 0.93f;
    state.daisy_stereo_target[0].depth = value;
}

void fx_modfx_global_set_depth_b(float value)
{
    if (value < 0.0f) value = 0.0f;
    if (value > 0.93f) value = 0.93f;
    state.daisy_stereo_target[1].depth = value;
}

void fx_modfx_global_set_feedback(float value)
{
    daisy_stereo_set_feedback_target(value);
}

void fx_modfx_global_set_offset(float value)
{
    if (state.model != FX_MODFX_JUNOLOGUE) {
        if (value < 0.0f) value = 0.0f;
        if (value > 1.0f) value = 1.0f;
        state.daisy_stereo_target[0].delay = (0.1f + 7.9f * value) * 48.0f;
        return;
    }
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    state.offset = (int32_t)((value * 2.0f - 1.0f) * 2147483647.0f);
    refresh();
}

void fx_modfx_global_set_offset_b(float value)
{
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    state.daisy_stereo_target[1].delay = (0.1f + 7.9f * value) * 48.0f;
}

void fx_modfx_global_set_width(float value)
{
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    state.daisy_stereo_matrix_main_target = 0.5f + 0.5f * value;
}

uint8_t fx_modfx_global_is_active(void) { return state.model != FX_MODFX_OFF; }

void fx_modfx_global_process_block(float *left, float *right, uint32_t frames)
{
    if ((left == NULL) || (right == NULL) || (frames == 0U)
        || (state.model == FX_MODFX_OFF)) return;
    switch (state.model) {
        case FX_MODFX_DAISY_STEREO: {
            float delay_increment[2];
            float amplitude_increment[2];
            float feedback_increment[2];
            const float inverse_frames = 1.0f / (float)frames;
            const float matrix_main_increment =
                (state.daisy_stereo_matrix_main_target
                 - state.daisy_stereo_matrix_main) * inverse_frames;
            for (uint8_t engine = 0U; engine < 2U; ++engine) {
                daisy_stereo_engine_t *const current = &state.daisy_stereo[engine];
                const daisy_stereo_parameters_t *const target =
                    &state.daisy_stereo_target[engine];
                const float target_amplitude = target->depth * target->delay;
                delay_increment[engine] = (target->delay - current->delay) * inverse_frames;
                amplitude_increment[engine] =
                    (target_amplitude - current->amplitude) * inverse_frames;
                feedback_increment[engine] =
                    (target->feedback - current->feedback) * inverse_frames;
                const float sign = (current->step < 0.0f) ? -1.0f : 1.0f;
                current->step = sign * 4.0f * target->rate_hz / 48000.0f;
            }
            for (uint32_t i = 0U; i < frames; ++i) {
                const float mono = (left[i] + right[i]) * 0.5f;
                const float wet_a = daisy_stereo_sample(
                    &state.daisy_stereo[0], history.daisy[0], mono,
                    delay_increment[0], amplitude_increment[0], feedback_increment[0]);
                const float wet_b = daisy_stereo_sample(
                    &state.daisy_stereo[1], history.daisy[1], mono,
                    delay_increment[1], amplitude_increment[1], feedback_increment[1]);
                state.daisy_stereo_matrix_main += matrix_main_increment;
                const float matrix_cross = 1.0f - state.daisy_stereo_matrix_main;
                left[i] = (state.daisy_stereo_matrix_main * wet_a
                           + matrix_cross * wet_b) * 0.5f;
                right[i] = (matrix_cross * wet_a
                            + state.daisy_stereo_matrix_main * wet_b) * 0.5f;
            }
            for (uint8_t engine = 0U; engine < 2U; ++engine) {
                state.daisy_stereo[engine].delay = state.daisy_stereo_target[engine].delay;
                state.daisy_stereo[engine].amplitude =
                    state.daisy_stereo_target[engine].depth
                    * state.daisy_stereo_target[engine].delay;
                state.daisy_stereo[engine].feedback =
                    state.daisy_stereo_target[engine].feedback;
            }
            state.daisy_stereo_matrix_main = state.daisy_stereo_matrix_main_target;
            break;
        }
        case FX_MODFX_JUNOLOGUE:
            for (uint32_t i = 0U; i < frames; ++i) {
                const stereo_float_t output = juno_sample(
                    (stereo_float_t){left[i], right[i]});
                left[i] = output.l;
                right[i] = output.r;
            }
            break;
        default:
            break;
    }
}
