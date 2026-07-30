#ifndef MONKEY_TEST_REPLAY_H
#define MONKEY_TEST_REPLAY_H

#include <stdint.h>

#include "Core/crash_capsule.h"
#include "Core/monkey_test_action.h"

typedef enum
{
    MONKEY_TEST_REPLAY_NEXT_PREFIX = 0,
    MONKEY_TEST_REPLAY_NEXT_TARGET,
    MONKEY_TEST_REPLAY_NEXT_MISMATCH,
    MONKEY_TEST_REPLAY_NEXT_ERROR
} monkey_test_replay_next_t;

void monkey_test_replay_init(void);
uint8_t monkey_test_replay_prepare(
    const crash_capsule_snapshot_t *snapshot);
monkey_test_replay_next_t monkey_test_replay_next(
    monkey_test_action_t *out_action);
uint32_t monkey_test_replay_seed(void);
uint32_t monkey_test_replay_target_index(void);

#endif
