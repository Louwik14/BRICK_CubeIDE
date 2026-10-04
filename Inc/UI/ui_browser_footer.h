#ifndef UI_BROWSER_FOOTER_H
#define UI_BROWSER_FOOTER_H

#include "UI/ui_browser_actions.h"
#include "buttons.h"
#include "drv_display.h"
#include "font.h"

static inline void ui_browser_draw_shift_footer(uint8_t y,
                                                 ui_browser_context_t context)
{
    const uint8_t shift = button_down(BTN_SHIFT);
    drv_display_set_font(&FONT_4X6);
    if (shift != 0U) {
        drv_display_fill_rect(0U, (uint8_t)(y - 1U), 8U, 7U);
        drv_display_draw_text_inverted(2U, y, "^");
    } else {
        drv_display_draw_rect(0U, (uint8_t)(y - 1U), 8U, 7U);
        drv_display_draw_text(2U, y, "^");
    }
    for (uint8_t page = 0U; page < 4U; ++page) {
        const uint8_t x = (uint8_t)(9U + page * 30U);
        const char *label = ui_browser_action_label(ui_browser_action_resolve_context(
            (button_id_t)((uint8_t)BTN_PAGE_1 + page), shift, context),
            context == UI_BROWSER_PROJECT);
        const uint8_t width = drv_display_text_width(label);
        drv_display_draw_text((uint8_t)(x + ((29U > width) ? (29U - width) / 2U : 0U)), y, label);
    }
}

#endif
