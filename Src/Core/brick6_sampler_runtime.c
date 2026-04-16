/**
 * @file brick6_sampler_runtime.c
 * @brief Minimal mono sampler backend with slice grid v1.
 */

#include "Core/brick6_sampler_runtime.h"

#include <string.h>

#include "Sampler/sample_pool.h"

typedef struct
{
    uint16_t sample_id;
    uint32_t position;
    uint8_t active;
    uint8_t note;
    uint8_t mode;
    uint8_t slice_count;
    uint8_t slice_index;
    float gain;
    float start;
    float end;
    float tune;
    float fade_in;
    float fade_out;
    uint32_t slice_begin[64U];
    uint32_t slice_end[64U];
} brick6_sampler_voice_t;

static brick6_sampler_voice_t g_sampler_voice[SEQ_TRACK_COUNT];

static const uint8_t g_sampler_slice_counts[] = {2U, 4U, 8U, 16U, 32U, 64U};

static void brick6_sampler_runtime_rebuild_grid(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    const sample_desc_t *const desc = sample_pool_get(voice->sample_id);
    if ((desc == NULL) || (desc->valid == 0U) || (desc->data == NULL) || (desc->length_frames == 0U))
    {
        voice->slice_count = 0U;
        return;
    }

    const uint32_t length_frames = desc->length_frames;
    const uint32_t slice_count = (voice->slice_count == 0U) ? 2U : voice->slice_count;
    for (uint32_t i = 0U; i < slice_count; ++i)
    {
        const uint32_t begin = (length_frames * i) / slice_count;
        const uint32_t end = (i + 1U >= slice_count) ? length_frames : ((length_frames * (i + 1U)) / slice_count);
        voice->slice_begin[i] = begin;
        voice->slice_end[i] = (end > begin) ? end : (begin + 1U);
    }
}

static uint8_t brick6_sampler_runtime_pick_slice_index(const brick6_sampler_voice_t *voice, uint8_t note)
{
    if ((voice == NULL) || (voice->slice_count == 0U))
    {
        return 0U;
    }
    return (uint8_t)(note % voice->slice_count);
}

void brick6_sampler_runtime_init(void)
{
    memset(g_sampler_voice, 0, sizeof(g_sampler_voice));
    for (uint8_t i = 0U; i < SEQ_TRACK_COUNT; ++i)
    {
        g_sampler_voice[i].gain = 1.0f;
        g_sampler_voice[i].start = 0.0f;
        g_sampler_voice[i].end = 1.0f;
        g_sampler_voice[i].slice_count = 2U;
    }
}

void brick6_sampler_runtime_reset_track(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    memset(&g_sampler_voice[track_id], 0, sizeof(g_sampler_voice[track_id]));
    g_sampler_voice[track_id].gain = 1.0f;
    g_sampler_voice[track_id].end = 1.0f;
    g_sampler_voice[track_id].slice_count = 2U;
}

void brick6_sampler_runtime_set_sample(uint8_t track_id, uint16_t sample_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_sampler_voice[track_id].sample_id = sample_id;
    g_sampler_voice[track_id].position = 0U;
    brick6_sampler_runtime_rebuild_grid(track_id);
}

void brick6_sampler_runtime_set_gain(uint8_t track_id, float gain)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    g_sampler_voice[track_id].gain = gain;
}

void brick6_sampler_runtime_set_start(uint8_t track_id, float start)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    g_sampler_voice[track_id].start = start;
}

void brick6_sampler_runtime_set_end(uint8_t track_id, float end)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    g_sampler_voice[track_id].end = end;
}

void brick6_sampler_runtime_set_mode(uint8_t track_id, uint8_t mode)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    g_sampler_voice[track_id].mode = (mode > 5U) ? 0U : mode;
}

void brick6_sampler_runtime_set_tune(uint8_t track_id, float tune)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    g_sampler_voice[track_id].tune = tune;
}

void brick6_sampler_runtime_set_fade_in(uint8_t track_id, float fade_in)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    g_sampler_voice[track_id].fade_in = fade_in;
}

void brick6_sampler_runtime_set_fade_out(uint8_t track_id, float fade_out)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    g_sampler_voice[track_id].fade_out = fade_out;
}

