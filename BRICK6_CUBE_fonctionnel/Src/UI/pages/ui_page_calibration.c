#include "pages/ui_page_calibration.h"

#include "ui_renderer_oled.h"
#include "App/Hall/hall_on_off.h"
#include "stm32h7xx_hal.h"

#define GRID_COLS 8
#define GRID_ROWS 2
#define CELL_W    16
#define CELL_H    20
#define GRID_X    0
#define GRID_Y    10

#define FRAME_INTERVAL_MS 16U

static uint8_t g_level[16];
static uint32_t g_last_draw = 0;

static void ui_page_calibration_enter(void)
{
    for(uint8_t i = 0; i < 16; i++)
    {
        g_level[i] = 0;
    }

    g_last_draw = 0;
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
    for(uint8_t i = 0; i < 16; i++)
    {
        if(hall_on_off_event(i))
        {
            if(g_level[i] < 3)
            {
                g_level[i]++;
            }
        }
    }
}

static void draw_cell(uint8_t x, uint8_t y, uint8_t level)
{
    u8g2_t *u8g2 = &g_u8g2;
    u8g2_DrawFrame(u8g2, x, y, CELL_W - 1, CELL_H - 1);

    if(level == 0)
        return;

    uint8_t h = (CELL_H - 2) / 3;

    if(level >= 1)
        u8g2_DrawFrame(u8g2, x + 1, y + 1, CELL_W - 3, h);

    if(level >= 2)
        u8g2_DrawFrame(u8g2, x + 1, y + 1 + h, CELL_W - 3, h);

    if(level >= 3)
        u8g2_DrawFrame(u8g2, x + 1, y + 1 + 2*h, CELL_W - 3, h);
}

static void ui_page_calibration_render(void)
{
    uint32_t now = HAL_GetTick();

    if((now - g_last_draw) < FRAME_INTERVAL_MS)
        return;

    g_last_draw = now;


    for(uint8_t r = 0; r < GRID_ROWS; r++)
    {
        for(uint8_t c = 0; c < GRID_COLS; c++)
        {
            uint8_t i = r * GRID_COLS + c;

            uint8_t x = GRID_X + c * CELL_W;
            uint8_t y = GRID_Y + r * CELL_H;

            draw_cell(x, y, g_level[i]);
        }
    }
}

const ui_page_t g_ui_page_calibration =
{
    .enter = ui_page_calibration_enter,
    .leave = ui_page_calibration_leave,
    .handle_event = ui_page_calibration_handle_event,
    .tick = ui_page_calibration_tick,
    .render = ui_page_calibration_render,
};
