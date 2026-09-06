#include "drv_display.h"

#include "Board/board_display_transport.h"
#include "UI/ui_service_wakeup.h"
#include "sdram.h"
#include "Platform/memory_layout.h"
#include "../../U8g2/u8g2.h"

#include <string.h>
#include <stdio.h>

/* ====================================================================== */
/*                          FRAMEBUFFER                                   */
/* ====================================================================== */

static uint8_t buffer[OLED_WIDTH * OLED_HEIGHT / 8] SDRAM_BSS;
static uint8_t flush_snapshot[OLED_WIDTH * OLED_HEIGHT / 8] DMA_BUFFER;

static u8g2_t g_u8g2;
static const uint8_t *g_active_font = u8g2_font_5x7_tr;
static drv_display_state_t g_display_state = DRV_DISPLAY_STATE_UNINIT;
static drv_display_stats_t g_display_stats;
static volatile uint8_t g_dma_payload_busy;
static volatile uint8_t g_dma_payload_done;
static volatile uint8_t g_dma_payload_error;
static uint8_t g_flush_active;
static uint8_t g_flush_page;

/* ====================================================================== */
/*                             SPI / GPIO                                 */
/* ====================================================================== */

static void transport_begin(uint8_t is_data)
{
    board_display_transport_begin(is_data);
}

static void transport_end(void)
{
    board_display_transport_end();
}

static uint8_t transport_tx(const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    board_display_tx_status_t rc = board_display_transport_tx(data, len, timeout_ms);
    if (rc == BOARD_DISPLAY_TX_OK)
    {
        g_display_stats.tx_ok++;
        return 1U;
    }

    g_display_stats.tx_err++;
    if (rc == BOARD_DISPLAY_TX_TIMEOUT)
    {
        g_display_stats.timeout_err++;
    }
    g_display_state = DRV_DISPLAY_STATE_FAULT;
    return 0U;
}

static uint8_t transport_burst(uint8_t is_data, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    uint8_t ok;

    if ((data == NULL) || (len == 0U))
    {
        return 1U;
    }

    transport_begin(is_data);
    ok = transport_tx(data, len, timeout_ms);
    transport_end();

    return ok;
}

static uint8_t send_cmd_burst(const uint8_t *cmds, size_t len)
{
    return transport_burst(0U, cmds, len, 20U);
}

static uint8_t send_data_burst_dma(const uint8_t *data, size_t len)
{
    board_display_tx_status_t rc;

    if ((data == NULL) || (len == 0U))
    {
        return 1U;
    }

    if (g_dma_payload_busy != 0U)
    {
        return 0U;
    }

    transport_begin(1U);
    g_dma_payload_busy = 1U;
    g_dma_payload_done = 0U;
    rc = board_display_transport_tx_dma(data, len);
    if (rc == BOARD_DISPLAY_TX_OK)
    {
        return 1U;
    }

    g_dma_payload_busy = 0U;
    transport_end();
    g_display_stats.tx_err++;
    g_display_state = DRV_DISPLAY_STATE_FAULT;
    return 0U;
}

static uint8_t ssd1309_init_sequence(void)
{
    static const uint8_t k_init_cmds[] = {
        0xAEU,       /* Display OFF */
        0xD5U, 0xA0U,/* Clock divide / osc */
        0xA8U, 0x3FU,/* Multiplex ratio */
        0xD3U, 0x00U,/* Display offset */
        0x40U,       /* Start line */
        0x20U, 0x02U,/* Page addressing mode */
        0xA1U,       /* Segment remap */
        0xC8U,       /* COM scan direction */
        0xDAU, 0x12U,/* COM pins config */
        0x81U, 0x6FU,/* Contrast */
        0xD9U, 0xD3U,/* Pre-charge */
        0xDBU, 0x20U,/* VCOMH */
        0x2EU,       /* Scroll OFF */
        0xA4U,       /* Resume RAM content display */
        0xA6U        /* Normal display */
    };

    if (send_cmd_burst(k_init_cmds, sizeof(k_init_cmds)) == 0U) return 0U;

    return 1U;
}

static uint8_t ssd1309_display_on(void)
{
    static const uint8_t k_on_cmd = 0xAFU;
    return send_cmd_burst(&k_on_cmd, sizeof(k_on_cmd));
}

void drv_display_off(void)
{
    static const uint8_t k_off_cmd = 0xAEU;
    (void)send_cmd_burst(&k_off_cmd, sizeof(k_off_cmd));
}

