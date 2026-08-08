#include "Sampler/sample_stream_scheduler.h"

#include <string.h>
#if defined(BRICK6_STREAM_CALIBRATION) && BRICK6_STREAM_CALIBRATION
#include "Core/stream_calibration.h"
#endif

static uint8_t g_sample_stream_scheduler_round_robin_cursor;
static uint8_t g_sample_stream_scheduler_round_active;
static uint8_t g_sample_stream_scheduler_slots_left;
static uint32_t g_sample_stream_scheduler_passes_left;
#if defined(BRICK6_STREAM_CALIBRATION) && BRICK6_STREAM_CALIBRATION
static uint8_t g_sample_stream_scheduler_calibration_passes =
    SAMPLE_STREAM_PAGES_PER_VOICE_PER_ROUND;
#endif

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
    g_sample_stream_scheduler_passes_left =
#if defined(BRICK6_STREAM_CALIBRATION) && BRICK6_STREAM_CALIBRATION
        g_sample_stream_scheduler_calibration_passes;
#else
        SAMPLE_STREAM_PAGES_PER_VOICE_PER_ROUND;
#endif
#if defined(BRICK6_STREAM_CALIBRATION) && BRICK6_STREAM_CALIBRATION
    brick6_stream_calibration_note_round_begin();
#endif
}

uint8_t sample_stream_scheduler_round_active(void)
{
    return g_sample_stream_scheduler_round_active;
}

#if defined(BRICK6_STREAM_CALIBRATION) && BRICK6_STREAM_CALIBRATION
void sample_stream_scheduler_calibration_set_passes(uint8_t passes)
{
    g_sample_stream_scheduler_calibration_passes = (passes != 0U) ? passes : 1U;
    g_sample_stream_scheduler_round_active = 0U;
}
#endif

uint8_t sample_stream_scheduler_pick(
    const sample_stream_scheduler_candidate_t *candidates,
    uint32_t candidate_count,
    sample_stream_scheduler_decision_t *out_decision)
{
    if ((g_sample_stream_scheduler_round_active == 0U) || (out_decision == 0))
    {
        return 0U;
    }
    for (;;)
    {
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
                const uint32_t distance =
                    (candidate->round_robin_slot + SAMPLE_STREAM_SCHEDULER_SLOT_COUNT
                     - g_sample_stream_scheduler_round_robin_cursor)
                    % SAMPLE_STREAM_SCHEDULER_SLOT_COUNT;
                if ((distance >= g_sample_stream_scheduler_slots_left)
                    || ((found != 0U) && (distance >= best_distance)))
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
            g_sample_stream_scheduler_slots_left -= (uint8_t)(best_distance + 1U);
            g_sample_stream_scheduler_round_robin_cursor =
                (uint8_t)((candidates[best_index].round_robin_slot + 1U)
                          % SAMPLE_STREAM_SCHEDULER_SLOT_COUNT);
            return 1U;
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
#if defined(BRICK6_STREAM_CALIBRATION) && BRICK6_STREAM_CALIBRATION
            brick6_stream_calibration_note_round_end();
#endif
            return 0U;
        }
    }
}
