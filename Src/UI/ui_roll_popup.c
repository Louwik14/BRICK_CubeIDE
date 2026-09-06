#include "ui_roll_popup.h"

#include "drv_display.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_model.h"
#include "UI/ui_service_wakeup.h"

#define UI_ROLL_POPUP_DURATION_MS 700U
#define UI_ROLL_POPUP_X 36U
#define UI_ROLL_POPUP_Y 17U
#define UI_ROLL_POPUP_W 56U
#define UI_ROLL_POPUP_HEIGHT 31U

typedef struct
{
    uint8_t active;
    seq_track_id_t track;
    seq_step_id_t step;
    uint8_t roll;
    uint32_t until_ms;
} ui_roll_popup_state_t;

static ui_roll_popup_state_t g_ui_roll_popup;

static uint8_t ui_roll_popup_center_x(uint8_t x, uint8_t w, const char *txt)
{
    const uint8_t text_w = drv_display_text_width(txt);
    if (text_w >= w)
    {
        return x;
    }
    return (uint8_t)(x + ((w - text_w) / 2U));
}

void ui_roll_popup_show(seq_track_id_t track, seq_step_id_t step, uint8_t roll, uint32_t now_ms)
{
    g_ui_roll_popup.active = 1U;
    g_ui_roll_popup.track = track;
    g_ui_roll_popup.step = step;
    g_ui_roll_popup.roll = roll;
    g_ui_roll_popup.until_ms = now_ms + UI_ROLL_POPUP_DURATION_MS;
    ui_service_dirty_set();
}

void ui_roll_popup_render(uint32_t now_ms)
{
    if ((g_ui_roll_popup.active == 0U)
            || ((int32_t)(g_ui_roll_popup.until_ms - now_ms) <= 0)
            || (seq_edit_step_is_pressed(g_ui_roll_popup.track, g_ui_roll_popup.step) == 0U))
    {
        g_ui_roll_popup.active = 0U;
        ui_service_dirty_set();
        return;
    }

    const char *const value = seq_model_step_roll_label(g_ui_roll_popup.roll);
    const uint8_t value_emph = seq_model_step_roll_is_emphasized(g_ui_roll_popup.roll);

    drv_display_fill_rect(UI_ROLL_POPUP_X, UI_ROLL_POPUP_Y, UI_ROLL_POPUP_W, UI_ROLL_POPUP_HEIGHT);
    drv_display_set_font(&FONT_4X6);
    drv_display_draw_text_inverted(ui_roll_popup_center_x(UI_ROLL_POPUP_X, UI_ROLL_POPUP_W, "ROLL"),
                                   (uint8_t)(UI_ROLL_POPUP_Y + 3U),
                                   "ROLL");
    if (value_emph != 0U)
    {
        drv_display_clear_rect((int)UI_ROLL_POPUP_X + 15, (int)UI_ROLL_POPUP_Y + 12, 26, 9);
        drv_display_draw_text(ui_roll_popup_center_x((uint8_t)(UI_ROLL_POPUP_X + 15U), 26U, value),
                              (uint8_t)(UI_ROLL_POPUP_Y + 14U),
                              value);
        drv_display_draw_rect((int)UI_ROLL_POPUP_X + 13, (int)UI_ROLL_POPUP_Y + 11, 30, 11);
    }
    else
    {
        drv_display_draw_text_inverted(ui_roll_popup_center_x(UI_ROLL_POPUP_X, UI_ROLL_POPUP_W, value),
                                       (uint8_t)(UI_ROLL_POPUP_Y + 14U),
                                       value);
    }
    drv_display_draw_text_inverted((uint8_t)(UI_ROLL_POPUP_X + 5U), (uint8_t)(UI_ROLL_POPUP_Y + 24U), "32");
    drv_display_draw_text_inverted((uint8_t)(UI_ROLL_POPUP_X + 21U), (uint8_t)(UI_ROLL_POPUP_Y + 24U), "24");
    drv_display_draw_text_inverted((uint8_t)(UI_ROLL_POPUP_X + 37U), (uint8_t)(UI_ROLL_POPUP_Y + 24U), "64");
}

void ui_roll_popup_service(uint32_t now_ms)
{
    if ((g_ui_roll_popup.active != 0U)
        && ((int32_t)(g_ui_roll_popup.until_ms - now_ms) <= 0))
    {
        g_ui_roll_popup.active = 0U;
        ui_service_dirty_set();
    }
}

uint8_t ui_roll_popup_next_deadline(uint32_t now_ms, uint32_t *out_deadline_ms)
{
    if ((out_deadline_ms == 0)
        || (g_ui_roll_popup.active == 0U)
        || ((int32_t)(g_ui_roll_popup.until_ms - now_ms) <= 0))
        return 0U;
    *out_deadline_ms = g_ui_roll_popup.until_ms;
    return 1U;
}