static uint8_t ssd1309_clear_controller_ram(void)
{
    for (uint8_t page = 0U; page < 8U; ++page)
    {
        uint8_t page_cmds[3];
        page_cmds[0] = (uint8_t)(0xB0U + page);
        page_cmds[1] = 0x00U;
        page_cmds[2] = 0x10U;

        if (send_cmd_burst(page_cmds, sizeof(page_cmds)) == 0U)
        {
            return 0U;
        }

        if (transport_burst(1U, &buffer[page * OLED_WIDTH], OLED_WIDTH, 20U) == 0U)
        {
            return 0U;
        }
    }

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

uint8_t drv_display_flush_in_progress(void)
{
    if ((g_dma_payload_busy != 0U) || (g_flush_active != 0U))
    {
        return 1U;
    }

    return 0U;
}

uint8_t drv_display_flush_continuation_pending(void)
{
    return ((g_dma_payload_done != 0U) || (g_dma_payload_error != 0U))
        ? 1U : 0U;
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
    static const uint8_t k_page_mode_cmds[] = { 0x20U, 0x02U };
    g_display_stats.flush_count++;

    if (g_dma_payload_error != 0U)
    {
        /* DMA error completion is a continuation event, not a terminal
         * display state.  The pending frame remains owned by the flush
         * service and is retried after this cleanup. */
        g_dma_payload_error = 0U;
        g_dma_payload_done = 0U;
        g_dma_payload_busy = 0U;
        g_flush_active = 0U;
        g_display_stats.flush_fail++;
        g_display_state = DRV_DISPLAY_STATE_READY;
    }

    if (g_display_state != DRV_DISPLAY_STATE_READY)
    {
        g_display_stats.flush_fail++;
        return;
    }

    if (g_dma_payload_error != 0U)
    {
        g_dma_payload_error = 0U;
        g_display_stats.flush_fail++;
        return;
    }

    if (g_dma_payload_busy != 0U)
    {
        return;
    }

    if (g_flush_active == 0U)
    {
        /*
         * U8g2 buffer uses page-oriented vertical bytes.
         * Keep the controller in page addressing mode so 0xB0+page and
         * page*OLED_WIDTH indexing stay aligned.
         */
        if (send_cmd_burst(k_page_mode_cmds, sizeof(k_page_mode_cmds)) == 0U)
        {
            g_display_stats.flush_fail++;
            g_dma_payload_error = 0U;
            return;
        }

        /*
         * Ownership contract:
         * - buffer: live render target written by U8g2/UI.
         * - flush_snapshot: frozen frame source consumed by DMA for one full
         *   8-page transfer, preventing inter-frame page mixing.
         */
        memcpy(flush_snapshot, buffer, sizeof(flush_snapshot));
        g_flush_active = 1U;
        g_flush_page = 0U;
    }
    else if (g_dma_payload_done != 0U)
    {
        g_dma_payload_done = 0U;
        g_flush_page++;
        if (g_flush_page >= 8U)
        {
            g_flush_active = 0U;
            return;
        }
    }

    {
        uint8_t page_cmds[3];
        page_cmds[0] = (uint8_t)(0xB0U + g_flush_page);
        page_cmds[1] = 0x00U;
        page_cmds[2] = 0x10U;

        if (send_cmd_burst(page_cmds, sizeof(page_cmds)) == 0U)
        {
            g_display_stats.flush_fail++;
            g_dma_payload_error = 0U;
            g_flush_active = 0U;
            return;
        }

        if (send_data_burst_dma(&flush_snapshot[g_flush_page * OLED_WIDTH], OLED_WIDTH) == 0U)
        {
            g_display_stats.flush_fail++;
            g_dma_payload_error = 0U;
            g_flush_active = 0U;
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
    g_dma_payload_busy = 0U;
    g_dma_payload_done = 0U;
    g_dma_payload_error = 0U;
    g_flush_active = 0U;
    g_flush_page = 0U;

    /*
     * Contract boundary:
     * - U8g2 handles rasterization into the framebuffer.
     * - drv_display owns SSD1309 reset/init and SPI transport flush.
     */

    /* Reset OLED */
    board_display_transport_reset();

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
    if ((ssd1309_clear_controller_ram() == 0U) || (ssd1309_display_on() == 0U))
    {
        g_display_state = DRV_DISPLAY_STATE_FAULT;
        return;
    }

    g_display_state = DRV_DISPLAY_STATE_READY;
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if ((board_display_transport_is_tx_callback(hspi) != 0U) && (g_dma_payload_busy != 0U))
    {
        transport_end();
        g_dma_payload_busy = 0U;
        g_dma_payload_done = 1U;
        g_display_stats.tx_ok++;
        __DMB();
        ui_service_wakeup(UI_SERVICE_WAKE_OLED);
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if ((board_display_transport_is_tx_callback(hspi) != 0U) && (g_dma_payload_busy != 0U))
    {
        transport_end();
        g_dma_payload_busy = 0U;
        g_dma_payload_error = 1U;
        g_dma_payload_done = 0U;
        g_flush_active = 0U;
        g_display_stats.tx_err++;
        g_display_state = DRV_DISPLAY_STATE_READY;
        __DMB();
        ui_service_wakeup(UI_SERVICE_WAKE_OLED);
    }
}

/* ====================================================================== */
/*                            FONT                                        */
/* ====================================================================== */

const font_t FONT_5X7 = { .id = 0U };
const font_t FONT_4X6 = { .id = 1U };
const font_t FONT_MINIMAL3X3 = { .id = 2U };
const font_t FONT_3X3BASIC = { .id = 3U };
const font_t FONT_PEAR = { .id = 4U };
const font_t FONT_HELVB14 = { .id = 5U };
const font_t FONT_OFF_COMPACT = { .id = 6U };

void drv_display_set_font(const font_t *font)
{
    if (font == &FONT_4X6)
    {
        g_active_font = u8g2_font_tom_thumb_4x6_tr;
    }
    else if (font == &FONT_MINIMAL3X3)
    {
        g_active_font = u8g2_font_minimal3x3_tu;
    }
    else if (font == &FONT_3X3BASIC)
    {
        g_active_font = u8g2_font_3x3basic_tr;
    }
    else if (font == &FONT_PEAR)
    {
        g_active_font = u8g2_font_pearfont_tr;
    }
    else if (font == &FONT_HELVB14)
    {
        g_active_font = u8g2_font_helvB14_tf;
    }
    else if (font == &FONT_OFF_COMPACT)
    {
        g_active_font = u8g2_font_6x13B_tf;
    }
    else
    {
        g_active_font = u8g2_font_5x7_tr;
    }

    u8g2_SetFont(&g_u8g2, g_active_font);
}

void drv_display_set_draw_color(uint8_t color)
{
    u8g2_SetDrawColor(&g_u8g2, color);
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
