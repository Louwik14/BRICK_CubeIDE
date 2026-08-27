#include "Core/control_music_output.h"

#include <stddef.h>
#include <string.h>

#include "Core/track_runtime.h"
#include "Core/brick6_sampler_multi_contract.h"
#include "Param/param_registry.h"
#include "Param/param_registry_runtime_state.h"
#include "Storage/memory_layout.h"

typedef struct
{
    uint32_t output_id;
    uint32_t trigger_id;
    uint32_t age;
    uint32_t binding_generation;
    uint8_t note;
    uint8_t alive;
    uint8_t multi;
    uint8_t reserved;
} control_music_output_t;

CONTROL_M4_SRAM2 static control_music_output_t
    g_control_music_outputs[BRICK_ENTITY_CAPACITY][CONTROL_MUSIC_OUTPUTS_PER_ENTITY];
static uint32_t g_control_music_output_age;
static control_music_output_death_observer_t
    g_control_music_output_death_observer[
        CONTROL_MUSIC_OUTPUT_DEATH_OBSERVER_CAPACITY];

#define CONTROL_MUSIC_WINDOW_MAX_FRAMES 64U
#define CONTROL_MUSIC_WINDOW_KIND_COUNT 3U
#define CONTROL_MUSIC_WINDOW_BUCKET_COUNT \
    (CONTROL_MUSIC_WINDOW_MAX_FRAMES * CONTROL_MUSIC_WINDOW_KIND_COUNT)
#define CONTROL_MUSIC_WINDOW_NONE UINT16_MAX

typedef struct
{
    control_music_action_t actions[CONTROL_MUSIC_INTERNAL_MAX_HORIZON_BURST];
    uint16_t next[CONTROL_MUSIC_INTERNAL_MAX_HORIZON_BURST];
    uint16_t head[CONTROL_MUSIC_WINDOW_BUCKET_COUNT];
    uint16_t tail[CONTROL_MUSIC_WINDOW_BUCKET_COUNT];
    uint16_t count;
} control_music_window_internal_t;

typedef struct
{
    control_music_action_t actions[CONTROL_MUSIC_EXTERNAL_QUEUE_CAPACITY - 1U];
    uint16_t next[CONTROL_MUSIC_EXTERNAL_QUEUE_CAPACITY - 1U];
    uint16_t head[CONTROL_MUSIC_WINDOW_BUCKET_COUNT];
    uint16_t tail[CONTROL_MUSIC_WINDOW_BUCKET_COUNT];
    uint16_t count;
} control_music_window_external_t;

SEQ_STATE_D2 static control_music_window_internal_t
    g_control_music_window_internal;
SEQ_STATE_D2 static control_music_window_external_t
    g_control_music_window_external;
static uint64_t g_control_music_window_first;
static uint64_t g_control_music_first_unpublished;
static uint16_t g_control_music_window_frames;
static uint16_t g_control_music_window_internal_limit;
static uint16_t g_control_music_window_external_limit;
static uint8_t g_control_music_window_active;

static void control_music_output_reset_window_buckets(void)
{
    for (uint16_t i = 0U; i < CONTROL_MUSIC_WINDOW_BUCKET_COUNT; ++i)
    {
        g_control_music_window_internal.head[i] = CONTROL_MUSIC_WINDOW_NONE;
        g_control_music_window_internal.tail[i] = CONTROL_MUSIC_WINDOW_NONE;
        g_control_music_window_external.head[i] = CONTROL_MUSIC_WINDOW_NONE;
        g_control_music_window_external.tail[i] = CONTROL_MUSIC_WINDOW_NONE;
    }
    g_control_music_window_internal.count = 0U;
    g_control_music_window_external.count = 0U;
}

