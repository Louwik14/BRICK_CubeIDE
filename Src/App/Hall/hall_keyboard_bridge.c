#include "App/Hall/hall_keyboard_bridge.h"

#include "App/Hall/hall_engine.h"
#include "App/control_rt_wakeup.h"
#include "IPC/live_event.h"
#include "Keyboard/keyboard_runtime.h"
#include "Storage/project_load_quiesce.h"
#include "ui_core.h"
#include "ui_event.h"
#include "stm32h7xx_hal.h"

#define HALL_EVENT_PROCESS_BUDGET 16U

static uint8_t g_hall_musical_active[HALL_KEY_COUNT];
static uint8_t g_hall_ui_active[HALL_KEY_COUNT];

void hall_keyboard_bridge_init(void)
{
    for (uint8_t key = 0U; key < HALL_KEY_COUNT; ++key)
    {
        g_hall_musical_active[key] = 0U;
        g_hall_ui_active[key] = 0U;
    }
    keyboard_runtime_init();
}

void hall_keyboard_bridge_process(void)
{
    live_event_t event;
    uint16_t processed = 0U;
    while ((processed < HALL_EVENT_PROCESS_BUDGET)
           && live_event_pop(&event))
    {
        ++processed;
        /* Project replacement owns ingress policy on CONTROL, after the
         * physical edge has safely left the ISR. */
        if (project_load_ingress_is_open() == 0U)
        {
            continue;
        }

        const uint8_t key = event.key;
        const uint8_t pressed = event.pressed;
        if (key >= HALL_KEY_COUNT)
        {
            continue;
        }
        const uint8_t shift_down =
            ((event.modifier_bits & LIVE_EVENT_MODIFIER_SHIFT) != 0U) ? 1U : 0U;
        const uint8_t track_select_armed =
            ((event.modifier_bits & LIVE_EVENT_MODIFIER_TRACK) != 0U) ? 1U : 0U;

        ui_hall_arbitration_snapshot_t snapshot = {0};
        const uint8_t snapshot_valid =
            ui_core_hall_arbitration_snapshot_read(&snapshot);
        if (snapshot_valid == 0U)
        {
            snapshot.consume_press_mask = UINT16_MAX;
            snapshot.consume_release_mask = UINT16_MAX;
        }

        uint8_t velocity = event.velocity;
        hall_engine_acknowledge_edge(key, pressed);

        if ((velocity == 0U) || (pressed == 0U))
        {
            velocity = 100U;
        }

        const uint16_t consume_mask = (pressed != 0U)
            ? snapshot.consume_press_mask : snapshot.consume_release_mask;
        const uint8_t ui_consumes_now =
            (key < HALL_UI_LANE_COUNT)
            && (((consume_mask & (uint16_t)(1U << key)) != 0U)
                || (track_select_armed != 0U)
                || ((shift_down != 0U)
                    && (snapshot.hall_mode == (uint8_t)UI_HALL_MODE_KEYBOARD)));
        const uint8_t ui_owns_release =
            ((pressed == 0U) && (g_hall_ui_active[key] != 0U)) ? 1U : 0U;
        if (((pressed != 0U) && (ui_consumes_now != 0U))
                || (ui_owns_release != 0U))
        {
            (void)ui_event_push_hall_context(
                key, pressed, event.tim5_tick, event.capture_ms, event.ingress_serial,
                shift_down, track_select_armed,
                snapshot.hall_mode, snapshot.context_track);
            g_hall_ui_active[key] = (pressed != 0U) ? 1U : 0U;
            continue;
        }

        keyboard_runtime_process_hall_timed(key, pressed != 0U, velocity,
                                            event.tim5_tick,
                                            event.ingress_serial,
                                            snapshot.hall_mode,
                                            shift_down,
                                            snapshot.context_track);
        g_hall_musical_active[key] = (pressed != 0U) ? 1U : 0U;
        if (pressed == 0U)
        {
            g_hall_ui_active[key] = 0U;
        }
    }

    if ((processed >= HALL_EVENT_PROCESS_BUDGET)
        && (live_event_depth() != 0U))
        control_rt_wakeup(CONTROL_RT_WAKE_HALL);

    keyboard_runtime_tick();
}
