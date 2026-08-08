#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "Sampler/sample_stream_scheduler.h"

static void fill_candidates(sample_stream_scheduler_candidate_t candidates[8])
{
    memset(candidates, 0, sizeof(sample_stream_scheduler_candidate_t) * 8U);
    for (uint8_t i = 0U; i < 8U; ++i)
    {
        candidates[i].active = 1U;
        candidates[i].round_robin_slot = i;
        candidates[i].need_index = (uint8_t)(7U - i);
        candidates[i].diagnostic_deadline_audio_frame = 8U - i;
    }
}

static void test_complete_rounds(void)
{
    sample_stream_scheduler_candidate_t candidates[8];
    sample_stream_scheduler_decision_t decision;
    fill_candidates(candidates);
    sample_stream_scheduler_init();
    sample_stream_scheduler_begin_round();
    for (uint32_t pass = 0U;
         pass < SAMPLE_STREAM_PAGES_PER_VOICE_PER_ROUND;
         ++pass)
    {
        for (uint8_t expected = 0U; expected < 8U; ++expected)
        {
            assert(sample_stream_scheduler_pick(candidates, 8U, &decision) != 0U);
            assert(decision.candidate_index == expected);
            assert(decision.round_robin_slot == expected);
        }
    }
    assert(sample_stream_scheduler_pick(candidates, 8U, &decision) == 0U);
    assert(sample_stream_scheduler_round_active() == 0U);
}

static void test_skipped_voices(void)
{
    sample_stream_scheduler_candidate_t candidates[8];
    sample_stream_scheduler_decision_t decision;
    static const uint8_t expected_slots[] = {0U, 2U, 5U, 7U};
    fill_candidates(candidates);
    candidates[1].active = 0U;
    candidates[3].active = 0U;
    candidates[4].active = 0U;
    candidates[6].active = 0U;
    sample_stream_scheduler_init();
    sample_stream_scheduler_begin_round();
    for (uint32_t pass = 0U;
         pass < SAMPLE_STREAM_PAGES_PER_VOICE_PER_ROUND;
         ++pass)
    {
        for (uint32_t i = 0U; i < sizeof(expected_slots); ++i)
        {
            assert(sample_stream_scheduler_pick(candidates, 8U, &decision) != 0U);
            assert(decision.round_robin_slot == expected_slots[i]);
        }
    }
    assert(sample_stream_scheduler_pick(candidates, 8U, &decision) == 0U);
}

static void test_resume_does_not_restart_round(void)
{
    sample_stream_scheduler_candidate_t candidates[8];
    sample_stream_scheduler_decision_t decision;
    fill_candidates(candidates);
    sample_stream_scheduler_init();
    sample_stream_scheduler_begin_round();
    assert(sample_stream_scheduler_pick(candidates, 8U, &decision) != 0U);
    assert(decision.round_robin_slot == 0U);
    sample_stream_scheduler_begin_round();
    assert(sample_stream_scheduler_pick(candidates, 8U, &decision) != 0U);
    assert(decision.round_robin_slot == 1U);
}

static void test_one_voice_once_per_pass(void)
{
    sample_stream_scheduler_candidate_t candidate;
    sample_stream_scheduler_decision_t decision;
    memset(&candidate, 0, sizeof(candidate));
    candidate.active = 1U;
    candidate.round_robin_slot = 3U;
    sample_stream_scheduler_init();
    sample_stream_scheduler_begin_round();
    for (uint32_t pass = 0U;
         pass < SAMPLE_STREAM_PAGES_PER_VOICE_PER_ROUND;
         ++pass)
    {
        assert(sample_stream_scheduler_pick(&candidate, 1U, &decision) != 0U);
        assert(decision.round_robin_slot == 3U);
    }
    assert(sample_stream_scheduler_pick(&candidate, 1U, &decision) == 0U);
}

int main(void)
{
    test_complete_rounds();
    test_skipped_voices();
    test_resume_does_not_restart_round();
    test_one_voice_once_per_pass();
    return 0;
}
