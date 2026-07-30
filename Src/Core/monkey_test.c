#include "Core/monkey_test.h"

#include <string.h>

#include "Core/engine_tasklet.h"
#include "Core/crash_capsule.h"
#include "Core/diagnostic_input.h"
#include "Core/diagnostic_watchdog.h"
#include "Core/monkey_test_monitor.h"
#include "Core/monkey_test_replay.h"
#include "Core/monkey_test_safety.h"
#include "Storage/memory_layout.h"
#include "Storage/monkey_test_log.h"
#include "UI/ui_boot_loading.h"
#include "stm32h7xx_hal.h"

#define MONKEY_TEST_MAX_ACTIONS_PER_TICK 8U
#define MONKEY_TEST_PERIODIC_LOG_MS      600000U

typedef struct
{
    monkey_test_view_t view;
    monkey_test_generator_t generator;
    monkey_test_action_t next_action;
    uint32_t started_ms;
    uint32_t schedule_origin_tick;
    uint32_t next_due_tick;
    uint32_t seed_nonce;
    uint32_t next_periodic_log_ms;
    uint8_t next_action_valid;
    uint8_t auto_resume_pending;
    uint8_t recovery_valid;
    monkey_test_replay_next_t replay_next_kind;
} monkey_test_runtime_t;

static monkey_test_runtime_t g_monkey_test;
STORAGE_STATE_SDRAM static crash_capsule_snapshot_t g_monkey_test_recovery;

static void monkey_test_view_reset_with_recovery(void)
{
    memset(&g_monkey_test.view, 0, sizeof(g_monkey_test.view));
    if (g_monkey_test.recovery_valid == 0U)
    {
        return;
    }
    g_monkey_test.view.recovery_available = 1U;
    g_monkey_test.view.last_failure_seed = g_monkey_test_recovery.seed;
    g_monkey_test.view.last_failure_action_index =
        g_monkey_test_recovery.action_index;
    g_monkey_test.view.last_reset_flags =
        g_monkey_test_recovery.reset_flags;
    g_monkey_test.view.crash_count =
        g_monkey_test_recovery.crash_count;
    g_monkey_test.view.last_issue =
        (g_monkey_test_recovery.fault_type
         == CRASH_CAPSULE_FAULT_WATCHDOG)
            ? MONKEY_TEST_ISSUE_WATCHDOG_RESET
            : MONKEY_TEST_ISSUE_FAULT_RESET;
    g_monkey_test.view.last_issue_severity =
        MONKEY_TEST_SEVERITY_STOP;
}

static uint8_t monkey_test_inject_action(const monkey_test_action_t *action)
{
    switch (action->type)
    {
        case MONKEY_TEST_ACTION_BUTTON_PRESS:
            return diagnostic_input_button(action->target, 1U);
        case MONKEY_TEST_ACTION_BUTTON_RELEASE:
            return diagnostic_input_button(action->target, 0U);
        case MONKEY_TEST_ACTION_ENCODER_DELTA:
            return diagnostic_input_encoder(action->target, action->value);
        case MONKEY_TEST_ACTION_KEY_PRESS:
            return diagnostic_input_key(action->target, 1U,
                                        (uint8_t)action->value);
        case MONKEY_TEST_ACTION_KEY_RELEASE:
            return diagnostic_input_key(action->target, 0U, 0U);
        default:
            return 0U;
    }
}

static void monkey_test_commit_and_inject_action(
    const monkey_test_action_t *action)
{
    const crash_capsule_breadcrumb_t breadcrumb = {
        .index = action->index,
        .logical_tick = action->logical_tick,
        .delay_ticks = action->delay_ticks,
        .value = action->value,
        .type = (uint8_t)action->type,
        .target = action->target
    };
    crash_capsule_record_breadcrumb(&breadcrumb,
                                    g_monkey_test.view.warning_count,
                                    g_monkey_test.view.error_count,
                                    g_monkey_test.view.crash_count);
    if (monkey_test_inject_action(action) == 0U)
    {
        g_monkey_test.view.warning_count++;
        g_monkey_test.view.last_issue = MONKEY_TEST_ISSUE_INPUT_REJECTED;
        g_monkey_test.view.last_issue_severity =
            MONKEY_TEST_SEVERITY_WARNING;
    }
    g_monkey_test.view.action_count = action->index + 1U;
    g_monkey_test.view.last_action_type = action->type;
    g_monkey_test.view.last_action_target = action->target;
    g_monkey_test.view.last_action_value = action->value;
}

