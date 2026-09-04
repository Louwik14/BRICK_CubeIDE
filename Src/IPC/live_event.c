#include "IPC/live_event.h"
#include "Storage/project_load_quiesce.h"

#include "stm32h7xx_hal.h"

#define LIVE_EVENT_QUEUE_MASK (LIVE_EVENT_QUEUE_CAPACITY - 1U)

static live_event_t g_live_event_queue[LIVE_EVENT_QUEUE_CAPACITY];
static volatile uint16_t g_live_event_head;
static volatile uint16_t g_live_event_tail;
static volatile uint32_t g_live_event_serial;
static volatile uint32_t g_live_event_drop_count;

static uint32_t live_event_enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return primask;
}

static void live_event_exit_critical(uint32_t primask)
{
    __DMB();
    __set_PRIMASK(primask);
}

void live_event_init(void)
{
    const uint32_t primask = live_event_enter_critical();

    g_live_event_head = 0U;
    g_live_event_tail = 0U;
    g_live_event_serial = 0U;
    g_live_event_drop_count = 0U;

    live_event_exit_critical(primask);
}

void live_event_discard_pending(void)
{
    const uint32_t primask = live_event_enter_critical();
    g_live_event_tail = g_live_event_head;
    live_event_exit_critical(primask);
}

bool live_event_submit_from_hall(uint8_t key,
                                 bool pressed,
                                 uint8_t velocity,
                                 uint32_t tim5_tick,
                                 uint8_t shift_down,
                                 uint8_t track_select_armed,
                                 uint8_t hall_mode,
                                 uint8_t context_track,
                                 uint32_t capture_ms)
{
    if (project_load_ingress_is_open() == 0U)
        return false;
    const uint32_t primask = live_event_enter_critical();
    if (project_load_ingress_is_open() == 0U)
    {
        live_event_exit_critical(primask);
        return false;
    }
    const uint16_t head = g_live_event_head;
    const uint16_t next = (uint16_t)((head + 1U) & LIVE_EVENT_QUEUE_MASK);

    if (next == g_live_event_tail)
    {
        g_live_event_drop_count++;
        live_event_exit_critical(primask);
        return false;
    }

    uint32_t serial = g_live_event_serial + 1U;
    if (serial == 0U)
    {
        serial = 1U;
    }
    g_live_event_serial = serial;

    g_live_event_queue[head] = (live_event_t){
        .tim5_tick = tim5_tick,
        .ingress_serial = serial,
        .occurrence_id = 0U,
        .key = key,
        .pressed = pressed ? 1U : 0U,
        .velocity = velocity,
        .source = LIVE_EVENT_SOURCE_HALL,
        .shift_down = (shift_down != 0U) ? 1U : 0U,
        .track_select_armed = (track_select_armed != 0U) ? 1U : 0U,
        .hall_mode = hall_mode,
        .context_track = context_track,
        .reserved = 0U,
        .capture_ms = capture_ms
    };
    __DMB();
    g_live_event_head = next;

    live_event_exit_critical(primask);
    return true;
}

bool live_event_pop(live_event_t *out_event)
{
    if (out_event == NULL)
    {
        return false;
    }

    const uint32_t primask = live_event_enter_critical();
    const uint16_t tail = g_live_event_tail;
    if (tail == g_live_event_head)
    {
        live_event_exit_critical(primask);
        return false;
    }

    *out_event = g_live_event_queue[tail];
    __DMB();
    g_live_event_tail = (uint16_t)((tail + 1U) & LIVE_EVENT_QUEUE_MASK);
    live_event_exit_critical(primask);
    return true;
}

uint16_t live_event_depth(void)
{
    const uint32_t primask = live_event_enter_critical();
    const uint16_t depth = (uint16_t)((g_live_event_head - g_live_event_tail)
                                      & LIVE_EVENT_QUEUE_MASK);
    live_event_exit_critical(primask);
    return depth;
}

uint32_t live_event_drop_count(void)
{
    return g_live_event_drop_count;
}
