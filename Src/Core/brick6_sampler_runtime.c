/**
 * @file brick6_sampler_runtime.c
 * @brief Sampler backend with forward Slicer v1 trigger support on the existing play-plan/cursor path.
 */

#include "Core/brick6_sampler_runtime.h"

#include <math.h>
#include <string.h>

#include "Audio/audio_float.h"
#include "Audio/mixer.h"
#include "Core/brick6_clip_shifter.h"
#include "Storage/memory_layout.h"
#include "Sampler/multi_sample_loader.h"
#include "Sampler/multi_sample_pool.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_pool.h"
#include "Sampler/sample_stream_manager.h"
#include "Sampler/sample_voice_reader.h"
#include "Seq/seq_runtime_exec.h"
#include "Seq/seq_runtime.h"
#include "UI/ui_track_catalog.h"

#define BRICK6_SAMPLER_Q16_ONE (65536U)
#define BRICK6_SAMPLER_CACHE_VOICE_BASE (2U)
#define BRICK6_SAMPLER_CLIP_SLOT_NONE 0xFFU
#define BRICK6_SAMPLER_CLIP_DEFAULT_GRAIN_FRAMES 1536U
#define BRICK6_SAMPLER_CACHE_VOICE_NONE UINT8_MAX
#define BRICK6_SAMPLER_MULTI_LOOKAHEAD_PAGES SAMPLE_PAGE_MULTI_LOOKAHEAD_PAGES
#define BRICK6_SAMPLER_MULTI_WINDOW_MASK_BITS (32U)
#define BRICK6_SAMPLER_MIN_READY_TARGET_FRAMES (8192U)

typedef struct
{
    uint8_t source_kind;
    uint16_t sample_id;
    uint16_t multi_instrument_id;
    uint16_t multi_sample_id;
    uint8_t owner_track_id;
    const sample_desc_t *sample;
    float position;
    uint8_t active;
    uint8_t note;
    uint8_t velocity;
    uint8_t mode;
    uint8_t slice_count;
    uint8_t slice_index;
    float gain;
    float trigger_velocity_gain;
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
    uint8_t release_pending;
    sample_play_plan_t play_plan;
    sample_voice_reader_t reader;
    uint32_t trigger_order;
    sample_stream_active_state_t stream_state;
    uint32_t pinned_first_page;
    uint32_t pinned_last_page;
    uint32_t slice_begin[64U];
    uint32_t slice_end[64U];
} brick6_sampler_voice_t;

typedef enum
{
    BRICK6_SAMPLER_VOICE_NONE = 0,
    BRICK6_SAMPLER_VOICE_CLASSIC,
    BRICK6_SAMPLER_VOICE_CLIP,
    BRICK6_SAMPLER_VOICE_MULTI
} brick6_sampler_voice_kind_t;

typedef enum
{
    BRICK6_SAMPLER_CLIP_STATE_IDLE = 0,
    BRICK6_SAMPLER_CLIP_STATE_PLAYING,
    BRICK6_SAMPLER_CLIP_STATE_STOPPED
} brick6_sampler_clip_state_t;

typedef struct
{
    uint16_t sample_id;
    float gain;
    float source_bpm;
    uint16_t grain_size;
    uint8_t sync_length;
    float pitch_semitones;
    uint8_t play_mode;
    uint8_t loop_enabled;
    uint8_t stretch_mode;
    uint8_t state;
    uint8_t use_shifter_engine;
    uint8_t clip_slot_index;
    uint32_t timing_ratio_q16;
    uint32_t pitch_ratio_q16;
} brick6_sampler_clip_runtime_t;

typedef struct
{
    uint16_t instrument_id;
    float gain;
} brick6_sampler_multi_track_state_t;

typedef struct
{
    uint8_t owner_track_id;
    brick6_clip_shifter_t shifter;
    float stretch_render_l[AUDIO_BLOCK_SIZE];
    float stretch_render_r[AUDIO_BLOCK_SIZE];
} brick6_sampler_clip_slot_t;

enum
{
    BRICK6_SAMPLER_LOOP_NONE = 0,
    BRICK6_SAMPLER_LOOP_FORWARD = 1,
    BRICK6_SAMPLER_LOOP_PINGPONG = 2
};

typedef enum
{
    BRICK6_SAMPLE_COMMON_TRIGGER_CLASSIC = 0,
    BRICK6_SAMPLE_COMMON_TRIGGER_MULTI
} brick6_sample_common_trigger_kind_t;

typedef enum
{
    BRICK6_SAMPLE_COMMON_PLAN_OK = 0,
    BRICK6_SAMPLE_COMMON_PLAN_INVALID_ARG,
    BRICK6_SAMPLE_COMMON_PLAN_RESOLVE_FAIL,
    BRICK6_SAMPLE_COMMON_PLAN_SOURCE_INVALID,
    BRICK6_SAMPLE_COMMON_PLAN_BUILD_FAIL,
    BRICK6_SAMPLE_COMMON_PLAN_PLAN_INVALID
} brick6_sample_common_plan_result_t;

typedef struct
{
    brick6_sample_common_trigger_kind_t kind;
    const brick6_sampler_voice_t *voice;
    const sample_play_plan_t *runtime_plan;
    uint32_t total_frames;
    uint32_t step_q16;
    float render_gain;
    uint16_t instrument_id;
    uint8_t track_id;
    uint8_t note;
    uint8_t velocity;
} brick6_sample_common_trigger_t;

static AUDIO_HOT brick6_sampler_voice_t g_sampler_voice[SEQ_TRACK_COUNT];
static AUDIO_HOT brick6_sampler_voice_t
    g_sampler_multi_voice[SAMPLER_MULTI_MAX_GLOBAL_VOICES];
static brick6_sampler_clip_runtime_t g_sampler_clip_runtime[SEQ_TRACK_COUNT];
static brick6_sampler_multi_track_state_t g_sampler_multi_track_state[SEQ_TRACK_COUNT];
static brick6_sampler_clip_slot_t g_sampler_clip_slots[BRICK6_MAX_CLIP_TRACKS];
static uint32_t g_sampler_voice_trigger_counter;
static CTRL_STATE uint8_t
    g_sampler_multi_stream_release_pending[MULTI_SAMPLE_POOL_MAX_SAMPLES];
static CTRL_STATE uint8_t
    g_sampler_multi_page0_reject_logged[MULTI_SAMPLE_POOL_MAX_SAMPLES];
static uint8_t g_sampler_multi_alloc_reject_reason =
    (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_NONE;

static brick6_sampler_runtime_diag_snapshot_t g_brick6_sampler_runtime_diag;
#define BRICK6_SAMPLER_RUNTIME_DIAG_INC(field) ((void)0)

#define BRICK6_SAMPLER_STEP_EPSILON (0.0001f)

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

static uint8_t brick6_sampler_runtime_cache_voice_id(uint8_t track_id);
static uint32_t brick6_sampler_runtime_multi_active_count(void);
static uint32_t brick6_sampler_runtime_multi_active_count_for_track(uint8_t track_id);
static uint8_t brick6_sampler_runtime_pick_slice_index(const brick6_sampler_voice_t *voice, uint8_t note);
static uint8_t brick6_sampler_runtime_resolve_grid_count(uint8_t raw_grid_count);
static float brick6_sampler_runtime_velocity_gain(uint8_t velocity);
static uint32_t brick6_sampler_runtime_ratio_to_q16(float ratio);
static uint32_t brick6_sampler_runtime_next_trigger_order(void);
static uint32_t brick6_sampler_runtime_clip_ratio_q16(float source_bpm);
static uint32_t brick6_sampler_runtime_clip_pitch_ratio_q16(float semitones);
static uint32_t brick6_sampler_runtime_clamp_region_begin(uint32_t length_frames, float start);
static uint32_t brick6_sampler_runtime_clamp_region_end(uint32_t length_frames, float end);
static uint32_t brick6_sampler_runtime_clip_resolve_timing_ratio_q16(uint8_t track_id,
                                                                     const sample_desc_t *desc,
                                                                     const brick6_sampler_clip_runtime_t *clip,
                                                                     uint32_t *out_region_begin,
                                                                     uint32_t *out_region_end);
static uint8_t brick6_sampler_runtime_clip_uses_shifter(const brick6_sampler_clip_runtime_t *clip);
static uint16_t brick6_sampler_runtime_clip_sanitize_grain_size(uint16_t grain_size);
static uint16_t brick6_sampler_runtime_clip_shifter_window_frames(uint16_t grain_size);
static brick6_sampler_clip_slot_t *brick6_sampler_runtime_clip_get_slot(uint8_t track_id);
static brick6_sampler_clip_slot_t *brick6_sampler_runtime_clip_ensure_slot(uint8_t track_id);
static void brick6_sampler_runtime_clip_release_slot(uint8_t track_id);
static void brick6_sampler_runtime_clip_configure_shifter(const brick6_sampler_clip_runtime_t *clip,
                                                          brick6_sampler_clip_slot_t *slot);
static void brick6_sampler_runtime_clip_reset_shifter(const brick6_sampler_clip_runtime_t *clip,
                                                      brick6_sampler_clip_slot_t *slot);
static void brick6_sampler_runtime_clip_reset(uint8_t track_id);
static uint8_t brick6_sampler_runtime_clip_start_playback(uint8_t track_id);
static void brick6_sampler_runtime_clip_stop_playback(uint8_t track_id);
static void brick6_sampler_runtime_clip_render_shifter(brick6_sampler_voice_t *voice,
                                                       brick6_sampler_clip_runtime_t *clip,
                                                       brick6_sampler_clip_slot_t *slot,
                                                       float *out_l,
                                                       float *out_r,
                                                       uint32_t frames);
static void brick6_sampler_render_sample_segment_cursor(brick6_sampler_voice_t *voice,
                                                        float *out_l,
                                                        float *out_r,
                                                        uint32_t frames);
static void brick6_sampler_render_multi(brick6_sampler_voice_t *voice,
                                        float *out_l,
                                        float *out_r,
                                        uint32_t frames);
static void brick6_sampler_runtime_multi_stop_voice(brick6_sampler_voice_t *voice, uint8_t reason);
static void brick6_sampler_runtime_multi_stop_track(uint8_t track_id);
static void brick6_sampler_runtime_multi_defer_stream_release(uint16_t multi_sample_id);
static void brick6_sampler_runtime_multi_service_streaming(void);
static void brick6_sampler_runtime_multi_service_stream_releases(void);
static uint8_t brick6_sampler_runtime_multi_prefetch_voice(brick6_sampler_voice_t *voice);
static uint32_t brick6_sampler_runtime_multi_ready_mask(sample_audio_key_t key,
                                                        uint32_t first_page,
                                                        uint32_t page_count);
static uint32_t brick6_sampler_runtime_multi_first_missing_page(sample_audio_key_t key,
                                                               uint32_t first_page,
                                                               uint32_t page_count);
static sample_audio_key_t brick6_sampler_runtime_multi_key(uint16_t multi_sample_id);
static void brick6_sampler_runtime_multi_release_voice_stream_owner(
    const brick6_sampler_voice_t *voice);
static void brick6_sampler_runtime_multi_diag_note_page0_reject(
    uint8_t track_id,
    uint8_t note,
    uint8_t velocity,
    uint16_t instrument_id,
    const multi_sample_resolve_result_t *resolved,
    const multi_sample_desc_t *sample,
    sample_page_state_t state0);
static uint8_t brick6_sampler_runtime_oneshot_voice_is_stealable(uint8_t track_id);
static brick6_sampler_voice_t *brick6_sampler_runtime_multi_alloc_voice(uint8_t track_id);
static void brick6_sampler_runtime_multi_track_reset(uint8_t track_id);
static brick6_sample_common_plan_result_t brick6_sampler_runtime_build_common_play_plan(
    const brick6_sample_common_trigger_t *trigger,
    sample_resolved_source_t *out_source,
    sample_play_plan_t *out_plan);
static void brick6_sampler_runtime_note_common_play_plan_result(
    brick6_sample_common_plan_result_t result,
    uint8_t classic);
void brick6_sampler_runtime_diag_reset(void);
void brick6_sampler_runtime_diag_get_snapshot(brick6_sampler_runtime_diag_snapshot_t *out_snapshot);

static uint8_t brick6_sampler_runtime_track_is_slicer(uint8_t track_id)
{
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track_id);
    if (ctx == NULL)
    {
        return 0U;
    }

    return (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_SLICER) ? 1U : 0U;
}

static void brick6_sampler_runtime_multi_track_reset(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_sampler_multi_track_state[track_id].instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    g_sampler_multi_track_state[track_id].gain = 1.0f;
}

static void brick6_sampler_runtime_diag_note_trigger(uint8_t track_id,
                                                     const brick6_sampler_voice_t *voice,
                                                     uint32_t start_frame,
                                                     uint32_t sample_length_frames)
{
    if (voice == NULL)
    {
        return;
    }

    g_brick6_sampler_runtime_diag.trigger_audio_timeline_sample =
        seq_runtime_exec_get_audio_timeline_sample();
    g_brick6_sampler_runtime_diag.first_output_audio_timeline_sample = 0U;
    g_brick6_sampler_runtime_diag.first_output_frame_offset = 0U;
    g_brick6_sampler_runtime_diag.first_output_valid = 0U;
    g_brick6_sampler_runtime_diag.start_frame = start_frame;
    g_brick6_sampler_runtime_diag.region_begin = voice->region_begin;
    g_brick6_sampler_runtime_diag.region_end = voice->region_end;
    g_brick6_sampler_runtime_diag.sample_length_frames = sample_length_frames;
    g_brick6_sampler_runtime_diag.fade_in_frames = voice->fade_in_frames;
    g_brick6_sampler_runtime_diag.fade_out_frames = voice->fade_out_frames;
    g_brick6_sampler_runtime_diag.sample_id = voice->sample_id;
    g_brick6_sampler_runtime_diag.track_id = track_id;
    g_brick6_sampler_runtime_diag.note = voice->note;
    g_brick6_sampler_runtime_diag.velocity = voice->velocity;
    g_brick6_sampler_runtime_diag.mode = voice->mode;
    g_brick6_sampler_runtime_diag.use_segment_cursor = voice->use_segment_cursor;
}