void monkey_test_init(void)
{
    memset(&g_monkey_test, 0, sizeof(g_monkey_test));
    diagnostic_input_init();
    diagnostic_watchdog_init();
    monkey_test_monitor_init();
    monkey_test_replay_init();
    monkey_test_safety_init();
    g_monkey_test.view.state = MONKEY_TEST_STATE_IDLE;
    if (crash_capsule_get_recovery(&g_monkey_test_recovery) != 0U)
    {
        g_monkey_test.recovery_valid = 1U;
        g_monkey_test.view.recovery_available = 1U;
        g_monkey_test.view.last_failure_seed = g_monkey_test_recovery.seed;
        g_monkey_test.view.last_failure_action_index =
            g_monkey_test_recovery.action_index;
        g_monkey_test.view.last_reset_flags =
            g_monkey_test_recovery.reset_flags;
        g_monkey_test.view.crash_count =
            g_monkey_test_recovery.crash_count;
        g_monkey_test.view.last_issue =
            (g_monkey_test_recovery.fault_type
             == CRASH_CAPSULE_FAULT_WATCHDOG)
                ? MONKEY_TEST_ISSUE_WATCHDOG_RESET
                : MONKEY_TEST_ISSUE_FAULT_RESET;
        g_monkey_test.view.last_issue_severity =
            MONKEY_TEST_SEVERITY_STOP;
    }
    if (crash_capsule_get_boot_recovery(&g_monkey_test_recovery) != 0U)
    {
        g_monkey_test.auto_resume_pending = 1U;
    }
}

void monkey_test_start(void)
{
    const uint32_t seed =
        HAL_GetTick() ^ engine_tick_count ^ (++g_monkey_test.seed_nonce * 0x9E3779B9U);
    monkey_test_start_seed(seed);
}

void monkey_test_start_seed(uint32_t seed)
{
    if (monkey_test_is_active() != 0U)
    {
        return;
    }
    if (crash_capsule_is_ready() == 0U)
    {
        g_monkey_test.view.state = MONKEY_TEST_STATE_STOPPED;
        g_monkey_test.view.error_count++;
        return;
    }

    const uint32_t seed_nonce = g_monkey_test.seed_nonce;
    if (monkey_test_safety_prepare() == 0U)
    {
        g_monkey_test.view.state = MONKEY_TEST_STATE_STOPPED;
        g_monkey_test.view.error_count++;
        return;
    }
    monkey_test_view_reset_with_recovery();
    diagnostic_input_release_all();
    monkey_test_generator_init(&g_monkey_test.generator, seed);
    monkey_test_generator_set_context(&g_monkey_test.generator,
                                      MONKEY_TEST_CONTEXT_SETTINGS);
    g_monkey_test.view.state = MONKEY_TEST_STATE_RUNNING;
    g_monkey_test.view.seed = g_monkey_test.generator.seed;
    g_monkey_test.started_ms = HAL_GetTick();
    g_monkey_test.schedule_origin_tick = engine_tick_count;
    g_monkey_test.next_due_tick = engine_tick_count;
    g_monkey_test.next_action_valid = 0U;
    g_monkey_test.seed_nonce = seed_nonce;
    crash_capsule_begin_session(g_monkey_test.view.seed);
    if (diagnostic_watchdog_arm(engine_tick_count) == 0U)
    {
        g_monkey_test.view.state = MONKEY_TEST_STATE_STOPPED;
        g_monkey_test.view.error_count++;
        crash_capsule_end_session(g_monkey_test.view.warning_count,
                                  g_monkey_test.view.error_count,
                                  g_monkey_test.view.crash_count);
        diagnostic_input_release_all();
        (void)monkey_test_safety_restore();
        return;
    }
    monkey_test_monitor_start(engine_tick_count);
    g_monkey_test.next_periodic_log_ms =
        g_monkey_test.started_ms + MONKEY_TEST_PERIODIC_LOG_MS;
    monkey_test_log_queue_event(
        MONKEY_TEST_LOG_EVENT_START, g_monkey_test.view.seed, 0U, 0U,
        g_monkey_test.view.warning_count, g_monkey_test.view.error_count,
        g_monkey_test.view.crash_count,
        (uint32_t)g_monkey_test.view.last_issue);
}

