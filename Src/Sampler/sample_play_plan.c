#include "Sampler/sample_play_plan.h"

#include <stdint.h>

#define SAMPLE_PLAY_PLAN_PAGE_NONE UINT32_MAX

static uint32_t sample_play_plan_region_frames(const sample_play_plan_t *plan)
{
    return (plan->region_end > plan->region_begin) ? (plan->region_end - plan->region_begin) : 0U;
}

static uint32_t sample_play_plan_clamp_requested_frames(const sample_play_plan_t *plan,
                                                        uint32_t requested_frames)
{
    const uint32_t region_frames = sample_play_plan_region_frames(plan);
    if ((region_frames == 0U) || (requested_frames == 0U))
    {
        return 0U;
    }
    return (requested_frames < region_frames) ? requested_frames : region_frames;
}

static uint32_t sample_play_plan_forward_pages(sample_audio_format_t format,
                                               uint32_t start_frame,
                                               uint32_t frames)
{
    const uint32_t frames_per_page = sample_audio_format_frames_per_page(format);
    const uint32_t page_offset = start_frame % frames_per_page;
    const uint32_t covered = page_offset + frames;
    return (covered + (frames_per_page - 1U)) / frames_per_page;
}

static uint32_t sample_play_plan_reverse_pages(sample_audio_format_t format,
                                               uint32_t start_frame,
                                               uint32_t frames)
{
    const uint32_t frames_per_page = sample_audio_format_frames_per_page(format);
    const uint32_t frames_in_first_page = (start_frame % frames_per_page) + 1U;
    if (frames <= frames_in_first_page)
    {
        return 1U;
    }
    return 1U + ((frames - frames_in_first_page + (frames_per_page - 1U)) / frames_per_page);
}

uint8_t sample_play_plan_frames_to_page_span(const sample_play_plan_t *plan,
                                             uint32_t requested_frames,
                                             sample_play_plan_page_span_t *out_span)
{
    sample_play_plan_page_span_t span = {
        .page_start = SAMPLE_PLAY_PLAN_PAGE_NONE,
        .page_end = SAMPLE_PLAY_PLAN_PAGE_NONE,
        .page_count = 0U,
        .first_page = SAMPLE_PLAY_PLAN_PAGE_NONE,
        .format = SAMPLE_AUDIO_FORMAT_INVALID,
        .stride_floats = 0U,
        .frames_per_page = 0U,
        .reverse = 0U,
        .valid = 0U,
    };

    if ((plan == 0) || (sample_play_plan_is_valid(plan) == 0U))
    {
        if (out_span != 0)
        {
            *out_span = span;
        }
        return 0U;
    }

    const uint32_t frames = sample_play_plan_clamp_requested_frames(plan, requested_frames);
    if (frames == 0U)
    {
        if (out_span != 0)
        {
            *out_span = span;
        }
        return 0U;
    }

    const sample_audio_format_t format = sample_audio_format_or_stereo(plan->format);
    const uint32_t frames_per_page = sample_audio_format_frames_per_page(format);
    const uint32_t first_page = sample_audio_format_page_index_from_frame(format, plan->start_frame);
    uint32_t page_count = (plan->direction != 0U)
                              ? sample_play_plan_reverse_pages(format, plan->start_frame, frames)
                              : sample_play_plan_forward_pages(format, plan->start_frame, frames);
    const uint32_t region_first_page = sample_audio_format_page_index_from_frame(format, plan->region_begin);
    const uint32_t region_last_page = sample_audio_format_page_index_from_frame(format, plan->region_end - 1U);

    if (plan->direction != 0U)
    {
        const uint32_t max_pages = first_page - region_first_page + 1U;
        if (page_count > max_pages)
        {
            page_count = max_pages;
        }
    }
    else
    {
        const uint32_t max_pages = region_last_page - first_page + 1U;
        if (page_count > max_pages)
        {
            page_count = max_pages;
        }
    }

    span.page_count = page_count;
    span.first_page = first_page;
    span.format = format;
    span.stride_floats = (uint16_t)sample_audio_format_stride_floats(format);
    span.frames_per_page = frames_per_page;
    span.reverse = (plan->direction != 0U) ? 1U : 0U;
    span.valid = (page_count != 0U) ? 1U : 0U;
    if (span.reverse != 0U)
    {
        span.page_end = first_page;
        span.page_start = first_page - (page_count - 1U);
    }
    else
    {
        span.page_start = first_page;
        span.page_end = first_page + page_count - 1U;
    }

    if (out_span != 0)
    {
        *out_span = span;
    }
    return span.valid;
}

uint32_t sample_play_plan_required_pages_for_frames(const sample_play_plan_t *plan,
                                                    uint32_t requested_frames)
{
    sample_play_plan_page_span_t span;
    return (sample_play_plan_frames_to_page_span(plan, requested_frames, &span) != 0U)
               ? span.page_count
               : 0U;
}
