/**
 * @file brick6_sampler_runtime.c
 * @brief Track-aware Sampler backend for RAM, Stream, Multi and RAM grid slicing.
 */

#include "Core/brick6_sampler_runtime.h"

#include <math.h>
#include <string.h>

#include "Audio/audio_float.h"
#include "Audio/mixer.h"
#include "Board/board_audio_format.h"
#include "Core/brick6_clip_shifter.h"
#include "Storage/memory_layout.h"
#include "Sampler/multi_sample_loader.h"
#include "Sampler/multi_sample_pool.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_pool.h"
#include "Sampler/sample_stream_manager.h"
#include "Sampler/sample_voice_reader.h"
#include "Sampler/sampler_ram_pool.h"
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
#define STEAL_DECLICK_SAMPLES (16U)
#define STEAL_DECLICK_TAIL_SLOTS (32U)
#define STEAL_DECLICK_EPSILON (0.0000001f)
#define BRICK6_SAMPLER_RAM_OUTPUT_SAMPLE_RATE_HZ (48000U)
#define BRICK6_SAMPLER_Q16_FRAC_MASK (BRICK6_SAMPLER_Q16_ONE - 1U)

typedef struct
{
    uint8_t source_kind;
    uint16_t sample_id;
    uint16_t multi_instrument_id;
    uint16_t multi_sample_id;
    uint16_t ram_slot;
    uint32_t ram_generation;
    uint8_t owner_track_id;
    const sample_desc_t *sample;
    const float *ram_data;
    float position;
    uint64_t ram_position_q16;
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
    float loop_start;
    float tune;
    uint32_t region_begin;
    uint32_t region_end;
    uint32_t loop_begin;
    uint32_t loop_frames;
    uint32_t fade_in_frames;
    uint32_t fade_out_frames;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    float step_signed;
    uint32_t ram_step_q16;
    uint8_t reverse;
    uint8_t loop_mode;
    uint8_t use_slice;
    uint8_t use_segment_cursor;
    uint8_t release_pending;
    uint8_t stream_owner_release_pending;
    uint16_t stream_owner_release_sample_id;
    sample_play_plan_t play_plan;
    sample_voice_reader_t reader;
    uint32_t trigger_order;
    uint32_t stream_owner_release_generation;
    sample_stream_active_state_t stream_state;
    sample_stream_active_state_t loop_stream_state;
    uint32_t pinned_first_page;
    uint32_t pinned_last_page;
    float last_out_l;
    float last_out_r;
    uint8_t last_out_valid;
    uint8_t start_fade_remaining;
    uint16_t ram_channels;
    sampler_ram_format_t ram_format;
    uint32_t slice_grid_generation;
    uint32_t slice_begin[64U];
    uint32_t slice_end[64U];
} brick6_sampler_voice_t;

typedef enum
{
    BRICK6_SAMPLER_VOICE_NONE = 0,
    BRICK6_SAMPLER_VOICE_CLASSIC,
    BRICK6_SAMPLER_VOICE_CLIP,
    BRICK6_SAMPLER_VOICE_MULTI,
    BRICK6_SAMPLER_VOICE_RAM
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
    float shifter_correction;
} brick6_sampler_clip_runtime_t;

typedef struct
{
    int16_t note_delta;
    uint32_t timing_ratio_q16;
    uint32_t pitch_ratio_q16;
    uint32_t step_q16;
    float shifter_correction;
} brick6_sampler_stream_pitch_plan_t;

typedef struct
{
    uint16_t instrument_id;
    float gain;
    uint8_t loop_enabled;
} brick6_sampler_multi_track_state_t;

typedef struct
{
    uint8_t owner_track_id;
    brick6_clip_shifter_t shifter;
    float stretch_render_l[AUDIO_BLOCK_SIZE];
    float stretch_render_r[AUDIO_BLOCK_SIZE];
} brick6_sampler_clip_slot_t;

typedef struct
{
    float start_l;
    float start_r;
    uint8_t owner_track_id;
    uint8_t remaining;
    uint8_t active;
} brick6_sampler_declick_tail_t;

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
static AUDIO_HOT brick6_sampler_declick_tail_t
    g_sampler_declick_tail[STEAL_DECLICK_TAIL_SLOTS];
static AUDIO_HOT ALIGN32 float g_sampler_ram_mono_discard[AUDIO_BLOCK_SIZE];
static uint32_t g_sampler_voice_trigger_counter;
static uint8_t g_sampler_multi_alloc_stole_voice;
static CTRL_STATE uint8_t
    g_sampler_multi_stream_release_pending[MULTI_SAMPLE_POOL_MAX_SAMPLES];
static CTRL_STATE uint8_t
    g_sampler_multi_page0_reject_logged[MULTI_SAMPLE_POOL_MAX_SAMPLES];
static uint8_t g_sampler_multi_alloc_reject_reason =
    (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_NONE;

static brick6_sampler_runtime_diag_snapshot_t g_brick6_sampler_runtime_diag;

#define BRICK6_SAMPLER_RUNTIME_DIAG_INC(field) ((void)0)

#define BRICK6_SAMPLER_STEP_EPSILON (0.0001f)

static uint8_t brick6_sampler_runtime_cache_voice_id(uint8_t track_id);
static uint32_t brick6_sampler_runtime_multi_active_count(void);
static uint32_t brick6_sampler_runtime_multi_active_count_for_track(uint8_t track_id);
static uint8_t brick6_sampler_runtime_resolve_grid_count(uint8_t raw_grid_count);
static float brick6_sampler_runtime_velocity_gain(uint8_t velocity);
static uint32_t brick6_sampler_runtime_ratio_to_q16(float ratio);
static uint32_t brick6_sampler_runtime_ram_step_q16(const brick6_sampler_voice_t *voice,
                                                    const sampler_ram_slot_t *ram);
static uint32_t brick6_sampler_runtime_next_trigger_order(void);
static uint32_t brick6_sampler_runtime_clip_ratio_q16(float source_bpm);
static uint32_t brick6_sampler_runtime_clamp_region_begin(uint32_t length_frames, float start);
static uint32_t brick6_sampler_runtime_clamp_region_end(uint32_t length_frames, float end);
static void brick6_sampler_runtime_build_effective_ram_region(uint32_t length_frames,
                                                              float start,
                                                              float end,
                                                              uint32_t *out_begin,
                                                              uint32_t *out_end);
static void brick6_sampler_runtime_build_effective_ram_bounds(uint32_t length_frames,
                                                              float start,
                                                              float end,
                                                              float loop_start,
                                                              uint8_t mode,
                                                              uint8_t use_loop_marker,
                                                              uint32_t *out_begin,
                                                              uint32_t *out_end,
                                                              uint32_t *out_loop_begin);
static void brick6_sampler_runtime_build_effective_ram_slice_region(uint32_t region_begin,
                                                                    uint32_t region_end,
                                                                    uint32_t slice_count,
                                                                    uint32_t slice_index,
                                                                    uint32_t *out_begin,
                                                                    uint32_t *out_end);
static uint8_t brick6_sampler_runtime_ram_mode_to_loop_mode(uint8_t mode,
                                                            uint32_t region_begin,
                                                            uint32_t region_end,
                                                            uint8_t loop_valid);
static void brick6_sampler_runtime_reconcile_ram_voice_bounds_live(uint8_t track_id);
static void brick6_sampler_runtime_reproject_ram_voice_tune_live(uint8_t track_id);
static uint32_t brick6_sampler_runtime_clip_resolve_timing_ratio_q16(uint8_t track_id,
                                                                     const sample_desc_t *desc,
                                                                     const brick6_sampler_clip_runtime_t *clip,
                                                                     uint32_t *out_region_begin,
                                                                     uint32_t *out_region_end);
static brick6_sampler_stream_pitch_plan_t brick6_sampler_runtime_stream_build_pitch_plan(
    uint8_t track_id,
    uint8_t played_note,
    const sample_desc_t *desc,
    const brick6_sampler_clip_runtime_t *clip,
    uint8_t use_shifter,
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
static uint8_t brick6_sampler_runtime_resolve_ram_source(uint16_t global_slot,
                                                         uint16_t *out_ram_slot,
                                                         const sampler_ram_slot_t **out_ram);
static uint8_t brick6_sampler_runtime_prepare_ram_slice_grid(
    uint8_t track_id,
    const sampler_ram_slot_t **out_ram);
static uint8_t brick6_sampler_runtime_trigger_ram(uint8_t track_id);
static uint8_t brick6_sampler_runtime_render_ram_forward_pitched(
    brick6_sampler_voice_t *voice,
    const sampler_ram_slot_t *ram,
    float *out_l,
    float *out_r,
    uint32_t frames,
    uint64_t *io_position_q16);
static uint8_t brick6_sampler_runtime_render_ram_reverse_pitched(
    brick6_sampler_voice_t *voice,
    const sampler_ram_slot_t *ram,
    float *out_l,
    float *out_r,
    uint32_t frames,
    uint64_t *io_position_q16);
static uint64_t brick6_sampler_runtime_wrap_q16(uint64_t offset_q16, uint64_t span_q16);
static inline float brick6_sampler_runtime_ram_fade_gain(brick6_sampler_voice_t *voice);
static uint8_t brick6_sampler_runtime_render_ram_pingpong_unpitched(
    brick6_sampler_voice_t *voice,
    const sampler_ram_slot_t *ram,
    float *out_l,
    float *out_r,
    uint32_t frames,
    uint32_t position);
static uint8_t brick6_sampler_runtime_render_ram_pingpong_pitched(
    brick6_sampler_voice_t *voice,
    const sampler_ram_slot_t *ram,
    float *out_l,
    float *out_r,
    uint32_t frames,
    uint64_t *io_position_q16);
static void brick6_sampler_runtime_render_ram(brick6_sampler_voice_t *voice,
                                              float *out_l,
                                              float *out_r,
                                              uint32_t frames);
static void brick6_sampler_runtime_clear_ram_voice(brick6_sampler_voice_t *voice);
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
static void brick6_sampler_runtime_voice_note_output(brick6_sampler_voice_t *voice,
                                                     float out_l,
                                                     float out_r);
static uint8_t brick6_sampler_runtime_begin_declick_tail(uint8_t track_id,
                                                         brick6_sampler_voice_t *voice);
static void brick6_sampler_runtime_start_declick_fade_in(brick6_sampler_voice_t *voice);
static uint8_t brick6_sampler_runtime_apply_start_fade(brick6_sampler_voice_t *voice,
                                                       float *fade,
                                                       uint32_t frames);
static void brick6_sampler_runtime_mix_declick_tails(uint8_t track_id,
                                                     float *out_l,
                                                     float *out_r,
                                                     uint32_t frames);
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

uint8_t brick6_sampler_runtime_ram_slice_mode_active(uint8_t track_id)
{
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track_id);
    if ((ctx == NULL)
        || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
        || (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
        || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER))
    {
        return 0U;
    }

    return ((ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_RAM)
            && (g_sampler_voice[track_id].slice_count != 0U))
               ? 1U
               : 0U;
}

static void brick6_sampler_runtime_voice_note_output(brick6_sampler_voice_t *voice,
                                                     float out_l,
                                                     float out_r)
{
    if (voice == NULL)
    {
        return;
    }

    voice->last_out_l = out_l;
    voice->last_out_r = out_r;
    voice->last_out_valid = 1U;
}

static uint8_t brick6_sampler_runtime_begin_declick_tail(uint8_t track_id,
                                                         brick6_sampler_voice_t *voice)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (voice == NULL) || (voice->active == 0U)
        || (voice->last_out_valid == 0U))
    {
        return 0U;
    }

    if ((fabsf(voice->last_out_l) <= STEAL_DECLICK_EPSILON)
        && (fabsf(voice->last_out_r) <= STEAL_DECLICK_EPSILON))
    {
        voice->last_out_l = 0.0f;
        voice->last_out_r = 0.0f;
        voice->last_out_valid = 0U;
        return 0U;
    }

    for (uint8_t i = 0U; i < STEAL_DECLICK_TAIL_SLOTS; ++i)
    {
        brick6_sampler_declick_tail_t *const tail = &g_sampler_declick_tail[i];
        if (tail->active != 0U)
        {
            continue;
        }

        tail->start_l = voice->last_out_l;
        tail->start_r = voice->last_out_r;
        tail->owner_track_id = track_id;
        tail->remaining = (uint8_t)STEAL_DECLICK_SAMPLES;
        tail->active = 1U;
        break;
    }

    voice->last_out_l = 0.0f;
    voice->last_out_r = 0.0f;
    voice->last_out_valid = 0U;
    return 1U;
}

static void brick6_sampler_runtime_start_declick_fade_in(brick6_sampler_voice_t *voice)
{
    if (voice == NULL)
    {
        return;
    }

    voice->start_fade_remaining = (uint8_t)STEAL_DECLICK_SAMPLES;
}

static uint8_t brick6_sampler_runtime_apply_start_fade(brick6_sampler_voice_t *voice,
                                                       float *fade,
                                                       uint32_t frames)
{
    if ((voice == NULL) || (fade == NULL) || (frames == 0U)
        || (voice->start_fade_remaining == 0U))
    {
        return 0U;
    }

    const float denom = (STEAL_DECLICK_SAMPLES > 1U)
                            ? (1.0f / (float)(STEAL_DECLICK_SAMPLES - 1U))
                            : 1.0f;
    for (uint32_t i = 0U; i < frames; ++i)
    {
        if (voice->start_fade_remaining == 0U)
        {
            break;
        }

        const float gain = (voice->start_fade_remaining < STEAL_DECLICK_SAMPLES)
                               ? ((float)(STEAL_DECLICK_SAMPLES - voice->start_fade_remaining) * denom)
                               : 0.0f;
        fade[i] *= gain;
        voice->start_fade_remaining--;
    }
    return 1U;
}

static void brick6_sampler_runtime_mix_declick_tails(uint8_t track_id,
                                                     float *out_l,
                                                     float *out_r,
                                                     uint32_t frames)
{
    if ((track_id >= SEQ_TRACK_COUNT) || (out_l == NULL) || (out_r == NULL)
        || (frames == 0U))
    {
        return;
    }

    const float denom = (STEAL_DECLICK_SAMPLES > 1U)
                            ? (1.0f / (float)(STEAL_DECLICK_SAMPLES - 1U))
                            : 1.0f;
    for (uint8_t t = 0U; t < STEAL_DECLICK_TAIL_SLOTS; ++t)
    {
        brick6_sampler_declick_tail_t *const tail = &g_sampler_declick_tail[t];
        if ((tail->active == 0U) || (tail->owner_track_id != track_id))
        {
            continue;
        }

        for (uint32_t i = 0U; (i < frames) && (tail->remaining != 0U); ++i)
        {
            const float gain = (tail->remaining > 1U)
                                   ? ((float)(tail->remaining - 1U) * denom)
                                   : 0.0f;
            out_l[i] += tail->start_l * gain;
            out_r[i] += tail->start_r * gain;
            tail->remaining--;
        }

        if (tail->remaining == 0U)
        {
            tail->active = 0U;
            tail->start_l = 0.0f;
            tail->start_r = 0.0f;
            tail->owner_track_id = UINT8_MAX;
        }
    }
}

