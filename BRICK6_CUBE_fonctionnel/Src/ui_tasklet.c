#include "ui_tasklet.h"
#include <stdint.h>
#include "drv_display.h"

void ui_tasklet_poll(void)
{
    static uint8_t init = 0U;

    if(!init)
    {
        init = 1U;
        drv_display_init();
    }
}
