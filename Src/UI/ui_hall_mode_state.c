#include "ui_hall_mode_state.h"

#include "ui_core_mute.h"
#include "ui_core_pattern.h"
#include "ui_core_runtime_bridge.h"

static ui_hall_mode_t g_ui_hall_mode = UI_HALL_MODE_SEQ;

ui_hall_mode_t ui_get_hall_mode(void)
{
    return g_ui_hall_mode;
}

void ui_set_hall_mode(ui_hall_mode_t mode)
{
    /*
     * Hall mode contract:
     * - single transition authority for raw hall mode
     * - owns cross-mode side effects/hooks
     * - consumers read through ui_get_hall_mode()
     */
    if ((uint8_t)mode >= (uint8_t)UI_HALL_MODE_COUNT)
    {
        return;
    }

    if (g_ui_hall_mode == mode)
    {
        return;
    }

    if ((g_ui_hall_mode == UI_HALL_MODE_MUTE) && (mode != UI_HALL_MODE_MUTE))
    {
        ui_core_mute_reset();
    }

    if ((g_ui_hall_mode == UI_HALL_MODE_PATTERN) && (mode != UI_HALL_MODE_PATTERN))
    {
        ui_core_pattern_abort();
    }

    ui_core_runtime_bridge_notify_hall_mode_changed(g_ui_hall_mode, mode);
    g_ui_hall_mode = mode;
}