static void brick6_sampler_runtime_multi_track_reset(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_sampler_multi_track_state[track_id].instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    g_sampler_multi_track_state[track_id].gain = 1.0f;
    g_sampler_multi_track_state[track_id].loop_enabled = 0U;
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

static uint8_t brick6_sampler_runtime_start_gate_check(
    const sample_play_plan_t *plan,
    uint8_t classic)
{
    sample_play_plan_ready_result_t ready;
    const sample_play_plan_ready_status_t status =
        sample_play_plan_check_ready_requirements(plan, &ready);

    g_brick6_sampler_runtime_diag.start_gate_last_status = (uint32_t)status;
    g_brick6_sampler_runtime_diag.start_gate_last_missing_page = ready.first_missing_page;
    g_brick6_sampler_runtime_diag.start_gate_last_pending_page = ready.first_pending_page;

    if (status == SAMPLE_PLAY_PLAN_READY_COMPLETE)
    {
        return 1U;
    }

    g_brick6_sampler_runtime_diag.start_gate_reject_count++;
    if (classic != 0U)
    {
        g_brick6_sampler_runtime_diag.start_gate_reject_classic_count++;
    }
    else
    {
        g_brick6_sampler_runtime_diag.start_gate_reject_multi_count++;
    }

    switch (status)
    {
        case SAMPLE_PLAY_PLAN_READY_INVALID:
            g_brick6_sampler_runtime_diag.start_gate_invalid_plan_count++;
            break;
        case SAMPLE_PLAY_PLAN_READY_PENDING:
            g_brick6_sampler_runtime_diag.start_gate_pending_count++;
            break;
        case SAMPLE_PLAY_PLAN_READY_PARTIAL:
            g_brick6_sampler_runtime_diag.start_gate_partial_count++;
            break;
        case SAMPLE_PLAY_PLAN_READY_MISSING:
        default:
            g_brick6_sampler_runtime_diag.start_gate_missing_count++;
            break;
    }

    return 0U;
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
        const uint8_t loop_enabled =
            (g_sampler_multi_track_state[trigger->track_id].loop_enabled != 0U) ? 1U : 0U;
        out_source->region_begin = 0U;
        out_source->region_end = trigger->total_frames;
        if ((out_source->loop_end <= out_source->loop_begin)
            || (out_source->loop_begin < out_source->region_begin)
            || (out_source->loop_end > out_source->region_end))
        {
            out_source->loop_begin = out_source->region_begin;
            out_source->loop_end = out_source->region_end;
        }
        out_source->loop_mode = (loop_enabled != 0U)
            ? (uint8_t)SAMPLE_PLAY_LOOP_FORWARD
            : (uint8_t)SAMPLE_PLAY_LOOP_NONE;
        out_source->reverse = 0U;
        out_source->rate = rate;
        out_source->gain = trigger->render_gain;
        out_source->owner_track_id = trigger->track_id;
        out_source->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_MULTI;

        options.start_frame = 0U;
        options.end_frame = trigger->total_frames;
        options.loop_begin = out_source->loop_begin;
        options.loop_end = out_source->loop_end;
        options.rate = rate;
        options.reverse = 0U;
        options.loop_mode = out_source->loop_mode;
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

static void brick6_sampler_runtime_multi_release_voice_vca(
    const brick6_sampler_voice_t *voice)
{
    uint8_t mix_track = 0U;
    if ((voice != NULL)
            && (voice->owner_track_id < SEQ_TRACK_COUNT)
            && (track_runtime_get_mix_target_track(voice->owner_track_id,
                                                    &mix_track) != 0U))
    {
        mixer_track_vca_note_off(mix_track, voice->note);
    }
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
    sample_stream_manager_release_owner((uint8_t)SAMPLE_STREAM_OWNER_MULTI_LOOP,
                                        voice_index,
                                        voice->trigger_order);
}

static void brick6_sampler_runtime_multi_release_voice_stream_owner_generation(uint8_t voice_index,
                                                                               uint32_t generation)
{
    if ((voice_index == UINT8_MAX) || (generation == 0U))
    {
        return;
    }

    sample_stream_manager_release_owner((uint8_t)SAMPLE_STREAM_OWNER_MULTI_VOICE,
                                        voice_index,
                                        generation);
    sample_stream_manager_release_owner((uint8_t)SAMPLE_STREAM_OWNER_MULTI_LOOP,
                                        voice_index,
                                        generation);
}

static void brick6_sampler_runtime_multi_mark_voice_stream_owner_release(
    brick6_sampler_voice_t *voice)
{
    const uint8_t voice_index = brick6_sampler_runtime_multi_voice_index(voice);
    if ((voice == NULL) || (voice_index == UINT8_MAX) || (voice->trigger_order == 0U))
    {
        return;
    }

    brick6_sampler_runtime_multi_release_voice_vca(voice);
    voice->stream_owner_release_pending = 1U;
    voice->stream_owner_release_generation = voice->trigger_order;
    voice->stream_owner_release_sample_id = voice->multi_sample_id;
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

    return (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_STREAM) ? 1U : 0U;
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
    if ((clip == NULL) || (slot == NULL))
    {
        return;
    }

    brick6_clip_shifter_set_window_frames(&slot->shifter,
                                          brick6_sampler_runtime_clip_shifter_window_frames(clip->grain_size));
    brick6_clip_shifter_set_pitch_correction(&slot->shifter, clip->shifter_correction);
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
    g_sampler_clip_runtime[track_id].shifter_correction = 1.0f;
    sample_voice_reader_reset(&g_sampler_voice[track_id].reader);
    g_sampler_voice[track_id].active = 0U;
    g_sampler_voice[track_id].note = STREAM_SAMPLER_ROOT_NOTE;
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

static brick6_sampler_stream_pitch_plan_t brick6_sampler_runtime_stream_build_pitch_plan(
    uint8_t track_id,
    uint8_t played_note,
    const sample_desc_t *desc,
    const brick6_sampler_clip_runtime_t *clip,
    uint8_t use_shifter,
    uint32_t *out_region_begin,
    uint32_t *out_region_end)
{
    brick6_sampler_stream_pitch_plan_t plan = {
        .note_delta = (int16_t)played_note - (int16_t)STREAM_SAMPLER_ROOT_NOTE,
        .timing_ratio_q16 = BRICK6_SAMPLER_Q16_ONE,
        .pitch_ratio_q16 = BRICK6_SAMPLER_Q16_ONE,
        .step_q16 = BRICK6_SAMPLER_Q16_ONE,
        .shifter_correction = 1.0f,
    };
    const uint32_t source_sample_rate = ((desc != NULL) && (desc->sample_rate != 0U))
                                            ? desc->sample_rate
                                            : BOARD_AUDIO_SAMPLE_RATE_HZ;
    const float semitones = (float)plan.note_delta
                            + ((clip != NULL) ? clip->pitch_semitones : 0.0f);
    const float musical_ratio = powf(2.0f, semitones / 12.0f);
    const float sample_rate_ratio = (float)source_sample_rate
                                    / (float)BOARD_AUDIO_SAMPLE_RATE_HZ;
    const float desired_pitch_ratio = sample_rate_ratio * musical_ratio;

    plan.timing_ratio_q16 = brick6_sampler_runtime_clip_resolve_timing_ratio_q16(
        track_id, desc, clip, out_region_begin, out_region_end);
    plan.pitch_ratio_q16 = brick6_sampler_runtime_ratio_to_q16(desired_pitch_ratio);

    const float timing_ratio = (float)plan.timing_ratio_q16
                               / (float)BRICK6_SAMPLER_Q16_ONE;
    const float represented_pitch_ratio = (float)plan.pitch_ratio_q16
                                          / (float)BRICK6_SAMPLER_Q16_ONE;
    if (use_shifter != 0U)
    {
        plan.step_q16 = brick6_sampler_runtime_ratio_to_q16(
            sample_rate_ratio * timing_ratio);
        const float reader_timing_ratio = (float)plan.step_q16
                                          / (float)BRICK6_SAMPLER_Q16_ONE;
        if (reader_timing_ratio > 0.0f)
        {
            plan.shifter_correction = represented_pitch_ratio / reader_timing_ratio;
        }
    }
    else
    {
        plan.step_q16 = brick6_sampler_runtime_ratio_to_q16(
            timing_ratio * desired_pitch_ratio);
    }

    return plan;
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
    brick6_sampler_stream_pitch_plan_t pitch_plan;
    uint8_t use_shifter = brick6_sampler_runtime_clip_uses_shifter(clip);
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

    pitch_plan = brick6_sampler_runtime_stream_build_pitch_plan(track_id,
                                                                voice->note,
                                                                desc,
                                                                clip,
                                                                use_shifter,
                                                                &region_begin,
                                                                &region_end);
    clip->timing_ratio_q16 = pitch_plan.timing_ratio_q16;
    clip->pitch_ratio_q16 = pitch_plan.pitch_ratio_q16;
    clip->shifter_correction = pitch_plan.shifter_correction;

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
    play_plan.step_q16 = pitch_plan.step_q16;
    play_plan.direction = 0U;
    play_plan.loop_mode = (clip->loop_enabled != 0U) ? BRICK6_SAMPLER_LOOP_FORWARD
                                                     : BRICK6_SAMPLER_LOOP_NONE;
    play_plan.stop_on_underrun = 1U;
    play_plan.kernel_type = (pitch_plan.step_q16 == BRICK6_SAMPLER_Q16_ONE)
                                ? SAMPLE_KERNEL_FWD_1X
                                : SAMPLE_KERNEL_PITCH_FWD_LINEAR;

    const uint8_t start_fade = brick6_sampler_runtime_begin_declick_tail(track_id, voice);
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
    voice->mode = 2U;
    voice->slice_count = 0U;
    voice->slice_index = 0U;
    voice->gain = clip->gain;
    voice->trigger_velocity_gain = 1.0f;
    voice->start = g_sampler_voice[track_id].start;
    voice->end = g_sampler_voice[track_id].end;
    voice->tune = 0.0f;
    voice->region_begin = region_begin;
    voice->region_end = region_end;
    voice->loop_frames = region_end - region_begin;
    voice->fade_in_frames = 0U;
    voice->fade_out_frames = 0U;
    voice->step_signed = (float)pitch_plan.step_q16 / (float)BRICK6_SAMPLER_Q16_ONE;
    voice->reverse = 0U;
    voice->loop_mode = (clip->loop_enabled != 0U) ? BRICK6_SAMPLER_LOOP_FORWARD
                                                  : BRICK6_SAMPLER_LOOP_NONE;
    voice->use_slice = 0U;
    voice->use_segment_cursor = 1U;
    voice->play_plan = play_plan;
    voice->trigger_order = brick6_sampler_runtime_next_trigger_order();
    voice->last_out_l = 0.0f;
    voice->last_out_r = 0.0f;
    voice->last_out_valid = 0U;
    voice->start_fade_remaining = 0U;
    voice->release_pending = 0U;
    if (start_fade != 0U)
    {
        brick6_sampler_runtime_start_declick_fade_in(voice);
    }
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

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    brick6_sampler_runtime_begin_declick_tail(track_id, voice);
    sample_voice_reader_stop(&voice->reader);
    sample_cache_stop_voice(brick6_sampler_runtime_cache_voice_id(track_id));
    voice->active = 0U;
    voice->position = 0.0f;
    voice->ram_position_q16 = 0ULL;
    voice->sample = NULL;
    voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    voice->release_pending = 0U;
    g_sampler_clip_runtime[track_id].state = (uint8_t)BRICK6_SAMPLER_CLIP_STATE_IDLE;
    g_sampler_clip_runtime[track_id].use_shifter_engine = 0U;
    brick6_sampler_runtime_clip_release_slot(track_id);
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

static uint32_t brick6_sampler_runtime_ram_step_q16(const brick6_sampler_voice_t *voice,
                                                    const sampler_ram_slot_t *ram)
{
    const float tune = (voice != NULL) ? voice->tune : 0.0f;
    const int32_t note = (voice != NULL) ? (int32_t)voice->note : 60;
    const uint32_t source_rate = ((ram != NULL) && (ram->sample_rate != 0U))
                                     ? ram->sample_rate
                                     : BRICK6_SAMPLER_RAM_OUTPUT_SAMPLE_RATE_HZ;
    const float rate_ratio = (float)source_rate
                             / (float)BRICK6_SAMPLER_RAM_OUTPUT_SAMPLE_RATE_HZ;
    const float pitch_ratio = powf(2.0f, ((float)(note - 60) + tune) * (1.0f / 12.0f));

    return brick6_sampler_runtime_ratio_to_q16(rate_ratio * pitch_ratio);
}

static uint32_t brick6_sampler_runtime_ram_slice_step_q16(const brick6_sampler_voice_t *voice,
                                                          const sampler_ram_slot_t *ram)
{
    const float tune = (voice != NULL) ? voice->tune : 0.0f;
    const uint32_t source_rate = ((ram != NULL) && (ram->sample_rate != 0U))
                                     ? ram->sample_rate
                                     : BRICK6_SAMPLER_RAM_OUTPUT_SAMPLE_RATE_HZ;
    const float rate_ratio = (float)source_rate
                             / (float)BRICK6_SAMPLER_RAM_OUTPUT_SAMPLE_RATE_HZ;
    const float pitch_ratio = powf(2.0f, tune * (1.0f / 12.0f));

    return brick6_sampler_runtime_ratio_to_q16(rate_ratio * pitch_ratio);
}

static uint8_t brick6_sampler_runtime_note_to_slice_index(uint8_t note, uint32_t slice_count)
{
    const uint8_t reference_note = 60U;
    uint32_t index = 0U;
    if (note > reference_note)
    {
        index = (uint32_t)(note - reference_note);
    }
    if ((slice_count != 0U) && (index >= slice_count))
    {
        index = slice_count - 1U;
    }
    return (uint8_t)index;
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

static void brick6_sampler_runtime_trigger_ram_slice(uint8_t track_id)
{
    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    const uint8_t cache_voice_id = brick6_sampler_runtime_cache_voice_id(track_id);
    uint16_t ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
    const sampler_ram_slot_t *ram = NULL;

    brick6_sampler_runtime_begin_declick_tail(track_id, voice);
    sample_cache_stop_voice(cache_voice_id);
    sample_voice_reader_reset(&voice->reader);

    if (brick6_sampler_runtime_resolve_ram_source(voice->sample_id,
                                                  &ram_slot,
                                                  &ram) != 0U)
    {
        uint32_t slice_count = (uint32_t)brick6_sampler_runtime_resolve_grid_count(
            voice->slice_count);
        if (slice_count == 0U)
        {
            slice_count = 1U;
        }
        if (slice_count > 64U)
        {
            slice_count = 64U;
        }
        uint32_t base_begin = 0U;
        uint32_t base_end = ram->frames;
        brick6_sampler_runtime_build_effective_ram_region(ram->frames,
                                                          voice->start,
                                                          voice->end,
                                                          &base_begin,
                                                          &base_end);
        const uint8_t slice_index =
            brick6_sampler_runtime_note_to_slice_index(voice->note, slice_count);
        uint32_t region_begin = base_begin;
        uint32_t region_end = base_end;
        brick6_sampler_runtime_build_effective_ram_slice_region(base_begin,
                                                                base_end,
                                                                slice_count,
                                                                (uint32_t)slice_index,
                                                                &region_begin,
                                                                &region_end);
        if ((region_end > region_begin) && (region_end <= ram->frames))
        {
            const uint8_t mode = voice->mode;
            const uint8_t reverse = (mode == 1U) ? 1U : 0U;
            uint32_t loop_begin = region_begin;
            const uint8_t loop_mode =
                brick6_sampler_runtime_ram_mode_to_loop_mode(mode,
                                                             region_begin,
                                                             region_end,
                                                             1U);
            const uint32_t step_q16 = brick6_sampler_runtime_ram_slice_step_q16(voice, ram);
            voice->active = 1U;
            voice->owner_track_id = track_id;
            voice->multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
            voice->multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
            voice->ram_slot = ram_slot;
            voice->ram_generation = ram->generation;
            voice->ram_data = ram->data;
            voice->ram_channels = ram->channels;
            voice->ram_format = ram->format;
            voice->position = (float)((reverse != 0U) ? (region_end - 1U) : region_begin);
            voice->ram_position_q16 =
                (uint64_t)((reverse != 0U) ? (region_end - 1U) : region_begin) << 16U;
            voice->region_begin = region_begin;
            voice->region_end = region_end;
            voice->loop_begin = loop_begin;
            voice->loop_frames = region_end - loop_begin;
            voice->step_signed = (float)step_q16 / (float)BRICK6_SAMPLER_Q16_ONE;
            voice->ram_step_q16 = step_q16;
            voice->loop_mode = loop_mode;
            voice->reverse = reverse;
            voice->use_slice = 1U;
            voice->slice_index = slice_index;
            voice->use_segment_cursor = 0U;
            voice->release_pending = 0U;
            voice->sample = NULL;
            voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_RAM;
            voice->trigger_velocity_gain =
                brick6_sampler_runtime_velocity_gain(voice->velocity);
            voice->trigger_order = brick6_sampler_runtime_next_trigger_order();
            memset(&voice->play_plan, 0, sizeof(voice->play_plan));
            brick6_sampler_runtime_start_declick_fade_in(voice);
            return;
        }
    }

    voice->active = 0U;
    voice->position = 0.0f;
    voice->ram_position_q16 = 0ULL;
    voice->sample = NULL;
    voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    voice->multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
    voice->ram_generation = 0U;
    voice->ram_data = NULL;
    voice->ram_channels = 0U;
    voice->ram_format = SAMPLER_RAM_FORMAT_NONE;
    voice->use_segment_cursor = 0U;
    voice->release_pending = 0U;
    memset(&voice->play_plan, 0, sizeof(voice->play_plan));
}

static uint8_t brick6_sampler_runtime_resolve_ram_source(uint16_t global_slot,
                                                         uint16_t *out_ram_slot,
                                                         const sampler_ram_slot_t **out_ram)
{
    if (out_ram_slot != NULL)
    {
        *out_ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
    }
    if (out_ram != NULL)
    {
        *out_ram = NULL;
    }

    uint16_t ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
    const sample_global_slot_t *const global = sample_global_pool_get_slot(global_slot);
    if ((global == NULL)
        || (global->kind != SAMPLE_GLOBAL_KIND_RAM)
        || (global->state != SAMPLE_GLOBAL_STATE_READY)
        || (sample_global_pool_resolve_backend(global_slot,
                                               SAMPLE_GLOBAL_KIND_RAM,
                                               &ram_slot) == 0U))
    {
        return 0U;
    }

    const sampler_ram_slot_t *const ram = sampler_ram_pool_get_slot(ram_slot);
    const uint16_t ram_channels = (ram != NULL)
                                      ? sampler_ram_format_channels(ram->format)
                                      : 0U;
    if ((ram == NULL)
        || (ram->state != SAMPLER_RAM_SLOT_READY)
        || (ram->global_slot != global_slot)
        || ((ram->format != SAMPLER_RAM_FORMAT_FLOAT32_MONO)
            && (ram->format != SAMPLER_RAM_FORMAT_FLOAT32_STEREO_INTERLEAVED))
        || (ram->data == NULL)
        || (ram->frames == 0U)
        || (ram->channels != ram_channels)
        || (ram->bytes_per_frame != sampler_ram_format_bytes_per_frame(ram->format)))
    {
        return 0U;
    }

    if (out_ram_slot != NULL)
    {
        *out_ram_slot = ram_slot;
    }
    if (out_ram != NULL)
    {
        *out_ram = ram;
    }
    return 1U;
}

static uint8_t brick6_sampler_runtime_prepare_ram_slice_grid(uint8_t track_id,
                                                              const sampler_ram_slot_t **out_ram)
{
    if (out_ram != NULL)
    {
        *out_ram = NULL;
    }
    if ((track_id >= SEQ_TRACK_COUNT)
        || (brick6_sampler_runtime_ram_slice_mode_active(track_id) == 0U))
    {
        return 0U;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    const sampler_ram_slot_t *ram = NULL;
    if (brick6_sampler_runtime_resolve_ram_source(voice->sample_id, NULL, &ram) == 0U)
    {
        voice->slice_grid_generation = 0U;
        return 0U;
    }
    if ((voice->slice_grid_generation == ram->generation)
        && (voice->slice_end[0] != 0U))
    {
        if (out_ram != NULL)
        {
            *out_ram = ram;
        }
        return 1U;
    }
    if (__get_IPSR() != 0U)
    {
        return 0U;
    }

    uint32_t slice_count = (uint32_t)brick6_sampler_runtime_resolve_grid_count(
        voice->slice_count);
    if (slice_count == 0U)
    {
        slice_count = 1U;
    }
    if (slice_count > 64U)
    {
        slice_count = 64U;
    }

    uint32_t region_begin = 0U;
    uint32_t region_end = ram->frames;
    brick6_sampler_runtime_build_effective_ram_region(ram->frames,
                                                      voice->start,
                                                      voice->end,
                                                      &region_begin,
                                                      &region_end);
    for (uint32_t i = 0U; i < slice_count; ++i)
    {
        uint32_t begin = region_begin;
        uint32_t end = region_end;
        brick6_sampler_runtime_build_effective_ram_slice_region(region_begin,
                                                                region_end,
                                                                slice_count,
                                                                i,
                                                                &begin,
                                                                &end);
        voice->slice_begin[i] = begin;
        voice->slice_end[i] = (end > begin) ? end : (begin + 1U);
        if (voice->slice_end[i] > ram->frames)
        {
            voice->slice_end[i] = ram->frames;
        }
    }
    for (uint32_t i = slice_count; i < 64U; ++i)
    {
        voice->slice_begin[i] = 0U;
        voice->slice_end[i] = 0U;
    }
    voice->slice_grid_generation = ram->generation;
    if (out_ram != NULL)
    {
        *out_ram = ram;
    }
    return 1U;
}

static uint8_t brick6_sampler_runtime_ram_slot_valid(const brick6_sampler_voice_t *voice,
                                                     const sampler_ram_slot_t **out_slot)
{
    if (out_slot != NULL)
    {
        *out_slot = NULL;
    }
    if ((voice == NULL)
        || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_RAM)
        || (voice->ram_slot == SAMPLER_RAM_POOL_INVALID_SLOT))
    {
        return 0U;
    }

    const sampler_ram_slot_t *const ram = sampler_ram_pool_get_slot(voice->ram_slot);
    const uint16_t ram_channels = (ram != NULL)
                                      ? sampler_ram_format_channels(ram->format)
                                      : 0U;
    if ((ram == NULL)
        || (ram->state != SAMPLER_RAM_SLOT_READY)
        || (ram->global_slot != voice->sample_id)
        || (ram->generation != voice->ram_generation)
        || ((ram->format != SAMPLER_RAM_FORMAT_FLOAT32_MONO)
            && (ram->format != SAMPLER_RAM_FORMAT_FLOAT32_STEREO_INTERLEAVED))
        || (ram->data == NULL)
        || (ram->frames == 0U)
        || (ram->channels != ram_channels)
        || (ram->bytes_per_frame != sampler_ram_format_bytes_per_frame(ram->format))
        || (voice->ram_format != ram->format)
        || (voice->ram_channels != ram_channels))
    {
        return 0U;
    }

    if (out_slot != NULL)
    {
        *out_slot = ram;
    }
    return 1U;
}

static uint8_t brick6_sampler_runtime_trigger_ram(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    const uint16_t global_slot = voice->sample_id;
    uint16_t ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
    const sampler_ram_slot_t *ram = NULL;
    if (brick6_sampler_runtime_resolve_ram_source(global_slot, &ram_slot, &ram) == 0U)
    {
        return 0U;
    }

    brick6_sampler_runtime_begin_declick_tail(track_id, voice);
    sample_cache_stop_voice(brick6_sampler_runtime_cache_voice_id(track_id));
    sample_voice_reader_reset(&voice->reader);

    uint32_t region_begin = 0U;
    uint32_t region_end = ram->frames;
    uint32_t loop_begin = 0U;
    const uint8_t mode = voice->mode;
    brick6_sampler_runtime_build_effective_ram_bounds(ram->frames,
                                                      voice->start,
                                                      voice->end,
                                                      voice->loop_start,
                                                      mode,
                                                      1U,
                                                      &region_begin,
                                                      &region_end,
                                                      &loop_begin);
    const uint8_t reverse = (mode == 1U) ? 1U : 0U;
    const uint32_t step_q16 = brick6_sampler_runtime_ram_step_q16(voice, ram);
    const uint8_t loop_mode =
        brick6_sampler_runtime_ram_mode_to_loop_mode(mode,
                                                     region_begin,
                                                     region_end,
                                                     1U);

    voice->active = 1U;
    voice->owner_track_id = track_id;
    voice->sample_id = global_slot;
    voice->multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->ram_slot = ram_slot;
    voice->ram_generation = ram->generation;
    voice->ram_data = ram->data;
    voice->ram_channels = ram->channels;
    voice->ram_format = ram->format;
    voice->position = (float)((reverse != 0U) ? (region_end - 1U) : region_begin);
    voice->ram_position_q16 =
        (uint64_t)((reverse != 0U) ? (region_end - 1U) : region_begin) << 16U;
    voice->region_begin = region_begin;
    voice->region_end = region_end;
    voice->loop_begin = loop_begin;
    voice->loop_frames = region_end - loop_begin;
    voice->step_signed = (float)step_q16 / (float)BRICK6_SAMPLER_Q16_ONE;
    voice->ram_step_q16 = step_q16;
    voice->loop_mode = loop_mode;
    voice->reverse = reverse;
    voice->use_segment_cursor = 0U;
    voice->release_pending = 0U;
    voice->sample = NULL;
    voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_RAM;
    voice->trigger_velocity_gain = brick6_sampler_runtime_velocity_gain(voice->velocity);
    voice->trigger_order = brick6_sampler_runtime_next_trigger_order();
    brick6_sampler_runtime_start_declick_fade_in(voice);
    return 1U;
}

static uint8_t brick6_sampler_runtime_cache_voice_id(uint8_t track_id)
{
    return (uint8_t)(BRICK6_SAMPLER_CACHE_VOICE_BASE + track_id);
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

static void brick6_sampler_runtime_build_effective_ram_region(uint32_t length_frames,
                                                              float start,
                                                              float end,
                                                              uint32_t *out_begin,
                                                              uint32_t *out_end)
{
    uint32_t region_begin = 0U;
    uint32_t region_end = length_frames;
    if (length_frames != 0U)
    {
        region_begin = brick6_sampler_runtime_clamp_region_begin(length_frames, start);
        region_end = brick6_sampler_runtime_clamp_region_end(length_frames, end);
        if (region_end <= region_begin)
        {
            region_end = region_begin + 1U;
            if (region_end > length_frames)
            {
                region_end = length_frames;
                region_begin = (length_frames > 1U) ? (length_frames - 1U) : 0U;
            }
        }
    }
    if (out_begin != NULL)
    {
        *out_begin = region_begin;
    }
    if (out_end != NULL)
    {
        *out_end = region_end;
    }
}

static void brick6_sampler_runtime_build_effective_ram_bounds(uint32_t length_frames,
                                                              float start,
                                                              float end,
                                                              float loop_start,
                                                              uint8_t mode,
                                                              uint8_t use_loop_marker,
                                                              uint32_t *out_begin,
                                                              uint32_t *out_end,
                                                              uint32_t *out_loop_begin)
{
    uint32_t region_begin = 0U;
    uint32_t region_end = length_frames;
    uint32_t loop_begin = 0U;
    if (length_frames != 0U)
    {
        region_begin = brick6_sampler_runtime_clamp_region_begin(length_frames, start);
        region_end = brick6_sampler_runtime_clamp_region_end(length_frames, end);
        loop_begin = region_begin;

        if ((mode == 2U) && (use_loop_marker != 0U))
        {
            loop_begin = brick6_sampler_runtime_clamp_region_begin(length_frames, loop_start);
            if (loop_begin < region_begin)
            {
                loop_begin = region_begin;
            }
            if (loop_begin >= length_frames)
            {
                loop_begin = length_frames - 1U;
            }
            if (region_end <= loop_begin)
            {
                region_end = loop_begin + 1U;
                if (region_end > length_frames)
                {
                    region_end = length_frames;
                    loop_begin = (length_frames > 1U) ? (length_frames - 1U) : 0U;
                    if (region_begin > loop_begin)
                    {
                        region_begin = loop_begin;
                    }
                }
            }
        }
        else if (region_end <= region_begin)
        {
            region_end = region_begin + 1U;
            if (region_end > length_frames)
            {
                region_end = length_frames;
                region_begin = (length_frames > 1U) ? (length_frames - 1U) : 0U;
            }
            loop_begin = region_begin;
        }

        if (region_end <= region_begin)
        {
            region_begin = 0U;
            region_end = (length_frames != 0U) ? 1U : 0U;
            loop_begin = region_begin;
        }
        if (loop_begin < region_begin)
        {
            loop_begin = region_begin;
        }
        if (loop_begin >= region_end)
        {
            loop_begin = region_begin;
        }
    }
    if (out_begin != NULL)
    {
        *out_begin = region_begin;
    }
    if (out_end != NULL)
    {
        *out_end = region_end;
    }
    if (out_loop_begin != NULL)
    {
        *out_loop_begin = loop_begin;
    }
}

static void brick6_sampler_runtime_build_effective_ram_slice_region(uint32_t region_begin,
                                                                    uint32_t region_end,
                                                                    uint32_t slice_count,
                                                                    uint32_t slice_index,
                                                                    uint32_t *out_begin,
                                                                    uint32_t *out_end)
{
    uint32_t slice_begin = region_begin;
    uint32_t slice_end = region_end;
    const uint32_t region_frames = (region_end > region_begin) ? (region_end - region_begin) : 0U;
    if ((region_frames != 0U) && (slice_count != 0U))
    {
        if (slice_index >= slice_count)
        {
            slice_index = slice_count - 1U;
        }
        slice_begin = region_begin
                      + (uint32_t)(((uint64_t)region_frames * (uint64_t)slice_index)
                                   / (uint64_t)slice_count);
        slice_end = ((slice_index + 1U) >= slice_count)
                        ? region_end
                        : (region_begin
                           + (uint32_t)(((uint64_t)region_frames
                                        * (uint64_t)(slice_index + 1U))
                                       / (uint64_t)slice_count));
        if (slice_end <= slice_begin)
        {
            if (slice_begin >= region_end)
            {
                slice_begin = region_end - 1U;
            }
            slice_end = slice_begin + 1U;
            if (slice_end > region_end)
            {
                slice_end = region_end;
            }
        }
    }
    if (out_begin != NULL)
    {
        *out_begin = slice_begin;
    }
    if (out_end != NULL)
    {
        *out_end = slice_end;
    }
}

static uint8_t brick6_sampler_runtime_ram_mode_to_loop_mode(uint8_t mode,
                                                            uint32_t region_begin,
                                                            uint32_t region_end,
                                                            uint8_t loop_valid)
{
    (void)loop_valid;
    if ((mode == 2U) && (region_end > region_begin))
    {
        return (uint8_t)BRICK6_SAMPLER_LOOP_FORWARD;
    }
    if (mode == 3U)
    {
        return ((region_end - region_begin) > 1U)
                   ? (uint8_t)BRICK6_SAMPLER_LOOP_PINGPONG
                   : (uint8_t)BRICK6_SAMPLER_LOOP_NONE;
    }
    return (uint8_t)BRICK6_SAMPLER_LOOP_NONE;
}

static void brick6_sampler_runtime_reconcile_ram_voice_bounds_live(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    const sampler_ram_slot_t *ram = NULL;
    if ((voice->active == 0U)
        || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_RAM)
        || (brick6_sampler_runtime_ram_slot_valid(voice, &ram) == 0U))
    {
        return;
    }

    uint32_t region_begin = 0U;
    uint32_t region_end = ram->frames;
    uint32_t loop_begin = 0U;
    const uint8_t slice_voice_active = ((voice->use_slice != 0U) && (voice->slice_count != 0U))
                                           ? 1U
                                           : 0U;
    brick6_sampler_runtime_build_effective_ram_bounds(ram->frames,
                                                      voice->start,
                                                      voice->end,
                                                      voice->loop_start,
                                                      voice->mode,
                                                      (slice_voice_active == 0U) ? 1U : 0U,
                                                      &region_begin,
                                                      &region_end,
                                                      &loop_begin);
    if (slice_voice_active != 0U)
    {
        const uint32_t slice_count =
            (uint32_t)brick6_sampler_runtime_resolve_grid_count(voice->slice_count);
        const uint32_t clamped_slice_count = (slice_count == 0U) ? 1U : slice_count;
        const uint32_t base_begin = region_begin;
        const uint32_t base_end = region_end;
        const uint32_t slice_index =
            ((uint32_t)voice->slice_index < clamped_slice_count)
                ? (uint32_t)voice->slice_index
                : 0U;
        brick6_sampler_runtime_build_effective_ram_slice_region(base_begin,
                                                                base_end,
                                                                clamped_slice_count,
                                                                slice_index,
                                                                &region_begin,
                                                                &region_end);
        loop_begin = region_begin;
    }

    const uint8_t loop_mode =
        brick6_sampler_runtime_ram_mode_to_loop_mode(voice->mode,
                                                     region_begin,
                                                     region_end,
                                                     1U);
    const uint64_t begin_q16 = (uint64_t)region_begin << 16U;
    const uint64_t end_q16 = (uint64_t)region_end << 16U;
    uint64_t position_q16 = voice->ram_position_q16;
    if ((position_q16 == 0ULL) && (voice->position > 0.0f))
    {
        position_q16 = (uint64_t)(voice->position * (float)BRICK6_SAMPLER_Q16_ONE + 0.5f);
    }

    voice->region_begin = region_begin;
    voice->region_end = region_end;
    voice->loop_begin = loop_begin;
    voice->loop_frames = region_end - loop_begin;
    voice->loop_mode = loop_mode;
    if (voice->mode == 1U)
    {
        voice->reverse = 1U;
    }
    else if (loop_mode != (uint8_t)BRICK6_SAMPLER_LOOP_PINGPONG)
    {
        voice->reverse = 0U;
    }

    uint64_t reconciled_q16 = position_q16;
    uint8_t recaled = 0U;
    if (voice->mode == 2U)
    {
        voice->reverse = 0U;
        if (position_q16 < begin_q16)
        {
            reconciled_q16 = begin_q16;
            recaled = 1U;
        }
        else if (position_q16 >= end_q16)
        {
            reconciled_q16 = (uint64_t)loop_begin << 16U;
            recaled = 1U;
        }
    }
    else if (voice->mode == 3U)
    {
        if (position_q16 < begin_q16)
        {
            reconciled_q16 = begin_q16;
            voice->reverse = 0U;
            recaled = 1U;
        }
        else if (position_q16 >= end_q16)
        {
            reconciled_q16 = (uint64_t)(region_end - 1U) << 16U;
            voice->reverse = 1U;
            recaled = 1U;
        }
    }
    else if (position_q16 < begin_q16)
    {
        reconciled_q16 = begin_q16;
        recaled = 1U;
    }

    if ((voice->mode <= 1U) && (position_q16 >= end_q16))
    {
        reconciled_q16 = (uint64_t)(region_end - 1U) << 16U;
        recaled = 1U;
    }

    if (recaled != 0U)
    {
        brick6_sampler_runtime_begin_declick_tail(track_id, voice);
        voice->ram_position_q16 = reconciled_q16;
        voice->position = (float)reconciled_q16 / (float)BRICK6_SAMPLER_Q16_ONE;
        brick6_sampler_runtime_start_declick_fade_in(voice);
    }
}

static void brick6_sampler_runtime_reproject_ram_voice_tune_live(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    const sampler_ram_slot_t *ram = NULL;
    if ((voice->active == 0U)
        || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_RAM)
        || (brick6_sampler_runtime_ram_slot_valid(voice, &ram) == 0U))
    {
        return;
    }

    const uint8_t slice_voice_active = ((voice->use_slice != 0U) && (voice->slice_count != 0U))
                                           ? 1U
                                           : 0U;
    const uint32_t step_q16 = (slice_voice_active != 0U)
                                  ? brick6_sampler_runtime_ram_slice_step_q16(voice, ram)
                                  : brick6_sampler_runtime_ram_step_q16(voice, ram);
    voice->ram_step_q16 = step_q16;
    voice->step_signed = (float)step_q16 / (float)BRICK6_SAMPLER_Q16_ONE;
}

static void brick6_sampler_runtime_rebuild_grid(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    voice->slice_grid_generation = 0U;
    memset(voice->slice_begin, 0, sizeof(voice->slice_begin));
    memset(voice->slice_end, 0, sizeof(voice->slice_end));

    if (brick6_sampler_runtime_ram_slice_mode_active(track_id) != 0U)
    {
        (void)brick6_sampler_runtime_prepare_ram_slice_grid(track_id, NULL);
    }
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
    memset(g_sampler_declick_tail, 0, sizeof(g_sampler_declick_tail));
    for (uint8_t i = 0U; i < BRICK6_MAX_CLIP_TRACKS; ++i)
    {
        g_sampler_clip_slots[i].owner_track_id = BRICK6_SAMPLER_CLIP_SLOT_NONE;
    }
    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        g_sampler_multi_voice[i].owner_track_id = UINT8_MAX;
        g_sampler_multi_voice[i].multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
        g_sampler_multi_voice[i].multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
        g_sampler_multi_voice[i].ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
        g_sampler_multi_voice[i].ram_step_q16 = BRICK6_SAMPLER_Q16_ONE;
        sample_stream_manager_active_state_reset(&g_sampler_multi_voice[i].stream_state);
        sample_stream_manager_active_state_reset(&g_sampler_multi_voice[i].loop_stream_state);
    }
    for (uint8_t i = 0U; i < SEQ_TRACK_COUNT; ++i)
    {
        g_sampler_voice[i].note = 60U;
        g_sampler_voice[i].source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
        g_sampler_voice[i].multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
        g_sampler_voice[i].multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
        g_sampler_voice[i].ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
        g_sampler_voice[i].velocity = 127U;
        g_sampler_voice[i].gain = 1.0f;
        g_sampler_voice[i].trigger_velocity_gain = 1.0f;
        g_sampler_voice[i].step_signed = 1.0f;
        g_sampler_voice[i].ram_step_q16 = BRICK6_SAMPLER_Q16_ONE;
        g_sampler_voice[i].start = 0.0f;
        g_sampler_voice[i].end = 1.0f;
        g_sampler_voice[i].loop_start = 0.0f;
        g_sampler_voice[i].slice_count = 0U;
        g_sampler_voice[i].loop_begin = 0U;
        g_sampler_voice[i].loop_mode = 0U;
        g_sampler_voice[i].reverse = 0U;
        sample_stream_manager_active_state_reset(&g_sampler_voice[i].stream_state);
        sample_stream_manager_active_state_reset(&g_sampler_voice[i].loop_stream_state);
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

    brick6_sampler_runtime_begin_declick_tail(track_id, &g_sampler_voice[track_id]);
    brick6_sampler_runtime_multi_stop_track(track_id);
    memset(&g_sampler_voice[track_id], 0, sizeof(g_sampler_voice[track_id]));
    sample_cache_stop_voice(brick6_sampler_runtime_cache_voice_id(track_id));
    sample_voice_reader_reset(&g_sampler_voice[track_id].reader);
    g_sampler_voice[track_id].note = 60U;
    g_sampler_voice[track_id].source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    g_sampler_voice[track_id].multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    g_sampler_voice[track_id].multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    g_sampler_voice[track_id].ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
    g_sampler_voice[track_id].ram_generation = 0U;
    g_sampler_voice[track_id].ram_data = NULL;
    g_sampler_voice[track_id].ram_channels = 0U;
    g_sampler_voice[track_id].ram_format = SAMPLER_RAM_FORMAT_NONE;
    g_sampler_voice[track_id].velocity = 127U;
    g_sampler_voice[track_id].gain = 1.0f;
    g_sampler_voice[track_id].trigger_velocity_gain = 1.0f;
    g_sampler_voice[track_id].step_signed = 1.0f;
    g_sampler_voice[track_id].ram_step_q16 = BRICK6_SAMPLER_Q16_ONE;
    g_sampler_voice[track_id].end = 1.0f;
    g_sampler_voice[track_id].loop_start = 0.0f;
    g_sampler_voice[track_id].loop_begin = 0U;
    g_sampler_voice[track_id].slice_count = 0U;
    g_sampler_voice[track_id].sample = NULL;
    g_sampler_voice[track_id].release_pending = 0U;
    sample_stream_manager_active_state_reset(&g_sampler_voice[track_id].stream_state);
    sample_stream_manager_active_state_reset(&g_sampler_voice[track_id].loop_stream_state);
    brick6_sampler_runtime_multi_track_reset(track_id);
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
        brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[track_id];
        if ((clip->sample_id != sample_id)
                && ((g_sampler_voice[track_id].active != 0U)
                    || (g_sampler_voice[track_id].reader.active != 0U)))
        {
            brick6_sampler_runtime_clip_stop_playback(track_id);
        }
        else if (clip->sample_id != sample_id)
        {
            sample_cache_stop_voice(brick6_sampler_runtime_cache_voice_id(track_id));
        }
        clip->sample_id = sample_id;
        return;
    }

    g_sampler_voice[track_id].sample_id = sample_id;
    g_sampler_voice[track_id].note = 60U;
    g_sampler_voice[track_id].position = 0.0f;
    if (brick6_sampler_runtime_ram_slice_mode_active(track_id) != 0U)
    {
        brick6_sampler_runtime_rebuild_grid(track_id);
    }
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

void brick6_sampler_runtime_stop_multi_instrument(uint16_t instrument_id)
{
    if ((instrument_id == MULTI_SAMPLE_POOL_INVALID_ID)
        || (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS))
    {
        return;
    }

    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
        if ((voice->active != 0U)
            && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_MULTI)
            && (voice->multi_instrument_id == instrument_id))
        {
            brick6_sampler_runtime_multi_stop_voice(
                voice,
                (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_STEAL);
        }
    }
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

void brick6_sampler_runtime_set_multi_loop(uint8_t track_id, uint8_t enabled)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_sampler_multi_track_state[track_id].loop_enabled = (enabled != 0U) ? 1U : 0U;
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
    brick6_sampler_runtime_reconcile_ram_voice_bounds_live(track_id);
    if (brick6_sampler_runtime_ram_slice_mode_active(track_id) != 0U)
    {
        brick6_sampler_runtime_rebuild_grid(track_id);
    }
}

void brick6_sampler_runtime_set_end(uint8_t track_id, float end)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    g_sampler_voice[track_id].end = end;
    brick6_sampler_runtime_reconcile_ram_voice_bounds_live(track_id);
    if (brick6_sampler_runtime_ram_slice_mode_active(track_id) != 0U)
    {
        brick6_sampler_runtime_rebuild_grid(track_id);
    }
}

void brick6_sampler_runtime_set_mode(uint8_t track_id, uint8_t mode)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    g_sampler_voice[track_id].mode = (mode > 5U) ? 0U : mode;
    brick6_sampler_runtime_reconcile_ram_voice_bounds_live(track_id);
}

void brick6_sampler_runtime_set_tune(uint8_t track_id, float tune)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    g_sampler_voice[track_id].tune = tune;
    brick6_sampler_runtime_reproject_ram_voice_tune_live(track_id);
}

