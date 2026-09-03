#include "ui_step_led_ownership.h"

#include "ui_page_manager.h"

static uint8_t ui_step_led_ownership_binary_steps_are_seq(void)
{
    return 1U;
}

uint8_t ui_step_led_ownership_page_needs_step_leds(uint8_t page_id)
{
    if (page_id == UI_PAGE_TEMPLATE_SEQ)
    {
        return 1U;
    }

    if (page_id == UI_PAGE_TEMPLATE_KEYBOARD)
    {
        return ui_step_led_ownership_binary_steps_are_seq();
    }

    return 0U;
}

uint8_t ui_step_led_ownership_hall_mode_needs_step_leds(ui_hall_mode_t mode)
{
    if (mode == UI_HALL_MODE_SEQ)
    {
        return 1U;
    }

    if (mode == UI_HALL_MODE_KEYBOARD)
    {
        return ui_step_led_ownership_binary_steps_are_seq();
    }

    return 0U;
}
