#include "Sampler/sample_stream_sequence.h"

#include <stddef.h>

static uint8_t sample_stream_sequence_contains(const uint32_t *pages,
                                                uint8_t count,
                                                uint32_t page)
{
    for (uint8_t i = 0U; i < count; ++i)
    {
        if (pages[i] == page)
        {
            return 1U;
        }
    }
    return 0U;
}

static uint8_t sample_stream_sequence_append(uint32_t page,
                                             uint32_t *pages,
                                             uint8_t capacity,
                                             uint8_t *count)
{
    if ((sample_stream_sequence_contains(pages, *count, page) != 0U))
    {
        return 1U;
    }
    if (*count >= capacity)
    {
        return 0U;
    }
    pages[*count] = page;
    (*count)++;
    return 1U;
}

static uint8_t sample_stream_sequence_append_forward(uint32_t first_page,
                                                     uint32_t last_page,
                                                     uint32_t *pages,
                                                     uint8_t capacity,
                                                     uint8_t *count)
{
    uint32_t page = first_page;
    for (;;)
    {
        if (*count >= capacity)
        {
            return 1U;
        }
        if (sample_stream_sequence_append(page, pages, capacity, count) == 0U)
        {
            return 0U;
        }
        if (*count >= capacity)
        {
            return 1U;
        }
        if (page >= last_page)
        {
            return 1U;
        }
        ++page;
    }
}

uint8_t sample_stream_sequence_build(const sample_stream_sequence_input_t *input,
                                     uint32_t *out_page_indices,
                                     uint8_t page_capacity,
                                     uint8_t *out_page_count)
{
    if ((input == NULL) || (out_page_indices == NULL) || (out_page_count == NULL)
        || (page_capacity == 0U)
        || (page_capacity > SAMPLE_STREAM_TARGET_NEEDS_PER_VOICE)
        || (input->frames_per_page == 0U)
        || (input->region_end <= input->region_begin)
        || (input->direction != 1))
    {
        return 0U;
    }

    *out_page_count = 0U;

    const uint8_t loop_valid = (uint8_t)((input->loop_enabled != 0U)
        && (input->loop_begin >= input->region_begin)
        && (input->loop_end > input->loop_begin)
        && (input->loop_end <= input->region_end));
    if ((input->loop_enabled != 0U) && (loop_valid == 0U))
    {
        return 0U;
    }

    uint32_t current = input->current_frame;
    if (current < input->region_begin)
    {
        current = input->region_begin;
    }
    if (current >= input->region_end)
    {
        current = input->region_end - 1U;
    }

    if (loop_valid != 0U)
    {
        if (current >= input->loop_end)
        {
            current = input->loop_begin;
        }
    }

    const uint32_t current_page = current / input->frames_per_page;
    const uint32_t first_segment_end =
        (loop_valid != 0U) ? input->loop_end : input->region_end;
    const uint32_t first_last_page = (first_segment_end - 1U) / input->frames_per_page;
    if (sample_stream_sequence_append_forward(current_page,
                                               first_last_page,
                                               out_page_indices,
                                               page_capacity,
                                               out_page_count) == 0U)
    {
        return 0U;
    }
    if ((loop_valid != 0U) && (*out_page_count < page_capacity))
    {
        const uint32_t loop_last_page = (input->loop_end - 1U) / input->frames_per_page;
        if (sample_stream_sequence_append_forward(input->loop_begin / input->frames_per_page,
                                                   loop_last_page,
                                                   out_page_indices,
                                                   page_capacity,
                                                   out_page_count) == 0U)
        {
            return 0U;
        }
    }

    return (*out_page_count != 0U) ? 1U : 0U;
}