void brick6_sampler_runtime_set_loop_start(uint8_t track_id, float loop_start)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }
    g_sampler_voice[track_id].loop_start = loop_start;
    if (brick6_sampler_runtime_ram_slice_mode_active(track_id) == 0U)
    {
        brick6_sampler_runtime_reconcile_ram_voice_bounds_live(track_id);
    }
}

void brick6_sampler_runtime_set_slice_count(uint8_t track_id, uint8_t slice_count)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_sampler_voice[track_id].slice_count = brick6_sampler_runtime_resolve_grid_count(slice_count);
    brick6_sampler_runtime_rebuild_grid(track_id);
}

uint8_t brick6_sampler_runtime_get_ram_playhead(uint8_t track_id,
                                                uint16_t sample_id,
                                                brick6_sampler_ram_playhead_snapshot_t *out_snapshot)
{
    if (out_snapshot != NULL)
    {
        memset(out_snapshot, 0, sizeof(*out_snapshot));
    }
    if ((track_id >= SEQ_TRACK_COUNT) || (out_snapshot == NULL))
    {
        return 0U;
    }

    const brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    if ((voice->active == 0U)
        || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_RAM)
        || (voice->sample_id != sample_id)
        || (voice->ram_slot == SAMPLER_RAM_POOL_INVALID_SLOT))
    {
        return 0U;
    }

    const sampler_ram_slot_t *const ram = sampler_ram_pool_get_slot(voice->ram_slot);
    if ((ram == NULL)
        || (ram->state != SAMPLER_RAM_SLOT_READY)
        || (ram->global_slot != sample_id)
        || (ram->generation != voice->ram_generation)
        || (ram->frames == 0U))
    {
        return 0U;
    }

    uint32_t frame = 0U;
    if (voice->position > 0.0f)
    {
        frame = (uint32_t)voice->position;
    }
    if (frame >= ram->frames)
    {
        frame = ram->frames - 1U;
    }

    out_snapshot->active = 1U;
    out_snapshot->reverse = voice->reverse;
    out_snapshot->sample_id = voice->sample_id;
    out_snapshot->ram_slot = voice->ram_slot;
    out_snapshot->ram_generation = voice->ram_generation;
    out_snapshot->frame = frame;
    out_snapshot->frame_count = ram->frames;
    out_snapshot->trigger_order = voice->trigger_order;
    return 1U;
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

    if (brick6_sampler_runtime_ram_slice_mode_active(track_id) != 0U)
    {
        brick6_sampler_runtime_trigger_ram_slice(track_id);
        return;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    if (brick6_sampler_runtime_trigger_ram(track_id) != 0U)
    {
        return;
    }

    brick6_sampler_runtime_begin_declick_tail(track_id, voice);
    sample_cache_stop_voice(brick6_sampler_runtime_cache_voice_id(track_id));
    sample_voice_reader_reset(&voice->reader);
    voice->active = 0U;
    voice->position = 0.0f;
    voice->sample = NULL;
    voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    voice->multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
    voice->ram_generation = 0U;
    voice->ram_data = NULL;
    voice->ram_channels = 0U;
    voice->ram_format = SAMPLER_RAM_FORMAT_NONE;
    voice->use_segment_cursor = 0U;
    voice->release_pending = 0U;
    memset(&voice->play_plan, 0, sizeof(voice->play_plan));
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

    if (brick6_sampler_runtime_track_is_clip(track_id) != 0U)
    {
        brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[track_id];
        clip->state = (brick6_sampler_runtime_clip_start_playback(track_id) != 0U)
                          ? (uint8_t)BRICK6_SAMPLER_CLIP_STATE_PLAYING
                          : (uint8_t)BRICK6_SAMPLER_CLIP_STATE_STOPPED;
        return;
    }

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

    const uint8_t stopped_track_id = voice->owner_track_id;
    const uint16_t stopped_multi_sample_id = voice->multi_sample_id;
    /* Normal Note Off already made this a no-op; EOF, steal and forced stops
     * also close a still-held mixer gate and cannot leave it stale. */
    brick6_sampler_runtime_multi_release_voice_vca(voice);
    if ((reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_NONE)
        || (reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_STEAL)
        || (reason == (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_REL_DONE))
    {
        brick6_sampler_runtime_begin_declick_tail(stopped_track_id, voice);
    }

    if (__get_IPSR() != 0U)
    {
        brick6_sampler_runtime_multi_mark_voice_stream_owner_release(voice);
    }
    else
    {
        brick6_sampler_runtime_multi_release_voice_stream_owner(voice);
    }

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
    sample_stream_manager_active_state_reset(&voice->loop_stream_state);
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

    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track_id);
    uint8_t mix_track = 0U;
    if ((ctx != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI)
            && (track_runtime_get_mix_target_track(track_id, &mix_track) != 0U))
    {
        mixer_track_vca_all_notes_off(mix_track);
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
        .format = voice->play_plan.format,
        .stride_floats = voice->play_plan.stride_floats,
        .frames_per_page = voice->play_plan.frames_per_page,
        .registration_epoch = voice->play_plan.registration_epoch,
        .current_frame = current_frame,
        .end_frame = voice->region_end,
        .step_q16 = voice->play_plan.step_q16,
        .direction = 1,
        .lookahead_pages = (uint8_t)(sample_audio_format_window_pages(
                                        sample_audio_format_or_stereo(voice->play_plan.format))
                                    - 1U),
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

static uint8_t brick6_sampler_runtime_multi_prefetch_loop_begin(brick6_sampler_voice_t *voice)
{
    if ((voice == NULL)
        || (voice->active == 0U)
        || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_MULTI)
        || (voice->multi_sample_id == MULTI_SAMPLE_POOL_INVALID_ID)
        || (voice->loop_mode != BRICK6_SAMPLER_LOOP_FORWARD)
        || (voice->play_plan.loop_mode != (uint8_t)SAMPLE_PLAY_LOOP_FORWARD)
        || (voice->play_plan.loop_end <= voice->play_plan.loop_begin)
        || (voice->play_plan.loop_begin >= voice->region_end))
    {
        return 0U;
    }

    const sample_stream_active_desc_t stream_desc = {
        .key = brick6_sampler_runtime_multi_key(voice->multi_sample_id),
        .format = voice->play_plan.format,
        .stride_floats = voice->play_plan.stride_floats,
        .frames_per_page = voice->play_plan.frames_per_page,
        .registration_epoch = voice->play_plan.registration_epoch,
        .current_frame = voice->play_plan.loop_begin,
        .end_frame = voice->region_end,
        .step_q16 = voice->play_plan.step_q16,
        .direction = 1,
        .lookahead_pages = (uint8_t)(sample_audio_format_window_pages(
                                        sample_audio_format_or_stereo(voice->play_plan.format))
                                    - 1U),
        .request_current_page = 1U,
        .owner_kind = (uint8_t)SAMPLE_STREAM_OWNER_MULTI_LOOP,
        .owner_id = brick6_sampler_runtime_multi_voice_index(voice),
        .owner_generation = voice->trigger_order,
        .state = &voice->loop_stream_state,
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
        .format = voice->play_plan.format,
        .stride_floats = voice->play_plan.stride_floats,
        .frames_per_page = voice->play_plan.frames_per_page,
        .registration_epoch = voice->play_plan.registration_epoch,
        .current_frame = 0U,
        .end_frame = voice->region_end,
        .step_q16 = voice->play_plan.step_q16,
        .direction = 1,
        .lookahead_pages = (uint8_t)(sample_audio_format_window_pages(
                                        sample_audio_format_or_stereo(voice->play_plan.format))
                                    - 1U),
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
    (void)brick6_sampler_runtime_multi_prefetch_loop_begin(voice);
}

static void brick6_sampler_runtime_multi_service_streaming(void)
{
    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        (void)brick6_sampler_runtime_multi_prefetch_voice(&g_sampler_multi_voice[i]);
        (void)brick6_sampler_runtime_multi_prefetch_loop_begin(&g_sampler_multi_voice[i]);
    }
}

static void brick6_sampler_runtime_multi_release_inactive_stream_owners(void)
{
    if (__get_IPSR() != 0U)
    {
        return;
    }

    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        brick6_sampler_voice_t *const voice = &g_sampler_multi_voice[i];
        if (voice->stream_owner_release_pending == 0U)
        {
            continue;
        }

        const uint8_t voice_index = brick6_sampler_runtime_multi_voice_index(voice);
        const uint32_t generation = voice->stream_owner_release_generation;
        const uint16_t stopped_multi_sample_id = voice->stream_owner_release_sample_id;
        brick6_sampler_runtime_multi_release_voice_stream_owner_generation(voice_index,
                                                                           generation);
        voice->stream_owner_release_pending = 0U;
        voice->stream_owner_release_generation = 0U;
        voice->stream_owner_release_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
        if ((voice->active == 0U) || (voice->trigger_order == generation))
        {
            sample_stream_manager_active_state_reset(&voice->stream_state);
            sample_stream_manager_active_state_reset(&voice->loop_stream_state);
        }
        brick6_sampler_runtime_multi_defer_stream_release(stopped_multi_sample_id);
        if (voice->active == 0U)
        {
            voice->trigger_order = 0U;
            voice->release_pending = 0U;
            voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
            voice->multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
            voice->multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
            voice->owner_track_id = UINT8_MAX;
        }
    }
}

