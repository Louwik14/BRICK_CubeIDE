#include "drv_display.h"

#include "spi.h"
#include "gpio.h"
#include "sdram.h"
#include "../../U8g2/u8g2.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern SPI_HandleTypeDef hspi5;

/* ====================================================================== */
/*                          FRAMEBUFFER                                   */
/* ====================================================================== */

static uint8_t buffer[OLED_WIDTH * OLED_HEIGHT / 8] SDRAM_BSS;

static u8g2_t g_u8g2;
static const uint8_t *g_active_font = u8g2_font_5x7_tr;

/* ====================================================================== */
/*                             SPI / GPIO                                 */
/* ====================================================================== */

static inline void cs_low(void)
{
    HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_RESET);
}

static inline void cs_high(void)
{
    HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET);
}

static inline void dc_cmd(void)
{
    HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_RESET);
}

static inline void dc_data(void)
{
    HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_SET);
}

static void send_cmd(uint8_t cmd)
{
    dc_cmd();
    cs_low();
    HAL_SPI_Transmit(&hspi5, &cmd, 1, 10);
    cs_high();
}

static void send_data(const uint8_t *data, size_t len)
{
    dc_data();
    cs_low();
    HAL_SPI_Transmit(&hspi5, (uint8_t*)data, len, 100);
    cs_high();
}

/* ====================================================================== */
/*                         FRAMEBUFFER ACCESS                             */
/* ====================================================================== */

uint8_t* drv_display_get_buffer(void)
{
    return buffer;
}

/* ====================================================================== */
/*                              CLEAR                                     */
/* ====================================================================== */

void drv_display_clear(void)
{
    memset(buffer, 0x00, sizeof(buffer));
}

/* ====================================================================== */
/*                              UPDATE                                    */
/* ====================================================================== */

void drv_display_update(void)
{
    /*
     * U8g2 buffer uses page-oriented vertical bytes.
     * Keep the controller in page addressing mode so 0xB0+page and
     * page*OLED_WIDTH indexing stay aligned.
     */
    send_cmd(0x20); send_cmd(0x02);

    for (uint8_t page = 0; page < 8; page++)
    {
        send_cmd(0xB0 + page);
        send_cmd(0x00);
        send_cmd(0x10);

        send_data(&buffer[page * OLED_WIDTH], OLED_WIDTH);
    }
}

/* ====================================================================== */
/*                           INITIALISATION                               */
/* ====================================================================== */

