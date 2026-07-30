#ifndef MONKEY_TEST_H
#define MONKEY_TEST_H

#include <stdint.h>

#include "Core/brick_build_config.h"
#include "Core/crash_capsule.h"
#include "Core/monkey_test_action.h"
#include "Core/monkey_test_monitor.h"

typedef enum
{
    MONKEY_TEST_STATE_IDLE = 0,
    MONKEY_TEST_STATE_RUNNING,
    MONKEY_TEST_STATE_STOPPED,
    MONKEY_TEST_STATE_REPLAYING,
    MONKEY_TEST_STATE_REPLAY_PAUSED,
    MONKEY_TEST_STATE_REPLAY_TARGET_DONE
} monkey_test_state_t;

typedef struct
{
    monkey_test_state_t state;
    uint32_t seed;
    uint32_t elapsed_ms;
    uint32_t action_count;
    uint32_t warning_count;
    uint32_t error_count;
    uint32_t crash_count;
    uint32_t last_failure_seed;
    uint32_t last_failure_action_index;
    uint32_t last_reset_flags;
    uint32_t log_write_count;
    uint32_t log_error_count;
    uint32_t replay_target_index;
    monkey_test_action_type_t last_action_type;
    monkey_test_issue_t last_issue;
    monkey_test_severity_t last_issue_severity;
    uint8_t last_action_target;
    int16_t last_action_value;
    uint8_t recovery_available;
    uint8_t log_pending;
} monkey_test_view_t;

#if BRICK_TEST_BUILD

void monkey_test_init(void);
void monkey_test_start(void);
void monkey_test_start_seed(uint32_t seed);
uint8_t monkey_test_replay_last_failure(void);
uint8_t monkey_test_replay_fire_target(void);
void monkey_test_stop(void);
void monkey_test_tick(void);
uint8_t monkey_test_is_active(void);
void monkey_test_get_view(monkey_test_view_t *out_view);
uint8_t monkey_test_get_last_failure(
    crash_capsule_snapshot_t *out_snapshot);

#endif

#endif