uint8_t monkey_test_replay_last_failure(void)
{
    if ((monkey_test_is_active() != 0U)
        || (g_monkey_test.recovery_valid == 0U))
    {
        return 0U;
    }
    if (monkey_test_replay_prepare(&g_monkey_test_recovery) == 0U)
    {
        g_monkey_test.view.error_count++;
        g_monkey_test.view.last_issue =
            MONKEY_TEST_ISSUE_REPLAY_MISMATCH;
        g_monkey_test.view.last_issue_severity =
            MONKEY_TEST_SEVERITY_STOP;
        return 0U;
    }
    if ((crash_capsule_is_ready() == 0U)
        || (monkey_test_safety_prepare() == 0U))
    {
        g_monkey_test.view.state = MONKEY_TEST_STATE_STOPPED;
        g_monkey_test.view.error_count++;
        return 0U;
    }

    monkey_test_view_reset_with_recovery();
    diagnostic_input_release_all();
    g_monkey_test.view.state = MONKEY_TEST_STATE_REPLAYING;
    g_monkey_test.view.seed = monkey_test_replay_seed();
    g_monkey_test.view.replay_target_index =
        monkey_test_replay_target_index();
    g_monkey_test.started_ms = HAL_GetTick();
    g_monkey_test.schedule_origin_tick = engine_tick_count;
    g_monkey_test.next_due_tick = engine_tick_count;
    g_monkey_test.next_action_valid = 0U;
    crash_capsule_begin_session(g_monkey_test.view.seed);
    if (diagnostic_watchdog_arm(engine_tick_count) == 0U)
    {
        g_monkey_test.view.state = MONKEY_TEST_STATE_STOPPED;
        g_monkey_test.view.error_count++;
        crash_capsule_end_session(g_monkey_test.view.warning_count,
                                  g_monkey_test.view.error_count,
                                  g_monkey_test.view.crash_count);
        diagnostic_input_release_all();
        (void)monkey_test_safety_restore();
        return 0U;
    }
    monkey_test_monitor_start(engine_tick_count);
    monkey_test_log_queue_event(
        MONKEY_TEST_LOG_EVENT_REPLAY_START, g_monkey_test.view.seed,
        0U, 0U, g_monkey_test.view.warning_count,
        g_monkey_test.view.error_count, g_monkey_test.view.crash_count,
        (uint32_t)g_monkey_test.view.last_issue);
    return 1U;
}

uint8_t monkey_test_replay_fire_target(void)
{
    if ((g_monkey_test.view.state != MONKEY_TEST_STATE_REPLAY_PAUSED)
        || (g_monkey_test.next_action_valid == 0U)
        || (g_monkey_test.replay_next_kind
            != MONKEY_TEST_REPLAY_NEXT_TARGET))
    {
        return 0U;
    }

#if defined(DEBUG)
    if ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U)
    {
        __BKPT(0);
    }
