/**
 * @file brick6_sampler_runtime.c
 * @brief Minimal mono sampler backend with slice grid v1.
 */

#include "Core/brick6_sampler_runtime.h"

#include <math.h>
#include <string.h>

#include "Sampler/sample_cache.h"
#include "Sampler/sample_pool.h"

#define BRICK6_SAMPLER_CACHE_VOICE_BASE (2U)

typedef struct
{
    uint16_t sample_id;
    const sample_desc_t *sample;
    float position;
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
    uint32_t region_begin;
    uint32_t region_end;
    uint32_t loop_frames;
    uint32_t fade_in_frames;
    uint32_t fade_out_frames;
    float step_signed;
    uint8_t reverse;
    uint8_t loop_mode;
    uint8_t use_slice;
    uint32_t slice_begin[64U];
    uint32_t slice_end[64U];
} brick6_sampler_voice_t;

static brick6_sampler_voice_t g_sampler_voice[SEQ_TRACK_COUNT];

static const uint8_t g_sampler_slice_counts[] = {2U, 4U, 8U, 16U, 32U, 64U};

static uint8_t brick6_sampler_runtime_mode_is_reverse(uint8_t mode)
{
    return ((mode == 1U) || (mode == 3U) || (mode == 5U)) ? 1U : 0U;
}

static uint8_t brick6_sampler_runtime_mode_uses_loop(uint8_t mode)
{
    return ((mode == 2U) || (mode == 3U)) ? 1U : 0U;
}

static uint8_t brick6_sampler_runtime_mode_uses_slice(uint8_t mode)
{
    return ((mode == 4U) || (mode == 5U)) ? 1U : 0U;
}

static uint8_t brick6_sampler_runtime_pick_slice_index(const brick6_sampler_voice_t *voice, uint8_t note);

static uint8_t brick6_sampler_runtime_cache_voice_id(uint8_t track_id)
{
    return (uint8_t)(BRICK6_SAMPLER_CACHE_VOICE_BASE + track_id);
}

static uint8_t brick6_sampler_runtime_supports_cache_forward_simple(const brick6_sampler_voice_t *voice)
{
    if (voice == NULL)
    {
        return 0U;
    }

    return ((voice->mode == 0U) && (voice->note == 60U) && (voice->tune == 0.0f)) ? 1U : 0U;
}

static uint32_t brick6_sampler_runtime_clamp_region_begin(uint32_t length_frames, float start)
{
    uint32_t begin = (uint32_t)(start * (float)length_frames);
    if (begin >= length_frames)
    {
        begin = (length_frames > 0U) ? (length_frames - 1U) : 0U;
    }
    return begin;
}

static uint32_t brick6_sampler_runtime_clamp_region_end(uint32_t length_frames, float end)
{
    uint32_t resolved_end = (uint32_t)(end * (float)length_frames);
    if ((resolved_end == 0U) || (resolved_end > length_frames))
    {
        resolved_end = length_frames;
    }
    return resolved_end;
}

static void brick6_sampler_runtime_compute_fade_frames(brick6_sampler_voice_t *voice)
{
    if ((voice == NULL) || (voice->loop_frames == 0U))
    {
        if (voice != NULL)
        {
            voice->fade_in_frames = 0U;
            voice->fade_out_frames = 0U;
        }
        return;
    }

    uint32_t fade_in_frames = (uint32_t)(voice->fade_in * (float)voice->loop_frames + 0.5f);
    uint32_t fade_out_frames = (uint32_t)(voice->fade_out * (float)voice->loop_frames + 0.5f);
    if (fade_in_frames > voice->loop_frames)
    {
        fade_in_frames = voice->loop_frames;
    }
    if (fade_out_frames > voice->loop_frames)
    {
        fade_out_frames = voice->loop_frames;
    }

    voice->fade_in_frames = fade_in_frames;
    voice->fade_out_frames = fade_out_frames;
}

