#include "Core/monkey_test_monitor.h"

#include <string.h>

#include "Core/brick6_sampler_runtime.h"
#include "Core/cpu_load.h"
#include "Core/looper_raw_debug.h"
#include "UI/ui_core.h"
#include "UI/ui_hall_mode_state.h"
#include "UI/ui_page_manager.h"

#define MONKEY_TEST_MONITOR_PERIOD_TICKS 150U
#define MONKEY_TEST_MONITOR_CANARY_HEAD  0x4D544831UL
#define MONKEY_TEST_MONITOR_CANARY_TAIL  0x4D545431UL

typedef struct
{
    uint32_t canary_head;
    uint32_t next_poll_tick;
    uint32_t cpu_over_90;
    uint32_t cpu_over_100;
    uint32_t sampler_underrun;
    uint32_t sampler_invalid;
    uint32_t looper_cache_miss;
    uint32_t looper_underrun;
    uint8_t active;
    uint32_t canary_tail;
} monkey_test_monitor_runtime_t;

static monkey_test_monitor_runtime_t g_monitor;
static brick6_sampler_runtime_health_snapshot_t g_sampler_snapshot;
static looper_raw_debug_health_snapshot_t g_looper_snapshot;

static uint8_t counter_advanced(uint32_t current, uint32_t previous)
{
    return ((current >= previous) && (current != previous)) ? 1U : 0U;
}

static void result_note(monkey_test_monitor_result_t *result,
                        monkey_test_issue_t issue,
                        monkey_test_severity_t severity)
{
    if (severity == MONKEY_TEST_SEVERITY_WARNING)
    {
        result->warning_count++;
    }
    else
    {
        result->error_count++;
    }

    if (severity >= result->severity)
    {
        result->issue = issue;
        result->severity = severity;
    }
    if (severity == MONKEY_TEST_SEVERITY_STOP)
    {
        result->stop_required = 1U;
    }
}

static void capture_baselines(void)
{
    cpu_load_metrics_t cpu;
    cpu_load_get_metrics(&cpu);
    brick6_sampler_runtime_get_health_snapshot(&g_sampler_snapshot);
    looper_raw_debug_get_health_snapshot(&g_looper_snapshot);

    g_monitor.cpu_over_90 = cpu.over_90_count;
    g_monitor.cpu_over_100 = cpu.over_100_count;
    g_monitor.sampler_underrun =
        g_sampler_snapshot.multi_page_underrun
        + g_sampler_snapshot.multi_stop_underrun;
    g_monitor.sampler_invalid =
        g_sampler_snapshot.multi_invalid_instrument_id;
    g_monitor.looper_cache_miss = g_looper_snapshot.cache_miss_count;
    g_monitor.looper_underrun =
        g_looper_snapshot.preroll_underrun_count
        + g_looper_snapshot.preroll_reused_after_wrap_count;
}

void monkey_test_monitor_init(void)
{
    memset(&g_monitor, 0, sizeof(g_monitor));
    g_monitor.canary_head = MONKEY_TEST_MONITOR_CANARY_HEAD;
    g_monitor.canary_tail = MONKEY_TEST_MONITOR_CANARY_TAIL;
}

void monkey_test_monitor_start(uint32_t engine_tick)
{
    g_monitor.canary_head = MONKEY_TEST_MONITOR_CANARY_HEAD;
    g_monitor.canary_tail = MONKEY_TEST_MONITOR_CANARY_TAIL;
    capture_baselines();
    g_monitor.next_poll_tick = engine_tick + MONKEY_TEST_MONITOR_PERIOD_TICKS;
    g_monitor.active = 1U;
}

void monkey_test_monitor_stop(void)
{
    g_monitor.active = 0U;
}

