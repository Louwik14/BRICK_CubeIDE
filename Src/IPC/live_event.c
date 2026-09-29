#include "IPC/live_event.h"
#include "IPC/note_audit_trace.h"
#include "Platform/memory_layout.h"
#include "Storage/project_load_quiesce.h"

#include "stm32h7xx_hal.h"

#define LIVE_EVENT_QUEUE_MASK (LIVE_EVENT_QUEUE_CAPACITY - 1U)

static live_event_t g_live_event_queue[LIVE_EVENT_QUEUE_CAPACITY];
static volatile uint16_t g_live_event_head;
static volatile uint16_t g_live_event_tail;
static volatile uint32_t g_live_event_serial;
static volatile uint32_t g_live_event_drop_count;

CONTROL_STATE_SDRAM volatile note_audit_record_t
    g_note_audit_control[NOTE_AUDIT_CONTROL_CAPACITY]
    __attribute__((used, externally_visible));
volatile uint32_t g_note_audit_control_sequence
    __attribute__((used, externally_visible));

void note_audit_control(uint16_t event, uint8_t track, uint8_t note,
                        uint8_t detail, uint8_t held_count,
                        uint16_t held_mask, uint32_t id, uint32_t aux)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t sequence = ++g_note_audit_control_sequence;
    if (sequence == 0U) sequence = ++g_note_audit_control_sequence;
    volatile note_audit_record_t *const record =
        &g_note_audit_control[(sequence - 1U) % NOTE_AUDIT_CONTROL_CAPACITY];
    record->sequence = 0U;
    record->tick = HAL_GetTick();
    record->id = id;
    record->aux = aux;
    record->event = event;
    record->track = track;
    record->note = note;
    record->detail = detail;
    record->held_count = held_count;
    record->held_mask = held_mask;
    __DMB();
    record->sequence = sequence;
    __set_PRIMASK(primask);
}

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
                                 uint32_t tim5_tick)
{
    if (project_load_ingress_is_open() == 0U)
    {
        note_audit_control(NOTE_AUDIT_HALL_DROP, key, 0U,
                           pressed ? 1U : 0U, 0U, 0U, 0U, 1U);
        return false;
    }
    const uint32_t primask = live_event_enter_critical();
    if (project_load_ingress_is_open() == 0U)
    {
        note_audit_control(NOTE_AUDIT_HALL_DROP, key, 0U,
                           pressed ? 1U : 0U, 0U, 0U, 0U, 2U);
        live_event_exit_critical(primask);
        return false;
    }
    const uint16_t head = g_live_event_head;
    const uint16_t next = (uint16_t)((head + 1U) & LIVE_EVENT_QUEUE_MASK);

    if (next == g_live_event_tail)
    {
        g_live_event_drop_count++;
        note_audit_control(NOTE_AUDIT_HALL_DROP, key, 0U,
                           pressed ? 1U : 0U, 0U, 0U,
                           g_live_event_drop_count, 3U);
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
        .source = LIVE_EVENT_SOURCE_HALL
    };
    __DMB();
    g_live_event_head = next;
    note_audit_control(NOTE_AUDIT_HALL_QUEUED, key, 0U,
                       pressed ? 1U : 0U, 0U, 0U, serial,
                       ((uint32_t)velocity << 16) | (tim5_tick & 0xFFFFU));

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