void brick6_sampler_runtime_set_slice_count(uint8_t track_id, uint8_t slice_count)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    uint8_t resolved = 2U;
    for (uint8_t i = 0U; i < (uint8_t)(sizeof(g_sampler_slice_counts) / sizeof(g_sampler_slice_counts[0])); ++i)
    {
        if (g_sampler_slice_counts[i] == slice_count)
        {
            resolved = slice_count;
            break;
        }
    }

    g_sampler_voice[track_id].slice_count = resolved;
    brick6_sampler_runtime_rebuild_grid(track_id);
}

void brick6_sampler_runtime_trigger(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_sampler_voice[track_id].position = 0U;
    g_sampler_voice[track_id].active = 1U;
}

void brick6_sampler_runtime_trigger_note(uint8_t track_id, uint8_t note)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_sampler_voice[track_id].note = note;
    brick6_sampler_runtime_trigger(track_id);
}

void brick6_sampler_runtime_stop(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_sampler_voice[track_id].active = 0U;
    g_sampler_voice[track_id].position = 0U;
}

static void brick6_sampler_render_sample(const sample_desc_t *desc,
                                         brick6_sampler_voice_t *voice,
                                         float *out_mono,
                                         uint32_t frames)
{
    if ((desc == NULL) || (voice == NULL) || (out_mono == NULL) || (frames == 0U))
    {
        return;
    }

    if ((desc->valid == 0U) || (desc->data == NULL) || (desc->length_frames == 0U))
    {
        voice->active = 0U;
        voice->position = 0U;
        return;
    }

    const uint32_t length_frames = desc->length_frames;
    const float *const data = desc->data;
    uint32_t start_frame = (uint32_t)(voice->start * (float)length_frames);
    uint32_t end_frame = (uint32_t)(voice->end * (float)length_frames);
    if (start_frame >= length_frames)
    {
        start_frame = length_frames - 1U;
    }
    if (end_frame == 0U || end_frame > length_frames)
    {
        end_frame = length_frames;
    }
    if (start_frame >= end_frame)
    {
        start_frame = 0U;
        end_frame = length_frames;
    }

    uint32_t loop_begin = start_frame;
    uint32_t loop_end = end_frame;
    if (voice->mode == 4U || voice->mode == 5U)
    {
        const uint8_t slice_count = (voice->slice_count == 0U) ? 2U : voice->slice_count;
        const uint8_t slice_index = brick6_sampler_runtime_pick_slice_index(voice, voice->note);
        const uint8_t resolved_index = (slice_count == 0U) ? 0U : (uint8_t)(slice_index % slice_count);
        loop_begin = voice->slice_begin[resolved_index];
        loop_end = voice->slice_end[resolved_index];
        if (loop_begin >= loop_end)
        {
            loop_begin = 0U;
            loop_end = length_frames;
        }
    }

    for (uint32_t i = 0U; i < frames; ++i)
    {
        if (voice->position < loop_begin)
        {
            voice->position = loop_begin;
        }

        if (voice->position >= loop_end)
        {
            voice->active = 0U;
            voice->position = 0U;
            break;
        }

        const uint32_t idx = voice->position * 2U;
        const float l = data[idx];
        const float r = data[idx + 1U];
        out_mono[i] += ((l + r) * 0.5f) * voice->gain;
        voice->position++;
    }
}

void brick6_sampler_runtime_render_track(const track_runtime_ctx_t *ctx,
                                         float *out_mono,
                                         uint32_t frames)
{
    if ((ctx == NULL) || (out_mono == NULL) || (frames == 0U))
    {
        return;
    }

    if ((ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER))
    {
        return;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[ctx->track_id];
    if ((voice->active == 0U) || (voice->sample_id >= SAMPLE_POOL_SIZE))
    {
        return;
    }

    const sample_desc_t *const desc = sample_pool_get(voice->sample_id);
    if ((desc == NULL) || (sample_pool_is_loaded(voice->sample_id) == 0U))
    {
        voice->active = 0U;
        voice->position = 0U;
        return;
    }

    if (voice->slice_count == 0U)
    {
        brick6_sampler_runtime_rebuild_grid(ctx->track_id);
    }

    brick6_sampler_render_sample(desc, voice, out_mono, frames);
}
