#include "App/Hall/hall_keyboard_bridge.h"

#include "App/Hall/hall_engine.h"
#include "Keyboard/keyboard_runtime.h"
#include "ui_core.h"

void hall_keyboard_bridge_init(void)
{
    keyboard_runtime_init();
}

void hall_keyboard_bridge_process(void)
{
    for (uint8_t key = 0U; key < HALL_KEY_COUNT; ++key)
    {
        const uint8_t note_on_pending = hall_engine_consume_note_on(key);
        const uint8_t note_off_pending = hall_engine_consume_note_off(key);
        uint8_t velocity = hall_engine_get_velocity(key);
        if ((hall_engine_get_velocity_valid(key) == 0U) || (velocity == 0U))
        {
            velocity = 100U;
        }

        if (ui_core_hall_note_is_suppressed(key) != 0U)
        {
            if (note_off_pending != 0U)
            {
                ui_core_clear_hall_note_suppression(key);
            }
            continue;
        }

        if (ui_hall_allows_injection(ui_get_active_track(), ui_get_hall_mode()) == 0U)
        {
            continue;
        }

        if (note_on_pending != 0U)
        {
            keyboard_runtime_process_hall(key, true, velocity);
        }

        if (note_off_pending != 0U)
        {
            keyboard_runtime_process_hall(key, false, velocity);
        }
    }

    keyboard_runtime_tick();
}