uint8_t monkey_test_monitor_poll(uint32_t engine_tick,
                                 monkey_test_monitor_result_t *out_result)
{
    if ((out_result == 0) || (g_monitor.active == 0U)
        || ((int32_t)(engine_tick - g_monitor.next_poll_tick) < 0))
    {
        return 0U;
    }

    memset(out_result, 0, sizeof(*out_result));
    g_monitor.next_poll_tick = engine_tick + MONKEY_TEST_MONITOR_PERIOD_TICKS;

    if ((g_monitor.canary_head != MONKEY_TEST_MONITOR_CANARY_HEAD)
        || (g_monitor.canary_tail != MONKEY_TEST_MONITOR_CANARY_TAIL))
    {
        result_note(out_result, MONKEY_TEST_ISSUE_MONITOR_CANARY,
                    MONKEY_TEST_SEVERITY_STOP);
        return 1U;
    }

    cpu_load_metrics_t cpu;
    cpu_load_get_metrics(&cpu);
    brick6_sampler_runtime_get_health_snapshot(&g_sampler_snapshot);
    looper_raw_debug_get_health_snapshot(&g_looper_snapshot);

    if (counter_advanced(cpu.over_100_count, g_monitor.cpu_over_100) != 0U)
    {
        result_note(out_result, MONKEY_TEST_ISSUE_CPU_OVER_100,
                    MONKEY_TEST_SEVERITY_RECOVERABLE);
    }
    else if (counter_advanced(cpu.over_90_count, g_monitor.cpu_over_90) != 0U)
    {
        result_note(out_result, MONKEY_TEST_ISSUE_CPU_OVER_90,
                    MONKEY_TEST_SEVERITY_WARNING);
    }

    const uint32_t sampler_underrun =
        g_sampler_snapshot.multi_page_underrun
        + g_sampler_snapshot.multi_stop_underrun;
    const uint32_t sampler_invalid =
        g_sampler_snapshot.multi_invalid_instrument_id;
    const uint32_t looper_underrun =
        g_looper_snapshot.preroll_underrun_count
        + g_looper_snapshot.preroll_reused_after_wrap_count;

    if (counter_advanced(sampler_underrun, g_monitor.sampler_underrun) != 0U)
    {
        result_note(out_result, MONKEY_TEST_ISSUE_SAMPLER_UNDERRUN,
                    MONKEY_TEST_SEVERITY_RECOVERABLE);
    }
    if (counter_advanced(sampler_invalid, g_monitor.sampler_invalid) != 0U)
    {
        result_note(out_result, MONKEY_TEST_ISSUE_SAMPLER_INVALID,
                    MONKEY_TEST_SEVERITY_RECOVERABLE);
    }
    if (counter_advanced(g_looper_snapshot.cache_miss_count,
                         g_monitor.looper_cache_miss) != 0U)
    {
        result_note(out_result, MONKEY_TEST_ISSUE_LOOPER_CACHE_MISS,
                    MONKEY_TEST_SEVERITY_WARNING);
    }
    if (counter_advanced(looper_underrun, g_monitor.looper_underrun) != 0U)
    {
        result_note(out_result, MONKEY_TEST_ISSUE_LOOPER_UNDERRUN,
                    MONKEY_TEST_SEVERITY_RECOVERABLE);
    }
    if ((ui_get_active_track() >= UI_TRACK_COUNT)
        || ((uint8_t)ui_get_hall_mode() >= (uint8_t)UI_HALL_MODE_COUNT)
        || (ui_page_get_id() >= UI_PAGE_COUNT))
    {
        result_note(out_result, MONKEY_TEST_ISSUE_UI_INVARIANT,
                    MONKEY_TEST_SEVERITY_STOP);
    }

    g_monitor.cpu_over_90 = cpu.over_90_count;
    g_monitor.cpu_over_100 = cpu.over_100_count;
    g_monitor.sampler_underrun = sampler_underrun;
    g_monitor.sampler_invalid = sampler_invalid;
    g_monitor.looper_cache_miss = g_looper_snapshot.cache_miss_count;
    g_monitor.looper_underrun = looper_underrun;
    return 1U;
}

const char *monkey_test_issue_label(monkey_test_issue_t issue)
{
    switch (issue)
    {
        case MONKEY_TEST_ISSUE_INPUT_REJECTED:
            return "INPUT";
        case MONKEY_TEST_ISSUE_CPU_OVER_90:
            return "CPU>90";
        case MONKEY_TEST_ISSUE_CPU_OVER_100:
            return "CPU>100";
        case MONKEY_TEST_ISSUE_SAMPLER_UNDERRUN:
            return "SAMP UND";
        case MONKEY_TEST_ISSUE_SAMPLER_INVALID:
            return "SAMP IDX";
        case MONKEY_TEST_ISSUE_LOOPER_CACHE_MISS:
            return "LOOP MISS";
        case MONKEY_TEST_ISSUE_LOOPER_UNDERRUN:
            return "LOOP UND";
        case MONKEY_TEST_ISSUE_UI_INVARIANT:
            return "UI INV";
        case MONKEY_TEST_ISSUE_MONITOR_CANARY:
            return "CANARY";
        case MONKEY_TEST_ISSUE_FAULT_RESET:
            return "FAULT RESET";
        case MONKEY_TEST_ISSUE_WATCHDOG_RESET:
            return "WATCHDOG RESET";
        case MONKEY_TEST_ISSUE_REPLAY_MISMATCH:
            return "REPLAY MISMATCH";
        case MONKEY_TEST_ISSUE_NONE:
        default:
            return "OK";
    }
}