void brick6_sampler_runtime_queue_stream_pages(void)
{
    brick6_sampler_runtime_multi_release_inactive_stream_owners();
    brick6_sampler_runtime_multi_service_streaming();
    brick6_sampler_runtime_multi_release_inactive_stream_owners();
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
    brick6_sampler_runtime_multi_release_inactive_stream_owners();
    brick6_sampler_runtime_multi_service_stream_releases();
    brick6_sampler_runtime_multi_service_streaming();
    for (uint8_t track_id = 0U; track_id < SEQ_TRACK_COUNT; ++track_id)
    {
        if (brick6_sampler_runtime_ram_slice_mode_active(track_id) != 0U)
        {
            (void)brick6_sampler_runtime_prepare_ram_slice_grid(track_id, NULL);
        }
    }
    brick6_sampler_runtime_multi_release_inactive_stream_owners();
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
            && ((voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_CLASSIC)
                || (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_RAM))
            && (brick6_sampler_runtime_ram_slice_mode_active(track_id) == 0U)
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
    g_sampler_multi_alloc_stole_voice = 0U;
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
            g_sampler_multi_alloc_stole_voice = 1U;
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
                g_sampler_multi_alloc_stole_voice = 1U;
                brick6_sampler_runtime_multi_stop_voice(
                    global,
                    (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_STEAL);
                return voice;
            }

            if (brick6_sampler_runtime_steal_oldest_oneshot() != 0U)
            {
                g_brick6_sampler_runtime_diag.multi_voice_stolen_global++;
                g_sampler_multi_alloc_stole_voice = 1U;
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
        g_sampler_multi_alloc_stole_voice = 1U;
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

    uint8_t local_start_fade = 0U;
    if (voice->active != 0U)
    {
        g_brick6_sampler_runtime_diag.multi_last_stolen_kind = voice->source_kind;
        g_brick6_sampler_runtime_diag.multi_last_stolen_track = track_id;
        local_start_fade = brick6_sampler_runtime_begin_declick_tail(track_id, voice);
        sample_voice_reader_stop(&voice->reader);
        voice->active = 0U;
        voice->position = 0.0f;
        voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
        voice->ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
        voice->ram_generation = 0U;
        voice->ram_data = NULL;
        voice->ram_channels = 0U;
        voice->ram_format = SAMPLER_RAM_FORMAT_NONE;
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
    if (brick6_sampler_runtime_start_gate_check(&common_plan, 0U) == 0U)
    {
        brick6_sampler_runtime_multi_release_voice_stream_owner(multi_voice);
        sample_voice_reader_reset(&multi_voice->reader);
        multi_voice->active = 0U;
        multi_voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
        multi_voice->trigger_order = 0U;
        multi_voice->owner_track_id = UINT8_MAX;
        multi_voice->release_pending = 0U;
        sample_stream_manager_active_state_reset(&multi_voice->stream_state);
        sample_stream_manager_active_state_reset(&multi_voice->loop_stream_state);
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
        .format = common_plan.format,
        .stride_floats = common_plan.stride_floats,
        .frames_per_page = common_plan.frames_per_page,
        .registration_epoch = common_plan.registration_epoch,
        .current_frame = common_plan.start_frame,
        .end_frame = common_plan.region_end,
        .step_q16 = common_plan.step_q16,
        .direction = (common_plan.direction != 0U) ? -1 : 1,
        .lookahead_pages = (uint8_t)(sample_audio_format_window_pages(
                                        sample_audio_format_or_stereo(common_plan.format))
                                    - 1U),
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
        sample_stream_manager_active_state_reset(&multi_voice->stream_state);
        sample_stream_manager_active_state_reset(&multi_voice->loop_stream_state);
        return 0U;
    }

    sample_stream_manager_active_state_reset(&multi_voice->loop_stream_state);
    if (common_plan.loop_mode == (uint8_t)SAMPLE_PLAY_LOOP_FORWARD)
    {
        const sample_stream_active_desc_t loop_reserve_desc = {
            .key = common_plan.key,
            .format = common_plan.format,
            .stride_floats = common_plan.stride_floats,
            .frames_per_page = common_plan.frames_per_page,
            .registration_epoch = common_plan.registration_epoch,
            .current_frame = common_plan.loop_begin,
            .end_frame = common_plan.region_end,
            .step_q16 = common_plan.step_q16,
            .direction = 1,
            .lookahead_pages = (uint8_t)(sample_audio_format_window_pages(
                                            sample_audio_format_or_stereo(common_plan.format))
                                        - 1U),
            .request_current_page = 1U,
            .owner_kind = (uint8_t)SAMPLE_STREAM_OWNER_MULTI_LOOP,
            .owner_id = brick6_sampler_runtime_multi_voice_index(multi_voice),
            .owner_generation = multi_voice->trigger_order,
            .state = &multi_voice->loop_stream_state,
        };
        if (sample_stream_manager_reserve_active_pages(&loop_reserve_desc) == 0U)
        {
            g_brick6_sampler_runtime_diag.multi_page_window_missing++;
            brick6_sampler_runtime_multi_release_voice_stream_owner(multi_voice);
            sample_voice_reader_reset(&multi_voice->reader);
            multi_voice->trigger_order = 0U;
            sample_stream_manager_active_state_reset(&multi_voice->stream_state);
            sample_stream_manager_active_state_reset(&multi_voice->loop_stream_state);
            return 0U;
        }
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
    multi_voice->last_out_l = 0.0f;
    multi_voice->last_out_r = 0.0f;
    multi_voice->last_out_valid = 0U;
    multi_voice->start_fade_remaining = 0U;
    if ((local_start_fade != 0U) || (g_sampler_multi_alloc_stole_voice != 0U))
    {
        brick6_sampler_runtime_start_declick_fade_in(multi_voice);
    }
    brick6_sampler_runtime_multi_prefetch_trigger(multi_voice);

    uint8_t mix_track = 0U;
    if (track_runtime_get_mix_target_track(track_id, &mix_track) != 0U)
    {
        mixer_track_vca_note_on(mix_track, note, velocity);
    }

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

        uint8_t mix_track = 0U;
        if (track_runtime_get_mix_target_track(track_id, &mix_track) != 0U)
        {
            mixer_track_vca_note_off(mix_track, note);
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

        if ((g_sampler_voice[track_id].active != 0U)
                && (g_sampler_voice[track_id].source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_CLIP))
        {
            g_sampler_voice[track_id].release_pending = 1U;
        }
        return;
    }

    return;
}

void brick6_sampler_runtime_note_off_note(uint8_t track_id, uint8_t note)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (brick6_sampler_runtime_track_is_clip(track_id) != 0U)
    {
        const brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
        if ((voice->active != 0U) && (voice->note == note))
        {
            brick6_sampler_runtime_note_off(track_id);
        }
        return;
    }

    const brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    if ((voice->active == 0U) || (voice->note != note))
    {
        return;
    }

    if (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_RAM)
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

    brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    brick6_sampler_runtime_begin_declick_tail(track_id, voice);
    voice->active = 0U;
    voice->position = 0.0f;
    voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    voice->multi_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->multi_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
    voice->ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
    voice->ram_generation = 0U;
    voice->ram_data = NULL;
    voice->ram_channels = 0U;
    voice->ram_format = SAMPLER_RAM_FORMAT_NONE;
    sample_voice_reader_stop(&voice->reader);
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

void brick6_sampler_runtime_get_health_snapshot(
    brick6_sampler_runtime_health_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL)
    {
        return;
    }

    out_snapshot->multi_page_underrun =
        g_brick6_sampler_runtime_diag.multi_page_underrun;
    out_snapshot->multi_stop_underrun =
        g_brick6_sampler_runtime_diag.multi_stop_underrun;
    out_snapshot->multi_invalid_instrument_id =
        g_brick6_sampler_runtime_diag.multi_invalid_instrument_id;
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
            float last_l = 0.0f;
            float last_r = 0.0f;
            float fade_buf[AUDIO_BLOCK_SIZE];
            const float *fade_ptr = 0;
            uint32_t fade_count = 0U;
            if (voice->start_fade_remaining != 0U)
            {
                for (uint32_t i = 0U; i < segment.frames; ++i)
                {
                    fade_buf[i] = 1.0f;
                }
                (void)brick6_sampler_runtime_apply_start_fade(voice, fade_buf, segment.frames);
                fade_ptr = fade_buf;
                fade_count = segment.frames;
            }
            if (segment.kernel_type == SAMPLE_KERNEL_REV_1X)
            {
                sample_voice_reader_mix_rev_1x(&segment, render_gain, fade_ptr, fade_count, out_l, out_r, produced, &last_l, &last_r);
            }
            else if (segment.kernel_type == SAMPLE_KERNEL_PITCH_FWD_LINEAR)
            {
                sample_voice_reader_mix_pitch_fwd_linear(&segment, render_gain, fade_ptr, fade_count, out_l, out_r, produced, &last_l, &last_r);
            }
            else if (segment.kernel_type == SAMPLE_KERNEL_PITCH_REV_LINEAR)
            {
                sample_voice_reader_mix_pitch_rev_linear(&segment, render_gain, fade_ptr, fade_count, out_l, out_r, produced, &last_l, &last_r);
            }
            else
            {
                sample_voice_reader_mix_fwd_1x(&segment, render_gain, fade_ptr, fade_count, out_l, out_r, produced, &last_l, &last_r);
            }
            brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
        }
        else
        {
            float fade_buf[AUDIO_BLOCK_SIZE];
            float last_l = 0.0f;
            float last_r = 0.0f;
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
            (void)brick6_sampler_runtime_apply_start_fade(voice, fade_buf, segment.frames);
            if (segment.kernel_type == SAMPLE_KERNEL_REV_1X)
            {
                sample_voice_reader_mix_rev_1x(&segment,
                                               render_gain,
                                               fade_buf,
                                               segment.frames,
                                               out_l,
                                               out_r,
                                               produced,
                                               &last_l,
                                               &last_r);
            }
            else if (segment.kernel_type == SAMPLE_KERNEL_PITCH_FWD_LINEAR)
            {
                sample_voice_reader_mix_pitch_fwd_linear(&segment,
                                                         render_gain,
                                                         fade_buf,
                                                         segment.frames,
                                                         out_l,
                                                         out_r,
                                                         produced,
                                                         &last_l,
                                                         &last_r);
            }
            else if (segment.kernel_type == SAMPLE_KERNEL_PITCH_REV_LINEAR)
            {
                sample_voice_reader_mix_pitch_rev_linear(&segment,
                                                         render_gain,
                                                         fade_buf,
                                                         segment.frames,
                                                         out_l,
                                                         out_r,
                                                         produced,
                                                         &last_l,
                                                         &last_r);
            }
            else
            {
                sample_voice_reader_mix_fwd_1x(&segment,
                                               render_gain,
                                               fade_buf,
                                               segment.frames,
                                               out_l,
                                               out_r,
                                               produced,
                                               &last_l,
                                               &last_r);
            }
            brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
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
            brick6_sampler_runtime_multi_mark_voice_stream_owner_release(voice);
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
            brick6_sampler_runtime_multi_mark_voice_stream_owner_release(voice);
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
            brick6_sampler_runtime_multi_mark_voice_stream_owner_release(voice);
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
            brick6_sampler_runtime_multi_mark_voice_stream_owner_release(voice);
            brick6_sampler_runtime_multi_defer_stream_release(voice->multi_sample_id);
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }

        BRICK6_SAMPLER_RUNTIME_DIAG_INC(segment_cursor_blocks);
        BRICK6_SAMPLER_RUNTIME_DIAG_INC(mixed_segments);

        float last_l = 0.0f;
        float last_r = 0.0f;
        float fade_buf[AUDIO_BLOCK_SIZE];
        const float *fade_ptr = 0;
        uint32_t fade_count = 0U;
        if (voice->start_fade_remaining != 0U)
        {
            for (uint32_t i = 0U; i < segment.frames; ++i)
            {
                fade_buf[i] = 1.0f;
            }
            (void)brick6_sampler_runtime_apply_start_fade(voice, fade_buf, segment.frames);
            fade_ptr = fade_buf;
            fade_count = segment.frames;
        }
        if (segment.kernel_type == SAMPLE_KERNEL_PITCH_FWD_LINEAR)
        {
            sample_voice_reader_mix_pitch_fwd_linear(&segment,
                                                     render_gain,
                                                     fade_ptr,
                                                     fade_count,
                                                     out_l,
                                                     out_r,
                                                     produced,
                                                     &last_l,
                                                     &last_r);
        }
        else
        {
            sample_voice_reader_mix_fwd_1x(&segment,
                                           render_gain,
                                           fade_ptr,
                                           fade_count,
                                           out_l,
                                           out_r,
                                           produced,
                                           &last_l,
                                           &last_r);
        }
        brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);

        const float position_before_commit = voice->reader.position;
        sample_voice_reader_commit_segment(&voice->reader, segment.frames);
        produced += segment.frames;
        voice->position = voice->reader.position;
        if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_FORWARD)
            && (voice->reader.active != 0U)
            && (voice->reader.position < position_before_commit))
        {
            sample_stream_manager_active_state_reset(&voice->stream_state);
        }
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
            brick6_sampler_runtime_multi_mark_voice_stream_owner_release(voice);
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
    if (frames != 0U)
    {
        brick6_sampler_runtime_voice_note_output(voice,
                                                 slot->stretch_render_l[frames - 1U],
                                                 slot->stretch_render_r[frames - 1U]);
    }
}

