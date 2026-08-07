#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "Sampler/sample_stream_scheduler.h"

int main(void)
{
    sample_stream_scheduler_candidate_t candidates[3];
    memset(candidates, 0, sizeof(candidates));
    for (uint8_t i = 0U; i < 3U; ++i)
    {
        candidates[i].active = 1U;
        candidates[i].round_robin_slot = i;
    }
    candidates[0].advance = 2U;
    candidates[0].consume_deadline_audio_frame = 1U;
    candidates[1].advance = 1U;
    candidates[1].consume_deadline_audio_frame = 100U;
    candidates[2].advance = 1U;
    candidates[2].consume_deadline_audio_frame = 2U;

    sample_stream_scheduler_decision_t decision;
    sample_stream_scheduler_init();
    assert(sample_stream_scheduler_pick(candidates, 3U, &decision) != 0U);
    assert(decision.candidate_index == 1U);

    candidates[0].active = 0U;
    assert(sample_stream_scheduler_pick(candidates, 3U, &decision) != 0U);
    assert(decision.candidate_index == 2U);
    return 0;
}
