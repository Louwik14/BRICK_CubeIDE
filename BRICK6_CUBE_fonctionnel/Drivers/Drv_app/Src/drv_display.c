#include "drv_display.h"

#include "spi.h"
#include "gpio.h"
#include "sdram.h"
#include "../../U8g2/u8g2.h"

#include <string.h>
#include <stdio.h>

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

/* ====================================================================== */
/*                          DRAW PRIMITIVES                               */
/* ====================================================================== */

void drv_display_draw_pixel(int x, int y, bool on)
{
    u8g2_SetDrawColor(&g_u8g2, on ? 1 : 0);
    u8g2_DrawPixel(&g_u8g2, x, y);
    u8g2_SetDrawColor(&g_u8g2, 1);
}

void drv_display_draw_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    u8g2_DrawFrame(&g_u8g2, x, y, w, h);
}

void drv_display_fill_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    u8g2_DrawBox(&g_u8g2, x, y, w, h);
}

void drv_display_clear_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    u8g2_SetDrawColor(&g_u8g2, 0);
    u8g2_DrawBox(&g_u8g2, x, y, w, h);
    u8g2_SetDrawColor(&g_u8g2, 1);
}

void drv_display_draw_line(int x1, int y1, int x2, int y2)
{
    u8g2_DrawLine(&g_u8g2, x1, y1, x2, y2);
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

void drv_display_draw_text_inverted(uint8_t x, uint8_t y, const char *txt)
{
    if (!txt)
        return;

    u8g2_SetDrawColor(&g_u8g2, 0);
    u8g2_DrawStr(&g_u8g2, x, drv_display_baseline(y), txt);
    u8g2_SetDrawColor(&g_u8g2, 1);
}

void drv_display_draw_number(uint8_t x, uint8_t y, int num)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", num);
    drv_display_draw_text(x, y, buf);
}

uint8_t drv_display_text_width(const char *txt)
{
    if (!txt)
        return 0U;

    return (uint8_t)u8g2_GetStrWidth(&g_u8g2, txt);
}

uint8_t drv_display_font_height(void)
{
    const int height = u8g2_GetAscent(&g_u8g2) - u8g2_GetDescent(&g_u8g2);
    return (height > 0) ? (uint8_t)height : 0U;
}
