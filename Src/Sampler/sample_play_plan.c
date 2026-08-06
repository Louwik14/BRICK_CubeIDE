#include "Sampler/sample_play_plan.h"

#include "Sampler/sample_page_cache.h"

#include <stdint.h>

#define SAMPLE_PLAY_PLAN_PAGE_NONE UINT32_MAX

typedef struct
{
    uint32_t required_pages;
    uint32_t ready_pages;
    uint32_t pending_pages;
    uint32_t missing_pages;
    uint32_t first_missing_page;
    uint32_t first_pending_page;
} sample_play_plan_ready_count_t;

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

static sample_play_plan_ready_status_t sample_play_plan_status_from_count(
    const sample_play_plan_ready_count_t *count)
{
    if ((count == 0) || (count->required_pages == 0U))
    {
        return SAMPLE_PLAY_PLAN_READY_INVALID;
    }
    if (count->missing_pages != 0U)
    {
        return SAMPLE_PLAY_PLAN_READY_MISSING;
    }
    if (count->ready_pages == count->required_pages)
    {
        return SAMPLE_PLAY_PLAN_READY_COMPLETE;
    }
    if ((count->ready_pages != 0U) && (count->pending_pages != 0U))
    {
        return SAMPLE_PLAY_PLAN_READY_PARTIAL;
    }
    if (count->pending_pages != 0U)
    {
        return SAMPLE_PLAY_PLAN_READY_PENDING;
    }
    return SAMPLE_PLAY_PLAN_READY_MISSING;
}

static sample_play_plan_ready_count_t sample_play_plan_count_ready_pages(
    const sample_play_plan_t *plan,
    uint32_t required_pages)
{
    sample_play_plan_ready_count_t count = {
        .required_pages = required_pages,
        .first_missing_page = SAMPLE_PLAY_PLAN_PAGE_NONE,
        .first_pending_page = SAMPLE_PLAY_PLAN_PAGE_NONE,
    };

    const sample_audio_format_t format = sample_audio_format_or_stereo(plan->format);
    uint32_t page_index = sample_audio_format_page_index_from_frame(format, plan->start_frame);
    for (uint32_t i = 0U; i < required_pages; ++i)
    {
        const sample_page_state_t state = sample_page_cache_get_page_state_key(plan->key, page_index);
        if (state == SAMPLE_PAGE_READY)
        {
            ++count.ready_pages;
        }
        else if ((state == SAMPLE_PAGE_QUEUED) || (state == SAMPLE_PAGE_IN_FLIGHT))
        {
            ++count.pending_pages;
            if (count.first_pending_page == SAMPLE_PLAY_PLAN_PAGE_NONE)
            {
                count.first_pending_page = page_index;
            }
        }
        else
        {
            ++count.missing_pages;
            if (count.first_missing_page == SAMPLE_PLAY_PLAN_PAGE_NONE)
            {
                count.first_missing_page = page_index;
            }
        }

        if (plan->direction != 0U)
        {
            if (page_index == 0U)
            {
                break;
            }
            --page_index;
        }
        else
        {
            ++page_index;
        }
    }

    return count;
}

sample_play_plan_ready_status_t sample_play_plan_check_ready_requirements(
    const sample_play_plan_t *plan,
    sample_play_plan_ready_result_t *out_result)
{
    sample_play_plan_ready_result_t result = {
        .min_status = SAMPLE_PLAY_PLAN_READY_INVALID,
        .window_status = SAMPLE_PLAY_PLAN_READY_INVALID,
        .first_required_page = SAMPLE_PLAY_PLAN_PAGE_NONE,
        .first_missing_page = SAMPLE_PLAY_PLAN_PAGE_NONE,
        .first_pending_page = SAMPLE_PLAY_PLAN_PAGE_NONE,
    };

    if ((plan == 0) || (sample_play_plan_is_valid(plan) == 0U))
    {
        if (out_result != 0)
        {
            *out_result = result;
        }
        return SAMPLE_PLAY_PLAN_READY_INVALID;
    }

    result.first_required_page = sample_audio_format_page_index_from_frame(
        sample_audio_format_or_stereo(plan->format), plan->start_frame);
    sample_play_plan_page_span_t min_span;
    (void)sample_play_plan_frames_to_page_span(plan, plan->min_ready_frames, &min_span);
    const uint32_t target_frames =
        (plan->target_window_frames != 0U) ? plan->target_window_frames : plan->min_ready_frames;
    sample_play_plan_page_span_t target_span;
    (void)sample_play_plan_frames_to_page_span(plan, target_frames, &target_span);

    const sample_play_plan_ready_count_t min_count =
        sample_play_plan_count_ready_pages(plan, min_span.page_count);
    const sample_play_plan_ready_count_t target_count =
        sample_play_plan_count_ready_pages(plan, target_span.page_count);

    result.min_required_pages = min_count.required_pages;
    result.min_ready_pages = min_count.ready_pages;
    result.min_pending_pages = min_count.pending_pages;
    result.min_missing_pages = min_count.missing_pages;
    result.min_status = sample_play_plan_status_from_count(&min_count);
    result.min_ready = (result.min_status == SAMPLE_PLAY_PLAN_READY_COMPLETE) ? 1U : 0U;

    result.target_required_pages = target_count.required_pages;
    result.target_ready_pages = target_count.ready_pages;
    result.target_pending_pages = target_count.pending_pages;
    result.target_missing_pages = target_count.missing_pages;
    result.min_page_start = min_span.page_start;
    result.min_page_end = min_span.page_end;
    result.target_page_start = target_span.page_start;
    result.target_page_end = target_span.page_end;
    result.window_status = sample_play_plan_status_from_count(&target_count);
    result.target_ready = (result.window_status == SAMPLE_PLAY_PLAN_READY_COMPLETE) ? 1U : 0U;

    result.first_missing_page =
        (min_count.first_missing_page != SAMPLE_PLAY_PLAN_PAGE_NONE)
            ? min_count.first_missing_page
            : target_count.first_missing_page;
    result.first_pending_page =
        (min_count.first_pending_page != SAMPLE_PLAY_PLAN_PAGE_NONE)
            ? min_count.first_pending_page
            : target_count.first_pending_page;

    if (out_result != 0)
    {
        *out_result = result;
    }
    return result.min_status;
}
