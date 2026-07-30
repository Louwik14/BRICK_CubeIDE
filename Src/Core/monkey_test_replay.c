#include "Core/monkey_test_replay.h"

#include <string.h>

typedef struct
{
    monkey_test_generator_t generator;
    crash_capsule_breadcrumb_t target;
    crash_capsule_breadcrumb_t breadcrumbs[
        CRASH_CAPSULE_BREADCRUMB_CAPACITY];
    uint32_t seed;
    uint32_t target_index;
    uint32_t breadcrumb_count;
    uint8_t ready;
} monkey_test_replay_runtime_t;

static monkey_test_replay_runtime_t g_replay;

static uint8_t monkey_test_replay_find_target(
    const crash_capsule_snapshot_t *snapshot,
    crash_capsule_breadcrumb_t *out_target)
{
    if ((snapshot == NULL) || (out_target == NULL)
        || (snapshot->breadcrumb_count == 0U)
        || (snapshot->breadcrumb_count > CRASH_CAPSULE_BREADCRUMB_CAPACITY))
    {
        return 0U;
    }

    const uint32_t count = snapshot->breadcrumb_count;
    const uint32_t oldest =
        (count == CRASH_CAPSULE_BREADCRUMB_CAPACITY)
            ? snapshot->breadcrumb_write_index : 0U;
    for (uint32_t offset = 0U; offset < count; ++offset)
    {
        const uint32_t slot =
            (oldest + offset) % CRASH_CAPSULE_BREADCRUMB_CAPACITY;
        if (snapshot->breadcrumbs[slot].index == snapshot->action_index)
        {
            *out_target = snapshot->breadcrumbs[slot];
            return 1U;
        }
    }
    return 0U;
}

static uint8_t monkey_test_replay_action_matches(
    const monkey_test_action_t *action,
    const crash_capsule_breadcrumb_t *breadcrumb)
{
    return ((action->index == breadcrumb->index)
            && (action->logical_tick == breadcrumb->logical_tick)
            && (action->delay_ticks == breadcrumb->delay_ticks)
            && (action->value == breadcrumb->value)
            && ((uint8_t)action->type == breadcrumb->type)
            && (action->target == breadcrumb->target)) ? 1U : 0U;
}

static const crash_capsule_breadcrumb_t *monkey_test_replay_find_breadcrumb(
    uint32_t action_index)
{
    for (uint32_t index = 0U;
         index < g_replay.breadcrumb_count;
         ++index)
    {
        if (g_replay.breadcrumbs[index].index == action_index)
        {
            return &g_replay.breadcrumbs[index];
        }
    }
    return NULL;
}

void monkey_test_replay_init(void)
{
    memset(&g_replay, 0, sizeof(g_replay));
}

uint8_t monkey_test_replay_prepare(
    const crash_capsule_snapshot_t *snapshot)
{
    memset(&g_replay, 0, sizeof(g_replay));
    if ((snapshot == NULL)
        || (monkey_test_replay_find_target(snapshot,
                                           &g_replay.target) == 0U))
    {
        return 0U;
    }

    g_replay.seed = snapshot->seed;
    g_replay.target_index = snapshot->action_index;
    const uint32_t count = snapshot->breadcrumb_count;
    const uint32_t oldest =
        (count == CRASH_CAPSULE_BREADCRUMB_CAPACITY)
            ? snapshot->breadcrumb_write_index : 0U;
    for (uint32_t offset = 0U; offset < count; ++offset)
    {
        const uint32_t slot =
            (oldest + offset) % CRASH_CAPSULE_BREADCRUMB_CAPACITY;
        g_replay.breadcrumbs[offset] = snapshot->breadcrumbs[slot];
    }
    g_replay.breadcrumb_count = count;
    monkey_test_generator_init(&g_replay.generator, snapshot->seed);
    monkey_test_generator_set_context(&g_replay.generator,
                                      MONKEY_TEST_CONTEXT_SETTINGS);
    g_replay.ready = 1U;
    return 1U;
}

monkey_test_replay_next_t monkey_test_replay_next(
    monkey_test_action_t *out_action)
{
    if ((out_action == NULL) || (g_replay.ready == 0U)
        || (monkey_test_generator_next(&g_replay.generator,
                                       out_action) == 0U))
    {
        return MONKEY_TEST_REPLAY_NEXT_ERROR;
    }
    const crash_capsule_breadcrumb_t *const archived =
        monkey_test_replay_find_breadcrumb(out_action->index);
    if ((archived != NULL)
        && (monkey_test_replay_action_matches(out_action, archived) == 0U))
    {
        return MONKEY_TEST_REPLAY_NEXT_MISMATCH;
    }
    if (out_action->index < g_replay.target_index)
    {
        return MONKEY_TEST_REPLAY_NEXT_PREFIX;
    }
    if (out_action->index != g_replay.target_index)
    {
        return MONKEY_TEST_REPLAY_NEXT_ERROR;
    }
    return (monkey_test_replay_action_matches(out_action,
                                              &g_replay.target) != 0U)
        ? MONKEY_TEST_REPLAY_NEXT_TARGET
        : MONKEY_TEST_REPLAY_NEXT_MISMATCH;
}

uint32_t monkey_test_replay_seed(void)
{
    return g_replay.seed;
}

uint32_t monkey_test_replay_target_index(void)
{
    return g_replay.target_index;
}