static uint8_t brick6_sampler_runtime_render_stream_fwd_1x_fast(
    brick6_sampler_voice_t *voice,
    float *out_l,
    float *out_r,
    uint32_t frames)
{
    if ((voice == NULL) || (out_l == NULL) || (out_r == NULL) || (frames == 0U)
        || (voice->active == 0U)
        || (voice->source_kind != (uint8_t)BRICK6_SAMPLER_VOICE_CLIP)
        || (voice->use_segment_cursor == 0U)
        || (voice->reverse != 0U)
        || (voice->loop_mode != BRICK6_SAMPLER_LOOP_NONE)
        || (voice->fade_in_frames != 0U)
        || (voice->fade_out_frames != 0U)
        || (voice->start_fade_remaining != 0U)
        || (voice->play_plan.kernel_type != SAMPLE_KERNEL_FWD_1X)
        || (voice->play_plan.direction != 0U)
        || (voice->play_plan.loop_mode != (uint8_t)SAMPLE_PLAY_LOOP_NONE)
        || (voice->play_plan.step_q16 != BRICK6_SAMPLER_Q16_ONE)
        || (fabsf(voice->step_signed - 1.0f) > BRICK6_SAMPLER_STEP_EPSILON))
    {
        return 0U;
    }

    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    uint32_t produced = 0U;
    while (produced < frames)
    {
        uint32_t rendered = 0U;
        float last_l = 0.0f;
        float last_r = 0.0f;
        if (sample_voice_reader_render_fwd_1x_ready_simple(&voice->reader,
                                                           render_gain,
                                                           out_l,
                                                           out_r,
                                                           frames - produced,
                                                           produced,
                                                           &rendered,
                                                           &last_l,
                                                           &last_r) == 0U)
        {
            break;
        }
        if (rendered == 0U)
        {
            break;
        }

        BRICK6_SAMPLER_RUNTIME_DIAG_INC(fast_path_blocks);
        BRICK6_SAMPLER_RUNTIME_DIAG_INC(mixed_segments);
        brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
        produced += rendered;
        voice->position = voice->reader.position;
        if (voice->reader.active == 0U)
        {
            voice->active = 0U;
            voice->position = 0.0f;
            sample_voice_reader_stop(&voice->reader);
            break;
        }
    }

    if (produced == 0U)
    {
        return 0U;
    }
    if ((produced < frames) && (voice->active != 0U))
    {
        brick6_sampler_render_sample_segment_cursor(voice,
                                                    &out_l[produced],
                                                    &out_r[produced],
                                                    frames - produced);
    }
    return 1U;
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
        sample_voice_reader_stop(&voice->reader);
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
        if (brick6_sampler_runtime_render_stream_fwd_1x_fast(voice, out_l, out_r, frames) != 0U)
        {
            return;
        }
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
        float last_l = 0.0f;
        float last_r = 0.0f;
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
                                                            &underrun,
                                                            &last_l,
                                                            &last_r);
        if (produced != 0U)
        {
            brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
        }
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
        float last_l = 0.0f;
        float last_r = 0.0f;
        if (has_fade == 0U)
        {
            const float sample_gain = render_gain;
            if (block.is_mono != 0U)
            {
                for (uint32_t i = 0U; i < block.frames; ++i)
                {
                    const float sample_l = src_l[i * block.frame_stride];
                    last_l = sample_l * sample_gain;
                    last_r = last_l;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                }
            }
            else
            {
                for (uint32_t i = 0U; i < block.frames; ++i)
                {
                    last_l = src_l[i * block.frame_stride] * sample_gain;
                    last_r = src_r[i * block.frame_stride] * sample_gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
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
                last_l = sample_l * sample_gain;
                last_r = sample_r * sample_gain;
                out_l[produced + i] += last_l;
                out_r[produced + i] += last_r;
            }
        }
        brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);

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