uint8_t control_music_output_begin_window(uint64_t first_sample,
                                          uint16_t frames)
{
    if ((g_control_music_window_active != 0U) || (frames == 0U)
            || (frames > CONTROL_MUSIC_WINDOW_MAX_FRAMES))
        return 0U;
    if (first_sample < g_control_music_first_unpublished)
        first_sample = g_control_music_first_unpublished;
    g_control_music_window_first = first_sample;
    g_control_music_window_frames = frames;
    g_control_music_window_internal_limit =
        control_music_queue_control_free(0U);
    if (g_control_music_window_internal_limit
            > CONTROL_MUSIC_INTERNAL_MAX_HORIZON_BURST)
        g_control_music_window_internal_limit =
            CONTROL_MUSIC_INTERNAL_MAX_HORIZON_BURST;
    g_control_music_window_external_limit =
        control_music_queue_control_free(1U);
    if (g_control_music_window_external_limit
            > (CONTROL_MUSIC_EXTERNAL_QUEUE_CAPACITY - 1U))
        g_control_music_window_external_limit =
            CONTROL_MUSIC_EXTERNAL_QUEUE_CAPACITY - 1U;
    control_music_output_reset_window_buckets();
    g_control_music_window_active = 1U;
    return 1U;
}

uint64_t control_music_output_first_unpublished_sample(uint64_t audio_sample)
{
    return (g_control_music_first_unpublished > audio_sample)
        ? g_control_music_first_unpublished : audio_sample;
}

void control_music_output_abort_window(void)
{
    if (g_control_music_window_active == 0U)
        return;
    control_music_output_reset_window_buckets();
    g_control_music_window_active = 0U;
}

static uint8_t control_music_output_stage(const control_music_action_t *action)
{
    if ((action == NULL)
            || (action->entity_id >= BRICK_ENTITY_CAPACITY)
            || (action->kind > (uint8_t)CONTROL_MUSIC_ACTION_RETRIGGER)
            || (action->output_id == 0U) || (action->note >= 128U)
            || (action->velocity >= 128U)
            || (action->due_sample < g_control_music_window_first)
            || (action->due_sample
                >= (g_control_music_window_first + g_control_music_window_frames)))
        return 0U;
    const uint16_t offset = (uint16_t)(
        action->due_sample - g_control_music_window_first);
    const uint16_t bucket = (uint16_t)(
        (offset * CONTROL_MUSIC_WINDOW_KIND_COUNT) + action->kind);
    const uint8_t external = (uint8_t)(
        (action->trigger_id & CONTROL_MUSIC_TRIGGER_EXTERNAL_FLAG) != 0U);
    control_music_action_t *actions;
    uint16_t *next;
    uint16_t *head;
    uint16_t *tail;
    uint16_t *count;
    uint16_t capacity;
    if (external != 0U)
    {
        actions = g_control_music_window_external.actions;
        next = g_control_music_window_external.next;
        head = g_control_music_window_external.head;
        tail = g_control_music_window_external.tail;
        count = &g_control_music_window_external.count;
        capacity = g_control_music_window_external_limit;
    }
    else
    {
        actions = g_control_music_window_internal.actions;
        next = g_control_music_window_internal.next;
        head = g_control_music_window_internal.head;
        tail = g_control_music_window_internal.tail;
        count = &g_control_music_window_internal.count;
        capacity = g_control_music_window_internal_limit;
    }
    if (*count >= capacity)
        return 0U;
    const uint16_t index = (*count)++;
    actions[index] = *action;
    next[index] = CONTROL_MUSIC_WINDOW_NONE;
    if (head[bucket] == CONTROL_MUSIC_WINDOW_NONE)
        head[bucket] = index;
    else
        next[tail[bucket]] = index;
    tail[bucket] = index;
    return 1U;
}

static uint8_t control_music_output_publish_batch(
    const control_music_action_t *actions, uint16_t count);

static uint8_t control_music_output_publish(const control_music_action_t *action)
{
    return control_music_output_publish_batch(action, 1U);
}

