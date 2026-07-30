#ifndef MONKEY_TEST_LOG_H
#define MONKEY_TEST_LOG_H

#include <stdint.h>

typedef enum
{
    MONKEY_TEST_LOG_EVENT_START = 0,
    MONKEY_TEST_LOG_EVENT_PERIODIC,
    MONKEY_TEST_LOG_EVENT_STOP,
    MONKEY_TEST_LOG_EVENT_REPLAY_START,
    MONKEY_TEST_LOG_EVENT_REPLAY_TARGET
} monkey_test_log_event_t;

typedef struct
{
    uint32_t write_count;
    uint32_t error_count;
    uint8_t recovery_pending;
    uint8_t event_pending;
} monkey_test_log_status_t;

void monkey_test_log_init(void);
void monkey_test_log_service(void);
void monkey_test_log_queue_event(monkey_test_log_event_t event,
                                 uint32_t seed,
                                 uint32_t elapsed_ms,
                                 uint32_t action_count,
                                 uint32_t warning_count,
                                 uint32_t error_count,
                                 uint32_t crash_count,
                                 uint32_t last_issue);
void monkey_test_log_get_status(monkey_test_log_status_t *out_status);

#endif
