#include "Core/live_parameter_event.h"

#include "stm32h7xx_hal.h"

#define LIVE_PARAMETER_EVENT_QUEUE_MASK (LIVE_PARAMETER_EVENT_QUEUE_CAPACITY - 1U)

static live_parameter_event_t g_live_parameter_event_queue[LIVE_PARAMETER_EVENT_QUEUE_CAPACITY];
static volatile uint16_t g_live_parameter_event_head;
static volatile uint16_t g_live_parameter_event_tail;
static volatile uint32_t g_live_parameter_event_drop_count;

static uint32_t live_parameter_event_enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return primask;
}

static void live_parameter_event_exit_critical(uint32_t primask)
{
    __DMB();
    __set_PRIMASK(primask);
}

void live_parameter_event_init(void)
{
    const uint32_t primask = live_parameter_event_enter_critical();

    g_live_parameter_event_head = 0U;
    g_live_parameter_event_tail = 0U;
    g_live_parameter_event_drop_count = 0U;

    live_parameter_event_exit_critical(primask);
}

bool live_parameter_event_submit(const live_parameter_event_t *event)
{
    if (event == 0)
    {
        return false;
    }

    const uint32_t primask = live_parameter_event_enter_critical();
    const uint16_t head = g_live_parameter_event_head;
    if ((uint16_t)(head - g_live_parameter_event_tail) >= LIVE_PARAMETER_EVENT_QUEUE_CAPACITY)
    {
        g_live_parameter_event_drop_count++;
        live_parameter_event_exit_critical(primask);
        return false;
    }

    g_live_parameter_event_queue[head & LIVE_PARAMETER_EVENT_QUEUE_MASK] = *event;
    __DMB();
    g_live_parameter_event_head = (uint16_t)(head + 1U);

    live_parameter_event_exit_critical(primask);
    return true;
}

bool live_parameter_event_pop(live_parameter_event_t *out_event)
{
    if (out_event == 0)
    {
        return false;
    }

    const uint32_t primask = live_parameter_event_enter_critical();
    const uint16_t tail = g_live_parameter_event_tail;
    if (tail == g_live_parameter_event_head)
    {
        live_parameter_event_exit_critical(primask);
        return false;
    }

    *out_event = g_live_parameter_event_queue[tail & LIVE_PARAMETER_EVENT_QUEUE_MASK];
    __DMB();
    g_live_parameter_event_tail = (uint16_t)(tail + 1U);

    live_parameter_event_exit_critical(primask);
    return true;
}

uint16_t live_parameter_event_depth(void)
{
    const uint32_t primask = live_parameter_event_enter_critical();
    const uint16_t depth = (uint16_t)(g_live_parameter_event_head - g_live_parameter_event_tail);
    live_parameter_event_exit_critical(primask);
    return depth;
}

uint32_t live_parameter_event_drop_count(void)
{
    return g_live_parameter_event_drop_count;
}
