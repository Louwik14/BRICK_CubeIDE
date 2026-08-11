#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_key.h"
#include "Sampler/sample_audio_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SAMPLE_KERNEL_FWD_1X = 0,
    SAMPLE_KERNEL_REV_1X,
    SAMPLE_KERNEL_PITCH_FWD_LINEAR,
    SAMPLE_KERNEL_PITCH_REV_LINEAR
} sample_kernel_type_t;

typedef enum
{
    SAMPLE_PLAY_LOOP_NONE = 0,
    SAMPLE_PLAY_LOOP_FORWARD,
    SAMPLE_PLAY_LOOP_PINGPONG
} sample_play_loop_mode_t;

typedef enum
{
    SAMPLE_PLAY_PLAN_BUILD_OK = 0,
    SAMPLE_PLAY_PLAN_BUILD_INVALID_ARG,
    SAMPLE_PLAY_PLAN_BUILD_INVALID_SOURCE,
    SAMPLE_PLAY_PLAN_BUILD_INVALID_REGION,
    SAMPLE_PLAY_PLAN_BUILD_INVALID_RATE
} sample_play_plan_build_result_t;

typedef enum
{
    SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_REGION = (1U << 0),
    SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_LOOP = (1U << 1),
    SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_DIRECTION = (1U << 2),
    SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_RATE = (1U << 3)
} sample_play_plan_build_flags_t;

typedef enum
{
    SAMPLE_PLAY_PLAN_READY_INVALID = 0,
    SAMPLE_PLAY_PLAN_READY_MISSING,
    SAMPLE_PLAY_PLAN_READY_PENDING,
    SAMPLE_PLAY_PLAN_READY_PARTIAL,
    SAMPLE_PLAY_PLAN_READY_COMPLETE
} sample_play_plan_ready_status_t;

typedef struct
{
    sample_audio_key_t key;
    const char *path;
    uint32_t total_frames;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint16_t block_align;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    uint16_t root_note;
    int16_t fine_tune_cents;
    uint32_t region_begin;
    uint32_t region_end;
    uint32_t loop_begin;
    uint32_t loop_end;
    uint8_t loop_mode;
    uint8_t reverse;
    uint8_t reserved;
    float rate;
    float gain;
    uint8_t owner_track_id;
    uint8_t note;
    uint8_t velocity;
    uint8_t source_kind;
    uint16_t instrument_id;
    uint16_t zone_id;
} sample_resolved_source_t;

typedef struct
{
    sample_audio_key_t key;
    uint16_t sample_id;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    uint32_t start_frame;
    uint32_t region_begin;
    uint32_t region_end;
    uint32_t loop_begin;
    uint32_t loop_end;
    uint32_t fade_in_frames;
    uint32_t fade_out_frames;
    uint32_t step_q16;
    uint8_t direction;
    uint8_t loop_mode;
    uint8_t stop_on_underrun;
    sample_kernel_type_t kernel_type;
    uint32_t min_ready_frames;
    uint32_t target_window_frames;
    uint32_t diagnostics_page;
    uint8_t diagnostics_reason;
    uint8_t start_gate_flags;
} sample_play_plan_t;

typedef struct
{
    uint32_t start_frame;
    uint32_t end_frame;
    uint32_t loop_begin;
    uint32_t loop_end;
    float rate;
    uint32_t min_ready_frames;
    uint32_t target_window_frames;
    uint32_t diagnostics_page;
    uint8_t flags;
    uint8_t reverse;
    uint8_t loop_mode;
    uint8_t stop_on_underrun;
    uint8_t diagnostics_reason;
    uint8_t start_gate_flags;
} sample_play_plan_build_options_t;

typedef struct
{
    uint32_t page_start;
    uint32_t page_end;
    uint32_t page_count;
    uint32_t first_page;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint8_t reverse;
    uint8_t valid;
} sample_play_plan_page_span_t;

typedef struct
{
    sample_play_plan_ready_status_t min_status;
    sample_play_plan_ready_status_t window_status;
    uint32_t first_required_page;
    uint32_t first_missing_page;
    uint32_t first_pending_page;
    uint32_t min_required_pages;
    uint32_t min_ready_pages;
    uint32_t min_pending_pages;
    uint32_t min_missing_pages;
    uint32_t target_required_pages;
    uint32_t target_ready_pages;
    uint32_t target_pending_pages;
    uint32_t target_missing_pages;
    uint32_t min_page_start;
    uint32_t min_page_end;
    uint32_t target_page_start;
    uint32_t target_page_end;
    uint8_t min_ready;
    uint8_t target_ready;
} sample_play_plan_ready_result_t;

uint8_t sample_play_plan_frames_to_page_span(const sample_play_plan_t *plan,
                                             uint32_t requested_frames,
                                             sample_play_plan_page_span_t *out_span);
uint32_t sample_play_plan_required_pages_for_frames(const sample_play_plan_t *plan,
                                                    uint32_t requested_frames);
sample_play_plan_ready_status_t sample_play_plan_check_ready_requirements(
    const sample_play_plan_t *plan,
    sample_play_plan_ready_result_t *out_result);

static inline void sample_resolved_source_init(sample_resolved_source_t *source)
{
    if (source != 0)
    {
        *source = (sample_resolved_source_t){0};
    }
}

static inline uint8_t sample_resolved_source_is_valid(const sample_resolved_source_t *source)
{
    return ((source != 0)
            && (source->total_frames != 0U)
            && (source->region_end > source->region_begin)
            && (source->region_end <= source->total_frames)) ? 1U : 0U;
}

static inline void sample_play_plan_init(sample_play_plan_t *plan)
{
    if (plan != 0)
    {
        *plan = (sample_play_plan_t){0};
    }
}