static void brick6_sampler_runtime_render_ram_unpitched(brick6_sampler_voice_t *voice,
                                                        const sampler_ram_slot_t *ram,
                                                        float *out_l,
                                                        float *out_r,
                                                        uint32_t frames,
                                                        uint32_t position)
{
    const uint32_t region_begin = voice->region_begin;
    const uint32_t region_end = voice->region_end;
    const uint32_t loop_begin = ((voice->loop_begin >= region_begin)
                                 && (voice->loop_begin < region_end))
                                    ? voice->loop_begin
                                    : region_begin;
    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    float fade_buf[AUDIO_BLOCK_SIZE];
    const uint8_t use_fade = (voice->start_fade_remaining != 0U) ? 1U : 0U;
    if (use_fade != 0U)
    {
        for (uint32_t i = 0U; i < frames; ++i)
        {
            fade_buf[i] = 1.0f;
        }
        (void)brick6_sampler_runtime_apply_start_fade(voice, fade_buf, frames);
    }

    float last_l = 0.0f;
    float last_r = 0.0f;
    const float *const data = ram->data;
    const uint32_t frame_stride = voice->ram_channels;
    const uint32_t right_offset = frame_stride - 1U;
    uint32_t produced = 0U;
    uint8_t terminal = 0U;
    while (produced < frames)
    {
        if ((voice->reverse == 0U) && (position >= region_end))
        {
            if (voice->loop_mode == BRICK6_SAMPLER_LOOP_FORWARD)
            {
                position = loop_begin;
            }
            else
            {
                terminal = 1U;
                break;
            }
        }
        if ((voice->reverse != 0U) && (position < region_begin))
        {
            terminal = 1U;
            break;
        }

        const uint32_t remaining = (voice->reverse != 0U)
                                       ? (position - region_begin + 1U)
                                       : (region_end - position);
        uint32_t todo = frames - produced;
        if (todo > remaining)
        {
            todo = remaining;
        }
        if (todo == 0U)
        {
            break;
        }

        for (uint32_t i = 0U; i < todo; ++i)
        {
            const uint32_t frame_index = (voice->reverse != 0U)
                                             ? (position - i)
                                             : (position + i);
            float gain = render_gain;
            if (use_fade != 0U)
            {
                gain *= fade_buf[produced + i];
            }
            const uint32_t src = frame_index * frame_stride;
            last_l = data[src] * gain;
            last_r = data[src + right_offset] * gain;
            out_l[produced + i] += last_l;
            out_r[produced + i] += last_r;
        }

        produced += todo;
        if (todo >= remaining)
        {
            if ((voice->reverse == 0U) && (voice->loop_mode == BRICK6_SAMPLER_LOOP_FORWARD))
            {
                position = loop_begin;
            }
            else
            {
                terminal = 1U;
            }
        }
        else if (voice->reverse != 0U)
        {
            position -= todo;
        }
        else
        {
            position += todo;
        }
        if (terminal != 0U)
        {
            break;
        }
    }

    if (produced != 0U)
    {
        brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
    }
    if (terminal != 0U)
    {
        voice->active = 0U;
        voice->position = 0.0f;
        voice->ram_position_q16 = 0ULL;
        voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
        voice->ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
        voice->ram_generation = 0U;
        voice->ram_data = NULL;
        voice->ram_channels = 0U;
        voice->ram_format = SAMPLER_RAM_FORMAT_NONE;
        return;
    }
    voice->ram_position_q16 = (uint64_t)position << 16U;
    voice->position = (float)position;
}

static inline float brick6_sampler_runtime_q16_frac_float(uint64_t position_q16)
{
    const uint32_t frac_q16 = (uint32_t)position_q16 & BRICK6_SAMPLER_Q16_FRAC_MASK;
    return (float)frac_q16 * (1.0f / (float)BRICK6_SAMPLER_Q16_ONE);
}

static inline float brick6_sampler_runtime_q16_to_float(uint64_t position_q16)
{
    const uint32_t frame_index = (uint32_t)(position_q16 >> 16U);
    return (float)frame_index + brick6_sampler_runtime_q16_frac_float(position_q16);
}

static uint8_t brick6_sampler_runtime_render_ram_forward_pitched(
    brick6_sampler_voice_t *voice,
    const sampler_ram_slot_t *ram,
    float *out_l,
    float *out_r,
    uint32_t frames,
    uint64_t *io_position_q16)
{
    if ((voice == NULL) || (ram == NULL) || (out_l == NULL) || (out_r == NULL)
        || (io_position_q16 == NULL) || (frames == 0U) || (voice->reverse != 0U)
        || (voice->loop_mode == BRICK6_SAMPLER_LOOP_PINGPONG))
    {
        return 0U;
    }

    const uint32_t loop_begin = ((voice->loop_begin >= voice->region_begin)
                                 && (voice->loop_begin < voice->region_end))
                                    ? voice->loop_begin
                                    : voice->region_begin;
    const uint64_t begin_q16 = (uint64_t)loop_begin << 16U;
    const uint64_t end_q16 = (uint64_t)voice->region_end << 16U;
    const uint64_t span_q16 = end_q16 - begin_q16;
    const uint64_t last_q16 = (uint64_t)(voice->region_end - 1U) << 16U;
    const uint32_t step_q16 = (voice->ram_step_q16 != 0U)
                                  ? voice->ram_step_q16
                                  : BRICK6_SAMPLER_Q16_ONE;
    if ((span_q16 == 0ULL) || (step_q16 == 0U))
    {
        return 0U;
    }

    uint64_t position_q16 = *io_position_q16;
    const float *const data = ram->data;
    const uint32_t frame_stride = voice->ram_channels;
    const uint32_t right_offset = frame_stride - 1U;
    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    float last_l = 0.0f;
    float last_r = 0.0f;
    uint32_t produced = 0U;
    uint8_t terminal = 0U;

    while (produced < frames)
    {
        if (position_q16 >= end_q16)
        {
            if (voice->loop_mode == BRICK6_SAMPLER_LOOP_FORWARD)
            {
                position_q16 = begin_q16
                               + brick6_sampler_runtime_wrap_q16(position_q16 - begin_q16,
                                                                 span_q16);
            }
            else
            {
                terminal = 1U;
                break;
            }
        }

        uint32_t todo = frames - produced;
        uint8_t safe_segment = 0U;
        if (position_q16 < last_q16)
        {
            const uint64_t distance_to_last_q16 = last_q16 - position_q16;
            const uint32_t until_last =
                (uint32_t)(((distance_to_last_q16 - 1ULL) / (uint64_t)step_q16) + 1ULL);
            if (todo > until_last)
            {
                todo = until_last;
            }
            safe_segment = 1U;
        }
        else
        {
            const uint64_t distance_to_end_q16 = end_q16 - position_q16;
            uint32_t until_end =
                (uint32_t)((distance_to_end_q16 + (uint64_t)step_q16 - 1ULL)
                           / (uint64_t)step_q16);
            if (until_end == 0U)
            {
                until_end = 1U;
            }
            if (todo > until_end)
            {
                todo = until_end;
            }
        }

        if (voice->start_fade_remaining == 0U)
        {
            if (safe_segment != 0U)
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t frame_index = (uint32_t)(position_q16 >> 16U);
                    const uint32_t src = frame_index * frame_stride;
                    last_l = data[src] * render_gain;
                    last_r = data[src + right_offset] * render_gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                    position_q16 += (uint64_t)step_q16;
                }
            }
            else
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t frame_index = (uint32_t)(position_q16 >> 16U);
                    const uint32_t src = frame_index * frame_stride;
                    last_l = data[src] * render_gain;
                    last_r = data[src + right_offset] * render_gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                    position_q16 += (uint64_t)step_q16;
                }
            }
        }
        else
        {
            if (safe_segment != 0U)
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t frame_index = (uint32_t)(position_q16 >> 16U);
                    const float gain = render_gain * brick6_sampler_runtime_ram_fade_gain(voice);
        const uint32_t src = frame_index * frame_stride;
        last_l = data[src] * gain;
        last_r = data[src + right_offset] * gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                    position_q16 += (uint64_t)step_q16;
                }
            }
            else
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t frame_index = (uint32_t)(position_q16 >> 16U);
                    const float gain = render_gain * brick6_sampler_runtime_ram_fade_gain(voice);
                    const uint32_t src = frame_index * frame_stride;
                    last_l = data[src] * gain;
                    last_r = data[src + right_offset] * gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                    position_q16 += (uint64_t)step_q16;
                }
            }
        }

        produced += todo;
        if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_NONE) && (position_q16 >= end_q16))
        {
            terminal = 1U;
            break;
        }
    }

    if (produced != 0U)
    {
        brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
    }
    *io_position_q16 = position_q16;
    return (terminal != 0U) ? 2U : 1U;
}

