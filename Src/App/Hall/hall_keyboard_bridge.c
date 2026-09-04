#include "App/Hall/hall_keyboard_bridge.h"

#include "App/Hall/hall_engine.h"
#include "IPC/live_event.h"
#include "Keyboard/keyboard_runtime.h"
#include "ui_core.h"
#include "ui_event.h"
#include "stm32h7xx_hal.h"

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
    while (live_event_pop(&event))
    {
        if (event.source != LIVE_EVENT_SOURCE_HALL)
        {
            continue;
        }

        const uint8_t key = event.key;
        const uint8_t pressed = event.pressed;
        if (key >= HALL_KEY_COUNT)
        {
            continue;
        }
        uint8_t velocity = event.velocity;
        hall_engine_acknowledge_edge(key, pressed);

        if ((velocity == 0U) || (pressed == 0U))
        {
            velocity = 100U;
        }

        ui_hall_arbitration_snapshot_t snapshot;
        const uint8_t snapshot_valid =
            ui_core_hall_arbitration_snapshot_read(&snapshot);
        if (snapshot_valid == 0U)
        {
            snapshot.consume_press_mask = UINT16_MAX;
            snapshot.consume_release_mask = UINT16_MAX;
        }

        const uint16_t consume_mask = (pressed != 0U)
            ? snapshot.consume_press_mask : snapshot.consume_release_mask;
        const uint8_t ui_consumes_now =
            (key < HALL_UI_LANE_COUNT)
            && (((consume_mask & (uint16_t)(1U << key)) != 0U)
                || (event.track_select_armed != 0U)
                || ((event.shift_down != 0U)
                    && (event.hall_mode == (uint8_t)UI_HALL_MODE_KEYBOARD)));
        const uint8_t ui_owns_release =
            ((pressed == 0U) && (g_hall_ui_active[key] != 0U)) ? 1U : 0U;
        if (((pressed != 0U) && (ui_consumes_now != 0U))
                || (ui_owns_release != 0U))
        {
            (void)ui_event_push_hall_context(
                key, pressed, event.tim5_tick, event.capture_ms, event.ingress_serial,
                event.shift_down, event.track_select_armed, event.hall_mode,
                event.context_track);
            g_hall_ui_active[key] = (pressed != 0U) ? 1U : 0U;
            continue;
        }

        keyboard_runtime_process_hall_timed(key, pressed != 0U, velocity,
                                            event.tim5_tick,
                                            event.ingress_serial,
                                            event.hall_mode,
                                            event.shift_down,
                                            event.context_track);
        g_hall_musical_active[key] = (pressed != 0U) ? 1U : 0U;
        if (pressed == 0U)
        {
            g_hall_ui_active[key] = 0U;
        }
    }

    keyboard_runtime_tick();
}