static uint8_t control_music_output_publish_batch(
    const control_music_action_t *actions, uint16_t count)
{
    if (g_control_music_window_active == 0U)
    {
        if ((actions == NULL) || (count == 0U)
                || (count > (CONTROL_MUSIC_OUTPUTS_PER_ENTITY + 1U)))
            return 0U;
        uint64_t first_sample = actions[0].due_sample;
        for (uint16_t i = 1U; i < count; ++i)
            if (actions[i].due_sample > first_sample)
                first_sample = actions[i].due_sample;
        first_sample = control_music_output_first_unpublished_sample(first_sample);
        if (control_music_output_begin_window(first_sample, 1U) == 0U)
            return 0U;
        control_music_action_t normalized[CONTROL_MUSIC_OUTPUTS_PER_ENTITY + 1U];
        for (uint16_t i = 0U; i < count; ++i)
        {
            normalized[i] = actions[i];
            normalized[i].due_sample = first_sample;
        }
        if (control_music_output_publish_batch(normalized, count) == 0U)
        {
            control_music_output_abort_window();
            return 0U;
        }
        if (control_music_output_commit_window() == 0U)
        {
            control_music_output_abort_window();
            return 0U;
        }
        return 1U;
    }
    if ((actions == NULL) || (count == 0U))
        return 0U;
    const uint8_t external = (uint8_t)(
        (actions[0].trigger_id & CONTROL_MUSIC_TRIGGER_EXTERNAL_FLAG) != 0U);
    const uint16_t staged_count = (external != 0U)
        ? g_control_music_window_external.count
        : g_control_music_window_internal.count;
    const uint16_t capacity = (external != 0U)
        ? g_control_music_window_external_limit
        : g_control_music_window_internal_limit;
    if ((uint32_t)staged_count + count > capacity)
        return 0U;
    for (uint16_t i = 0U; i < count; ++i)
    {
        const uint8_t action_external = (uint8_t)(
            (actions[i].trigger_id & CONTROL_MUSIC_TRIGGER_EXTERNAL_FLAG) != 0U);
        if ((action_external != external)
                || (actions[i].due_sample < g_control_music_window_first)
                || (actions[i].due_sample
                    >= (g_control_music_window_first
                        + g_control_music_window_frames)))
            return 0U;
    }
    for (uint16_t i = 0U; i < count; ++i)
        if (control_music_output_stage(&actions[i]) == 0U)
            return 0U;
    return 1U;
}

uint8_t control_music_output_commit_window(void)
{
    if (g_control_music_window_active == 0U)
        return 0U;
    if ((control_music_queue_control_free(0U)
            < g_control_music_window_internal.count)
            || (control_music_queue_control_free(1U)
                < g_control_music_window_external.count))
        return 0U;

    uint8_t accepted = 1U;
    if (g_control_music_window_internal.count != 0U)
    {
        accepted = control_music_queue_publish_ordered_window(
            g_control_music_window_internal.actions,
            g_control_music_window_internal.next,
            g_control_music_window_internal.head,
            (uint16_t)(g_control_music_window_frames
                       * CONTROL_MUSIC_WINDOW_KIND_COUNT),
            g_control_music_window_internal.count, 0U);
    }
    if ((accepted != 0U) && (g_control_music_window_external.count != 0U))
    {
        accepted = control_music_queue_publish_ordered_window(
            g_control_music_window_external.actions,
            g_control_music_window_external.next,
            g_control_music_window_external.head,
            (uint16_t)(g_control_music_window_frames
                       * CONTROL_MUSIC_WINDOW_KIND_COUNT),
            g_control_music_window_external.count, 1U);
    }
    if (accepted == 0U)
        return 0U;
    g_control_music_first_unpublished =
        g_control_music_window_first + g_control_music_window_frames;
    g_control_music_window_active = 0U;
    return accepted;
}

static void control_music_output_mark_dead(brick_entity_id_t entity_id,
                                           uint8_t index)
{
    control_music_output_t *const output =
        &g_control_music_outputs[entity_id][index];
    if (output->alive == 0U)
        return;
    const uint32_t output_id = output->output_id;
    output->alive = 0U;
    for (uint8_t i = 0U;
         i < CONTROL_MUSIC_OUTPUT_DEATH_OBSERVER_CAPACITY; ++i)
        if (g_control_music_output_death_observer[i] != NULL)
            g_control_music_output_death_observer[i](entity_id, output_id);
}

uint8_t control_music_output_register_death_observer(
    control_music_output_death_observer_t observer)
{
    if (observer == NULL)
        return 0U;
    for (uint8_t i = 0U;
         i < CONTROL_MUSIC_OUTPUT_DEATH_OBSERVER_CAPACITY; ++i)
    {
        if (g_control_music_output_death_observer[i] == observer)
            return 1U;
        if (g_control_music_output_death_observer[i] == NULL)
        {
            g_control_music_output_death_observer[i] = observer;
            return 1U;
        }
    }
    return 0U;
}