static void brick6_sampler_runtime_diag_note_first_output(uint8_t track_id,
                                                          const float *out_l,
                                                          const float *out_r,
                                                          uint32_t frames)
{
    if ((out_l == NULL) || (out_r == NULL) || (frames == 0U)
            || (g_brick6_sampler_runtime_diag.first_output_valid != 0U)
            || (g_brick6_sampler_runtime_diag.track_id != track_id))
    {
        return;
    }

    for (uint32_t i = 0U; i < frames; ++i)
    {
        if ((out_l[i] != 0.0f) || (out_r[i] != 0.0f))
        {
            g_brick6_sampler_runtime_diag.first_output_audio_timeline_sample =
                seq_runtime_exec_get_audio_timeline_sample();
            g_brick6_sampler_runtime_diag.first_output_frame_offset = i;
            g_brick6_sampler_runtime_diag.first_output_valid = 1U;
            return;
        }
    }
}

static uint32_t brick6_sampler_runtime_common_plan_reason(
    brick6_sample_common_plan_result_t result)
{
    return (uint32_t)result;
}

static void brick6_sampler_runtime_note_common_play_plan_result(
    brick6_sample_common_plan_result_t result,
    uint8_t classic)
{
    if (result == BRICK6_SAMPLE_COMMON_PLAN_OK)
    {
        return;
    }

    g_brick6_sampler_runtime_diag.common_plan_last_reason =
        brick6_sampler_runtime_common_plan_reason(result);
    if (classic != 0U)
    {
        g_brick6_sampler_runtime_diag.common_plan_classic_build_fail++;
    }
    else
    {
        g_brick6_sampler_runtime_diag.common_plan_multi_build_fail++;
    }
}

