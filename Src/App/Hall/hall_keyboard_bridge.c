#include "App/Hall/hall_keyboard_bridge.h"

#include "App/Hall/hall_engine.h"
#include "Board/board_product.h"
#include "Keyboard/keyboard_runtime.h"
#include "pages/ui_page_audio_rec.h"
#include "pages/ui_page_kit_assign.h"
#include "pages/ui_page_patch_assign.h"
#include "ui_core.h"
#include "ui_core_mute.h"

static uint8_t g_separate_hall_key_injected[HALL_KEY_COUNT];

void hall_keyboard_bridge_init(void)
{
    for (uint8_t key = 0U; key < HALL_KEY_COUNT; ++key)
    {
        g_separate_hall_key_injected[key] = 0U;
    }
    keyboard_runtime_init();
}

void hall_keyboard_bridge_process(void)
{
    const board_product_capabilities_t *caps = board_product_capabilities();
    const uint8_t has_separate_hall_keyboard =
        ((caps != 0) && (caps->has_separate_hall_keyboard != 0U)) ? 1U : 0U;

    for (uint8_t key = 0U; key < HALL_KEY_COUNT; ++key)
    {
        const uint8_t note_on_pending = hall_engine_consume_note_on(key);
        const uint8_t note_off_pending = hall_engine_consume_note_off(key);
        uint8_t velocity = hall_engine_get_velocity(key);
        if ((hall_engine_get_velocity_valid(key) == 0U) || (velocity == 0U))
        {
            velocity = 100U;
        }

        if ((has_separate_hall_keyboard == 0U) && (ui_core_hall_note_is_suppressed(key) != 0U))
        {
            if (note_off_pending != 0U)
            {
                ui_core_clear_hall_note_suppression(key);
            }
            continue;
        }

        const ui_hall_mode_t raw_mode = ui_get_hall_mode();
        const ui_hall_mode_t input_mode = (raw_mode == UI_HALL_MODE_MUTE)
            ? ui_core_mute_get_passthrough_hall_mode()
            : raw_mode;
        uint8_t injection_allowed = ui_hall_allows_injection(ui_get_active_track(), input_mode);
        if ((has_separate_hall_keyboard != 0U)
                && ((input_mode == UI_HALL_MODE_SEQ)
                    || (ui_page_kit_assign_is_open() != 0U)
                    || (ui_page_patch_assign_is_open() != 0U)
                    || (ui_page_audio_rec_is_open() != 0U)))
        {
            injection_allowed = 1U;
        }

        if (injection_allowed == 0U)
        {
            if ((has_separate_hall_keyboard != 0U)
                    && (note_off_pending != 0U)
                    && (g_separate_hall_key_injected[key] != 0U))
            {
                keyboard_runtime_process_hall(key, false, velocity);
                g_separate_hall_key_injected[key] = 0U;
            }
            continue;
        }

        if (note_on_pending != 0U)
        {
            keyboard_runtime_process_hall(key, true, velocity);
            if (has_separate_hall_keyboard != 0U)
            {
                g_separate_hall_key_injected[key] = 1U;
            }
        }

        if (note_off_pending != 0U)
        {
            keyboard_runtime_process_hall(key, false, velocity);
            if (has_separate_hall_keyboard != 0U)
            {
                g_separate_hall_key_injected[key] = 0U;
            }
        }
    }

    keyboard_runtime_tick();
}
