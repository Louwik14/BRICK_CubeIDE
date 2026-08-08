#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "Sampler/sample_stream_scheduler.h"

int main(void)
{
    sample_stream_scheduler_candidate_t candidates[8];
    memset(candidates, 0, sizeof(candidates));
    for (uint8_t i = 0U; i < 8U; ++i)
    {
        candidates[i].active = 1U;
        candidates[i].round_robin_slot = i;
        candidates[i].need_index = (uint8_t)(7U - i);
        candidates[i].diagnostic_deadline_audio_frame = 8U - i;
    }

    sample_stream_scheduler_decision_t decision;
    sample_stream_scheduler_init();
    for (uint8_t expected = 0U; expected < 8U; ++expected)
    {
        assert(sample_stream_scheduler_pick(candidates, 8U, &decision) != 0U);
        assert(decision.candidate_index == expected);
        assert(decision.round_robin_slot == expected);
    }
    assert(sample_stream_scheduler_pick(candidates, 8U, &decision) != 0U);
    assert(decision.round_robin_slot == 0U);

    candidates[1].active = 0U;
    candidates[3].active = 0U;
    candidates[4].active = 0U;
    candidates[6].active = 0U;
    assert(sample_stream_scheduler_pick(candidates, 8U, &decision) != 0U);
    assert(decision.round_robin_slot == 2U);
    assert(sample_stream_scheduler_pick(candidates, 8U, &decision) != 0U);
    assert(decision.round_robin_slot == 5U);
    assert(sample_stream_scheduler_pick(candidates, 8U, &decision) != 0U);
    assert(decision.round_robin_slot == 7U);
    assert(sample_stream_scheduler_pick(candidates, 8U, &decision) != 0U);
    assert(decision.round_robin_slot == 0U);

    candidates[0].active = 0U;
    candidates[2].active = 0U;
    candidates[5].active = 0U;
    candidates[7].active = 0U;
    assert(sample_stream_scheduler_pick(candidates, 8U, &decision) == 0U);
    return 0;
}