void drv_display_init(void)
{
    /* Reset OLED */
    HAL_GPIO_WritePin(OLED_RES_GPIO_Port, OLED_RES_Pin, GPIO_PIN_RESET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(OLED_RES_GPIO_Port, OLED_RES_Pin, GPIO_PIN_SET);
    HAL_Delay(50);

    /* SSD1309 init */
    send_cmd(0xAE);
    send_cmd(0xD5); send_cmd(0x80);
    send_cmd(0xA8); send_cmd(0x3F);
    send_cmd(0xD3); send_cmd(0x00);
    send_cmd(0x40);
    send_cmd(0x20); send_cmd(0x02);
    send_cmd(0xA1);
    send_cmd(0xC8);
    send_cmd(0xDA); send_cmd(0x12);
    send_cmd(0xAF);

    /*
     * Configure the U8g2 display descriptor first so tile width/height are
     * known (16x8 tiles for SSD1309 128x64), then attach our external buffer.
     */
    u8g2_SetupDisplay(&g_u8g2, u8x8_d_ssd1309_128x64_noname0, u8x8_cad_001, u8x8_byte_empty, NULL);

    /* Setup U8g2 to use external buffer */
    u8g2_SetupBuffer(&g_u8g2, buffer, 8, u8g2_ll_hvline_vertical_top_lsb, &u8g2_cb_r0);

    u8g2_SetFontMode(&g_u8g2, 1);
    u8g2_SetDrawColor(&g_u8g2, 1);
    u8g2_SetFont(&g_u8g2, g_active_font);

    drv_display_clear();
    drv_display_update();
}

/* ====================================================================== */
/*                            FONT                                        */
/* ====================================================================== */

const font_t FONT_5X7 = { .id = 0U };
const font_t FONT_4X6 = { .id = 1U };

void drv_display_set_font(const font_t *font)
{
    if (font == &FONT_4X6)
        g_active_font = u8g2_font_tom_thumb_4x6_tr;
    else
        g_active_font = u8g2_font_5x7_tr;

    u8g2_SetFont(&g_u8g2, g_active_font);
}

static inline int drv_display_baseline(int y)
{
    return y + u8g2_GetAscent(&g_u8g2);
}

static bool drv_display_clip_rect(int *x, int *y, int *w, int *h)
{
    int x0 = *x;
    int y0 = *y;
    int x1 = x0 + *w;
    int y1 = y0 + *h;

    if (x1 <= 0 || y1 <= 0 || x0 >= OLED_WIDTH || y0 >= OLED_HEIGHT)
    {
        return false;
    }

    if (x0 < 0)
    {
        x0 = 0;
    }
    if (y0 < 0)
    {
        y0 = 0;
    }
    if (x1 > OLED_WIDTH)
    {
        x1 = OLED_WIDTH;
    }
    if (y1 > OLED_HEIGHT)
    {
        y1 = OLED_HEIGHT;
    }

    *x = x0;
    *y = y0;
    *w = x1 - x0;
    *h = y1 - y0;

    return (*w > 0 && *h > 0);
}

static inline void drv_display_plot_raw(int x, int y, bool on)
{
    if (x < 0 || y < 0 || x >= OLED_WIDTH || y >= OLED_HEIGHT)
    {
        return;
    }

    const int index = (y >> 3) * OLED_WIDTH + x;
    const uint8_t mask = (uint8_t)(1U << (y & 0x07));

    if (on)
    {
        buffer[index] |= mask;
    }
    else
    {
        buffer[index] &= (uint8_t)~mask;
    }
}

static void drv_display_draw_hline_raw(int x, int y, int w, bool on)
{
    if (w <= 0 || y < 0 || y >= OLED_HEIGHT)
    {
        return;
    }

    int x0 = x;
    int x1 = x + w;

    if (x1 <= 0 || x0 >= OLED_WIDTH)
    {
        return;
    }

    if (x0 < 0)
    {
        x0 = 0;
    }
    if (x1 > OLED_WIDTH)
    {
        x1 = OLED_WIDTH;
    }

    const int index = (y >> 3) * OLED_WIDTH;
    const uint8_t mask = (uint8_t)(1U << (y & 0x07));

    for (int px = x0; px < x1; px++)
    {
        if (on)
        {
            buffer[index + px] |= mask;
        }
        else
        {
            buffer[index + px] &= (uint8_t)~mask;
        }
    }
}

static void drv_display_draw_vline_raw(int x, int y, int h, bool on)
{
    if (h <= 0 || x < 0 || x >= OLED_WIDTH)
    {
        return;
    }

    int y0 = y;
    int y1 = y + h;

    if (y1 <= 0 || y0 >= OLED_HEIGHT)
    {
        return;
    }

    if (y0 < 0)
    {
        y0 = 0;
    }
    if (y1 > OLED_HEIGHT)
    {
        y1 = OLED_HEIGHT;
    }

    for (int py = y0; py < y1; py++)
    {
        drv_display_plot_raw(x, py, on);
    }
}

static void drv_display_fill_rect_raw(int x, int y, int w, int h, bool on)
{
    if (drv_display_clip_rect(&x, &y, &w, &h) == false)
    {
        return;
    }

    for (int py = y; py < (y + h); py++)
    {
        drv_display_draw_hline_raw(x, py, w, on);
    }
}

/* ====================================================================== */
/*                          DRAW PRIMITIVES                               */
/* ====================================================================== */

void drv_display_draw_pixel(int x, int y, bool on)
{
    drv_display_plot_raw(x, y, on);
}

void drv_display_draw_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    if (drv_display_clip_rect(&x, &y, &w, &h) == false)
        return;

    drv_display_draw_hline_raw(x, y, w, true);

    if (h > 1)
    {
        drv_display_draw_hline_raw(x, y + h - 1, w, true);
    }

    if (h > 2)
    {
        drv_display_draw_vline_raw(x, y + 1, h - 2, true);
        drv_display_draw_vline_raw(x + w - 1, y + 1, h - 2, true);
    }
}

void drv_display_fill_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    drv_display_fill_rect_raw(x, y, w, h, true);
}

void drv_display_clear_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    drv_display_fill_rect_raw(x, y, w, h, false);
}

void drv_display_draw_line(int x1, int y1, int x2, int y2)
{
    int dx = abs(x2 - x1);
    int sx = (x1 < x2) ? 1 : -1;
    int dy = -abs(y2 - y1);
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx + dy;

    while (1)
    {
        drv_display_plot_raw(x1, y1, true);

        if (x1 == x2 && y1 == y2)
        {
            break;
        }

        const int e2 = err << 1;
        if (e2 >= dy)
        {
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y1 += sy;
        }
    }
}

/* ====================================================================== */
/*                               TEXT                                     */
/* ====================================================================== */

void drv_display_draw_char(uint8_t x, uint8_t y, char c)
{
    char txt[2] = { c, '\0' };
    u8g2_DrawStr(&g_u8g2, x, drv_display_baseline(y), txt);
}

void drv_display_draw_text(uint8_t x, uint8_t y, const char *txt)
{
    if (!txt)
        return;

    u8g2_DrawStr(&g_u8g2, x, drv_display_baseline(y), txt);
}

void drv_display_draw_number(uint8_t x, uint8_t y, int num)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", num);
    drv_display_draw_text(x, y, buf);
}
