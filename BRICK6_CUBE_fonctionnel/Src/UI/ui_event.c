#include "ui_event.h"

#include "buttons.h"

#define UI_EVENT_Q_LEN 32U

static ui_event_t g_ui_evt_q[UI_EVENT_Q_LEN];
static uint8_t g_ui_evt_w = 0U;
static uint8_t g_ui_evt_r = 0U;

static void ui_event_push(const ui_event_t *ev)
{
    const uint8_t next = (uint8_t)((g_ui_evt_w + 1U) & (UI_EVENT_Q_LEN - 1U));
    if (next == g_ui_evt_r)
    {
        return;
    }

    g_ui_evt_q[g_ui_evt_w] = *ev;
    g_ui_evt_w = next;
}

void ui_event_from_inputs(void)
{
    ui_event_t ev;
    for (uint8_t i = 0U; i < (uint8_t)BTN_COUNT; i++)
    {
        if (button_pressed((button_id_t)i) != 0U)
        {
            ev.type = UI_EVENT_BUTTON_PRESS;
            ev.id = i;
            ev.value = 1;
            ui_event_push(&ev);
        }

        if (button_released((button_id_t)i) != 0U)
        {
            ev.type = UI_EVENT_BUTTON_RELEASE;
            ev.id = i;
            ev.value = 0;
            ui_event_push(&ev);
        }
    }
}

bool ui_event_pop(ui_event_t *ev)
{
    if (ev == 0)
    {
        return false;
    }

    if (g_ui_evt_r == g_ui_evt_w)
    {
        return false;
    }

    *ev = g_ui_evt_q[g_ui_evt_r];
    g_ui_evt_r = (uint8_t)((g_ui_evt_r + 1U) & (UI_EVENT_Q_LEN - 1U));
    return true;
}
