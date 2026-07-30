#include "Core/diagnostic_input.h"

#include <stdbool.h>
#include <string.h>

#include "Keyboard/keyboard_runtime.h"
#include "buttons.h"
#include "encoders.h"

#define DIAGNOSTIC_KEY_COUNT 24U

static uint8_t g_diagnostic_keys[DIAGNOSTIC_KEY_COUNT];

void diagnostic_input_init(void)
{
    memset(g_diagnostic_keys, 0, sizeof(g_diagnostic_keys));
}

uint8_t diagnostic_input_button(uint8_t button, uint8_t pressed)
{
    if (button >= (uint8_t)BTN_COUNT)
    {
        return 0U;
    }

    return button_test_inject((button_id_t)button, pressed);
}

uint8_t diagnostic_input_encoder(uint8_t encoder, int16_t delta)
{
    return encoder_test_inject_delta(encoder, delta);
}

uint8_t diagnostic_input_key(uint8_t key, uint8_t pressed, uint8_t velocity)
{
    if (key >= DIAGNOSTIC_KEY_COUNT)
    {
        return 0U;
    }

    const uint8_t down = (pressed != 0U) ? 1U : 0U;
    g_diagnostic_keys[key] = down;
    keyboard_runtime_process_hall(key, down != 0U, velocity);
    return 1U;
}

void diagnostic_input_release_all(void)
{
    buttons_test_release_all();
    for (uint8_t key = 0U; key < DIAGNOSTIC_KEY_COUNT; key++)
    {
        if (g_diagnostic_keys[key] != 0U)
        {
            g_diagnostic_keys[key] = 0U;
            keyboard_runtime_process_hall(key, false, 0U);
        }
    }
}
