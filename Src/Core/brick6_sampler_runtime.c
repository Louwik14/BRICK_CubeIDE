/**
 * @file brick6_sampler_runtime.c
 * @brief Sampler backend with slice grid v1.
 */

#include "Core/brick6_sampler_runtime.h"

#include <math.h>
#include <string.h>

#include "Audio/audio_float.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_pool.h"
#include "Sampler/sample_voice_reader.h"

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
    uint8_t use_segment_cursor;
    sample_play_plan_t play_plan;
    sample_voice_reader_t reader;
    uint32_t slice_begin[64U];
    uint32_t slice_end[64U];
} brick6_sampler_voice_t;

enum
{
    BRICK6_SAMPLER_LOOP_NONE = 0,
    BRICK6_SAMPLER_LOOP_FORWARD = 1,
    BRICK6_SAMPLER_LOOP_PINGPONG = 2
};

static brick6_sampler_voice_t g_sampler_voice[SEQ_TRACK_COUNT];

#if BRICK6_SAMPLER_DIAG_ENABLE
static brick6_sampler_runtime_diag_snapshot_t g_brick6_sampler_runtime_diag;
#define BRICK6_SAMPLER_RUNTIME_DIAG_INC(field) (++g_brick6_sampler_runtime_diag.field)
#else
#define BRICK6_SAMPLER_RUNTIME_DIAG_INC(field) ((void)0)
#endif

volatile sample_cache_diag_snapshot_t g_sample_cache_diag_snapshot;
volatile sample_voice_reader_diag_snapshot_t g_sample_voice_reader_diag_snapshot;
volatile brick6_sampler_runtime_diag_snapshot_t g_brick6_sampler_runtime_diag_snapshot;

static const uint8_t g_sampler_slice_counts[] = {2U, 4U, 8U, 16U, 32U, 64U};

#define BRICK6_SAMPLER_STEP_EPSILON (0.0001f)

static uint32_t brick6_sampler_runtime_diag_count_active_voices(void)
{
    uint32_t active = 0U;
    for (uint32_t i = 0U; i < SEQ_TRACK_COUNT; ++i)
    {
        if (g_sampler_voice[i].active != 0U)
        {
            active++;
        }
    }

    return active;
}

static uint8_t brick6_sampler_runtime_mode_is_reverse(uint8_t mode)
{
    return ((mode == 1U) || (mode == 5U)) ? 1U : 0U;
}

static uint8_t brick6_sampler_runtime_mode_loop_kind(uint8_t mode)
{
    if (mode == 2U)
    {
        return BRICK6_SAMPLER_LOOP_FORWARD;
    }

    if (mode == 3U)
    {
        return BRICK6_SAMPLER_LOOP_PINGPONG;
    }

    return BRICK6_SAMPLER_LOOP_NONE;
}

static uint8_t brick6_sampler_runtime_mode_uses_slice(uint8_t mode)
{
    return ((mode == 4U) || (mode == 5U)) ? 1U : 0U;
}

static uint8_t brick6_sampler_runtime_pick_slice_index(const brick6_sampler_voice_t *voice, uint8_t note);
void brick6_sampler_runtime_diag_reset(void);
void brick6_sampler_runtime_diag_get_snapshot(brick6_sampler_runtime_diag_snapshot_t *out_snapshot);

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

    return ((voice->mode == 0U) || (voice->mode == 1U) || (voice->mode == 2U)
            || (voice->mode == 3U) || (voice->mode == 4U) || (voice->mode == 5U))
               ? 1U
               : 0U;
}

static uint8_t brick6_sampler_runtime_use_segment_cursor_path(const brick6_sampler_voice_t *voice)
{
    if (voice == NULL)
    {
        return 0U;
    }

    return ((fabsf(voice->step_signed - 1.0f) <= BRICK6_SAMPLER_STEP_EPSILON)
            && (((voice->mode == 0U) && (voice->reverse == 0U)
                 && (voice->loop_mode == BRICK6_SAMPLER_LOOP_NONE))
                || ((voice->mode == 1U) && (voice->reverse != 0U)
                    && (voice->loop_mode == BRICK6_SAMPLER_LOOP_NONE))
                || ((voice->mode == 2U) && (voice->reverse == 0U)
                    && (voice->loop_mode == BRICK6_SAMPLER_LOOP_FORWARD))
                || ((voice->mode == 3U) && (voice->reverse == 0U)
                    && (voice->loop_mode == BRICK6_SAMPLER_LOOP_PINGPONG)
                    && (voice->loop_frames >= 2U))))
               ? 1U
               : 0U;
}