static brick6_sample_common_plan_result_t brick6_sampler_runtime_build_common_play_plan(
    const brick6_sample_common_trigger_t *trigger,
    sample_resolved_source_t *out_source,
    sample_play_plan_t *out_plan)
{
    if (out_source != NULL)
    {
        sample_resolved_source_init(out_source);
    }
    if (out_plan != NULL)
    {
        sample_play_plan_init(out_plan);
    }
    if ((trigger == NULL) || (out_source == NULL) || (out_plan == NULL)
        || (trigger->track_id >= SEQ_TRACK_COUNT))
    {
        return BRICK6_SAMPLE_COMMON_PLAN_INVALID_ARG;
    }

    sample_play_plan_build_options_t options = {
        .min_ready_frames = BRICK6_SAMPLER_MIN_READY_TARGET_FRAMES,
        .target_window_frames = 0U,
        .owner_generation = 0U,
        .diagnostics_page = UINT32_MAX,
        .flags = (uint8_t)(SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_REGION
                           | SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_LOOP
                           | SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_DIRECTION
                           | SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_RATE),
        .stop_on_underrun = 1U,
        .owner_kind = 0U,
        .owner_id = trigger->track_id,
        .diagnostics_reason = 0U,
        .start_gate_flags = 0U,
    };

    if (trigger->kind == BRICK6_SAMPLE_COMMON_TRIGGER_CLASSIC)
    {
        const brick6_sampler_voice_t *const voice = trigger->voice;
        const sample_play_plan_t *const runtime_plan = trigger->runtime_plan;
        if ((voice == NULL) || (voice->sample == NULL) || (runtime_plan == NULL))
        {
            return BRICK6_SAMPLE_COMMON_PLAN_INVALID_ARG;
        }
        if (sample_cache_resolve_classic_source(voice->sample_id, out_source) == 0U)
        {
            return BRICK6_SAMPLE_COMMON_PLAN_RESOLVE_FAIL;
        }
        out_source->region_begin = voice->region_begin;
        out_source->region_end = voice->region_end;
        out_source->loop_begin = runtime_plan->loop_begin;
        out_source->loop_end = runtime_plan->loop_end;
        out_source->loop_mode = voice->loop_mode;
        out_source->reverse = voice->reverse;
        out_source->rate = (voice->step_signed > 0.0f) ? voice->step_signed : 1.0f;
        out_source->gain = voice->gain * voice->trigger_velocity_gain;
        out_source->fine_tune_cents = (int16_t)(voice->tune * 100.0f);
        out_source->owner_track_id = trigger->track_id;
        out_source->note = voice->note;
        out_source->velocity = voice->velocity;
        out_source->source_kind = voice->source_kind;

        options.start_frame = runtime_plan->start_frame;
        options.end_frame = voice->region_end;
        options.loop_begin = runtime_plan->loop_begin;
        options.loop_end = runtime_plan->loop_end;
        options.rate = out_source->rate;
        options.reverse = voice->reverse;
        options.loop_mode = voice->loop_mode;
    }
    else if (trigger->kind == BRICK6_SAMPLE_COMMON_TRIGGER_MULTI)
    {
        if ((trigger->instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
            || (trigger->note > 127U) || (trigger->velocity > 127U)
            || (trigger->total_frames == 0U))
        {
            return BRICK6_SAMPLE_COMMON_PLAN_INVALID_ARG;
        }
        if (multi_sample_pool_resolve_source(trigger->instrument_id,
                                             trigger->note,
                                             trigger->velocity,
                                             out_source) == 0U)
        {
            return BRICK6_SAMPLE_COMMON_PLAN_RESOLVE_FAIL;
        }
        const float rate = (trigger->step_q16 != 0U)
                               ? ((float)trigger->step_q16 / (float)BRICK6_SAMPLER_Q16_ONE)
                               : 1.0f;
        out_source->region_begin = 0U;
        out_source->region_end = trigger->total_frames;
        out_source->loop_begin = 0U;
        out_source->loop_end = trigger->total_frames;
        out_source->loop_mode = SAMPLE_PLAY_LOOP_NONE;
        out_source->reverse = 0U;
        out_source->rate = rate;
        out_source->gain = trigger->render_gain;
        out_source->owner_track_id = trigger->track_id;
        out_source->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_MULTI;

        options.start_frame = 0U;
        options.end_frame = trigger->total_frames;
        options.loop_begin = 0U;
        options.loop_end = trigger->total_frames;
        options.rate = rate;
        options.reverse = 0U;
        options.loop_mode = SAMPLE_PLAY_LOOP_NONE;
    }
    else
    {
        return BRICK6_SAMPLE_COMMON_PLAN_INVALID_ARG;
    }

    if (sample_resolved_source_is_valid(out_source) == 0U)
    {
        return BRICK6_SAMPLE_COMMON_PLAN_SOURCE_INVALID;
    }
    if (sample_play_plan_build_from_source(out_source, &options, out_plan)
        != SAMPLE_PLAY_PLAN_BUILD_OK)
    {
        return BRICK6_SAMPLE_COMMON_PLAN_BUILD_FAIL;
    }
    if (sample_play_plan_is_valid(out_plan) == 0U)
    {
        return BRICK6_SAMPLE_COMMON_PLAN_PLAN_INVALID;
    }
    return BRICK6_SAMPLE_COMMON_PLAN_OK;
}

static uint8_t brick6_sampler_runtime_multi_voice_index(const brick6_sampler_voice_t *voice)
{
    if ((voice >= &g_sampler_multi_voice[0])
        && (voice < &g_sampler_multi_voice[SAMPLER_MULTI_MAX_GLOBAL_VOICES]))
    {
        return (uint8_t)(voice - &g_sampler_multi_voice[0]);
    }

    return UINT8_MAX;
}

static sample_audio_key_t brick6_sampler_runtime_multi_key(uint16_t multi_sample_id)
{
    return sample_audio_key_multi(multi_sample_id);
}

static void brick6_sampler_runtime_multi_release_voice_stream_owner(
    const brick6_sampler_voice_t *voice)
{
    const uint8_t voice_index = brick6_sampler_runtime_multi_voice_index(voice);
    if ((voice == NULL) || (voice_index == UINT8_MAX) || (voice->trigger_order == 0U))
    {
        return;
    }

    sample_stream_manager_release_owner((uint8_t)SAMPLE_STREAM_OWNER_MULTI_VOICE,
                                        voice_index,
                                        voice->trigger_order);
}

static void brick6_sampler_runtime_multi_diag_note_reject(uint8_t track_id,
                                                          uint8_t note,
                                                          uint8_t velocity,
                                                          uint16_t instrument_id,
                                                          uint16_t sample_id,
                                                          uint8_t reason)
{
    g_brick6_sampler_runtime_diag.multi_last_reject_reason = reason;
    g_brick6_sampler_runtime_diag.multi_last_active_global =
        (uint8_t)brick6_sampler_runtime_multi_active_count();
    g_brick6_sampler_runtime_diag.multi_last_active_track =
        (uint8_t)brick6_sampler_runtime_multi_active_count_for_track(track_id);
    if (reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_TRACK_VOICE_LIMIT)
    {
        g_brick6_sampler_runtime_diag.multi_rejected_track_voice_limit++;
    }
    else if (reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_GLOBAL_VOICE_LIMIT)
    {
        g_brick6_sampler_runtime_diag.multi_rejected_global_voice_limit++;
    }
    (void)note;
    (void)velocity;
    (void)instrument_id;
    (void)sample_id;
}

static void brick6_sampler_runtime_multi_diag_note_page0_reject(
    uint8_t track_id,
    uint8_t note,
    uint8_t velocity,
    uint16_t instrument_id,
    const multi_sample_resolve_result_t *resolved,
    const multi_sample_desc_t *sample,
    sample_page_state_t state0)
{
    if ((resolved == NULL) || (sample == NULL))
    {
        brick6_sampler_runtime_multi_diag_note_reject(
            track_id,
            note,
            velocity,
            instrument_id,
            (resolved != NULL) ? resolved->multi_sample_id : MULTI_SAMPLE_POOL_INVALID_ID,
            (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_PAGE0_MISSING);
        return;
    }

    const uint16_t sample_id = resolved->multi_sample_id;
    g_brick6_sampler_runtime_diag.multi_last_reject_reason =
        (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_PAGE0_MISSING;
    g_brick6_sampler_runtime_diag.multi_last_active_global =
        (uint8_t)brick6_sampler_runtime_multi_active_count();
    g_brick6_sampler_runtime_diag.multi_last_active_track =
        (uint8_t)brick6_sampler_runtime_multi_active_count_for_track(track_id);

    if (sample_id >= MULTI_SAMPLE_POOL_MAX_SAMPLES)
    {
        return;
    }
    if (g_sampler_multi_page0_reject_logged[sample_id] != 0U)
    {
        return;
    }
    g_sampler_multi_page0_reject_logged[sample_id] = 1U;

    (void)note;
    (void)velocity;
    (void)instrument_id;
    (void)state0;
}

static void brick6_sampler_runtime_multi_diag_note_trigger(const brick6_sampler_voice_t *voice)
{
    if (voice == NULL)
    {
        return;
    }

    g_brick6_sampler_runtime_diag.multi_last_active_global =
        (uint8_t)brick6_sampler_runtime_multi_active_count();
    g_brick6_sampler_runtime_diag.multi_last_active_track =
        (uint8_t)brick6_sampler_runtime_multi_active_count_for_track(voice->owner_track_id);

}

static void brick6_sampler_runtime_multi_diag_note_steal(uint8_t track_id,
                                                         const brick6_sampler_voice_t *victim,
                                                         uint8_t reason)
{
    g_brick6_sampler_runtime_diag.multi_last_steal_reason = reason;
    g_brick6_sampler_runtime_diag.multi_last_active_global =
        (uint8_t)brick6_sampler_runtime_multi_active_count();
    g_brick6_sampler_runtime_diag.multi_last_active_track =
        (uint8_t)brick6_sampler_runtime_multi_active_count_for_track(track_id);

    (void)victim;
}

static void brick6_sampler_runtime_multi_diag_note_stop(const brick6_sampler_voice_t *voice,
                                                        uint8_t reason)
{
    if (voice == NULL)
    {
        return;
    }

    const uint32_t current_frame = (uint32_t)voice->position;
    const uint32_t end_frame = voice->region_end;
    g_brick6_sampler_runtime_diag.multi_last_stop_reason = reason;
    g_brick6_sampler_runtime_diag.multi_last_current_frame = current_frame;
    g_brick6_sampler_runtime_diag.multi_last_end_frame = end_frame;
    if (reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_DONE)
    {
        g_brick6_sampler_runtime_diag.multi_stop_done++;
    }
    else if (reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_UNDERRUN)
    {
        g_brick6_sampler_runtime_diag.multi_stop_underrun++;
    }
    else if (reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_STEAL)
    {
        g_brick6_sampler_runtime_diag.multi_stop_steal++;
    }
    else if (reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_REL_DONE)
    {
        g_brick6_sampler_runtime_diag.multi_stop_rel_done++;
    }
    g_brick6_sampler_runtime_diag.multi_last_active_global =
        (uint8_t)brick6_sampler_runtime_multi_active_count();
    g_brick6_sampler_runtime_diag.multi_last_active_track =
        (uint8_t)brick6_sampler_runtime_multi_active_count_for_track(voice->owner_track_id);

}

static uint8_t brick6_sampler_runtime_track_is_clip(uint8_t track_id)
{
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track_id);
    if (ctx == NULL)
    {
        return 0U;
    }

    return (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_CLIP) ? 1U : 0U;
}

static uint8_t brick6_sampler_runtime_clip_mode_is_off(const brick6_sampler_clip_runtime_t *clip)
{
    return ((clip != NULL) && (clip->stretch_mode == 0U)) ? 1U : 0U;
}

static uint8_t brick6_sampler_runtime_clip_mode_is_shifter(const brick6_sampler_clip_runtime_t *clip)
{
    return ((clip != NULL) && (clip->stretch_mode == 2U)) ? 1U : 0U;
}

static uint8_t brick6_sampler_runtime_clip_uses_shifter(const brick6_sampler_clip_runtime_t *clip)
{
    return (brick6_sampler_runtime_clip_mode_is_shifter(clip) != 0U) ? 1U : 0U;
}

static uint16_t brick6_sampler_runtime_clip_sanitize_grain_size(uint16_t grain_size)
{
    switch (grain_size)
    {
        case 384U:
        case 512U:
        case 768U:
        case 1024U:
        case 1536U:
        case 2048U:
            return grain_size;
        default:
            return BRICK6_SAMPLER_CLIP_DEFAULT_GRAIN_FRAMES;
    }
}

static uint16_t brick6_sampler_runtime_clip_shifter_window_frames(uint16_t grain_size)
{
    switch (brick6_sampler_runtime_clip_sanitize_grain_size(grain_size))
    {
        case 384U:
        case 512U:
        case 768U:
        case 1024U:
        case 1536U:
        case 2048U:
            return brick6_sampler_runtime_clip_sanitize_grain_size(grain_size);
        default:
            break;
    }

    return BRICK6_SAMPLER_CLIP_DEFAULT_GRAIN_FRAMES;
}

static brick6_sampler_clip_slot_t *brick6_sampler_runtime_clip_get_slot(uint8_t track_id)
{
    brick6_sampler_clip_runtime_t *clip;
    uint8_t slot_index;

    if (track_id >= SEQ_TRACK_COUNT)
    {
        return NULL;
    }

    clip = &g_sampler_clip_runtime[track_id];
    slot_index = clip->clip_slot_index;
    if (slot_index >= BRICK6_MAX_CLIP_TRACKS)
    {
        return NULL;
    }

    if (g_sampler_clip_slots[slot_index].owner_track_id != track_id)
    {
        return NULL;
    }

    return &g_sampler_clip_slots[slot_index];
}

static brick6_sampler_clip_slot_t *brick6_sampler_runtime_clip_ensure_slot(uint8_t track_id)
{
    brick6_sampler_clip_slot_t *slot;

    slot = brick6_sampler_runtime_clip_get_slot(track_id);
    if (slot != NULL)
    {
        return slot;
    }

    for (uint8_t i = 0U; i < BRICK6_MAX_CLIP_TRACKS; ++i)
    {
        if (g_sampler_clip_slots[i].owner_track_id != BRICK6_SAMPLER_CLIP_SLOT_NONE)
        {
            continue;
        }

        memset(&g_sampler_clip_slots[i], 0, sizeof(g_sampler_clip_slots[i]));
        g_sampler_clip_slots[i].owner_track_id = track_id;
        g_sampler_clip_runtime[track_id].clip_slot_index = i;
        brick6_clip_shifter_init(&g_sampler_clip_slots[i].shifter);
        return &g_sampler_clip_slots[i];
    }

    return NULL;
}

static void brick6_sampler_runtime_clip_release_slot(uint8_t track_id)
{
    brick6_sampler_clip_slot_t *slot;

    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    slot = brick6_sampler_runtime_clip_get_slot(track_id);
    if (slot != NULL)
    {
        memset(slot, 0, sizeof(*slot));
        slot->owner_track_id = BRICK6_SAMPLER_CLIP_SLOT_NONE;
    }

    g_sampler_clip_runtime[track_id].clip_slot_index = BRICK6_SAMPLER_CLIP_SLOT_NONE;
}

static void brick6_sampler_runtime_clip_configure_shifter(const brick6_sampler_clip_runtime_t *clip,
                                                          brick6_sampler_clip_slot_t *slot)
{
    float timing_ratio = 1.0f;
    float pitch_ratio = 1.0f;
    float pitch_correction = 1.0f;

    if ((clip == NULL) || (slot == NULL))
    {
        return;
    }

    if (clip->timing_ratio_q16 != 0U)
    {
        timing_ratio = (float)clip->timing_ratio_q16 / (float)BRICK6_SAMPLER_Q16_ONE;
    }
    if (clip->pitch_ratio_q16 != 0U)
    {
        pitch_ratio = (float)clip->pitch_ratio_q16 / (float)BRICK6_SAMPLER_Q16_ONE;
    }
    if (timing_ratio > 0.0f)
    {
        pitch_correction = pitch_ratio / timing_ratio;
    }

    brick6_clip_shifter_set_window_frames(&slot->shifter,
                                          brick6_sampler_runtime_clip_shifter_window_frames(clip->grain_size));
    brick6_clip_shifter_set_pitch_correction(&slot->shifter, pitch_correction);
}

static void brick6_sampler_runtime_clip_reset_shifter(const brick6_sampler_clip_runtime_t *clip,
                                                      brick6_sampler_clip_slot_t *slot)
{
    if ((clip == NULL) || (slot == NULL))
    {
        return;
    }

    brick6_clip_shifter_reset(&slot->shifter);
    brick6_sampler_runtime_clip_configure_shifter(clip, slot);
}

static void brick6_sampler_runtime_clip_reset(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    brick6_sampler_runtime_clip_release_slot(track_id);
    g_sampler_clip_runtime[track_id].sample_id = 0U;
    g_sampler_clip_runtime[track_id].gain = 1.0f;
    g_sampler_clip_runtime[track_id].source_bpm = 120.0f;
    g_sampler_clip_runtime[track_id].grain_size = BRICK6_SAMPLER_CLIP_DEFAULT_GRAIN_FRAMES;
    g_sampler_clip_runtime[track_id].sync_length = 0U;
    g_sampler_clip_runtime[track_id].pitch_semitones = 0.0f;
    g_sampler_clip_runtime[track_id].play_mode = 0U;
    g_sampler_clip_runtime[track_id].loop_enabled = 1U;
    g_sampler_clip_runtime[track_id].stretch_mode = 0U;
    g_sampler_clip_runtime[track_id].state = (uint8_t)BRICK6_SAMPLER_CLIP_STATE_IDLE;
    g_sampler_clip_runtime[track_id].use_shifter_engine = 0U;
    g_sampler_clip_runtime[track_id].clip_slot_index = BRICK6_SAMPLER_CLIP_SLOT_NONE;
    g_sampler_clip_runtime[track_id].timing_ratio_q16 = BRICK6_SAMPLER_Q16_ONE;
    g_sampler_clip_runtime[track_id].pitch_ratio_q16 = BRICK6_SAMPLER_Q16_ONE;
    sample_voice_reader_reset(&g_sampler_voice[track_id].reader);
    g_sampler_voice[track_id].active = 0U;
    g_sampler_voice[track_id].position = 0.0f;
    g_sampler_voice[track_id].sample = NULL;
    g_sampler_voice[track_id].source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    g_sampler_voice[track_id].multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    g_sampler_voice[track_id].multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    g_sampler_voice[track_id].release_pending = 0U;
    sample_stream_manager_active_state_reset(&g_sampler_voice[track_id].stream_state);
}

static uint32_t brick6_sampler_runtime_clip_ratio_q16(float source_bpm)
{
    if (source_bpm <= 0.0f)
    {
        return BRICK6_SAMPLER_Q16_ONE;
    }

    const float project_bpm = (float)seq_runtime_get_tempo_bpm_milli() * 0.001f;
    if (project_bpm <= 0.0f)
    {
        return BRICK6_SAMPLER_Q16_ONE;
    }

    float ratio = project_bpm / source_bpm;
    if (ratio < 0.5f)
    {
        ratio = 0.5f;
    }
    else if (ratio > 2.0f)
    {
        ratio = 2.0f;
    }

    return (uint32_t)(ratio * (float)BRICK6_SAMPLER_Q16_ONE + 0.5f);
}

static uint32_t brick6_sampler_runtime_clip_pitch_ratio_q16(float semitones)
{
    float ratio;
    if (semitones < -12.0f)
    {
        semitones = -12.0f;
    }
    else if (semitones > 12.0f)
    {
        semitones = 12.0f;
    }

    ratio = powf(2.0f, semitones / 12.0f);
    if (ratio < 0.5f)
    {
        ratio = 0.5f;
    }
    else if (ratio > 2.0f)
    {
        ratio = 2.0f;
    }
    return (uint32_t)(ratio * (float)BRICK6_SAMPLER_Q16_ONE + 0.5f);
}

static uint32_t brick6_sampler_runtime_clip_resolve_timing_ratio_q16(uint8_t track_id,
                                                                     const sample_desc_t *desc,
                                                                     const brick6_sampler_clip_runtime_t *clip,
                                                                     uint32_t *out_region_begin,
                                                                     uint32_t *out_region_end)
{
    uint32_t region_begin = 0U;
    uint32_t region_end = 0U;
    uint32_t ratio_q16 = BRICK6_SAMPLER_Q16_ONE;

    if ((track_id >= SEQ_TRACK_COUNT) || (desc == NULL) || (clip == NULL) || (desc->length_frames == 0U))
    {
        return ratio_q16;
    }

    region_begin = brick6_sampler_runtime_clamp_region_begin(desc->length_frames, g_sampler_voice[track_id].start);
    region_end = brick6_sampler_runtime_clamp_region_end(desc->length_frames, g_sampler_voice[track_id].end);
    if (region_begin >= region_end)
    {
        region_begin = 0U;
        region_end = desc->length_frames;
    }

    if (out_region_begin != NULL)
    {
        *out_region_begin = region_begin;
    }
    if (out_region_end != NULL)
    {
        *out_region_end = region_end;
    }

    if (brick6_sampler_runtime_clip_mode_is_off(clip) != 0U)
    {
        return BRICK6_SAMPLER_Q16_ONE;
    }

    if (clip->sync_length == 4U)
    {
        ratio_q16 = brick6_sampler_runtime_clip_ratio_q16(clip->source_bpm);
    }
    else if (clip->sync_length != 0U)
    {
        uint32_t target_bars = 0U;
        float target_seconds = 0.0f;
        const float project_bpm = (float)seq_runtime_get_tempo_bpm_milli() * 0.001f;
        const uint32_t sample_rate = (desc->sample_rate != 0U) ? desc->sample_rate : 48000U;
        const float source_seconds = (float)(region_end - region_begin) / (float)sample_rate;

        if (clip->sync_length == 1U)
        {
            target_bars = 1U;
        }
        else if (clip->sync_length == 2U)
        {
            target_bars = 2U;
        }
        else if (clip->sync_length == 3U)
        {
            target_bars = 4U;
        }

        if ((target_bars != 0U) && (project_bpm > 0.0f) && (source_seconds > 0.0f))
        {
            target_seconds = ((float)target_bars * 4.0f * 60.0f) / project_bpm;
            if (target_seconds > 0.0f)
            {
                float ratio = source_seconds / target_seconds;
                if (ratio < 0.5f)
                {
                    ratio = 0.5f;
                }
                else if (ratio > 2.0f)
                {
                    ratio = 2.0f;
                }
                ratio_q16 = (uint32_t)(ratio * (float)BRICK6_SAMPLER_Q16_ONE + 0.5f);
            }
        }
    }

    return ratio_q16;
}

static uint8_t brick6_sampler_runtime_clip_start_playback(uint8_t track_id)
{
    brick6_sampler_clip_slot_t *slot = NULL;
    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_clip(track_id) == 0U))
    {
        return 0U;
    }

    brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[track_id];
    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    const sample_desc_t *const desc = sample_pool_get(clip->sample_id);
    const uint8_t cache_voice_id = brick6_sampler_runtime_cache_voice_id(track_id);
    sample_play_plan_t play_plan;
    uint32_t ratio_q16 = BRICK6_SAMPLER_Q16_ONE;
    uint32_t pitch_ratio_q16 = BRICK6_SAMPLER_Q16_ONE;
    uint8_t use_shifter = brick6_sampler_runtime_clip_uses_shifter(clip);
    uint32_t step_q16 = BRICK6_SAMPLER_Q16_ONE;
    uint32_t region_begin = 0U;
    uint32_t region_end = 0U;

    if ((clip->sample_id >= SAMPLE_POOL_SIZE)
            || (desc == NULL)
            || (desc->valid == 0U)
            || (desc->length_frames == 0U)
            || (sample_cache_is_ready(clip->sample_id) == 0U))
    {
        if (clip->use_shifter_engine != 0U)
        {
            brick6_sampler_runtime_clip_release_slot(track_id);
        }
        return 0U;
    }

    if (use_shifter != 0U)
    {
        slot = brick6_sampler_runtime_clip_ensure_slot(track_id);
        if (slot == NULL)
        {
            use_shifter = 0U;
        }
    }
    else
    {
        brick6_sampler_runtime_clip_release_slot(track_id);
    }

    ratio_q16 = brick6_sampler_runtime_clip_resolve_timing_ratio_q16(track_id, desc, clip, &region_begin, &region_end);
    clip->timing_ratio_q16 = ratio_q16;
    clip->pitch_ratio_q16 = BRICK6_SAMPLER_Q16_ONE;
    if (use_shifter != 0U)
    {
        pitch_ratio_q16 = brick6_sampler_runtime_clip_pitch_ratio_q16(clip->pitch_semitones);
        clip->pitch_ratio_q16 = pitch_ratio_q16;
    }

    step_q16 = ratio_q16;

    memset(&play_plan, 0, sizeof(play_plan));
    play_plan.sample_id = clip->sample_id;
    play_plan.key = sample_audio_key_classic(clip->sample_id);
    play_plan.start_frame = region_begin;
    play_plan.region_begin = region_begin;
    play_plan.region_end = region_end;
    play_plan.loop_begin = region_begin;
    play_plan.loop_end = region_end;
    play_plan.fade_in_frames = 0U;
    play_plan.fade_out_frames = 0U;
    play_plan.step_q16 = step_q16;
    play_plan.direction = 0U;
    play_plan.loop_mode = (clip->loop_enabled != 0U) ? BRICK6_SAMPLER_LOOP_FORWARD
                                                     : BRICK6_SAMPLER_LOOP_NONE;
    play_plan.stop_on_underrun = 1U;
    play_plan.kernel_type = (step_q16 == BRICK6_SAMPLER_Q16_ONE)
                                ? SAMPLE_KERNEL_FWD_1X
                                : SAMPLE_KERNEL_PITCH_FWD_LINEAR;

    sample_cache_stop_voice(cache_voice_id);
    sample_voice_reader_reset(&voice->reader);

    if (sample_cache_start_voice_at(clip->sample_id, cache_voice_id, region_begin) == 0U)
    {
        if (use_shifter != 0U)
        {
            brick6_sampler_runtime_clip_release_slot(track_id);
        }
        return 0U;
    }

    if (sample_voice_reader_bind_play_plan(&voice->reader, &play_plan, cache_voice_id) == 0U)
    {
        sample_cache_stop_voice(cache_voice_id);
        sample_voice_reader_reset(&voice->reader);
        if (use_shifter != 0U)
        {
            brick6_sampler_runtime_clip_release_slot(track_id);
        }
        return 0U;
    }

    voice->sample_id = clip->sample_id;
    voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_CLIP;
    voice->multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->sample = desc;
    voice->position = (float)region_begin;
    voice->active = 1U;
    voice->note = 60U;
    voice->velocity = 127U;
    voice->mode = 2U;
    voice->slice_count = 0U;
    voice->slice_index = 0U;
    voice->gain = clip->gain;
    voice->trigger_velocity_gain = 1.0f;
    voice->start = g_sampler_voice[track_id].start;
    voice->end = g_sampler_voice[track_id].end;
    voice->tune = 0.0f;
    voice->fade_in = 0.0f;
    voice->fade_out = 0.0f;
    voice->region_begin = region_begin;
    voice->region_end = region_end;
    voice->loop_frames = region_end - region_begin;
    voice->fade_in_frames = 0U;
    voice->fade_out_frames = 0U;
    voice->step_signed = (float)step_q16 / (float)BRICK6_SAMPLER_Q16_ONE;
    voice->reverse = 0U;
    voice->loop_mode = (clip->loop_enabled != 0U) ? BRICK6_SAMPLER_LOOP_FORWARD
                                                  : BRICK6_SAMPLER_LOOP_NONE;
    voice->use_slice = 0U;
    voice->use_segment_cursor = 1U;
    voice->play_plan = play_plan;
    voice->trigger_order = brick6_sampler_runtime_next_trigger_order();
    clip->use_shifter_engine = use_shifter;
    if ((clip->use_shifter_engine != 0U) && (slot != NULL))
    {
        brick6_sampler_runtime_clip_reset_shifter(clip, slot);
    }
    return 1U;
}