static uint8_t brick6_sampler_runtime_render_ram_reverse_pitched(
    brick6_sampler_voice_t *voice,
    const sampler_ram_slot_t *ram,
    float *out_l,
    float *out_r,
    uint32_t frames,
    uint64_t *io_position_q16)
{
    if ((voice == NULL) || (ram == NULL) || (out_l == NULL) || (out_r == NULL)
        || (io_position_q16 == NULL) || (frames == 0U) || (voice->reverse == 0U)
        || (voice->loop_mode != BRICK6_SAMPLER_LOOP_NONE))
    {
        return 0U;
    }

    const uint64_t begin_q16 = (uint64_t)voice->region_begin << 16U;
    const uint64_t last_q16 = (uint64_t)(voice->region_end - 1U) << 16U;
    const uint32_t step_q16 = (voice->ram_step_q16 != 0U)
                                  ? voice->ram_step_q16
                                  : BRICK6_SAMPLER_Q16_ONE;
    if (step_q16 == 0U)
    {
        return 0U;
    }

    uint64_t position_q16 = *io_position_q16;
    const float *const data = ram->data;
    const uint32_t frame_stride = voice->ram_channels;
    const uint32_t right_offset = frame_stride - 1U;
    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    float last_l = 0.0f;
    float last_r = 0.0f;
    uint32_t produced = 0U;
    uint8_t terminal = 0U;

    while (produced < frames)
    {
        if (position_q16 < begin_q16)
        {
            terminal = 1U;
            break;
        }

        uint32_t todo = frames - produced;
        uint8_t edge_segment = 0U;
        if (position_q16 >= last_q16)
        {
            todo = 1U;
            edge_segment = 1U;
        }
        else
        {
            const uint64_t distance_from_begin = position_q16 - begin_q16;
            const uint32_t until_begin =
                (uint32_t)((distance_from_begin / (uint64_t)step_q16) + 1ULL);
            if (todo > until_begin)
            {
                todo = until_begin;
            }
        }

        const uint64_t segment_start_q16 = position_q16;
        if (voice->start_fade_remaining == 0U)
        {
            if (edge_segment != 0U)
            {
                const uint32_t frame_index = (uint32_t)(segment_start_q16 >> 16U);
                const uint32_t src0 = frame_index * frame_stride;
                const float l0 = data[src0];
                const float r0 = data[src0 + right_offset];
                last_l = l0 * render_gain;
                last_r = r0 * render_gain;
                out_l[produced] += last_l;
                out_r[produced] += last_r;
            }
            else
            {
                uint64_t render_position_q16 = segment_start_q16;
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t frame_index = (uint32_t)(render_position_q16 >> 16U);
                    const uint32_t src = frame_index * frame_stride;
                    last_l = data[src] * render_gain;
                    last_r = data[src + right_offset] * render_gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                    render_position_q16 -= (uint64_t)step_q16;
                }
            }
        }
        else
        {
            if (edge_segment != 0U)
            {
                const uint32_t frame_index = (uint32_t)(segment_start_q16 >> 16U);
                const uint32_t src0 = frame_index * frame_stride;
                const float gain = render_gain * brick6_sampler_runtime_ram_fade_gain(voice);
                const float l0 = data[src0];
                const float r0 = data[src0 + right_offset];
                last_l = l0 * gain;
                last_r = r0 * gain;
                out_l[produced] += last_l;
                out_r[produced] += last_r;
            }
            else
            {
                uint64_t render_position_q16 = segment_start_q16;
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t frame_index = (uint32_t)(render_position_q16 >> 16U);
                    const float gain = render_gain * brick6_sampler_runtime_ram_fade_gain(voice);
                    const uint32_t src = frame_index * frame_stride;
                    last_l = data[src] * gain;
                    last_r = data[src + right_offset] * gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                    render_position_q16 -= (uint64_t)step_q16;
                }
            }
        }

        produced += todo;
        const uint64_t reverse_advance_q16 = (uint64_t)step_q16 * (uint64_t)todo;
        const uint64_t distance_from_begin = segment_start_q16 - begin_q16;
        if (reverse_advance_q16 > distance_from_begin)
        {
            position_q16 = begin_q16;
            terminal = 1U;
            break;
        }
        position_q16 = segment_start_q16 - reverse_advance_q16;
    }

    if (produced != 0U)
    {
        brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
    }
    *io_position_q16 = position_q16;
    return (terminal != 0U) ? 2U : 1U;
}

static uint64_t brick6_sampler_runtime_wrap_q16(uint64_t offset_q16, uint64_t span_q16)
{
    while (offset_q16 >= span_q16)
    {
        offset_q16 -= span_q16;
    }
    return offset_q16;
}

static void brick6_sampler_runtime_reflect_pingpong_q16(uint64_t begin_q16,
                                                        uint64_t last_offset_q16,
                                                        uint64_t offset_q16,
                                                        uint64_t *io_position_q16,
                                                        uint8_t *io_reverse)
{
    const uint64_t period_q16 = last_offset_q16 * 2ULL;
    const uint64_t phase_q16 = brick6_sampler_runtime_wrap_q16(offset_q16, period_q16);
    if (phase_q16 <= last_offset_q16)
    {
        *io_reverse = 0U;
        *io_position_q16 = begin_q16 + phase_q16;
    }
    else
    {
        *io_reverse = 1U;
        *io_position_q16 = begin_q16 + (period_q16 - phase_q16);
    }
}

static inline float brick6_sampler_runtime_ram_fade_gain(brick6_sampler_voice_t *voice)
{
    if (voice->start_fade_remaining == 0U)
    {
        return 1.0f;
    }

    const float fade_scale = (STEAL_DECLICK_SAMPLES > 1U)
                                 ? (1.0f / (float)(STEAL_DECLICK_SAMPLES - 1U))
                                 : 1.0f;
    const uint8_t fade_pos = (uint8_t)(STEAL_DECLICK_SAMPLES - voice->start_fade_remaining);
    voice->start_fade_remaining--;
    return (float)fade_pos * fade_scale;
}

static uint8_t brick6_sampler_runtime_render_ram_pingpong_unpitched(
    brick6_sampler_voice_t *voice,
    const sampler_ram_slot_t *ram,
    float *out_l,
    float *out_r,
    uint32_t frames,
    uint32_t position)
{
    if ((voice == NULL) || (ram == NULL) || (out_l == NULL) || (out_r == NULL)
        || (frames == 0U) || (voice->loop_mode != BRICK6_SAMPLER_LOOP_PINGPONG))
    {
        return 0U;
    }

    const uint32_t begin = voice->region_begin;
    const uint32_t end = voice->region_end;
    if ((end <= begin) || ((end - begin) <= 1U) || (position < begin) || (position >= end))
    {
        return 0U;
    }

    const float *const data = ram->data;
    const uint32_t frame_stride = voice->ram_channels;
    const uint32_t right_offset = frame_stride - 1U;
    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    float last_l = 0.0f;
    float last_r = 0.0f;
    uint32_t produced = 0U;
    uint8_t reverse = voice->reverse;

    while (produced < frames)
    {
        uint32_t todo = frames - produced;
        if (reverse == 0U)
        {
            const uint32_t available = end - position;
            if (todo > available)
            {
                todo = available;
            }
            if (voice->start_fade_remaining == 0U)
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t src = (position + i) * frame_stride;
                    last_l = data[src] * render_gain;
                    last_r = data[src + right_offset] * render_gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                }
            }
            else
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t src = (position + i) * frame_stride;
                    const float gain = render_gain * brick6_sampler_runtime_ram_fade_gain(voice);
                    last_l = data[src] * gain;
                    last_r = data[src + right_offset] * gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                }
            }
            produced += todo;
            position += todo;
            if (position >= end)
            {
                position = end - 2U;
                reverse = 1U;
            }
        }
        else
        {
            const uint32_t available = position - begin + 1U;
            if (todo > available)
            {
                todo = available;
            }
            if (voice->start_fade_remaining == 0U)
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t src = (position - i) * frame_stride;
                    last_l = data[src] * render_gain;
                    last_r = data[src + right_offset] * render_gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                }
            }
            else
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t src = (position - i) * frame_stride;
                    const float gain = render_gain * brick6_sampler_runtime_ram_fade_gain(voice);
                    last_l = data[src] * gain;
                    last_r = data[src + right_offset] * gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                }
            }
            produced += todo;
            if (todo > (position - begin))
            {
                position = begin + 1U;
                reverse = 0U;
            }
            else
            {
                position -= todo;
            }
        }
    }

    if (produced != 0U)
    {
        brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
    }
    voice->reverse = reverse;
    voice->ram_position_q16 = (uint64_t)position << 16U;
    voice->position = (float)position;
    return 1U;
}

static uint8_t brick6_sampler_runtime_render_ram_pingpong_pitched(
    brick6_sampler_voice_t *voice,
    const sampler_ram_slot_t *ram,
    float *out_l,
    float *out_r,
    uint32_t frames,
    uint64_t *io_position_q16)
{
    if ((voice == NULL) || (ram == NULL) || (out_l == NULL) || (out_r == NULL)
        || (io_position_q16 == NULL) || (frames == 0U)
        || (voice->loop_mode != BRICK6_SAMPLER_LOOP_PINGPONG))
    {
        return 0U;
    }

    const uint64_t begin_q16 = (uint64_t)voice->region_begin << 16U;
    const uint32_t region_frames = voice->region_end - voice->region_begin;
    if (region_frames <= 1U)
    {
        return 0U;
    }

    const uint64_t last_q16 = (uint64_t)(voice->region_end - 1U) << 16U;
    const uint64_t last_offset_q16 = last_q16 - begin_q16;
    const uint32_t step_q16 = (voice->ram_step_q16 != 0U)
                                  ? voice->ram_step_q16
                                  : BRICK6_SAMPLER_Q16_ONE;
    if ((step_q16 == 0U) || (last_offset_q16 == 0ULL))
    {
        return 0U;
    }

    uint64_t position_q16 = *io_position_q16;
    uint8_t reverse = voice->reverse;
    if ((position_q16 < begin_q16) || (position_q16 > last_q16))
    {
        const uint64_t offset = (position_q16 >= begin_q16)
                                    ? (position_q16 - begin_q16)
                                    : (begin_q16 - position_q16);
        brick6_sampler_runtime_reflect_pingpong_q16(begin_q16,
                                                    last_offset_q16,
                                                    offset,
                                                    &position_q16,
                                                    &reverse);
    }

    const float *const data = ram->data;
    const uint32_t frame_stride = voice->ram_channels;
    const uint32_t right_offset = frame_stride - 1U;
    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    float last_l = 0.0f;
    float last_r = 0.0f;
    uint32_t produced = 0U;

    while (produced < frames)
    {
        uint32_t todo = frames - produced;
        if (reverse == 0U)
        {
            const uint64_t distance_to_last = last_q16 - position_q16;
            const uint32_t until_last =
                (uint32_t)((distance_to_last / (uint64_t)step_q16) + 1ULL);
            if (todo > until_last)
            {
                todo = until_last;
            }
            uint64_t render_position_q16 = position_q16;
            if (voice->start_fade_remaining == 0U)
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t frame_index = (uint32_t)(render_position_q16 >> 16U);
                    const uint32_t src = frame_index * frame_stride;
                    last_l = data[src] * render_gain;
                    last_r = data[src + right_offset] * render_gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                    render_position_q16 += (uint64_t)step_q16;
                }
            }
            else
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t frame_index = (uint32_t)(render_position_q16 >> 16U);
                    const float gain = render_gain * brick6_sampler_runtime_ram_fade_gain(voice);
                    const uint32_t src = frame_index * frame_stride;
                    last_l = data[src] * gain;
                    last_r = data[src + right_offset] * gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                    render_position_q16 += (uint64_t)step_q16;
                }
            }
            position_q16 += (uint64_t)step_q16 * (uint64_t)todo;
            produced += todo;
            if (position_q16 > last_q16)
            {
                brick6_sampler_runtime_reflect_pingpong_q16(begin_q16,
                                                            last_offset_q16,
                                                            position_q16 - begin_q16,
                                                            &position_q16,
                                                            &reverse);
            }
        }
        else
        {
            const uint64_t segment_distance_from_begin = position_q16 - begin_q16;
            const uint32_t until_begin =
                (uint32_t)((segment_distance_from_begin / (uint64_t)step_q16) + 1ULL);
            if (todo > until_begin)
            {
                todo = until_begin;
            }
            const uint64_t segment_start_q16 = position_q16;
            uint64_t render_position_q16 = segment_start_q16;
            if (voice->start_fade_remaining == 0U)
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t frame_index = (uint32_t)(render_position_q16 >> 16U);
                    const uint32_t src = frame_index * frame_stride;
                    last_l = data[src] * render_gain;
                    last_r = data[src + right_offset] * render_gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                    render_position_q16 -= (uint64_t)step_q16;
                }
            }
            else
            {
                for (uint32_t i = 0U; i < todo; ++i)
                {
                    const uint32_t frame_index = (uint32_t)(render_position_q16 >> 16U);
                    const float gain = render_gain * brick6_sampler_runtime_ram_fade_gain(voice);
                    const uint32_t src = frame_index * frame_stride;
                    last_l = data[src] * gain;
                    last_r = data[src + right_offset] * gain;
                    out_l[produced + i] += last_l;
                    out_r[produced + i] += last_r;
                    render_position_q16 -= (uint64_t)step_q16;
                }
            }
            const uint64_t reverse_advance_q16 = (uint64_t)step_q16 * (uint64_t)todo;
            const uint64_t distance_from_begin = segment_start_q16 - begin_q16;
            uint8_t bounced = 0U;
            if (reverse_advance_q16 > distance_from_begin)
            {
                bounced = 1U;
            }
            else
            {
                position_q16 = segment_start_q16 - reverse_advance_q16;
            }
            produced += todo;
            if (bounced != 0U)
            {
                brick6_sampler_runtime_reflect_pingpong_q16(begin_q16,
                                                            last_offset_q16,
                                                            reverse_advance_q16 - distance_from_begin,
                                                            &position_q16,
                                                            &reverse);
            }
        }
    }

    if (produced != 0U)
    {
        brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
    }
    voice->reverse = reverse;
    *io_position_q16 = position_q16;
    voice->ram_position_q16 = position_q16;
    voice->position = brick6_sampler_runtime_q16_to_float(position_q16);
    return 1U;
}