static float brick6_sampler_runtime_compute_step(const brick6_sampler_voice_t *voice)
{
    if (voice == NULL)
    {
        return 1.0f;
    }

    const float semitones = ((float)((int32_t)voice->note - 60)) + voice->tune;
    return powf(2.0f, semitones * (1.0f / 12.0f));
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
        voice->use_segment_cursor = 0U;
        memset(&voice->play_plan, 0, sizeof(voice->play_plan));
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
    voice->loop_mode = brick6_sampler_runtime_mode_loop_kind(voice->mode);
    voice->use_slice = use_slice;
    voice->loop_frames = (end > begin) ? (end - begin) : 0U;
    brick6_sampler_runtime_compute_fade_frames(voice);
    voice->step_signed = brick6_sampler_runtime_compute_step(voice);
    memset(&voice->play_plan, 0, sizeof(voice->play_plan));
    voice->play_plan.sample_id = voice->sample_id;
    voice->play_plan.start_frame = (voice->reverse != 0U) ? ((end > begin) ? (end - 1U) : begin) : begin;
    voice->play_plan.region_begin = begin;
    voice->play_plan.region_end = end;
    voice->play_plan.loop_begin = begin;
    voice->play_plan.loop_end = end;
    voice->play_plan.fade_in_frames = voice->fade_in_frames;
    voice->play_plan.fade_out_frames = voice->fade_out_frames;
    voice->play_plan.step_q16 = 65536U;
    voice->play_plan.direction = voice->reverse;
    voice->play_plan.loop_mode = voice->loop_mode;
    voice->play_plan.stop_on_underrun = 1U;
    voice->play_plan.kernel_type = (voice->reverse != 0U) ? SAMPLE_KERNEL_REV_1X : SAMPLE_KERNEL_FWD_1X;
    voice->use_segment_cursor = brick6_sampler_runtime_use_segment_cursor_path(voice);
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

static uint8_t brick6_sampler_runtime_is_terminal_position(const brick6_sampler_voice_t *voice, float position)
{
    if (voice == NULL)
    {
        return 1U;
    }

    if (voice->loop_mode != BRICK6_SAMPLER_LOOP_NONE)
    {
        return 0U;
    }

    if (voice->reverse != 0U)
    {
        return (position < (float)voice->region_begin) ? 1U : 0U;
    }

    return (position >= (float)voice->region_end) ? 1U : 0U;
}

void brick6_sampler_runtime_init(void)
{
    brick6_sampler_runtime_diag_reset();
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
        sample_voice_reader_reset(&g_sampler_voice[i].reader);
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
    sample_voice_reader_reset(&g_sampler_voice[track_id].reader);
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
    sample_cache_stop_voice(brick6_sampler_runtime_cache_voice_id(track_id));
    brick6_sampler_runtime_build_render_plan(track_id);
    if (voice->sample != NULL)
    {
        voice->position = brick6_sampler_runtime_initial_position(voice->region_begin,
                                                                  voice->region_end,
                                                                  voice->reverse);
        const uint32_t start_frame = (uint32_t)voice->position;
        if (sample_cache_start_voice_at(voice->sample_id,
                                        brick6_sampler_runtime_cache_voice_id(track_id),
                                        start_frame) != 0U)
        {
            uint8_t bind_ok = 0U;
            if (voice->use_segment_cursor != 0U)
            {
                voice->play_plan.start_frame = start_frame;
                bind_ok = sample_voice_reader_bind_play_plan(&voice->reader,
                                                             &voice->play_plan,
                                                             brick6_sampler_runtime_cache_voice_id(track_id));
            }
            else
            {
                sample_voice_reader_bind(&voice->reader,
                                         voice->sample_id,
                                         brick6_sampler_runtime_cache_voice_id(track_id),
                                         start_frame);
                sample_voice_reader_set_step(&voice->reader, voice->step_signed);
                bind_ok = 1U;
            }

            g_sampler_voice[track_id].active = bind_ok;
            if (bind_ok == 0U)
            {
                sample_cache_stop_voice(brick6_sampler_runtime_cache_voice_id(track_id));
            }
        }
        else
        {
            voice->position = 0.0f;
            sample_voice_reader_reset(&voice->reader);
            g_sampler_voice[track_id].active = 0U;
        }
    }
    else
    {
        voice->position = 0.0f;
        sample_voice_reader_reset(&voice->reader);
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
    sample_voice_reader_stop(&g_sampler_voice[track_id].reader);
}

void brick6_sampler_runtime_diag_reset(void)
{
#if BRICK6_SAMPLER_DIAG_ENABLE
    memset(&g_brick6_sampler_runtime_diag, 0, sizeof(g_brick6_sampler_runtime_diag));
#endif
}

void brick6_sampler_runtime_diag_get_snapshot(brick6_sampler_runtime_diag_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL)
    {
        return;
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));
#if BRICK6_SAMPLER_DIAG_ENABLE
    *out_snapshot = g_brick6_sampler_runtime_diag;
    out_snapshot->active_voices = brick6_sampler_runtime_diag_count_active_voices();
    if (out_snapshot->active_voices > out_snapshot->max_active_voices)
    {
        out_snapshot->max_active_voices = out_snapshot->active_voices;
    }
#endif
}