static void brick6_sampler_runtime_clip_stop_playback(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    sample_voice_reader_stop(&g_sampler_voice[track_id].reader);
    g_sampler_voice[track_id].active = 0U;
    g_sampler_voice[track_id].position = 0.0f;
    g_sampler_voice[track_id].sample = NULL;
    g_sampler_voice[track_id].source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    g_sampler_clip_runtime[track_id].use_shifter_engine = 0U;
    brick6_sampler_runtime_clip_release_slot(track_id);
}

static uint8_t brick6_sampler_runtime_slicer_note_in_range(const brick6_sampler_voice_t *voice)
{
    if (voice == NULL)
    {
        return 0U;
    }

    const uint8_t note = voice->note;
    if (note < 60U)
    {
        return 0U;
    }

    const uint8_t grid_count = brick6_sampler_runtime_resolve_grid_count(voice->slice_count);
    if (grid_count == 0U)
    {
        return (note == 60U) ? 1U : 0U;
    }

    return (((uint32_t)(note - 60U)) < (uint32_t)grid_count) ? 1U : 0U;
}

static void brick6_sampler_runtime_prepare_slicer_entry_pages(uint8_t track_id)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_slicer(track_id) == 0U))
    {
        return;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    const sample_cache_state_t cache_state = sample_cache_get_state(voice->sample_id);
    if ((cache_state != SAMPLE_CACHE_READY_PARTIAL) && (cache_state != SAMPLE_CACHE_PLAYING))
    {
        return;
    }

    const sample_desc_t *const desc = sample_pool_get(voice->sample_id);
    if ((desc == NULL) || (desc->valid == 0U) || (desc->length_frames == 0U))
    {
        return;
    }

    const uint32_t sample_length = desc->length_frames;
    const uint8_t grid_count = brick6_sampler_runtime_resolve_grid_count(voice->slice_count);
    const uint32_t slice_total = (grid_count == 0U) ? 1U : (uint32_t)grid_count;
    uint32_t last_page_index = UINT32_MAX;

    for (uint32_t i = 0U; i < slice_total; ++i)
    {
        const uint32_t slice_begin = (grid_count == 0U) ? 0U : ((sample_length * i) / (uint32_t)grid_count);
        const uint32_t page_index = slice_begin / SAMPLE_PAGE_FRAMES;
        if (page_index == last_page_index)
        {
            continue;
        }

        (void)sample_stream_manager_request_page(voice->sample_id, page_index);
        last_page_index = page_index;
    }
}

static uint8_t brick6_sampler_runtime_resolve_grid_count(uint8_t raw_grid_count)
{
    switch (raw_grid_count)
    {
        case 0U:
            return 0U;
        case 1U:
        case 2U:
            return 2U;
        case 3U:
        case 4U:
            return 4U;
        case 5U:
        case 8U:
            return 8U;
        case 6U:
        case 16U:
            return 16U;
        case 32U:
            return 32U;
        case 64U:
            return 64U;
        default:
            break;
    }

    if (raw_grid_count <= 2U)
    {
        return 2U;
    }
    if (raw_grid_count <= 4U)
    {
        return 4U;
    }
    if (raw_grid_count <= 8U)
    {
        return 8U;
    }
    if (raw_grid_count <= 16U)
    {
        return 16U;
    }
    if (raw_grid_count <= 32U)
    {
        return 32U;
    }

    return 64U;
}

static float brick6_sampler_runtime_velocity_gain(uint8_t velocity)
{
    return (float)velocity * (1.0f / 127.0f);
}

static uint32_t brick6_sampler_runtime_ratio_to_q16(float ratio)
{
    if (ratio < 0.03125f)
    {
        ratio = 0.03125f;
    }
    else if (ratio > 32.0f)
    {
        ratio = 32.0f;
    }

    return (uint32_t)(ratio * (float)BRICK6_SAMPLER_Q16_ONE + 0.5f);
}

static uint32_t brick6_sampler_runtime_next_trigger_order(void)
{
    g_sampler_voice_trigger_counter++;
    if (g_sampler_voice_trigger_counter == 0U)
    {
        g_sampler_voice_trigger_counter = 1U;
    }
    return g_sampler_voice_trigger_counter;
}

static float brick6_sampler_runtime_compute_global_tune_step(const brick6_sampler_voice_t *voice)
{
    if (voice == NULL)
    {
        return 1.0f;
    }

    return powf(2.0f, voice->tune * (1.0f / 12.0f));
}

static uint8_t brick6_sampler_runtime_build_slicer_plan(uint8_t track_id,
                                                        sample_play_plan_t *out_plan,
                                                        uint32_t *out_region_begin,
                                                        uint32_t *out_region_end,
                                                        float *out_step_signed)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (out_plan == NULL) || (out_region_begin == NULL)
        || (out_region_end == NULL) || (out_step_signed == NULL))
    {
        return 0U;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    const sample_desc_t *const desc = sample_pool_get(voice->sample_id);
    if ((desc == NULL) || (desc->valid == 0U) || (desc->length_frames == 0U)
        || (sample_cache_is_ready(voice->sample_id) == 0U))
    {
        return 0U;
    }

    const uint32_t sample_length = desc->length_frames;
    const uint8_t grid_count = brick6_sampler_runtime_resolve_grid_count(voice->slice_count);
    const uint8_t note = voice->note;
    uint32_t slice_begin = 0U;
    uint32_t slice_end = sample_length;

    if (note < 60U)
    {
        return 0U;
    }

    if (grid_count == 0U)
    {
        if (note != 60U)
        {
            return 0U;
        }
    }
    else
    {
        const uint32_t slice_index = (uint32_t)(note - 60U);
        if (slice_index >= grid_count)
        {
            return 0U;
        }

        slice_begin = (sample_length * slice_index) / (uint32_t)grid_count;
        slice_end = (slice_index + 1U >= (uint32_t)grid_count)
                        ? sample_length
                        : ((sample_length * (slice_index + 1U)) / (uint32_t)grid_count);
    }

    if (slice_begin >= sample_length)
    {
        return 0U;
    }
    if (slice_end <= slice_begin)
    {
        slice_end = slice_begin + 1U;
    }
    if (slice_end > sample_length)
    {
        slice_end = sample_length;
    }

    memset(out_plan, 0, sizeof(*out_plan));
    *out_step_signed = brick6_sampler_runtime_compute_global_tune_step(voice);
    out_plan->sample_id = voice->sample_id;
    out_plan->key = sample_audio_key_classic(voice->sample_id);
    out_plan->start_frame = slice_begin;
    out_plan->region_begin = slice_begin;
    out_plan->region_end = slice_end;
    out_plan->loop_begin = slice_begin;
    out_plan->loop_end = slice_end;
    out_plan->fade_in_frames = 0U;
    out_plan->fade_out_frames = 0U;
    out_plan->step_q16 = (uint32_t)((*out_step_signed * 65536.0f) + 0.5f);
    out_plan->direction = 0U;
    out_plan->loop_mode = BRICK6_SAMPLER_LOOP_NONE;
    out_plan->stop_on_underrun = 1U;
    out_plan->kernel_type =
        (fabsf(*out_step_signed - 1.0f) <= BRICK6_SAMPLER_STEP_EPSILON) ? SAMPLE_KERNEL_FWD_1X
                                                                         : SAMPLE_KERNEL_PITCH_FWD_LINEAR;

    *out_region_begin = slice_begin;
    *out_region_end = slice_end;
    return 1U;
}

static void brick6_sampler_runtime_trigger_slicer(uint8_t track_id)
{
    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    const uint8_t cache_voice_id = brick6_sampler_runtime_cache_voice_id(track_id);
    sample_play_plan_t play_plan;
    uint32_t slice_begin = 0U;
    uint32_t slice_end = 0U;
    float step_signed = 1.0f;
    const float trigger_velocity_gain = brick6_sampler_runtime_velocity_gain(voice->velocity);

    if (brick6_sampler_runtime_slicer_note_in_range(voice) == 0U)
    {
        return;
    }

    if (brick6_sampler_runtime_build_slicer_plan(track_id,
                                                 &play_plan,
                                                 &slice_begin,
                                                 &slice_end,
                                                 &step_signed)
        == 0U)
    {
        return;
    }

    sample_cache_stop_voice(cache_voice_id);
    sample_voice_reader_reset(&voice->reader);
    voice->active = 0U;
    voice->position = 0.0f;

    voice->sample = sample_pool_get(voice->sample_id);
    voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_CLASSIC;
    voice->multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->region_begin = slice_begin;
    voice->region_end = slice_end;
    voice->loop_frames = slice_end - slice_begin;
    voice->fade_in_frames = 0U;
    voice->fade_out_frames = 0U;
    voice->step_signed = step_signed;
    voice->trigger_velocity_gain = trigger_velocity_gain;
    voice->reverse = 0U;
    voice->loop_mode = BRICK6_SAMPLER_LOOP_NONE;
    voice->use_slice = 0U;
    voice->use_segment_cursor = 1U;
    voice->play_plan = play_plan;
    sample_resolved_source_t common_source;
    sample_play_plan_t common_plan;
    const brick6_sample_common_trigger_t common_trigger = {
        .kind = BRICK6_SAMPLE_COMMON_TRIGGER_CLASSIC,
        .voice = voice,
        .runtime_plan = &voice->play_plan,
        .track_id = track_id,
    };
    const brick6_sample_common_plan_result_t common_result =
        brick6_sampler_runtime_build_common_play_plan(&common_trigger,
                                                      &common_source,
                                                      &common_plan);
    brick6_sampler_runtime_note_common_play_plan_result(common_result, 1U);
    if (common_result != BRICK6_SAMPLE_COMMON_PLAN_OK)
    {
        return;
    }

    if (sample_cache_start_voice_at(voice->sample_id, cache_voice_id, common_plan.start_frame)
        == 0U)
    {
        return;
    }

    if (sample_voice_reader_bind_play_plan(&voice->reader, &common_plan, cache_voice_id)
        == 0U)
    {
        sample_cache_stop_voice(cache_voice_id);
        sample_voice_reader_reset(&voice->reader);
        voice->active = 0U;
        voice->position = 0.0f;
        return;
    }

    voice->play_plan = common_plan;
    voice->trigger_order = brick6_sampler_runtime_next_trigger_order();
    voice->position = (float)voice->play_plan.start_frame;
    voice->active = 1U;
}

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

static uint8_t brick6_sampler_runtime_use_segment_cursor_pitch_forward(const brick6_sampler_voice_t *voice)
{
    if (voice == NULL)
    {
        return 0U;
    }

    return (((voice->mode == 0U) && (voice->reverse == 0U)
             && (voice->loop_mode == BRICK6_SAMPLER_LOOP_NONE) && (voice->use_slice == 0U)
             && (voice->step_signed > 0.0f)
             && (fabsf(voice->step_signed - 1.0f) > BRICK6_SAMPLER_STEP_EPSILON)))
               ? 1U
               : 0U;
}

static uint8_t brick6_sampler_runtime_use_segment_cursor_pitch_loop_forward(const brick6_sampler_voice_t *voice)
{
    if (voice == NULL)
    {
        return 0U;
    }

    return (((voice->mode == 2U) && (voice->reverse == 0U)
             && (voice->loop_mode == BRICK6_SAMPLER_LOOP_FORWARD) && (voice->use_slice == 0U)
             && (voice->step_signed > 0.0f)
             && (fabsf(voice->step_signed - 1.0f) > BRICK6_SAMPLER_STEP_EPSILON)))
               ? 1U
               : 0U;
}