#endif

    monkey_test_commit_and_inject_action(&g_monkey_test.next_action);
    g_monkey_test.next_action_valid = 0U;
    g_monkey_test.view.state = MONKEY_TEST_STATE_REPLAY_TARGET_DONE;
    monkey_test_log_queue_event(
        MONKEY_TEST_LOG_EVENT_REPLAY_TARGET, g_monkey_test.view.seed,
        HAL_GetTick() - g_monkey_test.started_ms,
        g_monkey_test.view.action_count,
        g_monkey_test.view.warning_count,
        g_monkey_test.view.error_count, g_monkey_test.view.crash_count,
        (uint32_t)g_monkey_test.view.last_issue);
    return 1U;
}

void monkey_test_stop(void)
{
    if (monkey_test_is_active() == 0U)
    {
        return;
    }

    g_monkey_test.view.elapsed_ms = HAL_GetTick() - g_monkey_test.started_ms;
    g_monkey_test.view.state = MONKEY_TEST_STATE_STOPPED;
    monkey_test_log_queue_event(
        MONKEY_TEST_LOG_EVENT_STOP, g_monkey_test.view.seed,
        g_monkey_test.view.elapsed_ms, g_monkey_test.view.action_count,
        g_monkey_test.view.warning_count, g_monkey_test.view.error_count,
        g_monkey_test.view.crash_count,
        (uint32_t)g_monkey_test.view.last_issue);
    crash_capsule_end_session(g_monkey_test.view.warning_count,
                              g_monkey_test.view.error_count,
                              g_monkey_test.view.crash_count);
    monkey_test_monitor_stop();
    diagnostic_input_release_all();
    if (monkey_test_safety_restore() == 0U)
    {
        g_monkey_test.view.error_count++;
    }
}

static void monkey_test_replay_tick(void)
{
    uint8_t processed = 0U;
    while (processed < MONKEY_TEST_MAX_ACTIONS_PER_TICK)
    {
        if (g_monkey_test.next_action_valid == 0U)
        {
            g_monkey_test.replay_next_kind =
                monkey_test_replay_next(&g_monkey_test.next_action);
            if ((g_monkey_test.replay_next_kind
                 == MONKEY_TEST_REPLAY_NEXT_MISMATCH)
                || (g_monkey_test.replay_next_kind
                    == MONKEY_TEST_REPLAY_NEXT_ERROR))
            {
                g_monkey_test.view.error_count++;
                g_monkey_test.view.last_issue =
                    MONKEY_TEST_ISSUE_REPLAY_MISMATCH;
                g_monkey_test.view.last_issue_severity =
                    MONKEY_TEST_SEVERITY_STOP;
                monkey_test_stop();
                return;
            }
            g_monkey_test.next_due_tick =
                g_monkey_test.schedule_origin_tick
                + g_monkey_test.next_action.logical_tick;
            g_monkey_test.next_action_valid = 1U;
        }

        if ((int32_t)(engine_tick_count
                      - g_monkey_test.next_due_tick) < 0)
        {
            return;
        }
        if (g_monkey_test.replay_next_kind
            == MONKEY_TEST_REPLAY_NEXT_TARGET)
        {
            g_monkey_test.view.last_action_type =
                g_monkey_test.next_action.type;
            g_monkey_test.view.last_action_target =
                g_monkey_test.next_action.target;
            g_monkey_test.view.last_action_value =
                g_monkey_test.next_action.value;
            g_monkey_test.view.state =
                MONKEY_TEST_STATE_REPLAY_PAUSED;
            return;
        }

        monkey_test_commit_and_inject_action(
            &g_monkey_test.next_action);
        g_monkey_test.next_action_valid = 0U;
        processed++;
    }
}