void sampler_perf_diag_capture(void)
{
    sample_cache_diag_snapshot_t cache_snapshot;
    sample_voice_reader_diag_snapshot_t reader_snapshot;
    brick6_sampler_runtime_diag_snapshot_t runtime_snapshot;

    memset(&cache_snapshot, 0, sizeof(cache_snapshot));
    memset(&reader_snapshot, 0, sizeof(reader_snapshot));
    memset(&runtime_snapshot, 0, sizeof(runtime_snapshot));

    sample_cache_diag_get_snapshot(&cache_snapshot);
    sample_voice_reader_diag_get_snapshot(&reader_snapshot);
    brick6_sampler_runtime_diag_get_snapshot(&runtime_snapshot);

    g_sample_cache_diag_snapshot = cache_snapshot;
    g_sample_voice_reader_diag_snapshot = reader_snapshot;
    g_brick6_sampler_runtime_diag_snapshot = runtime_snapshot;
}

static void brick6_sampler_render_sample_segment_cursor(brick6_sampler_voice_t *voice,
                                                        float *out_l,
                                                        float *out_r,
                                                        uint32_t frames)
{
    if ((voice == NULL) || (out_l == NULL) || (out_r == NULL) || (frames == 0U))
    {
        return;
    }

    uint32_t produced = 0U;
    while (produced < frames)
    {
        sample_audio_segment_t segment;
        if ((sample_voice_reader_begin_segment(&voice->reader, frames - produced, &segment) == 0U)
            || (segment.status != SAMPLE_AUDIO_SEGMENT_OK) || (segment.frames == 0U))
        {
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }

        BRICK6_SAMPLER_RUNTIME_DIAG_INC(segment_cursor_blocks);
        BRICK6_SAMPLER_RUNTIME_DIAG_INC(mixed_segments);

        if ((voice->fade_in_frames == 0U) && (voice->fade_out_frames == 0U))
        {
            if (segment.kernel_type == SAMPLE_KERNEL_REV_1X)
            {
                sample_voice_reader_mix_rev_1x(&segment, voice->gain, 0, 0U, out_l, out_r, produced);
            }
            else
            {
                sample_voice_reader_mix_fwd_1x(&segment, voice->gain, 0, 0U, out_l, out_r, produced);
            }
        }
        else
        {
            float fade_buf[AUDIO_BLOCK_SIZE];
            for (uint32_t i = 0U; i < segment.frames; ++i)
            {
                const uint32_t loop_pos =
                    (segment.kernel_type == SAMPLE_KERNEL_REV_1X)
                        ? ((segment.start_frame >= (voice->region_begin + i))
                               ? (segment.start_frame - voice->region_begin - i)
                               : 0U)
                        : ((segment.start_frame + i) - voice->region_begin);
                fade_buf[i] = brick6_sampler_runtime_fade_gain(loop_pos,
                                                               voice->loop_frames,
                                                               voice->fade_in_frames,
                                                               voice->fade_out_frames);
            }
            if (segment.kernel_type == SAMPLE_KERNEL_REV_1X)
            {
                sample_voice_reader_mix_rev_1x(&segment,
                                               voice->gain,
                                               fade_buf,
                                               segment.frames,
                                               out_l,
                                               out_r,
                                               produced);
            }
            else
            {
                sample_voice_reader_mix_fwd_1x(&segment,
                                               voice->gain,
                                               fade_buf,
                                               segment.frames,
                                               out_l,
                                               out_r,
                                               produced);
            }
        }

        sample_voice_reader_commit_segment(&voice->reader, segment.frames);
        produced += segment.frames;
        voice->position = voice->reader.position;
        if (voice->reader.active == 0U)
        {
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }
    }
}

