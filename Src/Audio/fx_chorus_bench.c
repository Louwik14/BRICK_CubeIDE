#include "Audio/fx_chorus_bench.h"

#include <math.h>
#include <string.h>

#include "audio_float.h"
#include "memory_layout.h"

/*
 * Temporary comparison bench.  The Daisy model follows Electrosmith's
 * ChorusEngine triangle-LFO/fractional-delay algorithm, adapted to two
 * independent input channels (two engines total, not two stereo Choruses).
 * The Juno model is the single "I" Junologue path: two short modulated
 * reads are retained, while the plugin/platform wrappers and stmlib helpers
 * are deliberately not imported.
 */
#define CHORUS_BENCH_MAX_DELAY 2400U
#define CHORUS_BENCH_MICRO_DELAY 1024U
#define CHORUS_BENCH_JUNO_DELAY 512U

static float g_rate_hz = 48000.0f;
/* Prototype delay RAM is kept in SDRAM on both products. */
AUDIO_COLD_SDRAM static float g_micro[2][CHORUS_BENCH_MICRO_DELAY];
AUDIO_COLD_SDRAM static float g_daisy[2][CHORUS_BENCH_MAX_DELAY];
AUDIO_COLD_SDRAM static float g_juno[2][CHORUS_BENCH_JUNO_DELAY];
static uint32_t g_micro_write;
static uint32_t g_daisy_write[2];
static uint32_t g_juno_write[2];
static float g_micro_lfo;
static float g_daisy_lfo[2];
static float g_juno_lfo[2];
static float g_juno_lp[2];
static fx_chorus_bench_model_t g_model;
static float g_dry_l[AUDIO_BLOCK_SIZE];
static float g_dry_r[AUDIO_BLOCK_SIZE];

static float clampf(float x, float lo, float hi)
{
    return (x < lo) ? lo : ((x > hi) ? hi : x);
}

static float tri(float phase)
{
    phase -= floorf(phase);
    return (phase < 0.5f) ? (phase * 4.0f - 1.0f) : (3.0f - phase * 4.0f);
}

static float read_delay(const float *buffer, uint32_t size, uint32_t write, float delay)
{
    float pos = (float)write - clampf(delay, 1.0f, (float)size - 2.0f);
    while (pos < 0.0f) pos += (float)size;
    while (pos >= (float)size) pos -= (float)size;
    const uint32_t i0 = (uint32_t)pos;
    const uint32_t i1 = (i0 + 1U) % size;
    const float f = pos - (float)i0;
    return buffer[i0] + ((buffer[i1] - buffer[i0]) * f);
}

static void write_delay(float *buffer, uint32_t size, uint32_t *write, float value)
{
    buffer[*write] = clampf(value, -1.2f, 1.2f);
    *write = (*write + 1U) % size;
}

void fx_chorus_bench_init(float sample_rate)
{
    g_rate_hz = (sample_rate > 1000.0f) ? sample_rate : 48000.0f;
    memset(g_micro, 0, sizeof(g_micro));
    memset(g_daisy, 0, sizeof(g_daisy));
    memset(g_juno, 0, sizeof(g_juno));
    memset(g_juno_lp, 0, sizeof(g_juno_lp));
    g_micro_write = 0U;
    memset(g_daisy_write, 0, sizeof(g_daisy_write));
    memset(g_juno_write, 0, sizeof(g_juno_write));
    g_micro_lfo = 0.0f;
    memset(g_daisy_lfo, 0, sizeof(g_daisy_lfo));
    memset(g_juno_lfo, 0, sizeof(g_juno_lfo));
    g_model = FX_CHORUS_BENCH_NONE;
}

uint8_t fx_chorus_bench_is_model(uint8_t model)
{
    return (model >= (uint8_t)FX_CHORUS_BENCH_MICRO)
            && (model <= (uint8_t)FX_CHORUS_BENCH_JUNO);
}