static void brick6_sampler_runtime_render_ram(brick6_sampler_voice_t *voice,
                                              float *out_l,
                                              float *out_r,
                                              uint32_t frames)
{
    const sampler_ram_slot_t *ram = NULL;
    if ((voice == NULL) || (out_l == NULL) || (out_r == NULL) || (frames == 0U)
        || (voice->active == 0U))
    {
        return;
    }

    if (brick6_sampler_runtime_ram_slot_valid(voice, &ram) == 0U)
    {
        brick6_sampler_runtime_begin_declick_tail(voice->owner_track_id, voice);
        voice->active = 0U;
        voice->position = 0.0f;
        voice->ram_position_q16 = 0ULL;
        voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
        voice->ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
        voice->ram_generation = 0U;
        voice->ram_data = NULL;
        voice->ram_channels = 0U;
        voice->ram_format = SAMPLER_RAM_FORMAT_NONE;
        return;
    }

    uint64_t position_q16 = voice->ram_position_q16;
    if ((position_q16 == 0ULL) && (voice->position > 0.0f))
    {
        position_q16 = (uint64_t)(voice->position * (float)BRICK6_SAMPLER_Q16_ONE + 0.5f);
    }
    if ((voice->region_end <= voice->region_begin)
        || (voice->region_end > ram->frames)
        || ((voice->loop_mode == BRICK6_SAMPLER_LOOP_NONE)
            && (((voice->reverse == 0U) && (position_q16 >= ((uint64_t)voice->region_end << 16U)))
                || ((voice->reverse != 0U)
                    && (position_q16 < ((uint64_t)voice->region_begin << 16U))))))
    {
        voice->active = 0U;
        voice->position = 0.0f;
        voice->ram_position_q16 = 0ULL;
        return;
    }

    const uint64_t begin_q16 = (uint64_t)voice->region_begin << 16U;
    const uint64_t end_q16 = (uint64_t)voice->region_end << 16U;
    const uint32_t forward_loop_begin =
        ((voice->loop_begin >= voice->region_begin) && (voice->loop_begin < voice->region_end))
            ? voice->loop_begin
            : voice->region_begin;
    const uint64_t forward_loop_begin_q16 = (uint64_t)forward_loop_begin << 16U;
    const uint32_t region_frames = voice->region_end - voice->region_begin;
    const uint64_t forward_loop_span_q16 = end_q16 - forward_loop_begin_q16;
    const uint64_t last_q16 = (uint64_t)(voice->region_end - 1U) << 16U;
    const uint64_t last_offset_q16 = last_q16 - begin_q16;
    const uint32_t step_q16 = (voice->ram_step_q16 != 0U)
                                  ? voice->ram_step_q16
                                  : BRICK6_SAMPLER_Q16_ONE;
    if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_PINGPONG) && (region_frames > 1U))
    {
        if ((step_q16 == BRICK6_SAMPLER_Q16_ONE)
            && ((position_q16 & (uint64_t)(BRICK6_SAMPLER_Q16_ONE - 1U)) == 0ULL))
        {
            if (brick6_sampler_runtime_render_ram_pingpong_unpitched(
                    voice,
                    ram,
                    out_l,
                    out_r,
                    frames,
                    (uint32_t)(position_q16 >> 16U)) != 0U)
            {
                return;
            }
        }
        else if (brick6_sampler_runtime_render_ram_pingpong_pitched(
                     voice,
                     ram,
                     out_l,
                     out_r,
                     frames,
                     &position_q16) != 0U)
        {
            return;
        }
    }

    if ((step_q16 == BRICK6_SAMPLER_Q16_ONE)
        && ((position_q16 & (uint64_t)(BRICK6_SAMPLER_Q16_ONE - 1U)) == 0ULL)
        && (voice->loop_mode != BRICK6_SAMPLER_LOOP_PINGPONG))
    {
        brick6_sampler_runtime_render_ram_unpitched(voice,
                                                    ram,
                                                    out_l,
                                                    out_r,
                                                    frames,
                                                    (uint32_t)(position_q16 >> 16U));
        return;
    }

    if ((voice->reverse == 0U)
        && (voice->loop_mode != BRICK6_SAMPLER_LOOP_PINGPONG))
    {
        const uint8_t forward_result =
            brick6_sampler_runtime_render_ram_forward_pitched(voice,
                                                              ram,
                                                              out_l,
                                                              out_r,
                                                              frames,
                                                              &position_q16);
        if (forward_result != 0U)
        {
            if (forward_result == 2U)
            {
                voice->active = 0U;
                voice->position = 0.0f;
                voice->ram_position_q16 = 0ULL;
                voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
                voice->ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
                voice->ram_generation = 0U;
                voice->ram_data = NULL;
                voice->ram_channels = 0U;
                voice->ram_format = SAMPLER_RAM_FORMAT_NONE;
                return;
            }
            voice->ram_position_q16 = position_q16;
            voice->position = brick6_sampler_runtime_q16_to_float(position_q16);
            return;
        }
    }
    else if ((voice->reverse != 0U)
             && (voice->loop_mode == BRICK6_SAMPLER_LOOP_NONE))
    {
        const uint8_t reverse_result =
            brick6_sampler_runtime_render_ram_reverse_pitched(voice,
                                                              ram,
                                                              out_l,
                                                              out_r,
                                                              frames,
                                                              &position_q16);
        if (reverse_result != 0U)
        {
            if (reverse_result == 2U)
            {
                voice->active = 0U;
                voice->position = 0.0f;
                voice->ram_position_q16 = 0ULL;
                voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
                voice->ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
                voice->ram_generation = 0U;
                voice->ram_data = NULL;
                voice->ram_channels = 0U;
                voice->ram_format = SAMPLER_RAM_FORMAT_NONE;
                return;
            }
            voice->ram_position_q16 = position_q16;
            voice->position = brick6_sampler_runtime_q16_to_float(position_q16);
            return;
        }
    }

    const float render_gain = voice->gain * voice->trigger_velocity_gain;
    float last_l = 0.0f;
    float last_r = 0.0f;
    const float *const data = ram->data;
    const uint32_t frame_stride = voice->ram_channels;
    const uint32_t right_offset = frame_stride - 1U;
    uint32_t produced = 0U;
    uint8_t terminal = 0U;
    while (produced < frames)
    {
        if ((voice->reverse == 0U) && (position_q16 >= end_q16))
        {
            if (voice->loop_mode == BRICK6_SAMPLER_LOOP_FORWARD)
            {
                position_q16 = forward_loop_begin_q16
                               + brick6_sampler_runtime_wrap_q16(
                                   position_q16 - forward_loop_begin_q16,
                                   forward_loop_span_q16);
            }
            else if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_PINGPONG)
                     && (region_frames > 1U)
                     && (last_offset_q16 > 0ULL))
            {
                brick6_sampler_runtime_reflect_pingpong_q16(begin_q16,
                                                            last_offset_q16,
                                                            position_q16 - begin_q16,
                                                            &position_q16,
                                                            &voice->reverse);
            }
            else
            {
                terminal = 1U;
                break;
            }
        }
        if ((voice->reverse != 0U) && (position_q16 < begin_q16))
        {
            if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_PINGPONG)
                && (region_frames > 1U)
                && (last_offset_q16 > 0ULL))
            {
                brick6_sampler_runtime_reflect_pingpong_q16(begin_q16,
                                                            last_offset_q16,
                                                            begin_q16 - position_q16,
                                                            &position_q16,
                                                            &voice->reverse);
            }
            else
            {
                terminal = 1U;
                break;
            }
        }

        if ((position_q16 < begin_q16) || (position_q16 >= end_q16))
        {
            terminal = 1U;
            break;
        }

        const uint32_t frame_index = (uint32_t)(position_q16 >> 16U);
        if ((frame_index < voice->region_begin) || (frame_index >= voice->region_end))
        {
            terminal = 1U;
            break;
        }

        float gain = render_gain;
        if (voice->start_fade_remaining != 0U)
        {
            const float fade_scale = (STEAL_DECLICK_SAMPLES > 1U)
                                         ? (1.0f / (float)(STEAL_DECLICK_SAMPLES - 1U))
                                         : 1.0f;
            const uint8_t fade_pos = (uint8_t)(STEAL_DECLICK_SAMPLES - voice->start_fade_remaining);
            gain *= (float)fade_pos * fade_scale;
            voice->start_fade_remaining--;
        }
                    const uint32_t src = frame_index * frame_stride;
                    last_l = data[src] * gain;
                    last_r = data[src + right_offset] * gain;

        out_l[produced] += last_l;
        out_r[produced] += last_r;
        produced++;

        if (voice->reverse != 0U)
        {
            const uint64_t distance_from_begin = position_q16 - begin_q16;
            if (distance_from_begin < (uint64_t)step_q16)
            {
                if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_PINGPONG)
                    && (region_frames > 1U)
                    && (last_offset_q16 > 0ULL))
                {
                    brick6_sampler_runtime_reflect_pingpong_q16(
                        begin_q16,
                        last_offset_q16,
                        (uint64_t)step_q16 - distance_from_begin,
                        &position_q16,
                        &voice->reverse);
                }
                else
                {
                    terminal = 1U;
                    position_q16 = begin_q16;
                }
            }
            else
            {
                position_q16 -= (uint64_t)step_q16;
            }
        }
        else
        {
            position_q16 += (uint64_t)step_q16;
            if ((voice->loop_mode == BRICK6_SAMPLER_LOOP_NONE) && (position_q16 >= end_q16))
            {
                terminal = 1U;
            }
        }
        if (terminal != 0U)
        {
            break;
        }
    }

    if (produced != 0U)
    {
        brick6_sampler_runtime_voice_note_output(voice, last_l, last_r);
    }
    if (terminal != 0U)
    {
        voice->active = 0U;
        voice->position = 0.0f;
        voice->ram_position_q16 = 0ULL;
        voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
        voice->ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
        voice->ram_generation = 0U;
        voice->ram_data = NULL;
        voice->ram_channels = 0U;
        voice->ram_format = SAMPLER_RAM_FORMAT_NONE;
        return;
    }
    voice->ram_position_q16 = position_q16;
    voice->position = brick6_sampler_runtime_q16_to_float(position_q16);
}

static void brick6_sampler_runtime_clear_ram_voice(brick6_sampler_voice_t *voice)
{
    if (voice == NULL)
    {
        return;
    }

    voice->active = 0U;
    voice->position = 0.0f;
    voice->ram_position_q16 = 0ULL;
    voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
    voice->ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
    voice->ram_generation = 0U;
    voice->ram_data = NULL;
    voice->ram_channels = 0U;
    voice->ram_format = SAMPLER_RAM_FORMAT_NONE;
}

uint8_t brick6_sampler_runtime_track_has_active_ram_voice(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    const brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    return ((voice->active != 0U)
            && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_RAM)) ? 1U : 0U;
}

uint8_t brick6_sampler_runtime_track_ram_is_mono(uint8_t track_id)
{
    if (track_id >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    const brick6_sampler_voice_t *const voice = &g_sampler_voice[track_id];
    const sampler_ram_slot_t *ram = NULL;
    return ((voice->active != 0U)
            && (brick6_sampler_runtime_ram_slot_valid(voice, &ram) != 0U)
            && (ram != NULL)
            && (ram->format == SAMPLER_RAM_FORMAT_FLOAT32_MONO)) ? 1U : 0U;
}

void brick6_sampler_runtime_render_ram_track(const track_runtime_ctx_t *ctx,
                                             float *out_l,
                                             float *out_r,
                                             uint32_t frames)
{
    if ((ctx == NULL) || (out_l == NULL) || (out_r == NULL) || (frames == 0U))
    {
        return;
    }

    if ((ctx->track_id >= SEQ_TRACK_COUNT)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
            || ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_STREAM)
            || ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_MULTI))
    {
        return;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[ctx->track_id];
    if ((voice->active != 0U)
        && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_RAM)
        && (ctx->mix_track_id < MIXER_MAX_TRACKS)
        && (mixer_track_vca_is_running(ctx->mix_track_id) == 0U))
    {
        brick6_sampler_runtime_clear_ram_voice(voice);
    }

    if ((voice->active != 0U)
        && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_RAM))
    {
        brick6_sampler_runtime_render_ram(voice, out_l, out_r, frames);
    }

    brick6_sampler_runtime_mix_declick_tails(ctx->track_id, out_l, out_r, frames);
}

void brick6_sampler_runtime_render_ram_track_mono(const track_runtime_ctx_t *ctx,
                                                  float *out_mono,
                                                  uint32_t frames)
{
    if ((ctx == NULL) || (out_mono == NULL) || (frames == 0U)
        || (frames > AUDIO_BLOCK_SIZE)
        || (brick6_sampler_runtime_track_ram_is_mono(ctx->track_id) == 0U))
    {
        return;
    }

    memset(g_sampler_ram_mono_discard, 0, frames * sizeof(float));
    brick6_sampler_runtime_render_ram_track(ctx,
                                            out_mono,
                                            g_sampler_ram_mono_discard,
                                            frames);
}

void brick6_sampler_runtime_render_stream_track(const track_runtime_ctx_t *ctx,
                                                float *out_l,
                                                float *out_r,
                                                uint32_t frames)
{
    if ((ctx == NULL) || (out_l == NULL) || (out_r == NULL) || (frames == 0U))
    {
        return;
    }

    if ((ctx->track_id >= SEQ_TRACK_COUNT)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
            || ((track_runtime_type_t)ctx->type != TRACK_RUNTIME_TYPE_STREAM))
    {
        return;
    }

    brick6_sampler_clip_runtime_t *const clip = &g_sampler_clip_runtime[ctx->track_id];
    brick6_sampler_clip_slot_t *const slot = brick6_sampler_runtime_clip_get_slot(ctx->track_id);
    brick6_sampler_voice_t *const voice = &g_sampler_voice[ctx->track_id];

    if (clip->state != (uint8_t)BRICK6_SAMPLER_CLIP_STATE_PLAYING)
    {
        if ((voice->active != 0U) || (voice->reader.active != 0U) || (voice->release_pending != 0U))
        {
            brick6_sampler_runtime_clip_stop_playback(ctx->track_id);
        }
        brick6_sampler_runtime_mix_declick_tails(ctx->track_id, out_l, out_r, frames);
        return;
    }

    if ((voice->sample == NULL) || (voice->active == 0U))
    {
        brick6_sampler_runtime_clip_stop_playback(ctx->track_id);
        clip->state = (uint8_t)BRICK6_SAMPLER_CLIP_STATE_STOPPED;
        brick6_sampler_runtime_mix_declick_tails(ctx->track_id, out_l, out_r, frames);
        return;
    }

    if ((voice->release_pending != 0U)
            && (mixer_track_vca_requires_source(ctx->mix_track_id) == 0U))
    {
        brick6_sampler_runtime_clip_stop_playback(ctx->track_id);
        brick6_sampler_runtime_mix_declick_tails(ctx->track_id, out_l, out_r, frames);
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
        voice->release_pending = 0U;
        voice->sample = NULL;
        voice->source_kind = (uint8_t)BRICK6_SAMPLER_VOICE_NONE;
        brick6_sampler_runtime_clip_release_slot(ctx->track_id);
    }
    brick6_sampler_runtime_mix_declick_tails(ctx->track_id, out_l, out_r, frames);
}

void brick6_sampler_runtime_render_multi_track(const track_runtime_ctx_t *ctx,
                                               float *out_l,
                                               float *out_r,
                                               uint32_t frames)
{
    if ((ctx == NULL) || (out_l == NULL) || (out_r == NULL) || (frames == 0U))
    {
        return;
    }

    if ((ctx->track_id >= SEQ_TRACK_COUNT)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
            || ((track_runtime_type_t)ctx->type != TRACK_RUNTIME_TYPE_MULTI))
    {
        return;
    }

    for (uint8_t i = 0U; i < SAMPLER_MULTI_MAX_GLOBAL_VOICES; ++i)
    {
        brick6_sampler_voice_t *const multi_voice = &g_sampler_multi_voice[i];
        if ((multi_voice->active != 0U)
            && (multi_voice->owner_track_id == ctx->track_id))
        {
            if ((multi_voice->release_pending != 0U)
                    && (mixer_track_vca_requires_source(ctx->mix_track_id) == 0U))
            {
                brick6_sampler_runtime_multi_stop_voice(
                    multi_voice,
                    (uint8_t)BRICK6_SAMPLER_MULTI_DIAG_REASON_STOP_REL_DONE);
                continue;
            }
            brick6_sampler_render_multi(multi_voice, out_l, out_r, frames);
        }
    }

    brick6_sampler_runtime_mix_declick_tails(ctx->track_id, out_l, out_r, frames);
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

    if ((ctx->track_id >= SEQ_TRACK_COUNT)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER))
    {
        return;
    }

    switch ((track_runtime_type_t)ctx->type)
    {
        case TRACK_RUNTIME_TYPE_STREAM:
            brick6_sampler_runtime_render_stream_track(ctx, out_l, out_r, frames);
            return;

        case TRACK_RUNTIME_TYPE_MULTI:
            brick6_sampler_runtime_render_multi_track(ctx, out_l, out_r, frames);
            return;

        default:
            break;
    }

    brick6_sampler_voice_t *const voice = &g_sampler_voice[ctx->track_id];
    if ((voice->active != 0U)
        && (voice->source_kind == (uint8_t)BRICK6_SAMPLER_VOICE_RAM))
    {
        brick6_sampler_runtime_render_ram_track(ctx, out_l, out_r, frames);
        return;
    }

    if ((voice->active == 0U) || (voice->sample_id >= SAMPLE_POOL_SIZE))
    {
        brick6_sampler_runtime_mix_declick_tails(ctx->track_id, out_l, out_r, frames);
        return;
    }

    const sample_desc_t *const desc = voice->sample;
    if ((desc == NULL) || (desc->valid == 0U) || (desc->length_frames == 0U))
    {
        voice->active = 0U;
        voice->position = 0U;
        sample_voice_reader_stop(&voice->reader);
        brick6_sampler_runtime_mix_declick_tails(ctx->track_id, out_l, out_r, frames);
        return;
    }

    brick6_sampler_render_sample(desc, voice, out_l, out_r, frames);
    brick6_sampler_runtime_mix_declick_tails(ctx->track_id, out_l, out_r, frames);
}
