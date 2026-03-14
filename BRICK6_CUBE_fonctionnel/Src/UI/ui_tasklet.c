#include "ui_tasklet.h"

#include <stdint.h>

#include "drv_display.h"
#include "ui_core.h"

void ui_tasklet_poll(void)
{
    static uint8_t init = 0U;

    if (init == 0U)
    {
        init = 1U;
        drv_display_init();
        ui_core_init();
    }

    ui_core_tick();
}
