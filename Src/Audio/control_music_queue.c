#include "Audio/control_music_queue.h"

#include <stddef.h>

#include "Storage/memory_layout.h"
#include "stm32h7xx_hal.h"

typedef struct
{
    control_music_action_t actions[CONTROL_MUSIC_QUEUE_CAPACITY];
    volatile uint16_t head;
    volatile uint16_t tail;
} control_music_internal_queue_t;

typedef struct
{
    control_music_action_t actions[CONTROL_MUSIC_EXTERNAL_QUEUE_CAPACITY];
    volatile uint16_t head;
    volatile uint16_t tail;
} control_music_external_queue_t;

D3_IPC static control_music_internal_queue_t g_control_music_internal;
D3_IPC static control_music_external_queue_t g_control_music_external;
D3_IPC static volatile uint32_t g_control_music_panic_sequence;
D3_IPC static volatile uint32_t g_control_music_panic_consumed_sequence;
D3_IPC static volatile uint16_t g_control_music_panic_internal_cutoff;
D3_IPC static volatile uint16_t g_control_music_panic_external_cutoff;

static uint16_t control_music_queue_advance(uint16_t index, uint16_t capacity)
{
    ++index;
    return (index < capacity) ? index : 0U;
}

static uint16_t control_music_queue_free(uint16_t head, uint16_t tail,
                                         uint16_t capacity)
{
    const uint16_t used = (head >= tail)
        ? (uint16_t)(head - tail)
        : (uint16_t)(capacity - tail + head);
    return (uint16_t)((capacity - 1U) - used);
}

static uint8_t control_music_action_precedes(
    const control_music_action_t *left,
    const control_music_action_t *right)
{
    if (left->due_sample != right->due_sample)
        return (left->due_sample < right->due_sample) ? 1U : 0U;
    if (left->kind != right->kind)
        return (left->kind < right->kind) ? 1U : 0U;
    return 1U;
}

void control_music_queue_init(void)
{
    g_control_music_internal.head = 0U;
    g_control_music_internal.tail = 0U;
    g_control_music_external.head = 0U;
    g_control_music_external.tail = 0U;
    g_control_music_panic_sequence = 0U;
    g_control_music_panic_consumed_sequence = 0U;
    g_control_music_panic_internal_cutoff = 0U;
    g_control_music_panic_external_cutoff = 0U;
    __DMB();
}

void control_music_queue_request_panic(void)
{
    ++g_control_music_panic_sequence;
    if ((g_control_music_panic_sequence & 1U) == 0U)
        ++g_control_music_panic_sequence;
    __DMB();
    g_control_music_panic_internal_cutoff = g_control_music_internal.head;
    g_control_music_panic_external_cutoff = g_control_music_external.head;
    __DMB();
    ++g_control_music_panic_sequence;
    if (g_control_music_panic_sequence == 0U)
        g_control_music_panic_sequence = 2U;
    __DMB();
}

uint8_t control_music_queue_audio_consume_panic(void)
{
    const uint32_t sequence = g_control_music_panic_sequence;
    __DMB();
    if (((sequence & 1U) != 0U)
            || (sequence == g_control_music_panic_consumed_sequence))
        return 0U;
    const uint16_t internal_cutoff = g_control_music_panic_internal_cutoff;
    const uint16_t external_cutoff = g_control_music_panic_external_cutoff;
    __DMB();
    if (g_control_music_panic_sequence != sequence)
        return 0U;
    g_control_music_internal.tail = internal_cutoff;
    g_control_music_external.tail = external_cutoff;
    g_control_music_panic_consumed_sequence = sequence;
    __DMB();
    return 1U;
}

uint16_t control_music_queue_control_free(uint8_t external)
{
    const uint16_t capacity = (external != 0U)
        ? CONTROL_MUSIC_EXTERNAL_QUEUE_CAPACITY : CONTROL_MUSIC_QUEUE_CAPACITY;
    const uint16_t head = (external != 0U)
        ? g_control_music_external.head : g_control_music_internal.head;
    const uint16_t tail = (external != 0U)
        ? g_control_music_external.tail : g_control_music_internal.tail;
    return control_music_queue_free(head, tail, capacity);
}