static void brick6_sampler_render_sample(const sample_desc_t *desc,
                                         brick6_sampler_voice_t *voice,
                                         float *out_l,
                                         float *out_r,
                                         uint32_t frames)
{
    if ((desc == NULL) || (voice == NULL) || (out_l == NULL) || (out_r == NULL) || (frames == 0U))
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

    const uint32_t loop_begin = voice->region_begin;
    const uint32_t loop_end = voice->region_end;
    const uint32_t loop_frames = voice->loop_frames;
    const uint8_t has_fade = ((voice->fade_in_frames != 0U) || (voice->fade_out_frames != 0U)) ? 1U : 0U;
    uint32_t produced = 0U;

    if (voice->use_segment_cursor != 0U)
    {
        brick6_sampler_render_sample_segment_cursor(voice, out_l, out_r, frames);
        if (((voice->reverse == 0U) && (voice->loop_mode == BRICK6_SAMPLER_LOOP_NONE)
             && (voice->reader.position >= (float)voice->region_end))
            || ((voice->reverse != 0U) && (voice->reader.active == 0U)))
        {
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
        }
        return;
    }

    if ((voice->reverse != 0U) || (fabsf(voice->step_signed - 1.0f) > BRICK6_SAMPLER_STEP_EPSILON)
        || (voice->loop_mode == BRICK6_SAMPLER_LOOP_PINGPONG))
    {
        BRICK6_SAMPLER_RUNTIME_DIAG_INC(slow_path_blocks);
        BRICK6_SAMPLER_RUNTIME_DIAG_INC(mixed_segments);
        float fade_buf[AUDIO_BLOCK_SIZE];
        const float *fade_ptr = 0;
        uint32_t fade_count = 0U;
        uint8_t underrun = 0U;
        if (has_fade != 0U)
        {
            fade_ptr = fade_buf;
            fade_count = frames;
            for (uint32_t i = 0U; i < frames; ++i)
            {
                const uint32_t base_frame = (uint32_t)voice->reader.position;
                const uint32_t loop_pos = (voice->reverse != 0U)
                                              ? ((loop_end > 0U) && (base_frame < loop_end)
                                                     ? ((loop_end - 1U) - base_frame)
                                                     : 0U)
                                              : ((base_frame > loop_begin) ? (base_frame - loop_begin) : 0U);
                fade_buf[i] = brick6_sampler_runtime_fade_gain(loop_pos,
                                                               loop_frames,
                                                               voice->fade_in_frames,
                                                               voice->fade_out_frames);
            }
        }

        produced = sample_voice_reader_render_pitch_forward(&voice->reader,
                                                            loop_begin,
                                                            loop_end,
                                                            &voice->reverse,
                                                            voice->loop_mode,
                                                            voice->gain,
                                                            fade_ptr,
                                                            fade_count,
                                                            out_l,
                                                            out_r,
                                                            frames,
                                                            &underrun);
        voice->position = voice->reader.position;
        if ((underrun != 0U) || (produced < frames)
            || (brick6_sampler_runtime_is_terminal_position(voice, voice->reader.position) != 0U))
        {
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
        }

        return;
    }

    uint32_t position = (uint32_t)voice->position;

    while (produced < frames)
    {
        if (position >= loop_end)
        {
            if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_FORWARD) && (loop_end > loop_begin))
            {
                position = loop_begin;
                sample_voice_reader_seek(&voice->reader, loop_begin);
                continue;
            }

            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_commit_block(&voice->reader, 0U);
            sample_voice_reader_stop(&voice->reader);
            break;
        }

        uint32_t request_frames = frames - produced;
        const uint32_t region_remaining = loop_end - position;
        if (request_frames > region_remaining)
        {
            request_frames = region_remaining;
        }

        sample_cache_block_t block;
        if ((sample_voice_reader_begin_block(&voice->reader, request_frames, &block) == 0U)
            || (block.status != SAMPLE_CACHE_BLOCK_OK)
            || (block.frames == 0U))
        {
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_commit_block(&voice->reader, 0U);
            sample_voice_reader_stop(&voice->reader);
            break;
        }

        BRICK6_SAMPLER_RUNTIME_DIAG_INC(fast_path_blocks);
        BRICK6_SAMPLER_RUNTIME_DIAG_INC(mixed_segments);

        const float *src_l = block.l;
        const float *src_r = (block.is_mono != 0U) ? block.l : block.r;
        if (has_fade == 0U)
        {
            const float sample_gain = voice->gain;
            if (block.is_mono != 0U)
            {
                for (uint32_t i = 0U; i < block.frames; ++i)
                {
                    const float sample_l = src_l[i * block.frame_stride];
                    out_l[produced + i] += sample_l * sample_gain;
                    out_r[produced + i] += sample_l * sample_gain;
                }
            }
            else
            {
                for (uint32_t i = 0U; i < block.frames; ++i)
                {
                    out_l[produced + i] += src_l[i * block.frame_stride] * sample_gain;
                    out_r[produced + i] += src_r[i * block.frame_stride] * sample_gain;
                }
            }
        }
        else
        {
            uint32_t loop_pos = position - loop_begin;
            for (uint32_t i = 0U; i < block.frames; ++i)
            {
                const float fade_gain = brick6_sampler_runtime_fade_gain(loop_pos + i,
                                                                         loop_frames,
                                                                         voice->fade_in_frames,
                                                                         voice->fade_out_frames);
                const float sample_gain = voice->gain * fade_gain;
                const float sample_l = src_l[i * block.frame_stride];
                const float sample_r = (block.is_mono != 0U)
                                           ? sample_l
                                           : src_r[i * block.frame_stride];
                out_l[produced + i] += sample_l * sample_gain;
                out_r[produced + i] += sample_r * sample_gain;
            }
        }

        sample_voice_reader_commit_block(&voice->reader, block.frames);
        produced += block.frames;
        position += block.frames;
        if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_FORWARD) && (position >= loop_end)
            && (loop_end > loop_begin))
        {
            position = loop_begin;
            sample_voice_reader_seek(&voice->reader, loop_begin);
        }
    }

    voice->position = (float)position;
    if (brick6_sampler_runtime_is_terminal_position(voice, voice->position) != 0U)
    {
        voice->active = 0U;
        voice->position = 0.0f;
        sample_voice_reader_stop(&voice->reader);
    }
}

void brick6_sampler_runtime_render_track(const track_runtime_ctx_t *ctx,
                                         float *out_l,
                                         float *out_r,
                                         uint32_t frames)
{
    if ((ctx == NULL) || (out_l == NULL) || (out_r == NULL) || (frames == 0U))
    {
        return;
    }

    BRICK6_SAMPLER_RUNTIME_DIAG_INC(render_track_calls);
#if BRICK6_SAMPLER_DIAG_ENABLE
    {
        const uint32_t active_voices = brick6_sampler_runtime_diag_count_active_voices();
        if (active_voices > g_brick6_sampler_runtime_diag.max_active_voices)
        {
            g_brick6_sampler_runtime_diag.max_active_voices = active_voices;
        }
    }
#endif

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

    brick6_sampler_render_sample(desc, voice, out_l, out_r, frames);
}