static void brick6_sampler_runtime_build_render_plan(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    const sample_desc_t *const desc = sample_pool_get(voice->sample_id);
    if ((desc == NULL) || (desc->valid == 0U) || (desc->length_frames == 0U)
        || (sample_cache_is_ready(voice->sample_id) == 0U)
        || (brick6_sampler_runtime_supports_cache_forward_simple(voice) == 0U))
    {
        voice->sample = NULL;
        voice->active = 0U;
        voice->position = 0.0f;
        voice->loop_frames = 0U;
        voice->fade_in_frames = 0U;
        voice->fade_out_frames = 0U;
        return;
    }

    const uint32_t length_frames = desc->length_frames;
    uint32_t begin = brick6_sampler_runtime_clamp_region_begin(length_frames, voice->start);
    uint32_t end = brick6_sampler_runtime_clamp_region_end(length_frames, voice->end);
    uint8_t use_slice = brick6_sampler_runtime_mode_uses_slice(voice->mode);

    if (use_slice != 0U)
    {
        const uint8_t slice_count = (voice->slice_count == 0U) ? 2U : voice->slice_count;
        const uint8_t slice_index = brick6_sampler_runtime_pick_slice_index(voice, voice->note);
        const uint8_t resolved_index = (slice_count == 0U) ? 0U : (uint8_t)(slice_index % slice_count);
        uint32_t slice_begin = voice->slice_begin[resolved_index];
        uint32_t slice_end = voice->slice_end[resolved_index];

        if ((slice_end <= slice_begin) || (slice_begin >= length_frames))
        {
            slice_begin = 0U;
            slice_end = length_frames;
        }

        begin = slice_begin;
        end = (slice_end > begin) ? slice_end : (begin + 1U);
        if (end > length_frames)
        {
            end = length_frames;
        }
    }

    if (begin >= end)
    {
        begin = 0U;
        end = length_frames;
    }

    voice->sample = desc;
    voice->region_begin = begin;
    voice->region_end = end;
    voice->reverse = brick6_sampler_runtime_mode_is_reverse(voice->mode);
    voice->loop_mode = brick6_sampler_runtime_mode_uses_loop(voice->mode);
    voice->use_slice = use_slice;
    voice->loop_frames = (end > begin) ? (end - begin) : 0U;
    brick6_sampler_runtime_compute_fade_frames(voice);
    voice->step_signed = 1.0f;
}

