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
static drv_display_state_t g_display_state = DRV_DISPLAY_STATE_UNINIT;
static drv_display_stats_t g_display_stats;

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

static uint8_t spi_tx(const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    HAL_StatusTypeDef rc = HAL_SPI_Transmit(&hspi5, (uint8_t *)data, (uint16_t)len, timeout_ms);
    if (rc == HAL_OK)
    {
        g_display_stats.tx_ok++;
        return 1U;
    }

    g_display_stats.tx_err++;
    if (rc == HAL_TIMEOUT)
    {
        g_display_stats.timeout_err++;
    }
    g_display_state = DRV_DISPLAY_STATE_FAULT;
    return 0U;
}

static uint8_t send_cmd(uint8_t cmd)
{
    dc_cmd();
    cs_low();
    uint8_t ok = spi_tx(&cmd, 1U, 10U);
    cs_high();
    return ok;
}

static uint8_t send_data(const uint8_t *data, size_t len)
{
    dc_data();
    cs_low();
    uint8_t ok = spi_tx(data, len, 100U);
    cs_high();
    return ok;
}

static uint8_t send_cmd2(uint8_t a, uint8_t b)
{
    return (send_cmd(a) != 0U) && (send_cmd(b) != 0U);
}

static uint8_t ssd1309_init_sequence(void)
{
    if (send_cmd(0xAEU) == 0U) return 0U;          /* Display OFF */
    if (send_cmd2(0xD5U, 0xA0U) == 0U) return 0U;  /* Clock divide / osc */
    if (send_cmd2(0xA8U, 0x3FU) == 0U) return 0U;  /* Multiplex ratio */
    if (send_cmd2(0xD3U, 0x00U) == 0U) return 0U;  /* Display offset */
    if (send_cmd(0x40U) == 0U) return 0U;          /* Start line */
    if (send_cmd2(0x20U, 0x02U) == 0U) return 0U;  /* Page addressing mode */
    if (send_cmd(0xA1U) == 0U) return 0U;          /* Segment remap */
    if (send_cmd(0xC8U) == 0U) return 0U;          /* COM scan direction */
    if (send_cmd2(0xDAU, 0x12U) == 0U) return 0U;  /* COM pins config */
    if (send_cmd2(0x81U, 0x6FU) == 0U) return 0U;  /* Contrast */
    if (send_cmd2(0xD9U, 0xD3U) == 0U) return 0U;  /* Pre-charge */
    if (send_cmd2(0xDBU, 0x20U) == 0U) return 0U;  /* VCOMH */
    if (send_cmd(0x2EU) == 0U) return 0U;          /* Scroll OFF */
    if (send_cmd(0xA4U) == 0U) return 0U;          /* Resume RAM content display */
    if (send_cmd(0xA6U) == 0U) return 0U;          /* Normal display */
    if (send_cmd(0xAFU) == 0U) return 0U;          /* Display ON */

    return 1U;
}

/* ====================================================================== */
/*                         FRAMEBUFFER ACCESS                             */
/* ====================================================================== */

uint8_t* drv_display_get_buffer(void)
{
    return buffer;
}

drv_display_state_t drv_display_get_state(void)
{
    return g_display_state;
}

const drv_display_stats_t* drv_display_get_stats(void)
{
    return &g_display_stats;
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
    g_display_stats.flush_count++;

    if (g_display_state != DRV_DISPLAY_STATE_READY)
    {
        g_display_stats.flush_fail++;
        return;
    }

    /*
     * U8g2 buffer uses page-oriented vertical bytes.
     * Keep the controller in page addressing mode so 0xB0+page and
     * page*OLED_WIDTH indexing stay aligned.
     */
    if (send_cmd2(0x20U, 0x02U) == 0U)
    {
        g_display_stats.flush_fail++;
        return;
    }

    for (uint8_t page = 0; page < 8; page++)
    {
        if (send_cmd((uint8_t)(0xB0U + page)) == 0U ||
            send_cmd(0x00U) == 0U ||
            send_cmd(0x10U) == 0U)
        {
            g_display_stats.flush_fail++;
            return;
        }

        if (send_data(&buffer[page * OLED_WIDTH], OLED_WIDTH) == 0U)
        {
            g_display_stats.flush_fail++;
            return;
        }
    }
}

/* ====================================================================== */
/*                           INITIALISATION                               */
/* ====================================================================== */

void drv_display_init(void)
{
    memset(&g_display_stats, 0, sizeof(g_display_stats));
    g_display_state = DRV_DISPLAY_STATE_UNINIT;

    /*
     * Contract boundary:
     * - U8g2 handles rasterization into the framebuffer.
     * - drv_display owns SSD1309 reset/init and SPI transport flush.
     */

    /* Reset OLED */
    HAL_GPIO_WritePin(OLED_RES_GPIO_Port, OLED_RES_Pin, GPIO_PIN_RESET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(OLED_RES_GPIO_Port, OLED_RES_Pin, GPIO_PIN_SET);
    HAL_Delay(50);

    if (ssd1309_init_sequence() == 0U)
    {
        g_display_state = DRV_DISPLAY_STATE_FAULT;
        return;
    }

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
    g_display_state = DRV_DISPLAY_STATE_READY;
    drv_display_update();

    if (g_display_state != DRV_DISPLAY_STATE_READY)
    {
        g_display_state = DRV_DISPLAY_STATE_FAULT;
    }
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
