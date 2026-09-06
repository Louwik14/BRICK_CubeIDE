#include "Sampler/sample_stream_scheduler.h"

#include <string.h>

static uint8_t g_sample_stream_scheduler_round_robin_cursor;
static uint8_t g_sample_stream_scheduler_round_active;
static uint64_t g_sample_stream_scheduler_served_slots;

_Static_assert(SAMPLE_STREAM_SCHEDULER_SLOT_COUNT <= 64U,
               "stream scheduler served-slot tracking is too small");

void sample_stream_scheduler_init(void)
{
    g_sample_stream_scheduler_round_robin_cursor = 0U;
    g_sample_stream_scheduler_round_active = 0U;
    g_sample_stream_scheduler_served_slots = 0U;
}

void sample_stream_scheduler_begin_round(void)
{
    if (g_sample_stream_scheduler_round_active != 0U)
    {
        return;
    }
    g_sample_stream_scheduler_round_active = 1U;
    g_sample_stream_scheduler_served_slots = 0U;
}

uint8_t sample_stream_scheduler_round_active(void)
{
    return g_sample_stream_scheduler_round_active;
}

uint8_t sample_stream_scheduler_pick(
    const sample_stream_scheduler_candidate_t *candidates,
    uint32_t candidate_count,
    sample_stream_scheduler_decision_t *out_decision)
{
    if ((g_sample_stream_scheduler_round_active == 0U) || (out_decision == 0))
    {
        return 0U;
    }
    uint8_t found = 0U;
    uint32_t best_index = 0U;
    uint32_t best_distance = UINT32_MAX;
    if (candidates != 0)
    {
        for (uint32_t i = 0U; i < candidate_count; ++i)
        {
            const sample_stream_scheduler_candidate_t *const candidate = &candidates[i];
            if ((candidate->active == 0U)
                || (candidate->round_robin_slot >= SAMPLE_STREAM_SCHEDULER_SLOT_COUNT))
            {
                continue;
            }
            if ((g_sample_stream_scheduler_served_slots
                 & (UINT64_C(1) << candidate->round_robin_slot)) != 0U)
            {
                continue;
            }
            const uint32_t distance =
                (candidate->round_robin_slot + SAMPLE_STREAM_SCHEDULER_SLOT_COUNT
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
    }
    if (found != 0U)
    {
        memset(out_decision, 0, sizeof(*out_decision));
        out_decision->candidate_index = (uint8_t)best_index;
        out_decision->round_robin_slot = candidates[best_index].round_robin_slot;
        g_sample_stream_scheduler_served_slots |=
            UINT64_C(1) << out_decision->round_robin_slot;
        g_sample_stream_scheduler_round_robin_cursor =
            (uint8_t)((candidates[best_index].round_robin_slot + 1U)
                      % SAMPLE_STREAM_SCHEDULER_SLOT_COUNT);
        return 1U;
    }
    g_sample_stream_scheduler_round_active = 0U;
    g_sample_stream_scheduler_served_slots = 0U;
    return 0U;
}
