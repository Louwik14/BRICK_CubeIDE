#include "Audio/juno_midi_queue.h"

#include <string.h>

#if defined(__arm__) || defined(__thumb__) || defined(STM32H743xx)
#include "stm32h743xx.h"
static inline uint32_t juno_midi_enter_critical(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static inline void juno_midi_exit_critical(uint32_t primask)
{
    __set_PRIMASK(primask);
}
#else
static inline uint32_t juno_midi_enter_critical(void)
{
    return 0U;
}

static inline void juno_midi_exit_critical(uint32_t primask)
{
    (void)primask;
}
#endif

#define JUNO_MIDI_QUEUE_LEN 32U

typedef struct
{
    juno_midi_event_t buffer[JUNO_MIDI_QUEUE_LEN];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t count;
    volatile uint32_t drops;
} juno_midi_queue_state_t;

static juno_midi_queue_state_t g_juno_midi_queue;

void juno_midi_queue_init(void)
{
    memset(&g_juno_midi_queue, 0, sizeof(g_juno_midi_queue));
}

void juno_midi_queue_clear(void)
{
    uint32_t primask = juno_midi_enter_critical();
    g_juno_midi_queue.head = 0U;
    g_juno_midi_queue.tail = 0U;
    g_juno_midi_queue.count = 0U;
    juno_midi_exit_critical(primask);
}

uint8_t juno_midi_queue_push(const juno_midi_event_t *event)
{
    if(event == NULL)
        return 0U;

    uint32_t primask = juno_midi_enter_critical();

    if(g_juno_midi_queue.count >= JUNO_MIDI_QUEUE_LEN)
    {
        g_juno_midi_queue.drops++;
        juno_midi_exit_critical(primask);
        return 0U;
    }

    g_juno_midi_queue.buffer[g_juno_midi_queue.head] = *event;
    g_juno_midi_queue.head = (g_juno_midi_queue.head + 1U) % JUNO_MIDI_QUEUE_LEN;
    g_juno_midi_queue.count++;

    juno_midi_exit_critical(primask);
    return 1U;
}

uint8_t juno_midi_queue_pop(juno_midi_event_t *event)
{
    if(event == NULL)
        return 0U;

    uint32_t primask = juno_midi_enter_critical();

    if(g_juno_midi_queue.count == 0U)
    {
        juno_midi_exit_critical(primask);
        return 0U;
    }

    *event = g_juno_midi_queue.buffer[g_juno_midi_queue.tail];
    g_juno_midi_queue.tail = (g_juno_midi_queue.tail + 1U) % JUNO_MIDI_QUEUE_LEN;
    g_juno_midi_queue.count--;

    juno_midi_exit_critical(primask);
    return 1U;
}

uint32_t juno_midi_queue_drop_count(void)
{
    return g_juno_midi_queue.drops;
}