uint8_t control_music_queue_publish_batch(const control_music_action_t *actions,
                                          uint16_t count)
{
    if ((actions == NULL) || (count == 0U))
        return 0U;
    const uint8_t external = (uint8_t)(
        (actions[0].trigger_id & CONTROL_MUSIC_TRIGGER_EXTERNAL_FLAG) != 0U);
    const uint16_t capacity = external
        ? CONTROL_MUSIC_EXTERNAL_QUEUE_CAPACITY : CONTROL_MUSIC_QUEUE_CAPACITY;
    if (count >= capacity)
        return 0U;

    volatile uint16_t *const head_ptr = external
        ? &g_control_music_external.head : &g_control_music_internal.head;
    volatile uint16_t *const tail_ptr = external
        ? &g_control_music_external.tail : &g_control_music_internal.tail;
    control_music_action_t *const queue = external
        ? g_control_music_external.actions : g_control_music_internal.actions;
    uint16_t head = *head_ptr;
    const uint16_t tail = *tail_ptr;
    if (control_music_queue_free(head, tail, capacity) < count)
        return 0U;

    uint64_t previous_due = 0U;
    if (head != tail)
    {
        const uint16_t previous = (head == 0U)
            ? (uint16_t)(capacity - 1U) : (uint16_t)(head - 1U);
        previous_due = queue[previous].due_sample;
    }
    for (uint16_t i = 0U; i < count; ++i)
    {
        const control_music_action_t *const action = &actions[i];
        const uint8_t action_external = (uint8_t)(
            (action->trigger_id & CONTROL_MUSIC_TRIGGER_EXTERNAL_FLAG) != 0U);
        if ((action->entity_id >= BRICK_ENTITY_CAPACITY)
                || (action->kind > (uint8_t)CONTROL_MUSIC_ACTION_RETRIGGER)
                || (action->output_id == 0U) || (action->note >= 128U)
                || (action->velocity >= 128U)
                || (action_external != external)
                || ((i != 0U) && (action->due_sample < actions[i - 1U].due_sample))
                || ((head != tail) && (action->due_sample < previous_due)))
            return 0U;
    }
    for (uint16_t i = 0U; i < count; ++i)
    {
        queue[head] = actions[i];
        head = control_music_queue_advance(head, capacity);
    }
    __DMB();
    *head_ptr = head;
    return 1U;
}

uint8_t control_music_queue_publish_ordered_window(
    const control_music_action_t *actions,
    const uint16_t *next_indices,
    const uint16_t *bucket_heads,
    uint16_t bucket_count,
    uint16_t action_count,
    uint8_t external)
{
    if ((actions == NULL) || (next_indices == NULL) || (bucket_heads == NULL)
            || (bucket_count == 0U) || (action_count == 0U))
        return 0U;

    const uint16_t capacity = (external != 0U)
        ? CONTROL_MUSIC_EXTERNAL_QUEUE_CAPACITY : CONTROL_MUSIC_QUEUE_CAPACITY;
    volatile uint16_t *const head_ptr = (external != 0U)
        ? &g_control_music_external.head : &g_control_music_internal.head;
    volatile uint16_t *const tail_ptr = (external != 0U)
        ? &g_control_music_external.tail : &g_control_music_internal.tail;
    control_music_action_t *const queue = (external != 0U)
        ? g_control_music_external.actions : g_control_music_internal.actions;
    uint16_t head = *head_ptr;
    const uint16_t tail = *tail_ptr;
    if ((action_count >= capacity)
            || (control_music_queue_free(head, tail, capacity) < action_count))
        return 0U;

    uint16_t visited = 0U;
    uint64_t previous_due = 0U;
    if (head != tail)
    {
        const uint16_t previous = (head == 0U)
            ? (uint16_t)(capacity - 1U) : (uint16_t)(head - 1U);
        previous_due = queue[previous].due_sample;
    }
    for (uint16_t bucket = 0U; bucket < bucket_count; ++bucket)
    {
        uint16_t index = bucket_heads[bucket];
        while (index != UINT16_MAX)
        {
            if (index >= action_count)
                return 0U;
            const control_music_action_t *const action = &actions[index];
            const uint8_t action_external = (uint8_t)(
                (action->trigger_id & CONTROL_MUSIC_TRIGGER_EXTERNAL_FLAG) != 0U);
            if ((action->entity_id >= BRICK_ENTITY_CAPACITY)
                    || (action->kind > (uint8_t)CONTROL_MUSIC_ACTION_RETRIGGER)
                    || (action->output_id == 0U) || (action->note >= 128U)
                    || (action->velocity >= 128U)
                    || (action_external != external)
                    || ((head != tail) && (action->due_sample < previous_due)))
                return 0U;
            previous_due = action->due_sample;
            ++visited;
            if (visited > action_count)
                return 0U;
            index = next_indices[index];
        }
    }
    if (visited != action_count)
        return 0U;

    for (uint16_t bucket = 0U; bucket < bucket_count; ++bucket)
    {
        uint16_t index = bucket_heads[bucket];
        while (index != UINT16_MAX)
        {
            queue[head] = actions[index];
            head = control_music_queue_advance(head, capacity);
            index = next_indices[index];
        }
    }
    __DMB();
    *head_ptr = head;
    return 1U;
}

