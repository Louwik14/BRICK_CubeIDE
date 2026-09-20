#include "Sampler/sample_stream_scheduler.h"

static uint8_t g_sample_stream_scheduler_round_robin_cursor;
static uint8_t g_sample_stream_scheduler_round_active;
static uint8_t g_sample_stream_scheduler_slots_left;
static uint32_t g_sample_stream_scheduler_passes_left;

void sample_stream_scheduler_init(void)
{
    g_sample_stream_scheduler_round_robin_cursor = 0U;
    g_sample_stream_scheduler_round_active = 0U;
    g_sample_stream_scheduler_slots_left = 0U;
    g_sample_stream_scheduler_passes_left = 0U;
}

void sample_stream_scheduler_begin_round(void)
{
    if (g_sample_stream_scheduler_round_active != 0U)
    {
        return;
    }
    g_sample_stream_scheduler_round_active = 1U;
    g_sample_stream_scheduler_slots_left = SAMPLE_STREAM_SCHEDULER_SLOT_COUNT;
    g_sample_stream_scheduler_passes_left = SAMPLE_STREAM_PAGES_PER_VOICE_PER_ROUND;
}

uint8_t sample_stream_scheduler_round_active(void)
{
    return g_sample_stream_scheduler_round_active;
}

uint8_t sample_stream_scheduler_pick(
    sample_stream_scheduler_probe_fn probe,
    void *context,
    sample_stream_scheduler_candidate_t *out_candidate)
{
    if ((g_sample_stream_scheduler_round_active == 0U)
        || (probe == 0) || (out_candidate == 0))
    {
        return 0U;
    }
    for (;;)
    {
        for (uint8_t distance = 0U;
             distance < g_sample_stream_scheduler_slots_left;
             ++distance)
        {
            const uint8_t slot = (uint8_t)(
                (g_sample_stream_scheduler_round_robin_cursor + distance)
                % SAMPLE_STREAM_SCHEDULER_SLOT_COUNT);
            sample_stream_scheduler_candidate_t candidate;
            if ((probe(context, slot, &candidate) != 0U)
                && (candidate.active != 0U)
                && (candidate.round_robin_slot == slot))
            {
                *out_candidate = candidate;
                g_sample_stream_scheduler_slots_left -= (uint8_t)(distance + 1U);
                g_sample_stream_scheduler_round_robin_cursor = (uint8_t)(
                    (slot + 1U) % SAMPLE_STREAM_SCHEDULER_SLOT_COUNT);
                return 1U;
            }
        }
        g_sample_stream_scheduler_round_robin_cursor =
            (uint8_t)((g_sample_stream_scheduler_round_robin_cursor
                       + g_sample_stream_scheduler_slots_left)
                      % SAMPLE_STREAM_SCHEDULER_SLOT_COUNT);
        g_sample_stream_scheduler_slots_left = SAMPLE_STREAM_SCHEDULER_SLOT_COUNT;
        --g_sample_stream_scheduler_passes_left;
        if (g_sample_stream_scheduler_passes_left == 0U)
        {
            g_sample_stream_scheduler_round_active = 0U;
            g_sample_stream_scheduler_slots_left = 0U;
            return 0U;
        }
    }
}
