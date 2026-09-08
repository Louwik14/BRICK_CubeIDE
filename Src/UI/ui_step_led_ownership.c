#include "ui_step_led_ownership.h"

#include "ui_page_manager.h"

uint8_t ui_step_led_ownership_page_needs_step_leds(uint8_t page_id)
{
    if (page_id == UI_PAGE_TEMPLATE_SEQ)
    {
        return 1U;
    }

    if (page_id == UI_PAGE_TEMPLATE_KEYBOARD)
    {
        return 1U;
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
        return 1U;
    }

    return 0U;
}