static uint8_t brick6_sampler_runtime_use_segment_cursor_pitch_pingpong(const brick6_sampler_voice_t *voice)
{
    if (voice == NULL)
    {
        return 0U;
    }

    return (((voice->mode == 3U) && (voice->loop_mode == BRICK6_SAMPLER_LOOP_PINGPONG)
             && (voice->use_slice == 0U) && (voice->step_signed > 0.0f)
             && (fabsf(voice->step_signed - 1.0f) > BRICK6_SAMPLER_STEP_EPSILON)
             && (voice->loop_frames >= 2U)))
               ? 1U
               : 0U;
}

static uint8_t brick6_sampler_runtime_use_segment_cursor_pitch_reverse(const brick6_sampler_voice_t *voice)
{
    if (voice == NULL)
    {
        return 0U;
    }

    return (((voice->mode == 1U) && (voice->reverse != 0U)
             && (voice->loop_mode == BRICK6_SAMPLER_LOOP_NONE) && (voice->use_slice == 0U)
             && (voice->step_signed > 0.0f)
             && (fabsf(voice->step_signed - 1.0f) > BRICK6_SAMPLER_STEP_EPSILON)))
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
    voice->play_plan.key = sample_audio_key_classic(voice->sample_id);
    voice->play_plan.start_frame = (voice->reverse != 0U) ? ((end > begin) ? (end - 1U) : begin) : begin;
    voice->play_plan.region_begin = begin;
    voice->play_plan.region_end = end;
    voice->play_plan.loop_begin = begin;
    voice->play_plan.loop_end = end;
    voice->play_plan.fade_in_frames = voice->fade_in_frames;
    voice->play_plan.fade_out_frames = voice->fade_out_frames;
    const float step_q16 = voice->step_signed * 65536.0f;
    voice->play_plan.step_q16 = (step_q16 > 0.0f) ? (uint32_t)(step_q16 + 0.5f) : 65536U;
    voice->play_plan.direction = voice->reverse;
    voice->play_plan.loop_mode = voice->loop_mode;
    voice->play_plan.stop_on_underrun = 1U;
    if (brick6_sampler_runtime_use_segment_cursor_pitch_forward(voice) != 0U)
    {
        voice->play_plan.kernel_type = SAMPLE_KERNEL_PITCH_FWD_LINEAR;
        voice->use_segment_cursor = 1U;
    }
    else if (brick6_sampler_runtime_use_segment_cursor_pitch_loop_forward(voice) != 0U)
    {
        voice->play_plan.kernel_type = SAMPLE_KERNEL_PITCH_FWD_LINEAR;
        voice->use_segment_cursor = 1U;
    }
    else if (brick6_sampler_runtime_use_segment_cursor_pitch_pingpong(voice) != 0U)
    {
        voice->play_plan.kernel_type = SAMPLE_KERNEL_PITCH_FWD_LINEAR;
        voice->use_segment_cursor = 1U;
    }
    else if (brick6_sampler_runtime_use_segment_cursor_pitch_reverse(voice) != 0U)
    {
        voice->play_plan.kernel_type = SAMPLE_KERNEL_PITCH_REV_LINEAR;
        voice->use_segment_cursor = 1U;
    }
    else
    {
        voice->play_plan.kernel_type = (voice->reverse != 0U) ? SAMPLE_KERNEL_REV_1X : SAMPLE_KERNEL_FWD_1X;
        voice->use_segment_cursor = brick6_sampler_runtime_use_segment_cursor_path(voice);
    }
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
    const uint32_t slice_count = (uint32_t)brick6_sampler_runtime_resolve_grid_count(voice->slice_count);
    if (slice_count == 0U)
    {
        voice->slice_begin[0] = 0U;
        voice->slice_end[0] = length_frames;
        return;
    }

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
    memset(g_sampler_multi_voice, 0, sizeof(g_sampler_multi_voice));
    memset(g_sampler_multi_track_state, 0, sizeof(g_sampler_multi_track_state));
    memset(g_sampler_clip_runtime, 0, sizeof(g_sampler_clip_runtime));
    memset(g_sampler_clip_slots, 0, sizeof(g_sampler_clip_slots));
    for (uint8_t i = 0U; i < BRICK6_MAX_CLIP_TRACKS; ++i)
    {
        g_sampler_clip_slots[i].owner_track_id = BRICK6_SAMPLER_CLIP_SLOT_NONE;
    }
    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        g_sampler_multi_voice[i].owner_track_id = UINT8_MAX;
        g_sampler_multi_voice[i].multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
        g_sampler_multi_voice[i].multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
        sample_stream_manager_active_state_reset(&g_sampler_multi_voice[i].stream_state);
    }
    for (uint8_t i = 0U; i < SEQ_TRACK_COUNT; ++i)
    {
        g_sampler_voice[i].note = 60U;
        g_sampler_voice[i].source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
        g_sampler_voice[i].multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
        g_sampler_voice[i].multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
        g_sampler_voice[i].velocity = 127U;
        g_sampler_voice[i].gain = 1.0f;
        g_sampler_voice[i].trigger_velocity_gain = 1.0f;
        g_sampler_voice[i].start = 0.0f;
        g_sampler_voice[i].end = 1.0f;
        g_sampler_voice[i].slice_count = 0U;
        g_sampler_voice[i].loop_mode = 0U;
        g_sampler_voice[i].reverse = 0U;
        sample_stream_manager_active_state_reset(&g_sampler_voice[i].stream_state);
        brick6_sampler_runtime_multi_track_reset(i);
        sample_voice_reader_reset(&g_sampler_voice[i].reader);
        brick6_sampler_runtime_clip_reset(i);
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
    g_sampler_voice[track_id].source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    g_sampler_voice[track_id].multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    g_sampler_voice[track_id].multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    g_sampler_voice[track_id].velocity = 127U;
    g_sampler_voice[track_id].gain = 1.0f;
    g_sampler_voice[track_id].trigger_velocity_gain = 1.0f;
    g_sampler_voice[track_id].end = 1.0f;
    g_sampler_voice[track_id].slice_count = 0U;
    g_sampler_voice[track_id].sample = NULL;
    g_sampler_voice[track_id].release_pending = 0U;
    sample_stream_manager_active_state_reset(&g_sampler_voice[track_id].stream_state);
    brick6_sampler_runtime_multi_track_reset(track_id);
    brick6_sampler_runtime_multi_stop_track(track_id);
    brick6_sampler_runtime_clip_reset(track_id);
}

void brick6_sampler_runtime_set_sample(uint8_t track_id, uint16_t sample_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (brick6_sampler_runtime_track_is_clip(track_id) != 0U)
    {
        g_sampler_clip_runtime[track_id].sample_id = sample_id;
        return;
    }

    g_sampler_voice[track_id].sample_id = sample_id;
    g_sampler_voice[track_id].note = 60U;
    g_sampler_voice[track_id].position = 0.0f;
    brick6_sampler_runtime_rebuild_grid(track_id);
    brick6_sampler_runtime_prepare_slicer_entry_pages(track_id);
}

void brick6_sampler_runtime_set_gain(uint8_t track_id, float gain)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    if (brick6_sampler_runtime_track_is_clip(track_id) != 0U)
    {
        g_sampler_clip_runtime[track_id].gain = gain;
        g_sampler_voice[track_id].gain = gain;
        return;
    }
    g_sampler_voice[track_id].gain = gain;
}

void brick6_sampler_runtime_set_multi_instrument(uint8_t track_id, uint16_t instrument_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if ((instrument_id != MULTI_SAMPLE_POOL_INVALID_ID)
        && (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS))
    {
        g_brick6_sampler_runtime_diag.multi_invalid_instrument_id++;
        return;
    }

    g_sampler_multi_track_state[track_id].instrument_id = instrument_id;
}

uint8_t brick6_sampler_runtime_get_multi_instrument(uint8_t track_id, uint16_t *out_instrument_id)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (out_instrument_id == NULL))
    {
        return 0U;
    }

    *out_instrument_id = g_sampler_multi_track_state[track_id].instrument_id;
    return 1U;
}

void brick6_sampler_runtime_set_multi_gain(uint8_t track_id, float gain)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (gain < 0.0f)
    {
        gain = 0.0f;
    }
    else if (gain > 4.0f)
    {
        gain = 4.0f;
    }

    g_sampler_multi_track_state[track_id].gain = gain;
}

float brick6_sampler_runtime_get_multi_gain(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return 0.0f;
    }

    return g_sampler_multi_track_state[track_id].gain;
}

uint8_t brick6_sampler_runtime_multi_instrument_is_ready(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    const uint16_t instrument_id = g_sampler_multi_track_state[track_id].instrument_id;
    if (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
    {
        return 0U;
    }

    return multi_sample_is_ready(instrument_id);
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

    g_sampler_voice[track_id].slice_count = brick6_sampler_runtime_resolve_grid_count(slice_count);
    brick6_sampler_runtime_rebuild_grid(track_id);
    brick6_sampler_runtime_prepare_slicer_entry_pages(track_id);
}

void brick6_sampler_runtime_set_clip_source_bpm(uint8_t track_id, float source_bpm)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_clip(track_id) == 0U))
    {
        return;
    }

    g_sampler_clip_runtime[track_id].source_bpm = source_bpm;
}

void brick6_sampler_runtime_set_clip_sync_length(uint8_t track_id, uint8_t sync_length)
{
    brick6_sampler_clip_runtime_t *clip;
    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_clip(track_id) == 0U))
    {
        return;
    }

    clip = &g_sampler_clip_runtime[track_id];
    clip->sync_length = sync_length;
}

void brick6_sampler_runtime_set_clip_pitch(uint8_t track_id, float semitones)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_clip(track_id) == 0U))
    {
        return;
    }

    if (semitones < -12.0f)
    {
        semitones = -12.0f;
    }
    else if (semitones > 12.0f)
    {
        semitones = 12.0f;
    }

    g_sampler_clip_runtime[track_id].pitch_semitones = semitones;
}

void brick6_sampler_runtime_set_clip_play_mode(uint8_t track_id, uint8_t play_mode)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_clip(track_id) == 0U))
    {
        return;
    }

    g_sampler_clip_runtime[track_id].play_mode = (play_mode != 0U) ? 1U : 0U;
}

void brick6_sampler_runtime_set_clip_loop(uint8_t track_id, uint8_t loop_enabled)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_clip(track_id) == 0U))
    {
        return;
    }

    const uint8_t enabled = (loop_enabled != 0U) ? 1U : 0U;
    g_sampler_clip_runtime[track_id].loop_enabled = enabled;
    g_sampler_voice[track_id].loop_mode = (enabled != 0U) ? BRICK6_SAMPLER_LOOP_FORWARD
                                                          : BRICK6_SAMPLER_LOOP_NONE;
    g_sampler_voice[track_id].play_plan.loop_mode = g_sampler_voice[track_id].loop_mode;
    g_sampler_voice[track_id].reader.plan.loop_mode = g_sampler_voice[track_id].loop_mode;
}

void brick6_sampler_runtime_set_clip_stretch_mode(uint8_t track_id, uint8_t stretch_mode)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_clip(track_id) == 0U))
    {
        return;
    }

    g_sampler_clip_runtime[track_id].stretch_mode = (stretch_mode <= 2U) ? stretch_mode : 0U;
}

void brick6_sampler_runtime_set_clip_grain_size(uint8_t track_id, uint16_t grain_size)
{
    uint16_t sanitized_grain_size;

    if ((track_id >= SEQ_TRACK_COUNT) || (brick6_sampler_runtime_track_is_clip(track_id) == 0U))
    {
        return;
    }

    sanitized_grain_size = brick6_sampler_runtime_clip_sanitize_grain_size(grain_size);
    g_sampler_clip_runtime[track_id].grain_size = sanitized_grain_size;
}