static void brick6_sampler_runtime_rebuild_grid(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    const sample_desc_t *const desc = sample_pool_get(voice->sample_id);
    if ((desc == NULL) || (desc->valid == 0U) || (desc->length_frames == 0U))
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

static float brick6_sampler_runtime_initial_position(uint32_t begin,
                                                     uint32_t end,
                                                     uint8_t reverse)
{
    if (reverse != 0U)
    {
        return (end > 0U) ? (float)(end - 1U) : 0.0f;
    }

    return (float)begin;
}

static uint8_t brick6_sampler_runtime_pick_slice_index(const brick6_sampler_voice_t *voice, uint8_t note)
{
    if ((voice == NULL) || (voice->slice_count == 0U))
    {
        return 0U;
    }
    return (uint8_t)(note % voice->slice_count);
}

static float brick6_sampler_runtime_fade_gain(uint32_t frame_index,
                                              uint32_t loop_frames,
                                              uint32_t fade_in_frames,
                                              uint32_t fade_out_frames)
{
    if (loop_frames == 0U)
    {
        return 1.0f;
    }

    float gain = 1.0f;
    if ((fade_in_frames > 0U) && (frame_index < fade_in_frames))
    {
        gain *= (float)frame_index / (float)fade_in_frames;
    }

    if ((fade_out_frames > 0U) && (frame_index >= (loop_frames - fade_out_frames)))
    {
        const uint32_t fade_pos = frame_index - (loop_frames - fade_out_frames);
        gain *= 1.0f - ((float)fade_pos / (float)fade_out_frames);
    }

    return (gain < 0.0f) ? 0.0f : gain;
}

static uint8_t brick6_sampler_runtime_cache_interp(uint8_t voice_id,
                                                   uint32_t length_frames,
                                                   float position,
                                                   float *out_sample)
{
    if ((out_sample == NULL) || (length_frames == 0U))
    {
        return 0U;
    }

    if (position < 0.0f)
    {
        position = 0.0f;
    }

    const float max_pos = (length_frames > 1U) ? (float)(length_frames - 1U) : 0.0f;
    if (position > max_pos)
    {
        position = max_pos;
    }

    const uint32_t idx = (uint32_t)position;
    float left = 0.0f;
    float right = 0.0f;
    if (sample_cache_read_voice_frame(voice_id, idx, &left, &right) == 0U)
    {
        return 0U;
    }

    *out_sample = (left + right) * 0.5f;
    return 1U;
}

void brick6_sampler_runtime_init(void)
{
    memset(g_sampler_voice, 0, sizeof(g_sampler_voice));
    for (uint8_t i = 0U; i < SEQ_TRACK_COUNT; ++i)
    {
        g_sampler_voice[i].note = 60U;
        g_sampler_voice[i].gain = 1.0f;
        g_sampler_voice[i].start = 0.0f;
        g_sampler_voice[i].end = 1.0f;
        g_sampler_voice[i].slice_count = 2U;
        g_sampler_voice[i].loop_mode = 0U;
        g_sampler_voice[i].reverse = 0U;
    }
}

void brick6_sampler_runtime_reset_track(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    memset(&g_sampler_voice[track_id], 0, sizeof(g_sampler_voice[track_id]));
    sample_cache_stop_voice(brick6_sampler_runtime_cache_voice_id(track_id));
    g_sampler_voice[track_id].note = 60U;
    g_sampler_voice[track_id].gain = 1.0f;
    g_sampler_voice[track_id].end = 1.0f;
    g_sampler_voice[track_id].slice_count = 2U;
    g_sampler_voice[track_id].sample = NULL;
}

void brick6_sampler_runtime_set_sample(uint8_t track_id, uint16_t sample_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_sampler_voice[track_id].sample_id = sample_id;
    g_sampler_voice[track_id].note = 60U;
    g_sampler_voice[track_id].position = 0.0f;
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

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    brick6_sampler_runtime_build_render_plan(track_id);
    if (voice->sample != NULL)
    {
        voice->position = brick6_sampler_runtime_initial_position(voice->region_begin,
                                                                  voice->region_end,
                                                                  voice->reverse);
        if (sample_cache_start_voice(voice->sample_id, brick6_sampler_runtime_cache_voice_id(track_id)) != 0U)
        {
            g_sampler_voice[track_id].active = 1U;
        }
        else
        {
            voice->position = 0.0f;
            g_sampler_voice[track_id].active = 0U;
        }
    }
    else
    {
        voice->position = 0.0f;
        g_sampler_voice[track_id].active = 0U;
    }
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
    g_sampler_voice[track_id].position = 0.0f;
    sample_cache_stop_voice(brick6_sampler_runtime_cache_voice_id(track_id));
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

    if ((desc->valid == 0U) || (desc->length_frames == 0U))
    {
        voice->active = 0U;
        voice->position = 0.0f;
        return;
    }

    if (voice->sample != desc)
    {
        voice->sample = desc;
    }

    const uint32_t length_frames = desc->length_frames;
    const uint8_t cache_voice_id =
        brick6_sampler_runtime_cache_voice_id((uint8_t)(voice - g_sampler_voice));
    const uint32_t loop_begin = voice->region_begin;
    const uint32_t loop_end = voice->region_end;
    const uint32_t loop_frames = voice->loop_frames;
    float position = voice->position;

    for (uint32_t i = 0U; i < frames; ++i)
    {
        if (position >= (float)loop_end)
        {
            voice->active = 0U;
            voice->position = 0.0f;
            sample_cache_stop_voice(cache_voice_id);
            break;
        }

        float sample = 0.0f;
        if (brick6_sampler_runtime_cache_interp(cache_voice_id, length_frames, position, &sample) == 0U)
        {
            voice->active = 0U;
            voice->position = 0.0f;
            sample_cache_stop_voice(cache_voice_id);
            break;
        }
        const uint32_t frame_index = (uint32_t)(position - (float)loop_begin);
        const float fade_gain = brick6_sampler_runtime_fade_gain(frame_index,
                                                                loop_frames,
                                                                voice->fade_in_frames,
                                                                voice->fade_out_frames);
        out_mono[i] += sample * voice->gain * fade_gain;
        position += 1.0f;
    }

    voice->position = position;
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

    const sample_desc_t *const desc = voice->sample;
    if ((desc == NULL) || (desc->valid == 0U) || (desc->length_frames == 0U))
    {
        voice->active = 0U;
        voice->position = 0U;
        return;
    }

    brick6_sampler_render_sample(desc, voice, out_mono, frames);
}