uint8_t control_music_queue_publish(const control_music_action_t *action)
{
    return control_music_queue_publish_batch(action, 1U);
}

uint8_t control_music_queue_audio_peek(control_music_action_t *out_action)
{
    if (out_action == NULL)
        return 0U;
    const uint16_t internal_tail = g_control_music_internal.tail;
    const uint16_t external_tail = g_control_music_external.tail;
    const uint8_t has_internal =
        (internal_tail != g_control_music_internal.head) ? 1U : 0U;
    const uint8_t has_external =
        (external_tail != g_control_music_external.head) ? 1U : 0U;
    if ((has_internal == 0U) && (has_external == 0U))
        return 0U;
    __DMB();
    if ((has_external == 0U)
            || ((has_internal != 0U)
                && (control_music_action_precedes(
                    &g_control_music_internal.actions[internal_tail],
                    &g_control_music_external.actions[external_tail]) != 0U)))
        *out_action = g_control_music_internal.actions[internal_tail];
    else
        *out_action = g_control_music_external.actions[external_tail];
    return 1U;
}

uint8_t control_music_queue_audio_pop(const control_music_action_t *consumed)
{
    if (consumed == NULL)
        return 0U;
    if ((consumed->trigger_id & CONTROL_MUSIC_TRIGGER_EXTERNAL_FLAG) != 0U)
    {
        if (g_control_music_external.tail == g_control_music_external.head)
            return 0U;
        g_control_music_external.tail = control_music_queue_advance(
            g_control_music_external.tail, CONTROL_MUSIC_EXTERNAL_QUEUE_CAPACITY);
    }
    else
    {
        if (g_control_music_internal.tail == g_control_music_internal.head)
            return 0U;
        g_control_music_internal.tail = control_music_queue_advance(
            g_control_music_internal.tail, CONTROL_MUSIC_QUEUE_CAPACITY);
    }
    __DMB();
    return 1U;
}

uint16_t control_music_queue_audio_pending_count(void)
{
    const uint16_t internal = (g_control_music_internal.head
            >= g_control_music_internal.tail)
        ? (uint16_t)(g_control_music_internal.head
                     - g_control_music_internal.tail)
        : (uint16_t)(CONTROL_MUSIC_QUEUE_CAPACITY
                     - g_control_music_internal.tail
                     + g_control_music_internal.head);
    const uint16_t external = (g_control_music_external.head
            >= g_control_music_external.tail)
        ? (uint16_t)(g_control_music_external.head
                     - g_control_music_external.tail)
        : (uint16_t)(CONTROL_MUSIC_EXTERNAL_QUEUE_CAPACITY
                     - g_control_music_external.tail
                     + g_control_music_external.head);
    return (uint16_t)(internal + external);
}

uint16_t control_music_queue_audio_frames_until_due(uint64_t sample_now,
                                                    uint16_t max_frames)
{
    control_music_action_t action;
    if ((max_frames == 0U) || (control_music_queue_audio_peek(&action) == 0U))
        return max_frames;
    if (action.due_sample <= sample_now)
        return 0U;
    const uint64_t distance = action.due_sample - sample_now;
    return (distance < max_frames) ? (uint16_t)distance : max_frames;
}