static inline uint8_t sample_play_plan_is_valid(const sample_play_plan_t *plan)
{
    return ((plan != 0)
            && (plan->region_end > plan->region_begin)
            && (plan->start_frame >= plan->region_begin)
            && (plan->start_frame < plan->region_end)) ? 1U : 0U;
}

static inline uint32_t sample_play_plan_rate_to_q16(float rate)
{
    if (rate <= 0.0f)
    {
        return 0U;
    }

    const float scaled = (rate * 65536.0f) + 0.5f;
    if (scaled < 1.0f)
    {
        return 1U;
    }
    if (scaled > 4294967295.0f)
    {
        return UINT32_MAX;
    }
    return (uint32_t)scaled;
}

static inline sample_play_plan_build_result_t sample_play_plan_build_from_source(
    const sample_resolved_source_t *source,
    const sample_play_plan_build_options_t *options,
    sample_play_plan_t *out_plan)
{
    if (out_plan != 0)
    {
        sample_play_plan_init(out_plan);
    }
    if ((source == 0) || (out_plan == 0))
    {
        return SAMPLE_PLAY_PLAN_BUILD_INVALID_ARG;
    }
    if (sample_resolved_source_is_valid(source) == 0U)
    {
        return SAMPLE_PLAY_PLAN_BUILD_INVALID_SOURCE;
    }

    const uint8_t flags = (options != 0) ? options->flags : 0U;
    const uint32_t region_begin =
        ((flags & SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_REGION) != 0U)
            ? source->region_begin
            : ((options != 0) ? options->start_frame : source->region_begin);
    const uint32_t region_end =
        ((flags & SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_REGION) != 0U)
            ? source->region_end
            : ((options != 0) ? options->end_frame : source->region_end);
    if ((region_end <= region_begin) || (region_end > source->total_frames))
    {
        return SAMPLE_PLAY_PLAN_BUILD_INVALID_REGION;
    }

    const uint8_t reverse =
        ((flags & SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_DIRECTION) != 0U)
            ? source->reverse
            : ((options != 0) ? options->reverse : source->reverse);
    const uint32_t start_frame =
        (reverse != 0U) ? ((region_end > region_begin) ? (region_end - 1U) : region_begin)
                        : region_begin;

    const uint32_t loop_begin =
        ((flags & SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_LOOP) != 0U)
            ? source->loop_begin
            : ((options != 0) ? options->loop_begin : source->loop_begin);
    const uint32_t loop_end =
        ((flags & SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_LOOP) != 0U)
            ? source->loop_end
            : ((options != 0) ? options->loop_end : source->loop_end);
    uint8_t loop_mode =
        ((flags & SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_LOOP) != 0U)
            ? source->loop_mode
            : ((options != 0) ? options->loop_mode : source->loop_mode);
    if ((loop_mode != (uint8_t)SAMPLE_PLAY_LOOP_NONE)
        && ((loop_end <= loop_begin) || (loop_begin < region_begin) || (loop_end > region_end)))
    {
        loop_mode = (uint8_t)SAMPLE_PLAY_LOOP_NONE;
    }

    const float rate =
        ((flags & SAMPLE_PLAY_PLAN_BUILD_USE_SOURCE_RATE) != 0U)
            ? source->rate
            : ((options != 0) ? options->rate : source->rate);
    const uint32_t step_q16 = sample_play_plan_rate_to_q16((rate > 0.0f) ? rate : 1.0f);
    if (step_q16 == 0U)
    {
        return SAMPLE_PLAY_PLAN_BUILD_INVALID_RATE;
    }

    out_plan->key = source->key;
    out_plan->sample_id = source->key.object_id;
    out_plan->format = sample_audio_format_or_stereo(source->format);
    out_plan->stride_floats = (uint16_t)sample_audio_format_stride_floats(out_plan->format);
    out_plan->frames_per_page = sample_audio_format_frames_per_page(out_plan->format);
    out_plan->registration_epoch = source->registration_epoch;
    out_plan->start_frame = start_frame;
    out_plan->region_begin = region_begin;
    out_plan->region_end = region_end;
    out_plan->loop_begin = (loop_mode != (uint8_t)SAMPLE_PLAY_LOOP_NONE) ? loop_begin : region_begin;
    out_plan->loop_end = (loop_mode != (uint8_t)SAMPLE_PLAY_LOOP_NONE) ? loop_end : region_end;
    out_plan->fade_in_frames = 0U;
    out_plan->fade_out_frames = 0U;
    out_plan->step_q16 = step_q16;
    out_plan->direction = reverse;
    out_plan->loop_mode = loop_mode;
    out_plan->stop_on_underrun = (options != 0) ? options->stop_on_underrun : 1U;
    out_plan->kernel_type =
        (reverse != 0U)
            ? ((step_q16 == 65536U) ? SAMPLE_KERNEL_REV_1X : SAMPLE_KERNEL_PITCH_REV_LINEAR)
            : ((step_q16 == 65536U) ? SAMPLE_KERNEL_FWD_1X : SAMPLE_KERNEL_PITCH_FWD_LINEAR);
    out_plan->min_ready_frames = (options != 0) ? options->min_ready_frames : 0U;
    out_plan->target_window_frames = (options != 0) ? options->target_window_frames : 0U;
    out_plan->diagnostics_page = (options != 0) ? options->diagnostics_page : UINT32_MAX;
    out_plan->diagnostics_reason = (options != 0) ? options->diagnostics_reason : 0U;
    out_plan->start_gate_flags = (options != 0) ? options->start_gate_flags : 0U;
    return sample_play_plan_is_valid(out_plan) != 0U
               ? SAMPLE_PLAY_PLAN_BUILD_OK
               : SAMPLE_PLAY_PLAN_BUILD_INVALID_REGION;
}

#ifdef __cplusplus
}
#endif