void monkey_test_tick(void)
{
    if ((g_monkey_test.auto_resume_pending != 0U)
        && (ui_boot_loading_is_active() == 0U))
    {
        const uint32_t new_seed =
            g_monkey_test_recovery.seed + 0x9E3779B9U;
        g_monkey_test.auto_resume_pending = 0U;
        monkey_test_start_seed(new_seed);
    }

    if (monkey_test_is_active() == 0U)
    {
        return;
    }

    g_monkey_test.view.elapsed_ms = HAL_GetTick() - g_monkey_test.started_ms;
    if ((g_monkey_test.view.state == MONKEY_TEST_STATE_RUNNING)
        && ((int32_t)(HAL_GetTick()
                      - g_monkey_test.next_periodic_log_ms) >= 0))
    {
        monkey_test_log_queue_event(
            MONKEY_TEST_LOG_EVENT_PERIODIC, g_monkey_test.view.seed,
            g_monkey_test.view.elapsed_ms, g_monkey_test.view.action_count,
            g_monkey_test.view.warning_count, g_monkey_test.view.error_count,
            g_monkey_test.view.crash_count,
            (uint32_t)g_monkey_test.view.last_issue);
        g_monkey_test.next_periodic_log_ms +=
            MONKEY_TEST_PERIODIC_LOG_MS;
    }
    monkey_test_monitor_result_t monitor_result;
    if (monkey_test_monitor_poll(engine_tick_count, &monitor_result) != 0U)
    {
        g_monkey_test.view.warning_count += monitor_result.warning_count;
        g_monkey_test.view.error_count += monitor_result.error_count;
        if (monitor_result.issue != MONKEY_TEST_ISSUE_NONE)
        {
            g_monkey_test.view.last_issue = monitor_result.issue;
            g_monkey_test.view.last_issue_severity = monitor_result.severity;
        }
        if (monitor_result.stop_required != 0U)
        {
            monkey_test_stop();
            return;
        }
    }
    if (g_monkey_test.view.state == MONKEY_TEST_STATE_REPLAYING)
    {
        monkey_test_replay_tick();
        return;
    }
    if (g_monkey_test.view.state != MONKEY_TEST_STATE_RUNNING)
    {
        return;
    }

    uint8_t processed = 0U;
    while (processed < MONKEY_TEST_MAX_ACTIONS_PER_TICK)
    {
        if (g_monkey_test.next_action_valid == 0U)
        {
            if (monkey_test_generator_next(&g_monkey_test.generator,
                                           &g_monkey_test.next_action) == 0U)
            {
                return;
            }
            g_monkey_test.next_due_tick =
                g_monkey_test.schedule_origin_tick
                + g_monkey_test.next_action.logical_tick;
            g_monkey_test.next_action_valid = 1U;
        }

        if ((int32_t)(engine_tick_count - g_monkey_test.next_due_tick) < 0)
        {
            return;
        }

        monkey_test_commit_and_inject_action(
            &g_monkey_test.next_action);
        g_monkey_test.next_action_valid = 0U;
        processed++;
    }
}

uint8_t monkey_test_is_active(void)
{
    return ((g_monkey_test.view.state == MONKEY_TEST_STATE_RUNNING)
            || (g_monkey_test.view.state
                == MONKEY_TEST_STATE_REPLAYING)
            || (g_monkey_test.view.state
                == MONKEY_TEST_STATE_REPLAY_PAUSED)
            || (g_monkey_test.view.state
                == MONKEY_TEST_STATE_REPLAY_TARGET_DONE)) ? 1U : 0U;
}

void monkey_test_get_view(monkey_test_view_t *out_view)
{
    if (out_view != 0)
    {
        *out_view = g_monkey_test.view;
        monkey_test_log_status_t log_status;
        monkey_test_log_get_status(&log_status);
        out_view->log_write_count = log_status.write_count;
        out_view->log_error_count = log_status.error_count;
        out_view->log_pending =
            ((log_status.recovery_pending != 0U)
             || (log_status.event_pending != 0U)) ? 1U : 0U;
    }
}

uint8_t monkey_test_get_last_failure(
    crash_capsule_snapshot_t *out_snapshot)
{
    if ((out_snapshot == 0) || (g_monkey_test.recovery_valid == 0U))
    {
        return 0U;
    }
    *out_snapshot = g_monkey_test_recovery;
    return 1U;
}
