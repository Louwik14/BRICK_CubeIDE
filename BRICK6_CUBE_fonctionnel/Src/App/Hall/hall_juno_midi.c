#include "App/Hall/hall_juno_midi.h"

#include "App/Hall/hall_engine.h"
#include "Keyboard/keyboard_runtime.h"
#include "ui_core.h"

void hall_juno_midi_init(void)
{
    keyboard_runtime_init();
}

void hall_juno_midi_process(void)
{
    for (uint8_t key = 0U; key < HALL_KEY_COUNT; ++key)
    {
        const uint8_t note_on_pending = hall_engine_consume_note_on(key);
        const uint8_t note_off_pending = hall_engine_consume_note_off(key);

        if (ui_core_hall_note_is_suppressed(key) != 0U)
        {
            if (note_off_pending != 0U)
            {
                ui_core_clear_hall_note_suppression(key);
            }
            continue;
        }

        const ui_hall_mode_t hall_mode = ui_get_hall_mode();
        if ((hall_mode != UI_HALL_MODE_KEYBOARD) && (hall_mode != UI_HALL_MODE_ARP))
        {
            continue;
        }

        uint8_t velocity = hall_engine_get_velocity(key);
        if ((hall_engine_get_velocity_valid(key) == 0U) || (velocity == 0U))
        {
            velocity = 100U;
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
