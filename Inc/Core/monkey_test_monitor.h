#ifndef MONKEY_TEST_MONITOR_H
#define MONKEY_TEST_MONITOR_H

#include <stdint.h>

typedef enum
{
    MONKEY_TEST_ISSUE_NONE = 0,
    MONKEY_TEST_ISSUE_INPUT_REJECTED,
    MONKEY_TEST_ISSUE_CPU_OVER_90,
    MONKEY_TEST_ISSUE_CPU_OVER_100,
    MONKEY_TEST_ISSUE_SAMPLER_UNDERRUN,
    MONKEY_TEST_ISSUE_SAMPLER_INVALID,
    MONKEY_TEST_ISSUE_LOOPER_CACHE_MISS,
    MONKEY_TEST_ISSUE_LOOPER_UNDERRUN,
    MONKEY_TEST_ISSUE_UI_INVARIANT,
    MONKEY_TEST_ISSUE_MONITOR_CANARY,
    MONKEY_TEST_ISSUE_FAULT_RESET,
    MONKEY_TEST_ISSUE_WATCHDOG_RESET,
    MONKEY_TEST_ISSUE_REPLAY_MISMATCH
} monkey_test_issue_t;

typedef enum
{
    MONKEY_TEST_SEVERITY_NONE = 0,
    MONKEY_TEST_SEVERITY_WARNING,
    MONKEY_TEST_SEVERITY_RECOVERABLE,
    MONKEY_TEST_SEVERITY_STOP
} monkey_test_severity_t;

typedef struct
{
    uint32_t warning_count;
    uint32_t error_count;
    monkey_test_issue_t issue;
    monkey_test_severity_t severity;
    uint8_t stop_required;
} monkey_test_monitor_result_t;

void monkey_test_monitor_init(void);
void monkey_test_monitor_start(uint32_t engine_tick);
void monkey_test_monitor_stop(void);
uint8_t monkey_test_monitor_poll(uint32_t engine_tick,
                                 monkey_test_monitor_result_t *out_result);
const char *monkey_test_issue_label(monkey_test_issue_t issue);

#endif
