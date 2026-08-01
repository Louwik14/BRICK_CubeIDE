#include "ui_step_led_ownership.h"

#include "Board/board_product.h"
#include "ui_page_manager.h"

static uint8_t ui_step_led_ownership_lowcost_binary_steps_are_seq(void)
{
    const board_product_capabilities_t *const caps = board_product_capabilities();
    return ((caps != 0)
            && (caps->has_step_binary_lanes != 0U)
            && (caps->has_separate_hall_keyboard != 0U)) ? 1U : 0U;
}

uint8_t ui_step_led_ownership_page_needs_step_leds(uint8_t page_id)
{
    if (page_id == UI_PAGE_TEMPLATE_SEQ)
    {
        return 1U;
    }

    if (page_id == UI_PAGE_TEMPLATE_KEYBOARD)
    {
        return ui_step_led_ownership_lowcost_binary_steps_are_seq();
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
        return ui_step_led_ownership_lowcost_binary_steps_are_seq();
    }

    return 0U;
}
