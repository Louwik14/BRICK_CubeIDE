#include "Audio/audio_chain_diag.h"

#include <limits.h>
#include "stm32h7xx_hal.h"

volatile audio_chain_diag_t g_audio_chain_diag
    __attribute__((used, externally_visible));

void __attribute__((used, noinline, externally_visible)) audio_chain_diag_reset(void)
{
    volatile uint32_t *p = (volatile uint32_t *)&g_audio_chain_diag;
    for (uint32_t i = 0; i < sizeof(g_audio_chain_diag) / 4U; ++i) p[i] = 0U;
    g_audio_chain_diag.magic = 0x41434431U;
    g_audio_chain_diag.version = 1U;
}

static uint32_t magnitude(int32_t x)
{
    return (x < 0) ? (uint32_t)(-(int64_t)x) : (uint32_t)x;
}

static int32_t float_q24(float x)
{
    if (!(x >= -1.0f)) return (x < 0.0f) ? -8388608 : 0;
    if (x >= 1.0f) return 8388607;
    return (int32_t)(x * 8388608.0f);
}

static int32_t signed24(int32_t x)
{
    return (int32_t)((uint32_t)x << 8) >> 8;
}

static void observe(uint32_t stage, int32_t l, int32_t r, uint32_t index)
{
    volatile audio_chain_diag_stage_t *s = &g_audio_chain_diag.stage[stage];
    const uint32_t stream_index = s->frames_seen;
    const uint32_t slot = stream_index % AUDIO_CHAIN_DIAG_PERIOD;
    const uint32_t threshold = (stage <= AUDIO_CHAIN_ENGINE_INPUT) ? 128U : 4096U;
    uint32_t error = 0U;

    if ((s->armed == 0U) && ((magnitude(l) > 335544U)
            || (magnitude(r) > 335544U))) {
        s->armed = 1U;
        s->armed_frame = stream_index;
    }
    if ((s->armed != 0U) && ((stream_index - s->armed_frame) >= 4800U)) {
        const uint32_t el = magnitude(l - s->history[slot][0]);
        const uint32_t er = magnitude(r - s->history[slot][1]);
        error = (el > er) ? el : er;
        s->samples_checked++;
        if (error > s->max_error) s->max_error = error;
        if (error > threshold) {
            s->glitch_count++;
            if (s->glitch_count == 1U) {
                s->first_glitch_tick = HAL_GetTick();
                s->first_glitch_block = s->blocks;
                s->first_glitch_sample_index = index;
                s->first_glitch_stream_index = stream_index;
                for (uint32_t k = 0U; k < 4U; ++k) {
                    const uint32_t prev = (slot + AUDIO_CHAIN_DIAG_PERIOD - 4U + k)
                                        % AUDIO_CHAIN_DIAG_PERIOD;
                    s->snapshot[k][0] = s->history[prev][0];
                    s->snapshot[k][1] = s->history[prev][1];
                }
                s->snapshot[4][0] = l;
                s->snapshot[4][1] = r;
                s->snapshot_count = 5U;
            }
        }
    }
    if ((s->glitch_count != 0U) && (s->snapshot_count < AUDIO_CHAIN_DIAG_SNAPSHOT)) {
        const uint32_t next = s->first_glitch_stream_index + s->snapshot_count - 4U;
        if (stream_index == next) {
            s->snapshot[s->snapshot_count][0] = l;
            s->snapshot[s->snapshot_count][1] = r;
            s->snapshot_count++;
        }
    }
    s->history[slot][0] = l;
    s->history[slot][1] = r;
    s->frames_seen = stream_index + 1U;
}

void audio_chain_diag_i32(uint32_t stage, const int32_t *interleaved,
                          uint32_t frames, uint32_t stride, uint32_t shift)
{
    if ((stage >= AUDIO_CHAIN_STAGE_COUNT) || (interleaved == 0)) return;
    g_audio_chain_diag.stage[stage].blocks++;
    for (uint32_t n = 0U; n < frames; ++n) {
        int32_t l = interleaved[n * stride];
        int32_t r = interleaved[n * stride + 1U];
        if (shift == 8U) { l >>= 8; r >>= 8; }
        else { l = signed24(l); r = signed24(r); }
        observe(stage, l, r, n);
    }
}

void audio_chain_diag_float(uint32_t stage, const float *left,
                            const float *right, uint32_t frames)
{
    if ((stage >= AUDIO_CHAIN_STAGE_COUNT) || (left == 0)) return;
    g_audio_chain_diag.stage[stage].blocks++;
    for (uint32_t n = 0U; n < frames; ++n)
        observe(stage, float_q24(left[n]), float_q24(right[n]), n);
}

void audio_chain_diag_compare_pcm_float(const int32_t *pcm,
                                        const float *interleaved, uint32_t frames)
{
    for (uint32_t n = 0U; n < frames * 2U; ++n) {
        const int32_t difference = (pcm[n] >> 8) - float_q24(interleaved[n]);
        if (magnitude(difference) > 2U) g_audio_chain_diag.pcm_float_mismatch_count++;
    }
}

void audio_chain_diag_float_interleaved(uint32_t stage, const float *samples,
                                        uint32_t frames)
{
    if ((stage >= AUDIO_CHAIN_STAGE_COUNT) || (samples == 0)) return;
    g_audio_chain_diag.stage[stage].blocks++;
    for (uint32_t n = 0U; n < frames; ++n)
        observe(stage, float_q24(samples[2U * n]),
                float_q24(samples[2U * n + 1U]), n);
}

void audio_chain_diag_compare_float_int24(const float *left, const float *right,
                                           const int32_t *tx, uint32_t frames,
                                           uint32_t stride)
{
    for (uint32_t n = 0U; n < frames; ++n) {
        if (magnitude(float_q24(left[n]) - signed24(tx[n * stride])) > 3U)
            g_audio_chain_diag.float_int24_mismatch_count++;
        if (magnitude(float_q24(right[n]) - signed24(tx[n * stride + 1U])) > 3U)
            g_audio_chain_diag.float_int24_mismatch_count++;
    }
}
