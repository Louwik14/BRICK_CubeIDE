#include "pages/ui_page_calibration.h"

#include <stdio.h>

#include "drv_display.h"
#include "App/Hall/hall_calibration.h"

static void ui_page_calibration_enter(void)
{
    hall_calibration_start();
}

static void ui_page_calibration_leave(void)
{
}

static void ui_page_calibration_handle_event(const ui_event_t *ev)
{
    (void)ev;
}

static void ui_page_calibration_tick(void)
{
    hall_calibration_process();
}

static void ui_page_calibration_render(void)
{
    char txt[8];

    drv_display_draw_text(0,0,"CALIBRATION");

    for(uint8_t i=0;i<16;i++)
    {
        uint8_t x = (i % 4) * 32;
        uint8_t y = 16 + (i / 4) * 12;

        uint8_t hits = hall_calibration_get_count(i);

        if(hits >= 3)
            snprintf(txt,sizeof(txt),"OK");
        else
            snprintf(txt,sizeof(txt),"%u/3",hits);

        drv_display_draw_text(x,y,txt);
    }

    if(hall_calibration_is_done())
        drv_display_draw_text(0,60,"CAL OK");
}

const ui_page_t g_ui_page_calibration = {
    .enter = ui_page_calibration_enter,
    .leave = ui_page_calibration_leave,
    .handle_event = ui_page_calibration_handle_event,
    .tick = ui_page_calibration_tick,
    .render = ui_page_calibration_render,
};