static uint8_t control_music_output_limit(brick_entity_id_t entity_id)
{
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(entity_id);
    if ((ctx == NULL)
            || ((ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
                && !((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                    && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI))))
        return 1U;

    float configured = 1.0f;
    (void)param_registry_control_shadow_get(entity_id, PARAM_CFG_POLY_VOICES,
                                            &configured);
    uint8_t limit = (configured >= 1.0f) ? (uint8_t)configured : 1U;
    return (limit > CONTROL_MUSIC_OUTPUTS_PER_ENTITY)
        ? CONTROL_MUSIC_OUTPUTS_PER_ENTITY : limit;
}

static uint8_t control_music_output_is_multi(brick_entity_id_t entity_id)
{
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(entity_id);
    return ((ctx != NULL)
            && (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI)) ? 1U : 0U;
}

static uint8_t control_music_output_multi_live_count(void)
{
    uint8_t count = 0U;
    for (brick_entity_id_t entity_id = 0U;
         entity_id < BRICK_ENTITY_CAPACITY; ++entity_id)
    {
        for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
            count += ((g_control_music_outputs[entity_id][i].alive != 0U)
                && (g_control_music_outputs[entity_id][i].multi != 0U))
                ? 1U : 0U;
    }
    return count;
}

static uint8_t control_music_output_ref_is_excluded(
    brick_entity_id_t entity_id, uint8_t index,
    const brick_entity_id_t *excluded_entities,
    const uint8_t *excluded_indices, uint8_t excluded_count)
{
    for (uint8_t i = 0U; i < excluded_count; ++i)
        if ((excluded_entities[i] == entity_id)
                && (excluded_indices[i] == index))
            return 1U;
    return 0U;
}

static uint8_t control_music_output_find_oldest_multi(
    const brick_entity_id_t *excluded_entities,
    const uint8_t *excluded_indices, uint8_t excluded_count,
    brick_entity_id_t *out_entity, uint8_t *out_index)
{
    uint8_t found = 0U;
    uint32_t oldest_age = UINT32_MAX;
    for (brick_entity_id_t entity_id = 0U;
         entity_id < BRICK_ENTITY_CAPACITY; ++entity_id)
    {
        for (uint8_t index = 0U;
             index < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++index)
        {
            const control_music_output_t *const output =
                &g_control_music_outputs[entity_id][index];
            if ((output->alive == 0U) || (output->multi == 0U)
                    || (control_music_output_ref_is_excluded(
                        entity_id, index, excluded_entities, excluded_indices,
                        excluded_count) != 0U))
                continue;
            if ((found == 0U) || (output->age < oldest_age))
            {
                found = 1U;
                oldest_age = output->age;
                *out_entity = entity_id;
                *out_index = index;
            }
        }
    }
    return found;
}

static int8_t control_music_output_find(brick_entity_id_t entity_id,
                                        uint32_t output_id)
{
    for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
        if ((g_control_music_outputs[entity_id][i].alive != 0U)
                && (g_control_music_outputs[entity_id][i].output_id == output_id))
            return (int8_t)i;
    return -1;
}

static uint8_t control_music_output_live_count(brick_entity_id_t entity_id)
{
    uint8_t count = 0U;
    for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
        count += (g_control_music_outputs[entity_id][i].alive != 0U) ? 1U : 0U;
    return count;
}

static int8_t control_music_output_find_free(brick_entity_id_t entity_id)
{
    for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
        if (g_control_music_outputs[entity_id][i].alive == 0U)
            return (int8_t)i;
    return -1;
}

static int8_t control_music_output_find_oldest(brick_entity_id_t entity_id,
                                               uint8_t excluded_mask)
{
    int8_t oldest_index = -1;
    uint32_t oldest_age = UINT32_MAX;
    for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
    {
        const control_music_output_t *const output =
            &g_control_music_outputs[entity_id][i];
        if ((output->alive == 0U)
                || ((excluded_mask & (uint8_t)(1U << i)) != 0U))
            continue;
        if (output->age < oldest_age)
        {
            oldest_age = output->age;
            oldest_index = (int8_t)i;
        }
    }
    return oldest_index;
}

uint8_t control_music_output_submit(const control_music_action_t *action)
{
    if ((action == NULL) || (action->entity_id >= BRICK_ENTITY_CAPACITY)
            || (action->kind > (uint8_t)CONTROL_MUSIC_ACTION_RETRIGGER)
            || (action->output_id == 0U))
        return 0U;

    const brick_entity_id_t entity_id = action->entity_id;
    const int8_t existing = control_music_output_find(entity_id,
                                                       action->output_id);
    if (action->kind == (uint8_t)CONTROL_MUSIC_ACTION_STOP)
    {
        if (existing < 0)
        {
            return 1U;
        }
        if (control_music_output_publish(action) == 0U)
        {
            return 0U;
        }
        control_music_output_mark_dead(entity_id, (uint8_t)existing);
        return 1U;
    }

    if (existing >= 0)
    {
        control_music_action_t retrigger = *action;
        retrigger.kind = (uint8_t)CONTROL_MUSIC_ACTION_RETRIGGER;
        if (control_music_output_publish(&retrigger) == 0U)
            return 0U;
        control_music_output_t *const output =
            &g_control_music_outputs[entity_id][(uint8_t)existing];
        output->age = ++g_control_music_output_age;
        output->note = action->note;
        output->trigger_id = action->trigger_id;
        return 1U;
    }

    const uint8_t live_count = control_music_output_live_count(entity_id);
    const uint8_t limit = control_music_output_limit(entity_id);
    control_music_action_t batch[CONTROL_MUSIC_OUTPUTS_PER_ENTITY + 1U];
    brick_entity_id_t victim_entities[CONTROL_MUSIC_OUTPUTS_PER_ENTITY + 1U];
    uint8_t victim_indices[CONTROL_MUSIC_OUTPUTS_PER_ENTITY + 1U];
    uint16_t count = 0U;
    uint8_t victim_count = 0U;
    if (control_music_output_is_multi(entity_id) != 0U)
    {
        const uint8_t per_track_stops = (live_count >= limit)
            ? (uint8_t)(live_count - limit + 1U) : 0U;
        uint8_t excluded_mask = 0U;
        for (; victim_count < per_track_stops; ++victim_count)
        {
            const int8_t target = control_music_output_find_oldest(
                entity_id, excluded_mask);
            if (target < 0)
                return 0U;
            victim_entities[victim_count] = entity_id;
            victim_indices[victim_count] = (uint8_t)target;
            excluded_mask |= (uint8_t)(1U << (uint8_t)target);
        }
        const uint8_t multi_live_count = control_music_output_multi_live_count();
        if (((uint16_t)multi_live_count - victim_count + 1U)
                > BRICK6_SAMPLER_MULTI_MAX_VOICES)
        {
            brick_entity_id_t target_entity = BRICK_ENTITY_INVALID_ID;
            uint8_t target_index = UINT8_MAX;
            if (control_music_output_find_oldest_multi(
                    victim_entities, victim_indices, victim_count,
                    &target_entity, &target_index) == 0U)
                return 0U;
            victim_entities[victim_count] = target_entity;
            victim_indices[victim_count++] = target_index;
        }
    }
    else
    {
        const uint8_t stop_count = (live_count >= limit)
            ? (uint8_t)(live_count - limit + 1U) : 0U;
        uint8_t excluded_mask = 0U;
        for (victim_count = 0U; victim_count < stop_count; ++victim_count)
        {
            const int8_t target = control_music_output_find_oldest(
                entity_id, excluded_mask);
            if (target < 0)
                return 0U;
            victim_entities[victim_count] = entity_id;
            victim_indices[victim_count] = (uint8_t)target;
            excluded_mask |= (uint8_t)(1U << (uint8_t)target);
        }
    }
    for (uint8_t i = 0U; i < victim_count; ++i)
    {
        const brick_entity_id_t victim_entity = victim_entities[i];
        const uint8_t victim_index = victim_indices[i];
        const control_music_output_t *const victim =
            &g_control_music_outputs[victim_entity][victim_index];
        batch[count++] = (control_music_action_t){
            .due_sample = action->due_sample,
            .binding_generation = victim->binding_generation,
            .output_id = victim->output_id,
            .trigger_id = (victim->trigger_id
                    & ~CONTROL_MUSIC_TRIGGER_EXTERNAL_FLAG)
                | (action->trigger_id
                    & CONTROL_MUSIC_TRIGGER_EXTERNAL_FLAG),
            .entity_id = victim_entity,
            .kind = (uint8_t)CONTROL_MUSIC_ACTION_STOP,
            .note = victim->note
        };
    }
    batch[count++] = *action;
    if (control_music_output_publish_batch(batch, count) == 0U)
    {
        return 0U;
    }

    for (uint8_t i = 0U; i < victim_count; ++i)
        control_music_output_mark_dead(victim_entities[i], victim_indices[i]);
    const int8_t target = control_music_output_find_free(entity_id);
    if (target < 0)
        return 0U;

    g_control_music_outputs[entity_id][(uint8_t)target] =
        (control_music_output_t){
            .output_id = action->output_id,
            .trigger_id = action->trigger_id,
            .age = ++g_control_music_output_age,
            .binding_generation = action->binding_generation,
            .note = action->note,
            .alive = 1U,
            .multi = (control_music_output_is_multi(entity_id) != 0U)
        };
    return 1U;
}

uint8_t control_music_output_close_entity(brick_entity_id_t entity_id,
                                          uint64_t due_sample)
{
    return control_music_output_close_entities(&entity_id, 1U, due_sample);
}

uint8_t control_music_output_close_entities(
    const brick_entity_id_t *entity_ids, uint8_t entity_count,
    uint64_t due_sample)
{
    if ((entity_ids == NULL) || (entity_count == 0U)
            || (entity_count > BRICK_ENTITY_CAPACITY))
        return 0U;

    uint8_t selected[BRICK_ENTITY_CAPACITY] = { 0U };
    uint16_t counts[2U] = { 0U, 0U };
    for (uint8_t entity_index = 0U;
         entity_index < entity_count; ++entity_index)
    {
        const brick_entity_id_t entity_id = entity_ids[entity_index];
        if (entity_id >= BRICK_ENTITY_CAPACITY)
            return 0U;
        if (selected[entity_id] != 0U)
            continue;
        selected[entity_id] = 1U;
        for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
        {
            const control_music_output_t *const output =
                &g_control_music_outputs[entity_id][i];
            if (output->alive == 0U)
                continue;
            const uint8_t external = (uint8_t)(
                (output->trigger_id
                    & CONTROL_MUSIC_TRIGGER_EXTERNAL_FLAG) != 0U);
            ++counts[external];
        }
    }

    if ((counts[0U] == 0U) && (counts[1U] == 0U))
        return 1U;

    uint8_t opened_window = 0U;
    if (g_control_music_window_active == 0U)
    {
        due_sample = control_music_output_first_unpublished_sample(due_sample);
        if (control_music_output_begin_window(due_sample, 1U) == 0U)
            return 0U;
        opened_window = 1U;
    }

    if ((g_control_music_window_internal.count + counts[0U]
            > g_control_music_window_internal_limit)
            || (g_control_music_window_external.count + counts[1U]
                > g_control_music_window_external_limit))
    {
        if (opened_window != 0U)
            control_music_output_abort_window();
        return 0U;
    }

    for (brick_entity_id_t entity_id = 0U;
         entity_id < BRICK_ENTITY_CAPACITY; ++entity_id)
    {
        if (selected[entity_id] == 0U)
            continue;
        for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
        {
            const control_music_output_t *const output =
                &g_control_music_outputs[entity_id][i];
            if (output->alive == 0U)
                continue;
            const control_music_action_t action = {
                .due_sample = due_sample,
                .binding_generation = output->binding_generation,
                .output_id = output->output_id,
                .trigger_id = output->trigger_id,
                .entity_id = entity_id,
                .kind = (uint8_t)CONTROL_MUSIC_ACTION_STOP,
                .note = output->note
            };
            if (control_music_output_publish(&action) == 0U)
            {
                if (opened_window != 0U)
                    control_music_output_abort_window();
                return 0U;
            }
        }
    }

    if ((opened_window != 0U)
            && (control_music_output_commit_window() == 0U))
    {
        control_music_output_abort_window();
        return 0U;
    }

    for (brick_entity_id_t entity_id = 0U;
         entity_id < BRICK_ENTITY_CAPACITY; ++entity_id)
        if (selected[entity_id] != 0U)
            for (uint8_t i = 0U;
                 i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
                if (g_control_music_outputs[entity_id][i].alive != 0U)
                    control_music_output_mark_dead(entity_id, i);
    return 1U;
}

void control_music_output_panic_all(void)
{
    if (g_control_music_window_active != 0U)
        control_music_output_reset_window_buckets();
    for (brick_entity_id_t entity_id = 0U;
         entity_id < BRICK_ENTITY_CAPACITY; ++entity_id)
        for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
            control_music_output_mark_dead(entity_id, i);
    control_music_queue_request_panic();
}