static void process_micro(float *left, float *right, uint32_t frames, float depth, float rate)
{
    const float hz = 0.08f + (rate * 1.0f);
    const float base = 0.004f * g_rate_hz;
    const float span = (0.001f + depth * 0.004f) * g_rate_hz;
    for (uint32_t i = 0U; i < frames; ++i)
    {
        const float lfo = tri(g_micro_lfo);
        g_micro_lfo += hz / g_rate_hz;
        const float dl = base + (lfo * span);
        const float dr = base - (lfo * span);
        g_micro[0][g_micro_write] = clampf(left[i], -1.2f, 1.2f);
        g_micro[1][g_micro_write] = clampf(right[i], -1.2f, 1.2f);
        left[i] = read_delay(g_micro[0], CHORUS_BENCH_MICRO_DELAY, g_micro_write, dl);
        right[i] = read_delay(g_micro[1], CHORUS_BENCH_MICRO_DELAY, g_micro_write, dr);
        g_micro_write = (g_micro_write + 1U) % CHORUS_BENCH_MICRO_DELAY;
    }
}

static void process_daisy(float *left, float *right, uint32_t frames, float depth, float rate)
{
    const float hz = 0.05f + (rate * 0.75f);
    const float base = 0.004f * g_rate_hz;
    const float span = depth * base * 0.93f;
    for (uint32_t i = 0U; i < frames; ++i)
    {
        for (uint8_t ch = 0U; ch < 2U; ++ch)
        {
            float *sample = (ch == 0U) ? &left[i] : &right[i];
            const float lfo = tri(g_daisy_lfo[ch]);
            g_daisy_lfo[ch] += hz / g_rate_hz;
            write_delay(g_daisy[ch], CHORUS_BENCH_MAX_DELAY, &g_daisy_write[ch], *sample);
            const float delayed = read_delay(g_daisy[ch], CHORUS_BENCH_MAX_DELAY,
                                             g_daisy_write[ch], base + (lfo * span));
            *sample = (*sample + delayed) * 0.5f;
        }
    }
}

static void process_juno(float *left, float *right, uint32_t frames, float depth, float rate)
{
    const float hz = 0.513f + (rate * 0.35f);
    const float base = 0.00335f * g_rate_hz;
    const float span = (0.0005f + depth * 0.0018f) * g_rate_hz;
    const float lp = clampf(0.10f + (depth * 0.35f), 0.05f, 0.5f);
    for (uint32_t i = 0U; i < frames; ++i)
    {
        for (uint8_t ch = 0U; ch < 2U; ++ch)
        {
            float *sample = (ch == 0U) ? &left[i] : &right[i];
            const float lfo = tri(g_juno_lfo[ch]);
            g_juno_lfo[ch] += hz / g_rate_hz;
            write_delay(g_juno[ch], CHORUS_BENCH_JUNO_DELAY, &g_juno_write[ch], *sample);
            const float delayed_a = read_delay(g_juno[ch], CHORUS_BENCH_JUNO_DELAY,
                                               g_juno_write[ch], base + (lfo * span));
            const float delayed_b = read_delay(g_juno[ch], CHORUS_BENCH_JUNO_DELAY,
                                               g_juno_write[ch], base - (lfo * span));
            const float wet = (delayed_a + delayed_b) * 0.5f;
            g_juno_lp[ch] += lp * (wet - g_juno_lp[ch]);
            *sample = (*sample * 0.70710678f) + (g_juno_lp[ch] * 0.70710678f);
        }
    }
}

void fx_chorus_bench_process(float *left, float *right, uint32_t frames,
                             fx_chorus_bench_model_t model, float wet,
                             float depth, float rate)
{
    if ((left == NULL) || (right == NULL) || (frames == 0U) || !fx_chorus_bench_is_model((uint8_t)model))
        return;
    if (g_model != model)
    {
        fx_chorus_bench_init(g_rate_hz);
        g_model = model;
    }
    if (frames > AUDIO_BLOCK_SIZE) frames = AUDIO_BLOCK_SIZE;
    memcpy(g_dry_l, left, frames * sizeof(float));
    memcpy(g_dry_r, right, frames * sizeof(float));
    depth = clampf(depth, 0.0f, 1.0f);
    rate = clampf(rate, 0.0f, 1.0f);
    wet = clampf(wet, 0.0f, 1.0f);
    if (model == FX_CHORUS_BENCH_MICRO) process_micro(left, right, frames, depth, rate);
    else if (model == FX_CHORUS_BENCH_DAISY) process_daisy(left, right, frames, depth, rate);
    else process_juno(left, right, frames, depth, rate);
    for (uint32_t i = 0U; i < frames; ++i)
    {
        left[i] = g_dry_l[i] + ((left[i] - g_dry_l[i]) * wet);
        right[i] = g_dry_r[i] + ((right[i] - g_dry_r[i]) * wet);
    }
}