void brick6_sampler_runtime_trigger(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (brick6_sampler_runtime_track_is_clip(track_id) != 0U)
    {
        brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[track_id];
        const uint8_t launch_mode = (clip->play_mode != 0U) ? 1U : 0U;

        switch ((brick6_sampler_clip_state_t)clip->state)
        {
            case BRICK6_SAMPLER_CLIP_STATE_IDLE:
            case BRICK6_SAMPLER_CLIP_STATE_STOPPED:
                clip->state = (brick6_sampler_runtime_clip_start_playback(track_id) != 0U)
                                  ? (uint8_t)BRICK6_SAMPLER_CLIP_STATE_PLAYING
                                  : (uint8_t)BRICK6_SAMPLER_CLIP_STATE_STOPPED;
                break;

            case BRICK6_SAMPLER_CLIP_STATE_PLAYING:
                if (launch_mode == 0U)
                {
                    clip->state = (brick6_sampler_runtime_clip_start_playback(track_id) != 0U)
                                      ? (uint8_t)BRICK6_SAMPLER_CLIP_STATE_PLAYING
                                      : (uint8_t)BRICK6_SAMPLER_CLIP_STATE_STOPPED;
                }
                break;

            default:
                clip->state = (uint8_t)BRICK6_SAMPLER_CLIP_STATE_IDLE;
                break;
        }

        return;
    }

    if (brick6_sampler_runtime_track_is_slicer(track_id) != 0U)
    {
        brick6_sampler_runtime_trigger_slicer(track_id);
        return;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    const float trigger_velocity_gain = brick6_sampler_runtime_velocity_gain(voice->velocity);
    sample_cache_stop_voice(brick6_sampler_runtime_cache_voice_id(track_id));
    brick6_sampler_runtime_build_render_plan(track_id);
    if (voice->sample != NULL)
    {
        voice->position = brick6_sampler_runtime_initial_position(voice->region_begin,
                                                                  voice->region_end,
                                                                  voice->reverse);
        const uint32_t start_frame = (uint32_t)voice->position;
        const uint32_t sample_length_frames =
            (voice->sample != NULL) ? voice->sample->length_frames : 0U;
        voice->play_plan.start_frame = start_frame;
        voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_CLASSIC;
        voice->multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
        voice->multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
        voice->trigger_velocity_gain = trigger_velocity_gain;
        sample_resolved_source_t common_source;
        sample_play_plan_t common_plan;
        const brick6_sample_common_trigger_t common_trigger = {
            .kind = BRICK6_SAMPLE_COMMON_TRIGGER_CLASSIC,
            .voice = voice,
            .runtime_plan = &voice->play_plan,
            .track_id = track_id,
        };
        const brick6_sample_common_plan_result_t common_result =
            brick6_sampler_runtime_build_common_play_plan(&common_trigger,
                                                          &common_source,
                                                          &common_plan);
        brick6_sampler_runtime_note_common_play_plan_result(common_result, 1U);
        if (common_result != BRICK6_SAMPLE_COMMON_PLAN_OK)
        {
            voice->position = 0.0f;
            sample_voice_reader_reset(&voice->reader);
            g_sampler_voice[track_id].active = 0U;
            return;
        }
        if (sample_cache_start_voice_at(voice->sample_id,
                                        brick6_sampler_runtime_cache_voice_id(track_id),
                                        common_plan.start_frame) != 0U)
        {
            const uint8_t bind_ok = sample_voice_reader_bind_play_plan(
                &voice->reader,
                &common_plan,
                brick6_sampler_runtime_cache_voice_id(track_id));
            g_sampler_voice[track_id].active = bind_ok;
            if (bind_ok == 0U)
            {
                sample_cache_stop_voice(brick6_sampler_runtime_cache_voice_id(track_id));
            }
            else
            {
                voice->play_plan = common_plan;
                voice->position = (float)voice->play_plan.start_frame;
                voice->trigger_order = brick6_sampler_runtime_next_trigger_order();
                brick6_sampler_runtime_diag_note_trigger(track_id,
                                                         voice,
                                                         voice->play_plan.start_frame,
                                                         sample_length_frames);
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
    brick6_sampler_runtime_trigger_note_velocity(track_id, note, 127U);
}

void brick6_sampler_runtime_trigger_note_velocity(uint8_t track_id, uint8_t note, uint8_t velocity)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_sampler_voice[track_id].note = note;
    g_sampler_voice[track_id].velocity = velocity;
    brick6_sampler_runtime_trigger(track_id);
}

static uint32_t brick6_sampler_runtime_multi_ready_mask(sample_audio_key_t key,
                                                        uint32_t first_page,
                                                        uint32_t page_count)
{
    uint32_t ready_mask = 0U;

    for (uint32_t i = 0U; i < page_count; ++i)
    {
        if (sample_page_cache_get_page_state_key(key, first_page + i) == SAMPLE_PAGE_READY)
        {
            if (i < BRICK6_SAMPLER_MULTI_WINDOW_MASK_BITS)
            {
                ready_mask |= (1UL << i);
            }
        }
    }

    return ready_mask;
}

static uint32_t brick6_sampler_runtime_multi_first_missing_page(sample_audio_key_t key,
                                                               uint32_t first_page,
                                                               uint32_t page_count)
{
    for (uint32_t i = 0U; i < page_count; ++i)
    {
        const uint32_t page = first_page + i;
        if (sample_page_cache_get_page_state_key(key, page) != SAMPLE_PAGE_READY)
        {
            return page;
        }
    }

    return UINT32_MAX;
}

static void brick6_sampler_runtime_multi_stop_voice(brick6_sampler_voice_t *voice, uint8_t reason)
{
    if (voice == NULL)
    {
        return;
    }

    const uint16_t stopped_multi_sample_id = voice->multi_sample_id;
    brick6_sampler_runtime_multi_release_voice_stream_owner(voice);

    if ((reason != (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_NONE)
        && (voice->active != 0U)
        && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_MULTI))
    {
        brick6_sampler_runtime_multi_diag_note_stop(voice, reason);
    }

    sample_voice_reader_stop(&voice->reader);
    voice->active = 0U;
    voice->position = 0.0f;
    voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    voice->multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->owner_track_id = UINT8_MAX;
    voice->release_pending = 0U;
    sample_stream_manager_active_state_reset(&voice->stream_state);
    voice->trigger_order = 0U;

    brick6_sampler_runtime_multi_defer_stream_release(stopped_multi_sample_id);
}

static void brick6_sampler_runtime_multi_defer_stream_release(uint16_t multi_sample_id)
{
    if (multi_sample_id < MULTI_SAMPLE_POOL_MAX_SAMPLES)
    {
        g_sampler_multi_stream_release_pending[multi_sample_id] = 1U;
    }
}

static void brick6_sampler_runtime_multi_stop_track(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        if (g_sampler_multi_voice[i].owner_track_id == track_id)
        {
            brick6_sampler_runtime_multi_stop_voice(&g_sampler_multi_voice[i],
                                                    (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_NONE);
        }
    }
}

static uint8_t brick6_sampler_runtime_multi_prefetch_voice(brick6_sampler_voice_t *voice)
{
    if ((voice == NULL)
        || (voice->active == 0U)
        || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_MULTI)
        || (voice->multi_sample_id == MULTI_SAMPLE_POOL_INVALID_ID)
        || (voice->region_end == 0U))
    {
        return 0U;
    }

    const uint32_t current_frame = (uint32_t)voice->reader.position;
    if (current_frame >= voice->region_end)
    {
        return 0U;
    }

    const sample_stream_active_desc_t stream_desc = {
        .key = brick6_sampler_runtime_multi_key(voice->multi_sample_id),
        .current_frame = current_frame,
        .end_frame = voice->region_end,
        .direction = 1,
        .lookahead_pages = BRICK6_SAMPLER_MULTI_LOOKAHEAD_PAGES,
        .request_current_page = 1U,
        .owner_kind = (uint8_t)SAMPLE_STREAM_OWNER_MULTI_VOICE,
        .owner_id = brick6_sampler_runtime_multi_voice_index(voice),
        .owner_generation = voice->trigger_order,
        .state = &voice->stream_state,
    };
    const uint8_t requested =
        sample_stream_manager_queue_active_pages(&stream_desc);
    if (requested != 0U)
    {
        g_brick6_sampler_runtime_diag.multi_prefetch_request_count++;
    }
    return requested;
}

static void brick6_sampler_runtime_multi_prefetch_trigger(brick6_sampler_voice_t *voice)
{
    if ((voice == NULL)
        || (voice->active == 0U)
        || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_MULTI)
        || (voice->multi_sample_id == MULTI_SAMPLE_POOL_INVALID_ID)
        || (voice->region_end == 0U))
    {
        return;
    }

    const sample_stream_active_desc_t stream_desc = {
        .key = brick6_sampler_runtime_multi_key(voice->multi_sample_id),
        .current_frame = 0U,
        .end_frame = voice->region_end,
        .direction = 1,
        .lookahead_pages = BRICK6_SAMPLER_MULTI_LOOKAHEAD_PAGES,
        .request_current_page = 1U,
        .owner_kind = (uint8_t)SAMPLE_STREAM_OWNER_MULTI_VOICE,
        .owner_id = brick6_sampler_runtime_multi_voice_index(voice),
        .owner_generation = voice->trigger_order,
        .state = &voice->stream_state,
    };
    if (sample_stream_manager_queue_active_pages(&stream_desc) != 0U)
    {
        g_brick6_sampler_runtime_diag.multi_prefetch_request_count++;
    }
}

static void brick6_sampler_runtime_multi_service_streaming(void)
{
    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        (void)brick6_sampler_runtime_multi_prefetch_voice(&g_sampler_multi_voice[i]);
    }
}

void brick6_sampler_runtime_queue_stream_pages(void)
{
    brick6_sampler_runtime_multi_service_streaming();
}

static uint8_t brick6_sampler_runtime_multi_sample_has_active_voice(uint16_t multi_sample_id)
{
    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        const brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
        if ((voice->active != 0U)
            && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_MULTI)
            && (voice->multi_sample_id == multi_sample_id))
        {
            return 1U;
        }
    }

    return 0U;
}

static void brick6_sampler_runtime_multi_service_stream_releases(void)
{
    if (__get_IPSR() != 0U)
    {
        return;
    }

    for (uint16_t multi_sample_id = 0U;
         multi_sample_id < MULTI_SAMPLE_POOL_MAX_SAMPLES;
         ++multi_sample_id)
    {
        if (g_sampler_multi_stream_release_pending[multi_sample_id] == 0U)
        {
            continue;
        }

        if (brick6_sampler_runtime_multi_sample_has_active_voice(multi_sample_id) != 0U)
        {
            continue;
        }

        sample_stream_manager_release_key(brick6_sampler_runtime_multi_key(multi_sample_id));
        g_sampler_multi_stream_release_pending[multi_sample_id] = 0U;
    }
}

void brick6_sampler_runtime_service(void)
{
    brick6_sampler_runtime_multi_service_stream_releases();
    brick6_sampler_runtime_multi_service_streaming();
}

static uint32_t brick6_sampler_runtime_multi_active_count(void)
{
    uint32_t active = 0U;
    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        const brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
        if ((voice->active != 0U)
            && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_MULTI))
        {
            active++;
        }
    }
    return active;
}

static uint32_t brick6_sampler_runtime_multi_active_count_for_track(uint8_t track_id)
{
    uint32_t active = 0U;
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        const brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
        if ((voice->active != 0U)
            && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_MULTI)
            && (voice->owner_track_id == track_id))
        {
            active++;
        }
    }
    return active;
}

static uint32_t brick6_sampler_runtime_global_volable_voice_count(void)
{
    uint32_t active = brick6_sampler_runtime_multi_active_count();
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        if (brick6_sampler_runtime_oneshot_voice_is_stealable(track) != 0U)
        {
            active++;
        }
    }
    return active;
}

static brick6_sampler_voice_t *brick6_sampler_runtime_multi_oldest_track(uint8_t track_id)
{
    brick6_sampler_voice_t *oldest = NULL;
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return NULL;
    }

    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
        if ((voice->active == 0U)
            || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_MULTI))
        {
            continue;
        }
        if (voice->owner_track_id != track_id)
        {
            continue;
        }
        if ((oldest == NULL) || (voice->trigger_order < oldest->trigger_order))
        {
            oldest = voice;
        }
    }
    return oldest;
}

static brick6_sampler_voice_t *brick6_sampler_runtime_multi_oldest_global(void)
{
    brick6_sampler_voice_t *oldest = NULL;
    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
        if ((voice->active == 0U)
            || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_MULTI))
        {
            continue;
        }
        if ((oldest == NULL) || (voice->trigger_order < oldest->trigger_order))
        {
            oldest = voice;
        }
    }
    return oldest;
}

static uint8_t brick6_sampler_runtime_oneshot_voice_is_stealable(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    const brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    return ((voice->active != 0U)
            && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_CLASSIC)
            && (brick6_sampler_runtime_track_is_slicer(track_id) == 0U)
            && (brick6_sampler_runtime_track_is_clip(track_id) == 0U))
               ? 1U
               : 0U;
}

static uint8_t brick6_sampler_runtime_steal_oldest_oneshot(void)
{
    uint8_t found = 0U;
    uint8_t oldest_track = 0U;
    uint32_t oldest_order = UINT32_MAX;

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        if (brick6_sampler_runtime_oneshot_voice_is_stealable(track) == 0U)
        {
            continue;
        }
        if ((found == 0U) || (g_sampler_voice[track].trigger_order < oldest_order))
        {
            found = 1U;
            oldest_track = track;
            oldest_order = g_sampler_voice[track].trigger_order;
        }
    }

    if (found == 0U)
    {
        return 0U;
    }

    g_brick6_sampler_runtime_diag.multi_last_stolen_kind =
        g_sampler_voice[oldest_track].source_kind;
    g_brick6_sampler_runtime_diag.multi_last_stolen_track = oldest_track;
    brick6_sampler_runtime_stop(oldest_track);
    return 1U;
}

