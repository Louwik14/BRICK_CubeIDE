#include "control_events.h"
#include "stm32h7xx_hal.h"

#define CONTROL_EVT_Q_LEN 64U

static control_event_t g_evt_q[CONTROL_EVT_Q_LEN];
static volatile uint32_t g_evt_write;
static volatile uint32_t g_evt_read;

void control_event_init(void)
{
    g_evt_write = 0U;
    g_evt_read = 0U;
}

bool control_event_push(const control_event_t *evt)
{
    if(evt == 0)
        return false;

    uint32_t next = (g_evt_write + 1U) & (CONTROL_EVT_Q_LEN - 1U);
    if(next == g_evt_read)
        return false;

    g_evt_q[g_evt_write] = *evt;
    __DMB();
    g_evt_write = next;
    return true;
}

bool control_event_pop(control_event_t *evt)
{
    if(evt == 0)
        return false;

    if(g_evt_read == g_evt_write)
        return false;

    *evt = g_evt_q[g_evt_read];
    g_evt_read = (g_evt_read + 1U) & (CONTROL_EVT_Q_LEN - 1U);
    return true;
}
