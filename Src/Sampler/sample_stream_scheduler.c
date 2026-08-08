#include "Sampler/sample_stream_scheduler.h"

#include <string.h>

static uint8_t g_sample_stream_scheduler_round_robin_cursor;

void sample_stream_scheduler_init(void)
{
    g_sample_stream_scheduler_round_robin_cursor = 0U;
}

uint8_t sample_stream_scheduler_pick(
    const sample_stream_scheduler_candidate_t *candidates,
    uint32_t candidate_count,
    sample_stream_scheduler_decision_t *out_decision)
{
    if ((candidates == 0) || (candidate_count == 0U) || (out_decision == 0))
    {
        return 0U;
    }

    uint8_t found = 0U;
    uint32_t best_index = 0U;
    uint32_t best_distance = UINT32_MAX;
    for (uint32_t i = 0U; i < candidate_count; ++i)
    {
        const sample_stream_scheduler_candidate_t *const candidate = &candidates[i];
        if (candidate->active == 0U)
        {
            continue;
        }
        if (candidate->round_robin_slot >= SAMPLE_STREAM_SCHEDULER_SLOT_COUNT)
        {
            continue;
        }

        const uint32_t distance =
            (candidate->round_robin_slot + SAMPLE_STREAM_SCHEDULER_MAX_CANDIDATES
             - g_sample_stream_scheduler_round_robin_cursor)
            % SAMPLE_STREAM_SCHEDULER_SLOT_COUNT;
        if ((found != 0U) && (distance >= best_distance))
        {
            continue;
        }
        found = 1U;
        best_index = i;
        best_distance = distance;
    }

    if (found == 0U)
    {
        return 0U;
    }

    memset(out_decision, 0, sizeof(*out_decision));
    out_decision->candidate_index = (uint8_t)best_index;
    out_decision->round_robin_slot = candidates[best_index].round_robin_slot;
    g_sample_stream_scheduler_round_robin_cursor =
        (uint8_t)((candidates[best_index].round_robin_slot + 1U)
                  % SAMPLE_STREAM_SCHEDULER_SLOT_COUNT);
    return 1U;
}