static brick6_sampler_voice_t *brick6_sampler_runtime_multi_alloc_voice(uint8_t track_id)
{
    g_sampler_multi_alloc_reject_reason =
        (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_NONE;
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return NULL;
    }

    if (brick6_sampler_runtime_multi_active_count_for_track(track_id)
        >= SAMPLER_MULTI_MAX_VOICES_PER_TRACK)
    {
        brick6_sampler_voice_t *const same_track =
            brick6_sampler_runtime_multi_oldest_track(track_id);
        if (same_track != NULL)
        {
            g_brick6_sampler_runtime_diag.multi_voice_stolen_same_track++;
            g_brick6_sampler_runtime_diag.multi_last_stolen_kind =
                same_track->source_kind;
            g_brick6_sampler_runtime_diag.multi_last_stolen_track = track_id;
            brick6_sampler_runtime_multi_diag_note_steal(
                track_id,
                same_track,
                (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STEAL_SAME_TRACK);
            brick6_sampler_runtime_multi_stop_voice(
                same_track,
                (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_STEAL);
            return same_track;
        }

        g_brick6_sampler_runtime_diag.multi_voice_rejected_no_voice++;
        g_sampler_multi_alloc_reject_reason =
            (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_TRACK_VOICE_LIMIT;
        return NULL;
    }

    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
        if (voice->active == 0U)
        {
            if (brick6_sampler_runtime_global_volable_voice_count()
                < SAMPLER_MULTI_MAX_GLOBAL_VOICES)
            {
                return voice;
            }

            brick6_sampler_voice_t *const global =
                brick6_sampler_runtime_multi_oldest_global();
            if (global != NULL)
            {
                g_brick6_sampler_runtime_diag.multi_voice_stolen_global++;
                g_brick6_sampler_runtime_diag.multi_last_stolen_kind =
                    global->source_kind;
                g_brick6_sampler_runtime_diag.multi_last_stolen_track =
                    global->owner_track_id;
                brick6_sampler_runtime_multi_diag_note_steal(
                    track_id,
                    global,
                    (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_GLOBAL_VOICE_LIMIT);
                brick6_sampler_runtime_multi_stop_voice(
                    global,
                    (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_STEAL);
                return voice;
            }

            if (brick6_sampler_runtime_steal_oldest_oneshot() != 0U)
            {
                g_brick6_sampler_runtime_diag.multi_voice_stolen_global++;
                return voice;
            }

            g_brick6_sampler_runtime_diag.multi_voice_rejected_no_voice++;
            g_sampler_multi_alloc_reject_reason =
                (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_GLOBAL_VOICE_LIMIT;
            return NULL;
        }
    }

    brick6_sampler_voice_t *const global = brick6_sampler_runtime_multi_oldest_global();
    if (global != NULL)
    {
        g_brick6_sampler_runtime_diag.multi_voice_stolen_global++;
        g_brick6_sampler_runtime_diag.multi_last_stolen_kind = global->source_kind;
        g_brick6_sampler_runtime_diag.multi_last_stolen_track =
            global->owner_track_id;
        brick6_sampler_runtime_multi_diag_note_steal(
            track_id,
            global,
            (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_GLOBAL_VOICE_LIMIT);
        brick6_sampler_runtime_multi_stop_voice(
            global,
            (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_STEAL);
        return global;
    }

    g_brick6_sampler_runtime_diag.multi_voice_rejected_no_voice++;
    g_sampler_multi_alloc_reject_reason =
        (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_GLOBAL_VOICE_LIMIT;
    return NULL;
}

uint8_t brick6_sampler_runtime_trigger_multi_note_velocity(uint8_t track_id,
                                                           uint16_t instrument_id,
                                                           uint8_t note,
                                                           uint8_t velocity,
                                                           float gain)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (note > 127U) || (velocity > 127U)
        || (brick6_sampler_runtime_track_is_clip(track_id) != 0U))
    {
        return 0U;
    }

    if (velocity == 0U)
    {
        brick6_sampler_runtime_note_off_multi_track_note(track_id, note);
        return 1U;
    }

    if (instrument_id == MULTI_SAMPLE_POOL_INVALID_ID)
    {
        instrument_id = g_sampler_multi_track_state[track_id].instrument_id;
        if (instrument_id == MULTI_SAMPLE_POOL_INVALID_ID)
        {
            g_brick6_sampler_runtime_diag.multi_no_instrument_assigned++;
            g_brick6_sampler_runtime_diag.multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
            brick6_sampler_runtime_multi_diag_note_reject(
                track_id,
                note,
                velocity,
                MULTI_SAMPLE_POOL_INVALID_ID,
                MULTI_SAMPLE_POOL_INVALID_ID,
                (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_NO_INSTRUMENT);
            return 0U;
        }
    }
    if (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
    {
        g_brick6_sampler_runtime_diag.multi_invalid_instrument_id++;
        g_brick6_sampler_runtime_diag.multi_instrument_id = instrument_id;
        brick6_sampler_runtime_multi_diag_note_reject(
            track_id,
            note,
            velocity,
            instrument_id,
            MULTI_SAMPLE_POOL_INVALID_ID,
            (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_INVALID_SAMPLE);
        return 0U;
    }

    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track_id);
    if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
        || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER))
    {
        return 0U;
    }

    if (multi_sample_is_ready(instrument_id) == 0U)
    {
        g_brick6_sampler_runtime_diag.multi_note_on_rejected_not_ready++;
        g_brick6_sampler_runtime_diag.multi_instrument_not_ready++;
        g_brick6_sampler_runtime_diag.multi_instrument_id = instrument_id;
        brick6_sampler_runtime_multi_diag_note_reject(
            track_id,
            note,
            velocity,
            instrument_id,
            MULTI_SAMPLE_POOL_INVALID_ID,
            (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_INSTRUMENT_NOT_READY);
        return 0U;
    }

    multi_sample_resolve_result_t resolved;
    if (multi_sample_pool_resolve(instrument_id, note, velocity, &resolved) == 0U)
    {
        g_brick6_sampler_runtime_diag.multi_resolve_fail++;
        g_brick6_sampler_runtime_diag.multi_instrument_id = instrument_id;
        g_brick6_sampler_runtime_diag.note = note;
        g_brick6_sampler_runtime_diag.velocity = velocity;
        brick6_sampler_runtime_multi_diag_note_reject(
            track_id,
            note,
            velocity,
            instrument_id,
            MULTI_SAMPLE_POOL_INVALID_ID,
            (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_NO_ZONE);
        return 0U;
    }

    const multi_sample_desc_t *const sample =
        multi_sample_pool_get_sample(resolved.multi_sample_id);
    if ((sample == 0) || (sample->total_frames == 0U))
    {
        g_brick6_sampler_runtime_diag.multi_resolve_fail++;
        brick6_sampler_runtime_multi_diag_note_reject(
            track_id,
            note,
            velocity,
            instrument_id,
            resolved.multi_sample_id,
            (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_INVALID_SAMPLE);
        return 0U;
    }

    const sample_audio_key_t key = brick6_sampler_runtime_multi_key(resolved.multi_sample_id);
    const uint32_t required_pages = 1U;
    g_brick6_sampler_runtime_diag.multi_last_ready_mask =
        brick6_sampler_runtime_multi_ready_mask(key, 0U, required_pages);
    g_brick6_sampler_runtime_diag.multi_last_missing_page =
        brick6_sampler_runtime_multi_first_missing_page(key, 0U, required_pages);
    const sample_page_state_t state0 = sample_page_cache_get_page_state_key(key, 0U);
    if (state0 != SAMPLE_PAGE_READY)
    {
        g_brick6_sampler_runtime_diag.multi_page0_missing++;
        g_brick6_sampler_runtime_diag.multi_instrument_id = instrument_id;
        g_brick6_sampler_runtime_diag.multi_sample_id = resolved.multi_sample_id;
        brick6_sampler_runtime_multi_diag_note_page0_reject(track_id,
                                                            note,
                                                            velocity,
                                                            instrument_id,
                                                            &resolved,
                                                            sample,
                                                            state0);
        return 0U;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    if ((voice->active != 0U)
        && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_CLIP))
    {
        return 0U;
    }

    if (voice->active != 0U)
    {
        g_brick6_sampler_runtime_diag.multi_last_stolen_kind = voice->source_kind;
        g_brick6_sampler_runtime_diag.multi_last_stolen_track = track_id;
        sample_voice_reader_stop(&voice->reader);
        voice->active = 0U;
        voice->position = 0.0f;
        voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    }

    brick6_sampler_voice_t *const multi_voice =
        brick6_sampler_runtime_multi_alloc_voice(track_id);
    if (multi_voice == NULL)
    {
        uint8_t reason = g_sampler_multi_alloc_reject_reason;
        if (reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_NONE)
        {
            reason = (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_GLOBAL_VOICE_LIMIT;
        }
        brick6_sampler_runtime_multi_diag_note_reject(track_id,
                                                      note,
                                                      velocity,
                                                      instrument_id,
                                                      resolved.multi_sample_id,
                                                      reason);
        return 0U;
    }

    sample_play_plan_t play_plan;
    memset(&play_plan, 0, sizeof(play_plan));
    play_plan.key = key;
    play_plan.sample_id = resolved.multi_sample_id;
    play_plan.start_frame = 0U;
    play_plan.region_begin = 0U;
    play_plan.region_end = sample->total_frames;
    play_plan.loop_begin = 0U;
    play_plan.loop_end = sample->total_frames;
    const float ratio = powf(2.0f, (float)resolved.pitch_semitones * (1.0f / 12.0f));
    play_plan.step_q16 = brick6_sampler_runtime_ratio_to_q16(ratio);
    play_plan.direction = 0U;
    play_plan.loop_mode = BRICK6_SAMPLER_LOOP_NONE;
    play_plan.stop_on_underrun = 1U;
    play_plan.kernel_type = (play_plan.step_q16 == BRICK6_SAMPLER_Q16_ONE)
                                ? SAMPLE_KERNEL_FWD_1X
                                : SAMPLE_KERNEL_PITCH_FWD_LINEAR;
    const float multi_trigger_velocity_gain =
        (resolved.zone_is_single_velocity_layer != 0U)
            ? brick6_sampler_runtime_velocity_gain(velocity)
            : 1.0f;
    const float expected_render_gain =
        g_sampler_multi_track_state[track_id].gain * gain * multi_trigger_velocity_gain;
    sample_resolved_source_t common_source;
    sample_play_plan_t common_plan;
    const brick6_sample_common_trigger_t common_trigger = {
        .kind = BRICK6_SAMPLE_COMMON_TRIGGER_MULTI,
        .runtime_plan = &play_plan,
        .total_frames = sample->total_frames,
        .step_q16 = play_plan.step_q16,
        .render_gain = expected_render_gain,
        .instrument_id = instrument_id,
        .track_id = track_id,
        .note = note,
        .velocity = velocity,
    };
    const brick6_sample_common_plan_result_t common_result =
        brick6_sampler_runtime_build_common_play_plan(&common_trigger,
                                                      &common_source,
                                                      &common_plan);
    brick6_sampler_runtime_note_common_play_plan_result(common_result, 0U);
    if (common_result != BRICK6_SAMPLE_COMMON_PLAN_OK)
    {
        return 0U;
    }

    sample_voice_reader_reset(&multi_voice->reader);
    if (sample_voice_reader_bind_play_plan(&multi_voice->reader,
                                           &common_plan,
                                           BRICK6_SAMPLER_CACHE_VOICE_NONE) == 0U)
    {
        sample_voice_reader_reset(&multi_voice->reader);
        g_brick6_sampler_runtime_diag.multi_page0_missing++;
        brick6_sampler_runtime_multi_diag_note_page0_reject(
            track_id,
            note,
            velocity,
            instrument_id,
            &resolved,
            sample,
            sample_page_cache_get_page_state_key(key, 0U));
        return 0U;
    }

    multi_voice->trigger_order = brick6_sampler_runtime_next_trigger_order();
    sample_stream_manager_active_state_reset(&multi_voice->stream_state);
    const sample_stream_active_desc_t reserve_desc = {
        .key = common_plan.key,
        .current_frame = common_plan.start_frame,
        .end_frame = common_plan.region_end,
        .direction = (common_plan.direction != 0U) ? -1 : 1,
        .lookahead_pages = BRICK6_SAMPLER_MULTI_LOOKAHEAD_PAGES,
        .request_current_page = 1U,
        .owner_kind = (uint8_t)SAMPLE_STREAM_OWNER_MULTI_VOICE,
        .owner_id = brick6_sampler_runtime_multi_voice_index(multi_voice),
        .owner_generation = multi_voice->trigger_order,
        .state = &multi_voice->stream_state,
    };
    if (sample_stream_manager_reserve_active_pages(&reserve_desc) == 0U)
    {
        g_brick6_sampler_runtime_diag.multi_page_window_missing++;
        brick6_sampler_runtime_multi_release_voice_stream_owner(multi_voice);
        sample_voice_reader_reset(&multi_voice->reader);
        multi_voice->trigger_order = 0U;
        return 0U;
    }

    multi_voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_MULTI;
    multi_voice->sample_id = resolved.multi_sample_id;
    multi_voice->multi_instrument_id = instrument_id;
    multi_voice->multi_sample_id = resolved.multi_sample_id;
    multi_voice->owner_track_id = track_id;
    multi_voice->sample = NULL;
    multi_voice->position = (float)common_plan.start_frame;
    multi_voice->active = 1U;
    multi_voice->note = note;
    multi_voice->velocity = velocity;
    multi_voice->mode = 0U;
    multi_voice->gain = g_sampler_multi_track_state[track_id].gain * gain;
    multi_voice->trigger_velocity_gain =
        multi_trigger_velocity_gain;
    multi_voice->region_begin = common_plan.region_begin;
    multi_voice->region_end = common_plan.region_end;
    multi_voice->loop_frames = common_plan.region_end - common_plan.region_begin;
    multi_voice->fade_in_frames = common_plan.fade_in_frames;
    multi_voice->fade_out_frames = common_plan.fade_out_frames;
    multi_voice->step_signed = (float)common_plan.step_q16 / (float)BRICK6_SAMPLER_Q16_ONE;
    multi_voice->reverse = common_plan.direction;
    multi_voice->loop_mode = common_plan.loop_mode;
    multi_voice->use_slice = 0U;
    multi_voice->use_segment_cursor = 1U;
    multi_voice->release_pending = 0U;
    multi_voice->play_plan = common_plan;
    brick6_sampler_runtime_multi_prefetch_trigger(multi_voice);

    g_brick6_sampler_runtime_diag.multi_instrument_id = instrument_id;
    g_brick6_sampler_runtime_diag.multi_sample_id = resolved.multi_sample_id;
    g_brick6_sampler_runtime_diag.multi_gain_applied =
        (g_sampler_multi_track_state[track_id].gain != 1.0f) ? 1U : 0U;
    g_brick6_sampler_runtime_diag.multi_voice_started++;
    brick6_sampler_runtime_diag_note_trigger(track_id, multi_voice, 0U, sample->total_frames);
    brick6_sampler_runtime_multi_diag_note_trigger(multi_voice);
    return 1U;
}

uint8_t brick6_sampler_runtime_trigger_multi_track_note_velocity(uint8_t track_id,
                                                                 uint8_t note,
                                                                 uint8_t velocity)
{
    return brick6_sampler_runtime_trigger_multi_note_velocity(track_id,
                                                             MULTI_SAMPLE_POOL_INVALID_ID,
                                                             note,
                                                             velocity,
                                                             1.0f);
}

void brick6_sampler_runtime_note_off_multi_track_note(uint8_t track_id, uint8_t note)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (note > 127U))
    {
        return;
    }

    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
        if ((voice->active == 0U)
            || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_MULTI)
            || (voice->owner_track_id != track_id)
            || (voice->note != note))
        {
            continue;
        }

        voice->release_pending = 1U;
    }
}

void brick6_sampler_runtime_note_off(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (brick6_sampler_runtime_track_is_clip(track_id) != 0U)
    {
        if (g_sampler_clip_runtime[track_id].play_mode != 0U)
        {
            return;
        }

        brick6_sampler_runtime_clip_stop_playback(track_id);
        g_sampler_clip_runtime[track_id].state = (uint8_t)BRICK6_SAMPLER_CLIP_STATE_IDLE;
        return;
    }

    brick6_sampler_runtime_stop(track_id);
}

void brick6_sampler_runtime_note_off_note(uint8_t track_id, uint8_t note)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (brick6_sampler_runtime_track_is_clip(track_id) != 0U)
    {
        brick6_sampler_runtime_note_off(track_id);
        return;
    }

    const brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    if ((voice->active == 0U) || (voice->note != note))
    {
        return;
    }

    brick6_sampler_runtime_stop(track_id);
}

void brick6_sampler_runtime_stop(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (brick6_sampler_runtime_track_is_clip(track_id) != 0U)
    {
        brick6_sampler_runtime_clip_stop_playback(track_id);
        g_sampler_clip_runtime[track_id].state = (uint8_t)BRICK6_SAMPLER_CLIP_STATE_IDLE;
        return;
    }

    g_sampler_voice[track_id].active = 0U;
    g_sampler_voice[track_id].position = 0.0f;
    g_sampler_voice[track_id].source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    g_sampler_voice[track_id].multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    g_sampler_voice[track_id].multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    sample_voice_reader_stop(&g_sampler_voice[track_id].reader);
    brick6_sampler_runtime_multi_stop_track(track_id);
}

void brick6_sampler_runtime_stop_transport_clips(void)
{
    for (uint8_t track_id = 0U; track_id < SEQ_TRACK_COUNT; ++track_id)
    {
        brick6_sampler_runtime_stop(track_id);
    }
}

void brick6_sampler_runtime_diag_reset(void)
{
    memset(&g_brick6_sampler_runtime_diag, 0, sizeof(g_brick6_sampler_runtime_diag));
    memset(g_sampler_multi_stream_release_pending,
           0,
           sizeof(g_sampler_multi_stream_release_pending));
    memset(g_sampler_multi_page0_reject_logged,
           0,
           sizeof(g_sampler_multi_page0_reject_logged));
}

void brick6_sampler_runtime_diag_get_snapshot(brick6_sampler_runtime_diag_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL)
    {
        return;
    }

    *out_snapshot = g_brick6_sampler_runtime_diag;
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

    const float render_gain = voice->gain * voice->trigger_velocity_gain;

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
                sample_voice_reader_mix_rev_1x(&segment, render_gain, 0, 0U, out_l, out_r, produced);
            }
            else if (segment.kernel_type == SAMPLE_KERNEL_PITCH_FWD_LINEAR)
            {
                sample_voice_reader_mix_pitch_fwd_linear(&segment, render_gain, 0, 0U, out_l, out_r, produced);
            }
            else if (segment.kernel_type == SAMPLE_KERNEL_PITCH_REV_LINEAR)
            {
                sample_voice_reader_mix_pitch_rev_linear(&segment, render_gain, 0, 0U, out_l, out_r, produced);
            }
            else
            {
                sample_voice_reader_mix_fwd_1x(&segment, render_gain, 0, 0U, out_l, out_r, produced);
            }
        }
        else
        {
            float fade_buf[AUDIO_BLOCK_SIZE];
            for (uint32_t i = 0U; i < segment.frames; ++i)
            {
                uint32_t loop_pos = 0U;
                if (segment.kernel_type == SAMPLE_KERNEL_REV_1X)
                {
                    loop_pos = (segment.start_frame >= (voice->region_begin + i))
                                   ? (segment.start_frame - voice->region_begin - i)
                                   : 0U;
                }
                else if (segment.kernel_type == SAMPLE_KERNEL_PITCH_FWD_LINEAR)
                {
                    const float source_frame = segment.source_position + ((float)i * segment.source_step);
                    const uint32_t base_frame = (uint32_t)source_frame;
                    loop_pos = (base_frame > voice->region_begin) ? (base_frame - voice->region_begin) : 0U;
                }
                else if (segment.kernel_type == SAMPLE_KERNEL_PITCH_REV_LINEAR)
                {
                    const float source_frame = segment.source_position - ((float)i * segment.source_step);
                    const uint32_t base_frame = (uint32_t)source_frame;
                    loop_pos = ((voice->region_end > 0U) && (base_frame < voice->region_end))
                                   ? ((voice->region_end - 1U) - base_frame)
                                   : 0U;
                }
                else
                {
                    loop_pos = (segment.start_frame + i) - voice->region_begin;
                }
                fade_buf[i] = brick6_sampler_runtime_fade_gain(loop_pos,
                                                               voice->loop_frames,
                                                               voice->fade_in_frames,
                                                               voice->fade_out_frames);
            }
            if (segment.kernel_type == SAMPLE_KERNEL_REV_1X)
            {
                sample_voice_reader_mix_rev_1x(&segment,
                                               render_gain,
                                               fade_buf,
                                               segment.frames,
                                               out_l,
                                               out_r,
                                               produced);
            }
            else if (segment.kernel_type == SAMPLE_KERNEL_PITCH_FWD_LINEAR)
            {
                sample_voice_reader_mix_pitch_fwd_linear(&segment,
                                                         render_gain,
                                                         fade_buf,
                                                         segment.frames,
                                                         out_l,
                                                         out_r,
                                                         produced);
            }
            else if (segment.kernel_type == SAMPLE_KERNEL_PITCH_REV_LINEAR)
            {
                sample_voice_reader_mix_pitch_rev_linear(&segment,
                                                         render_gain,
                                                         fade_buf,
                                                         segment.frames,
                                                         out_l,
                                                         out_r,
                                                         produced);
            }
            else
            {
                sample_voice_reader_mix_fwd_1x(&segment,
                                               render_gain,
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

static void brick6_sampler_render_multi(brick6_sampler_voice_t *voice,
                                        float *out_l,
                                        float *out_r,
                                        uint32_t frames)
{
    if ((voice == NULL) || (out_l == NULL) || (out_r == NULL) || (frames == 0U)
        || (voice->active == 0U)
        || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_MULTI))
    {
        return;
    }

    if (voice->release_pending != 0U)
    {
        uint8_t mix_track = 0U;
        if ((track_runtime_get_mix_target_track(voice->owner_track_id, &mix_track) != 0U)
            && (mixer_track_vca_is_running(mix_track) == 0U))
        {
            brick6_sampler_runtime_multi_stop_voice(
                voice,
                (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_REL_DONE);
            return;
        }
    }

    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    uint32_t produced = 0U;
    while (produced < frames)
    {
        sample_audio_segment_t segment;
        if (sample_voice_reader_begin_segment(&voice->reader, frames - produced, &segment) == 0U)
        {
            g_brick6_sampler_runtime_diag.multi_page_underrun++;
            brick6_sampler_runtime_multi_diag_note_stop(
                voice,
                (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_UNDERRUN);
            brick6_sampler_runtime_multi_release_voice_stream_owner(voice);
            brick6_sampler_runtime_multi_defer_stream_release(voice->multi_sample_id);
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }

        if (segment.status == SAMPLE_AUDIO_SEGMENT_DONE)
        {
            const uint8_t stop_reason =
                (voice->reader.position >= (float)voice->region_end)
                    ? (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_DONE
                    : (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_UNDERRUN;
            if (stop_reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_UNDERRUN)
            {
                g_brick6_sampler_runtime_diag.multi_page_underrun++;
            }
            brick6_sampler_runtime_multi_diag_note_stop(voice, stop_reason);
            brick6_sampler_runtime_multi_release_voice_stream_owner(voice);
            brick6_sampler_runtime_multi_defer_stream_release(voice->multi_sample_id);
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }

        if (segment.status != SAMPLE_AUDIO_SEGMENT_OK)
        {
            if (segment.status == SAMPLE_AUDIO_SEGMENT_UNDERRUN)
            {
                g_brick6_sampler_runtime_diag.multi_page_underrun++;
            }
            brick6_sampler_runtime_multi_diag_note_stop(
                voice,
                (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_UNDERRUN);
            brick6_sampler_runtime_multi_release_voice_stream_owner(voice);
            brick6_sampler_runtime_multi_defer_stream_release(voice->multi_sample_id);
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }

        if (segment.frames == 0U)
        {
            g_brick6_sampler_runtime_diag.multi_page_underrun++;
            brick6_sampler_runtime_multi_diag_note_stop(
                voice,
                (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_UNDERRUN);
            brick6_sampler_runtime_multi_release_voice_stream_owner(voice);
            brick6_sampler_runtime_multi_defer_stream_release(voice->multi_sample_id);
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }

        BRICK6_SAMPLER_RUNTIME_DIAG_INC(segment_cursor_blocks);
        BRICK6_SAMPLER_RUNTIME_DIAG_INC(mixed_segments);

        if (segment.kernel_type == SAMPLE_KERNEL_PITCH_FWD_LINEAR)
        {
            sample_voice_reader_mix_pitch_fwd_linear(&segment,
                                                     render_gain,
                                                     0,
                                                     0U,
                                                     out_l,
                                                     out_r,
                                                     produced);
        }
        else
        {
            sample_voice_reader_mix_fwd_1x(&segment,
                                           render_gain,
                                           0,
                                           0U,
                                           out_l,
                                           out_r,
                                           produced);
        }

        sample_voice_reader_commit_segment(&voice->reader, segment.frames);
        produced += segment.frames;
        voice->position = voice->reader.position;
        if (voice->reader.active == 0U)
        {
            const uint8_t stop_reason =
                (voice->reader.position >= (float)voice->region_end)
                    ? (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_DONE
                    : (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_UNDERRUN;
            if (stop_reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_UNDERRUN)
            {
                g_brick6_sampler_runtime_diag.multi_page_underrun++;
            }
            brick6_sampler_runtime_multi_diag_note_stop(voice, stop_reason);
            brick6_sampler_runtime_multi_release_voice_stream_owner(voice);
            brick6_sampler_runtime_multi_defer_stream_release(voice->multi_sample_id);
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }
    }
}

static void brick6_sampler_runtime_clip_render_shifter(brick6_sampler_voice_t *voice,
                                                       brick6_sampler_clip_runtime_t *clip,
                                                       brick6_sampler_clip_slot_t *slot,
                                                       float *out_l,
                                                       float *out_r,
                                                       uint32_t frames)
{
    if ((voice == NULL) || (clip == NULL) || (slot == NULL) || (out_l == NULL) || (out_r == NULL) || (frames == 0U))
    {
        return;
    }

    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    memset(slot->stretch_render_l, 0, sizeof(slot->stretch_render_l));
    memset(slot->stretch_render_r, 0, sizeof(slot->stretch_render_r));

    brick6_sampler_runtime_clip_configure_shifter(clip, slot);
    brick6_sampler_render_sample_segment_cursor(voice,
                                                slot->stretch_render_l,
                                                slot->stretch_render_r,
                                                frames);
    brick6_clip_shifter_process_stereo(&slot->shifter,
                                       slot->stretch_render_l,
                                       slot->stretch_render_r,
                                       frames);

    for (uint32_t i = 0U; i < frames; ++i)
    {
        out_l[i] += slot->stretch_render_l[i];
        out_r[i] += slot->stretch_render_r[i];
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

    const float render_gain = voice->gain * voice->trigger_velocity_gain;

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
                                                            render_gain,
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
            const float sample_gain = render_gain;
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
                const float sample_gain = render_gain * fade_gain;
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

    if ((ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER))
    {
        return;
    }

    if ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_CLIP)
    {
        brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[ctx->track_id];
        brick6_sampler_clip_slot_t *const slot = brick6_sampler_runtime_clip_get_slot(ctx->track_id);
        brick6_sampler_voice_t *const voice = &g_sampler_voice[ctx->track_id];

        if (clip->state != (uint8_t)BRICK6_SAMPLER_CLIP_STATE_PLAYING)
        {
            voice->active = 0U;
            voice->position = 0.0f;
            return;
        }

        if ((voice->sample == NULL) || (voice->active == 0U))
        {
            clip->state = (uint8_t)BRICK6_SAMPLER_CLIP_STATE_STOPPED;
            return;
        }

        if (clip->use_shifter_engine != 0U)
        {
            if (slot == NULL)
            {
                clip->use_shifter_engine = 0U;
                brick6_sampler_render_sample(voice->sample, voice, out_l, out_r, frames);
            }
            else
            {
                brick6_sampler_runtime_clip_render_shifter(voice, clip, slot, out_l, out_r, frames);
            }
        }
        else
        {
            brick6_sampler_render_sample(voice->sample, voice, out_l, out_r, frames);
        }
        if (voice->active == 0U)
        {
            clip->state = (uint8_t)BRICK6_SAMPLER_CLIP_STATE_STOPPED;
        }
        return;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[ctx->track_id];
    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        brick6_sampler_voice_t *const multi_voice = &g_sampler_multi_voice[i];
        if ((multi_voice->active != 0U)
            && (multi_voice->owner_track_id == ctx->track_id))
        {
            brick6_sampler_render_multi(multi_voice, out_l, out_r, frames);
        }
    }

    if ((voice->active == 0U) || (voice->sample_id >= SAMPLE_POOL_SIZE))
    {
        brick6_sampler_runtime_diag_note_first_output(ctx->track_id, out_l, out_r, frames);
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
    brick6_sampler_runtime_diag_note_first_output(ctx->track_id, out_l, out_r, frames);
}
