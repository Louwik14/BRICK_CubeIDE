#include "Audio/control_audio_queue.h"

#include "Storage/memory_layout.h"
#include "stm32h7xx_hal.h"

typedef struct
{
    control_audio_event_t events[CONTROL_AUDIO_QUEUE_CAPACITY];
    volatile uint16_t head;
    volatile uint16_t tail;
} control_audio_queue_state_t;

CTRL_STATE static control_audio_queue_state_t g_control_audio_queue;

static uint16_t control_audio_queue_advance(uint16_t index)
{
    ++index;
    return (index < CONTROL_AUDIO_QUEUE_CAPACITY) ? index : 0U;
}

static uint16_t control_audio_queue_free(uint16_t head, uint16_t tail)
{
    const uint16_t used = (head >= tail)
        ? (uint16_t)(head - tail)
        : (uint16_t)(CONTROL_AUDIO_QUEUE_CAPACITY - tail + head);
    return (uint16_t)((CONTROL_AUDIO_QUEUE_CAPACITY - 1U) - used);
}

void control_audio_queue_init(void)
{
    g_control_audio_queue.head = 0U;
    g_control_audio_queue.tail = 0U;
    __DMB();
}

uint8_t control_audio_queue_publish_batch(const control_audio_event_t *events,
                                          uint16_t count)
{
    if ((events == NULL) || (count == 0U)
            || (count >= CONTROL_AUDIO_QUEUE_CAPACITY))
        return 0U;

    uint16_t head = g_control_audio_queue.head;
    const uint16_t tail = g_control_audio_queue.tail;
    if (control_audio_queue_free(head, tail) < count)
        return 0U;

    uint64_t previous_due = 0U;
    if (head != tail)
    {
        const uint16_t previous = (head == 0U)
            ? (uint16_t)(CONTROL_AUDIO_QUEUE_CAPACITY - 1U)
            : (uint16_t)(head - 1U);
        previous_due = g_control_audio_queue.events[previous].due_sample;
    }

    for (uint16_t i = 0U; i < count; ++i)
    {
        const control_audio_event_t *const event = &events[i];
        const uint8_t is_barrier = (uint8_t)(
            event->kind >= CONTROL_AUDIO_EVENT_CLOSE_ENTITY);
        if ((event->entity_id >= BRICK_ENTITY_CAPACITY)
                || (event->kind > CONTROL_AUDIO_EVENT_MULTI_STOP)
                || ((event->kind <= CONTROL_AUDIO_EVENT_NOTE_ON)
                    && ((event->note >= 128U)
                        || (event->velocity >= 128U)
                        || (event->occurrence_token == 0U)))
                || ((i != 0U) && (event->due_sample < events[i - 1U].due_sample))
                || ((head != tail) && (event->due_sample < previous_due)
                    && (is_barrier == 0U)))
            return 0U;
    }

    for (uint16_t i = 0U; i < count; ++i)
    {
        g_control_audio_queue.events[head] = events[i];
        if ((events[i].kind >= CONTROL_AUDIO_EVENT_CLOSE_ENTITY)
                && (head != tail)
                && (g_control_audio_queue.events[head].due_sample < previous_due))
            g_control_audio_queue.events[head].due_sample = previous_due;
        head = control_audio_queue_advance(head);
    }
    __DMB();
    g_control_audio_queue.head = head;
    return 1U;
}

uint8_t control_audio_queue_publish(const control_audio_event_t *event)
{
    return control_audio_queue_publish_batch(event, 1U);
}

uint8_t control_audio_queue_audio_peek(control_audio_event_t *out_event)
{
    if (out_event == NULL)
        return 0U;
    const uint16_t tail = g_control_audio_queue.tail;
    __DMB();
    if (tail == g_control_audio_queue.head)
        return 0U;
    *out_event = g_control_audio_queue.events[tail];
    return 1U;
}

uint8_t control_audio_queue_audio_pop(void)
{
    const uint16_t tail = g_control_audio_queue.tail;
    __DMB();
    if (tail == g_control_audio_queue.head)
        return 0U;
    g_control_audio_queue.tail = control_audio_queue_advance(tail);
    __DMB();
    return 1U;
}

uint16_t control_audio_queue_audio_frames_until_due(uint64_t sample_now,
                                                    uint16_t max_frames)
{
    control_audio_event_t event;
    if ((max_frames == 0U) || (control_audio_queue_audio_peek(&event) == 0U))
        return max_frames;
    if (event.due_sample <= sample_now)
        return 0U;
    const uint64_t distance = event.due_sample - sample_now;
    return (distance < max_frames) ? (uint16_t)distance : max_frames;
}
