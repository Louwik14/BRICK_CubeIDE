#include <assert.h>
#include <stdint.h>

#include "Sampler/sample_stream_arch_contract.h"
#include "Sampler/sample_stream_sequence.h"

static void expect_pages(const sample_stream_sequence_input_t *input,
                         const uint32_t *expected,
                         uint8_t expected_count)
{
    uint32_t pages[SAMPLE_STREAM_TARGET_NEEDS_PER_VOICE] = { 0U };
    uint8_t count = 0U;
    assert(sample_stream_sequence_build(input,
                                        pages,
                                        SAMPLE_STREAM_TARGET_NEEDS_PER_VOICE,
                                        &count) != 0U);
    assert(count == expected_count);
    for (uint8_t i = 0U; i < count; ++i)
    {
        assert(pages[i] == expected[i]);
    }
}

int main(void)
{
    const sample_stream_sequence_input_t forward = {
        .current_frame = 210U,
        .region_begin = 0U,
        .region_end = 450U,
        .loop_begin = 0U,
        .loop_end = 0U,
        .frames_per_page = 100U,
        .direction = 1,
        .loop_enabled = 0U,
    };
    const uint32_t forward_expected[] = { 2U, 3U, 4U };
    expect_pages(&forward, forward_expected, 3U);

    sample_stream_sequence_input_t reverse = {
        .current_frame = 310U,
        .region_begin = 0U,
        .region_end = 450U,
        .loop_begin = 0U,
        .loop_end = 0U,
        .frames_per_page = 100U,
        .direction = -1,
        .loop_enabled = 0U,
    };
    uint32_t rejected_pages[SAMPLE_STREAM_TARGET_NEEDS_PER_VOICE] = { 0U };
    uint8_t rejected_count = 0U;
    assert(sample_stream_sequence_build(&reverse,
                                        rejected_pages,
                                        SAMPLE_STREAM_TARGET_NEEDS_PER_VOICE,
                                        &rejected_count) == 0U);

    const sample_stream_sequence_input_t loop = {
        .current_frame = 350U,
        .region_begin = 0U,
        .region_end = 900U,
        .loop_begin = 200U,
        .loop_end = 600U,
        .frames_per_page = 100U,
        .direction = 1,
        .loop_enabled = 1U,
    };
    const uint32_t loop_expected[] = { 3U, 4U, 5U, 2U };
    expect_pages(&loop, loop_expected, 4U);

    const sample_stream_sequence_input_t bounded = {
        .current_frame = 0U,
        .region_begin = 0U,
        .region_end = 1000U,
        .loop_begin = 0U,
        .loop_end = 0U,
        .frames_per_page = 100U,
        .direction = 1,
        .loop_enabled = 0U,
    };
    const uint32_t bounded_expected[] = { 0U, 1U, 2U, 3U, 4U, 5U };
    uint32_t bounded_pages[SAMPLE_STREAM_TARGET_MOBILE_NEEDS_PER_VOICE] = { 0U };
    uint8_t bounded_count = 0U;
    assert(sample_stream_sequence_build(&bounded,
                                        bounded_pages,
                                        SAMPLE_STREAM_TARGET_MOBILE_NEEDS_PER_VOICE,
                                        &bounded_count) != 0U);
    assert(bounded_count == SAMPLE_STREAM_TARGET_MOBILE_NEEDS_PER_VOICE);
    for (uint8_t i = 0U; i < bounded_count; ++i)
    {
        assert(bounded_pages[i] == bounded_expected[i]);
    }

    return 0;
}
