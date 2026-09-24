#include "drv_display.h"

#include "Board/board_display_transport.h"
#include "sdram.h"
#include "Platform/memory_layout.h"
#include "Platform/brick_media_clock.h"
#include "UI/ui_render_prof.h"
#include "../../U8g2/u8g2.h"

#include <string.h>
#include <stdio.h>

#include "drv_font4x6_generated.inc"
#include "drv_font5x7_generated.inc"
#include "drv_font6x13b_generated.inc"
#include "drv_fonthelvb14_generated.inc"

/* ====================================================================== */
/*                          FRAMEBUFFER                                   */
/* ====================================================================== */

static uint8_t buffer[OLED_WIDTH * OLED_HEIGHT / 8] SDRAM_BSS;
static uint8_t flush_snapshot[OLED_WIDTH * OLED_HEIGHT / 8] DMA_BUFFER;
static uint8_t flush_transfer[OLED_WIDTH * OLED_HEIGHT / 8] DMA_BUFFER;

static u8g2_t g_u8g2;
static const uint8_t *g_active_font = u8g2_font_5x7_tr;
static drv_display_state_t g_display_state = DRV_DISPLAY_STATE_UNINIT;
static drv_display_stats_t g_display_stats;
static volatile uint8_t g_dma_payload_busy;
static volatile uint8_t g_dma_payload_done;
static volatile uint8_t g_dma_payload_error;
static uint8_t g_flush_active;
static uint32_t g_prof_flush_wall_start;
static uint8_t g_prof_flush_active;
static uint8_t g_prof_service_poll_call;

static void display_prof_add_cycles(volatile ui_render_prof_cycles_t *stats,
                                    uint32_t elapsed)
{
    stats->count++;
    stats->total_cycles += elapsed;
    if (elapsed < stats->min_cycles) stats->min_cycles = elapsed;
    if (elapsed > stats->max_cycles) stats->max_cycles = elapsed;
}

static void display_prof_add_value(volatile ui_render_prof_value_t *stats,
                                   uint32_t value)
{
    stats->count++;
    stats->total_value += value;
    if (value < stats->min_value) stats->min_value = value;
    if (value > stats->max_value) stats->max_value = value;
}

void ui_render_prof_note_flush_service_poll(void)
{
    g_prof_service_poll_call = 1U;
}

void ui_render_prof_flush_reset_tracking(void)
{
    g_prof_flush_wall_start = 0U;
    g_prof_flush_active = 0U;
    g_prof_service_poll_call = 0U;
}

static void display_prof_finish_flush(uint8_t success)
{
    if (g_prof_flush_active == 0U) return;
    if (success != 0U) g_ui_render_prof.flush.complete_count++;
    else g_ui_render_prof.flush.failed_count++;
    display_prof_add_value(&g_ui_render_prof.flush.update_calls_per_flush,
                           g_ui_render_prof.flush.current_update_calls);
    display_prof_add_value(&g_ui_render_prof.flush.service_polls_per_flush,
                           g_ui_render_prof.flush.current_service_polls);
    display_prof_add_value(&g_ui_render_prof.flush.wall_ticks,
                           brick_media_clock_now_tick() - g_prof_flush_wall_start);
    g_prof_flush_active = 0U;
}

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
    uint8_t window_cmds[8];
    uint32_t prof_start;
    uint32_t full_prepare_cycles = 0U;
    uint16_t transfer_len = 0U;
    const uint8_t called_from_service = g_prof_service_poll_call;
    g_prof_service_poll_call = 0U;
    g_display_stats.flush_count++;

    if (g_display_state != DRV_DISPLAY_STATE_READY)
    {
        g_display_stats.flush_fail++;
        return;
    }

    if (g_dma_payload_error != 0U)
    {
        g_dma_payload_error = 0U;
        g_display_stats.flush_fail++;
        display_prof_finish_flush(0U);
        return;
    }

    if (g_prof_flush_active != 0U)
    {
        g_ui_render_prof.flush.current_update_calls++;
        g_ui_render_prof.flush.current_service_polls += called_from_service;
    }

    if (g_dma_payload_busy != 0U)
    {
        return;
    }

    if (g_flush_active == 0U)
    {
        uint8_t min_x = OLED_WIDTH;
        uint8_t max_x = 0U;
        uint8_t min_page = (uint8_t)(OLED_HEIGHT / 8U);
        uint8_t max_page = 0U;
        g_prof_flush_active = 1U;
        g_prof_flush_wall_start = brick_media_clock_now_tick();
        g_ui_render_prof.flush.current_update_calls = 1U;
        g_ui_render_prof.flush.current_service_polls = called_from_service;
        /*
         * Ownership contract:
         * - buffer: live render target written by U8g2/UI.
         * - flush_snapshot: frozen frame source consumed by DMA for one full
         *   8-page transfer, preventing inter-frame page mixing.
         */
        prof_start = DWT->CYCCNT;
        for (uint8_t page = 0U; page < (uint8_t)(OLED_HEIGHT / 8U); ++page)
        {
            for (uint8_t x = 0U; x < OLED_WIDTH; ++x)
            {
                const uint16_t offset = (uint16_t)page * OLED_WIDTH + x;
                if (buffer[offset] != flush_snapshot[offset])
                {
                    if (x < min_x) min_x = x;
                    if (x > max_x) max_x = x;
                    if (page < min_page) min_page = page;
                    if (page > max_page) max_page = page;
                }
            }
        }
        display_prof_add_cycles(&g_ui_render_prof.flush.dirty_scan,
                                DWT->CYCCNT - prof_start);

        prof_start = DWT->CYCCNT;
        memcpy(flush_snapshot, buffer, sizeof(flush_snapshot));
        display_prof_add_cycles(&g_ui_render_prof.flush.snapshot_memcpy,
                                DWT->CYCCNT - prof_start);

        prof_start = DWT->CYCCNT;
        if (min_x < OLED_WIDTH)
        {
            const uint8_t width = (uint8_t)(max_x - min_x + 1U);
            for (uint8_t page = min_page; page <= max_page; ++page)
            {
                memcpy(&flush_transfer[transfer_len],
                       &flush_snapshot[(uint16_t)page * OLED_WIDTH + min_x],
                       width);
                transfer_len = (uint16_t)(transfer_len + width);
            }
        }
        display_prof_add_cycles(&g_ui_render_prof.flush.dirty_pack,
                                DWT->CYCCNT - prof_start);

        if (transfer_len == 0U)
        {
            g_ui_render_prof.flush.unchanged_count++;
            display_prof_add_value(&g_ui_render_prof.flush.bytes_per_flush, 0U);
            display_prof_finish_flush(1U);
            return;
        }

        if (transfer_len == sizeof(flush_transfer))
            g_ui_render_prof.flush.full_window_count++;
        else
            g_ui_render_prof.flush.partial_window_count++;
        display_prof_add_value(&g_ui_render_prof.flush.bytes_per_flush, transfer_len);

        window_cmds[0] = 0x20U;
        window_cmds[1] = 0x00U;
        window_cmds[2] = 0x21U;
        window_cmds[3] = min_x;
        window_cmds[4] = max_x;
        window_cmds[5] = 0x22U;
        window_cmds[6] = min_page;
        window_cmds[7] = max_page;

        prof_start = DWT->CYCCNT;
        if (send_cmd_burst(window_cmds, sizeof(window_cmds)) == 0U)
        {
            display_prof_add_cycles(&g_ui_render_prof.flush.full_prepare_launch,
                                    DWT->CYCCNT - prof_start);
            g_display_stats.flush_fail++;
            display_prof_finish_flush(0U);
            return;
        }
        full_prepare_cycles = DWT->CYCCNT - prof_start;
        g_flush_active = 1U;

        prof_start = DWT->CYCCNT;
        if (send_data_burst_dma(flush_transfer, transfer_len) == 0U)
        {
            full_prepare_cycles += DWT->CYCCNT - prof_start;
            display_prof_add_cycles(&g_ui_render_prof.flush.full_prepare_launch,
                                    full_prepare_cycles);
            g_display_stats.flush_fail++;
            g_flush_active = 0U;
            display_prof_finish_flush(0U);
            return;
        }
        full_prepare_cycles += DWT->CYCCNT - prof_start;
        display_prof_add_cycles(&g_ui_render_prof.flush.full_prepare_launch,
                                full_prepare_cycles);
        return;
    }

    if (g_dma_payload_done != 0U)
    {
        g_dma_payload_done = 0U;
        g_flush_active = 0U;
        display_prof_finish_flush(1U);
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
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if ((board_display_transport_is_tx_callback(hspi) != 0U) && (g_dma_payload_busy != 0U))
    {
        transport_end();
        g_dma_payload_busy = 0U;
        g_dma_payload_error = 1U;
        g_display_stats.tx_err++;
        g_display_state = DRV_DISPLAY_STATE_FAULT;
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
    const uint8_t *next_font;

    if (font == &FONT_4X6)
    {
        next_font = u8g2_font_tom_thumb_4x6_tr;
    }
    else if (font == &FONT_MINIMAL3X3)
    {
        next_font = u8g2_font_minimal3x3_tu;
    }
    else if (font == &FONT_3X3BASIC)
    {
        next_font = u8g2_font_3x3basic_tr;
    }
    else if (font == &FONT_PEAR)
    {
        next_font = u8g2_font_pearfont_tr;
    }
    else if (font == &FONT_HELVB14)
    {
        next_font = u8g2_font_helvB14_tf;
    }
    else if (font == &FONT_OFF_COMPACT)
    {
        next_font = u8g2_font_6x13B_tf;
    }
    else
    {
        next_font = u8g2_font_5x7_tr;
    }

    if (next_font == g_active_font)
    {
        return;
    }
    g_active_font = next_font;
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

static uint8_t drv_fastfont_can_render(const char *txt)
{
    const uint8_t *p = (const uint8_t *)txt;

    while (*p != 0U)
    {
        if ((*p < 0x20U) || (*p > 0x7eU))
        {
            return 0U;
        }
        ++p;
    }
    return 1U;
}

static inline void drv_fastfont_apply_column(int x, int y, uint32_t bits,
                                             uint8_t color)
{
    if ((x < 0) || (x >= OLED_WIDTH) || (y >= OLED_HEIGHT) || (bits == 0U))
    {
        return;
    }
    if (y < 0)
    {
        const int clipped = -y;
        if (clipped >= 32) return;
        bits >>= clipped;
        y = 0;
        if (bits == 0U) return;
    }

    uint8_t page = (uint8_t)y >> 3;
    uint64_t shifted = (uint64_t)bits << ((uint8_t)y & 7U);
    while ((shifted != 0U) && (page < (OLED_HEIGHT / 8U)))
    {
        uint8_t *dst = &buffer[(uint16_t)page * OLED_WIDTH + (uint16_t)x];
        const uint8_t mask = (uint8_t)shifted;
        if (color == 0U) *dst &= (uint8_t)~mask;
        else if (color == 1U) *dst |= mask;
        else *dst ^= mask;
        shifted >>= 8;
        ++page;
    }
}

static void drv_font4x6_draw(uint8_t x, uint8_t y, const char *txt,
                             uint8_t color)
{
    int cursor = x;
    const int baseline = (int)y + 5;
    const uint8_t *p = (const uint8_t *)txt;

    while (*p != 0U)
    {
        const drv_font4x6_glyph_t *glyph = &g_font4x6_ascii[*p - 0x20U];
        const int glyph_x = cursor + glyph->x_offset;
        const int glyph_y = baseline + glyph->top_offset;

        for (uint8_t col = 0U; col < glyph->width; ++col)
        {
            drv_fastfont_apply_column(glyph_x + col, glyph_y,
                                      glyph->columns[col], color);
        }
        cursor += glyph->advance;
        ++p;
    }
}

static uint8_t drv_font4x6_width(const char *txt)
{
    const uint8_t *p = (const uint8_t *)txt;
    int width = 0;
    int initial_offset = 0;
    const drv_font4x6_glyph_t *last = NULL;

    while (*p != 0U)
    {
        const drv_font4x6_glyph_t *glyph = &g_font4x6_ascii[*p - 0x20U];
        if (last == NULL) initial_offset = glyph->x_offset;
        width += glyph->advance;
        last = glyph;
        ++p;
    }
    if ((last != NULL) && (last->width != 0U))
    {
        width -= last->advance;
        width += last->width + last->x_offset;
        if (initial_offset > 0) width += initial_offset;
    }
    return (uint8_t)width;
}

static void drv_font5x7_draw(uint8_t x, uint8_t y, const char *txt,
                             uint8_t color)
{
    int cursor = x;
    const int baseline = (int)y + 6;
    const uint8_t *p = (const uint8_t *)txt;

    while (*p != 0U)
    {
        const drv_font5x7_glyph_t *glyph = &g_font5x7_ascii[*p - 0x20U];
        const int glyph_x = cursor + glyph->x_offset;
        const int glyph_y = baseline + glyph->top_offset;

        for (uint8_t col = 0U; col < glyph->width; ++col)
        {
            drv_fastfont_apply_column(glyph_x + col, glyph_y,
                                      glyph->columns[col], color);
        }
        cursor += glyph->advance;
        ++p;
    }
}

static uint8_t drv_font5x7_width(const char *txt)
{
    const uint8_t *p = (const uint8_t *)txt;
    int width = 0;
    int initial_offset = 0;
    const drv_font5x7_glyph_t *last = NULL;

    while (*p != 0U)
    {
        const drv_font5x7_glyph_t *glyph = &g_font5x7_ascii[*p - 0x20U];
        if (last == NULL) initial_offset = glyph->x_offset;
        width += glyph->advance;
        last = glyph;
        ++p;
    }
    if ((last != NULL) && (last->width != 0U))
    {
        width -= last->advance;
        width += last->width + last->x_offset;
        if (initial_offset > 0) width += initial_offset;
    }
    return (uint8_t)width;
}

#define DRV_FASTFONT_DEFINE(NAME, GLYPH_TYPE, TABLE, BASELINE)                 \
static void NAME##_draw(uint8_t x, uint8_t y, const char *txt, uint8_t color) \
{                                                                             \
    int cursor = x;                                                           \
    const int baseline = (int)y + (BASELINE);                                 \
    const uint8_t *p = (const uint8_t *)txt;                                  \
    while (*p != 0U)                                                         \
    {                                                                         \
        const GLYPH_TYPE *glyph = &(TABLE)[*p - 0x20U];                       \
        const int glyph_x = cursor + glyph->x_offset;                         \
        const int glyph_y = baseline + glyph->top_offset;                     \
        for (uint8_t col = 0U; col < glyph->width; ++col)                     \
            drv_fastfont_apply_column(glyph_x + col, glyph_y,                 \
                                      glyph->columns[col], color);             \
        cursor += glyph->advance;                                             \
        ++p;                                                                  \
    }                                                                         \
}                                                                             \
static uint8_t NAME##_width(const char *txt)                                  \
{                                                                             \
    const uint8_t *p = (const uint8_t *)txt;                                  \
    int width = 0;                                                            \
    int initial_offset = 0;                                                   \
    const GLYPH_TYPE *last = NULL;                                            \
    while (*p != 0U)                                                         \
    {                                                                         \
        const GLYPH_TYPE *glyph = &(TABLE)[*p - 0x20U];                       \
        if (last == NULL) initial_offset = glyph->x_offset;                   \
        width += glyph->advance;                                              \
        last = glyph;                                                         \
        ++p;                                                                  \
    }                                                                         \
    if ((last != NULL) && (last->width != 0U))                                \
    {                                                                         \
        width -= last->advance;                                               \
        width += last->width + last->x_offset;                                \
        if (initial_offset > 0) width += initial_offset;                      \
    }                                                                         \
    return (uint8_t)width;                                                    \
}

DRV_FASTFONT_DEFINE(drv_font6x13b, drv_font6x13b_glyph_t,
                    g_font6x13b_ascii, 9)
DRV_FASTFONT_DEFINE(drv_fonthelvb14, drv_fonthelvb14_glyph_t,
                    g_fonthelvb14_ascii, 14)

/* ====================================================================== */
/*                          DRAW PRIMITIVES                               */
/* ====================================================================== */

void drv_display_draw_pixel(int x, int y, bool on)
{
    u8g2_SetDrawColor(&g_u8g2, on ? 1 : 0);
    u8g2_DrawPixel(&g_u8g2, x, y);
    u8g2_SetDrawColor(&g_u8g2, 1);
}

void drv_display_draw_bitmap_1bpp(int x, int y, int w, int h, const uint8_t *bitmap)
{
    if ((bitmap == NULL) || (w <= 0) || (h <= 0))
        return;

    const uint8_t bitmap_mode = g_u8g2.bitmap_transparency;
    u8g2_SetBitmapMode(&g_u8g2, 1U);
    u8g2_DrawXBM(&g_u8g2, x, y, (u8g2_uint_t)w, (u8g2_uint_t)h, bitmap);
    u8g2_SetBitmapMode(&g_u8g2, bitmap_mode);
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
    if ((g_active_font == u8g2_font_tom_thumb_4x6_tr) &&
        drv_fastfont_can_render(txt))
    {
        drv_font4x6_draw(x, y, txt, u8g2_GetDrawColor(&g_u8g2));
        return;
    }
    if ((g_active_font == u8g2_font_5x7_tr) && drv_fastfont_can_render(txt))
    {
        drv_font5x7_draw(x, y, txt, u8g2_GetDrawColor(&g_u8g2));
        return;
    }
    if ((g_active_font == u8g2_font_6x13B_tf) && drv_fastfont_can_render(txt))
    {
        drv_font6x13b_draw(x, y, txt, u8g2_GetDrawColor(&g_u8g2));
        return;
    }
    if ((g_active_font == u8g2_font_helvB14_tf) && drv_fastfont_can_render(txt))
    {
        drv_fonthelvb14_draw(x, y, txt, u8g2_GetDrawColor(&g_u8g2));
        return;
    }
    u8g2_DrawStr(&g_u8g2, x, drv_display_baseline(y), txt);
}

void drv_display_draw_text(uint8_t x, uint8_t y, const char *txt)
{
    if (!txt)
        return;

    if ((g_active_font == u8g2_font_tom_thumb_4x6_tr) &&
        drv_fastfont_can_render(txt))
    {
        drv_font4x6_draw(x, y, txt, u8g2_GetDrawColor(&g_u8g2));
        return;
    }
    if ((g_active_font == u8g2_font_5x7_tr) && drv_fastfont_can_render(txt))
    {
        drv_font5x7_draw(x, y, txt, u8g2_GetDrawColor(&g_u8g2));
        return;
    }
    if ((g_active_font == u8g2_font_6x13B_tf) && drv_fastfont_can_render(txt))
    {
        drv_font6x13b_draw(x, y, txt, u8g2_GetDrawColor(&g_u8g2));
        return;
    }
    if ((g_active_font == u8g2_font_helvB14_tf) && drv_fastfont_can_render(txt))
    {
        drv_fonthelvb14_draw(x, y, txt, u8g2_GetDrawColor(&g_u8g2));
        return;
    }
    u8g2_DrawStr(&g_u8g2, x, drv_display_baseline(y), txt);
}

void drv_display_draw_text_inverted(uint8_t x, uint8_t y, const char *txt)
{
    if (!txt)
        return;

    u8g2_SetDrawColor(&g_u8g2, 0);
    if ((g_active_font == u8g2_font_tom_thumb_4x6_tr) &&
        drv_fastfont_can_render(txt))
    {
        drv_font4x6_draw(x, y, txt, 0U);
    }
    else if ((g_active_font == u8g2_font_5x7_tr) &&
             drv_fastfont_can_render(txt))
    {
        drv_font5x7_draw(x, y, txt, 0U);
    }
    else if ((g_active_font == u8g2_font_6x13B_tf) &&
             drv_fastfont_can_render(txt))
    {
        drv_font6x13b_draw(x, y, txt, 0U);
    }
    else if ((g_active_font == u8g2_font_helvB14_tf) &&
             drv_fastfont_can_render(txt))
    {
        drv_fonthelvb14_draw(x, y, txt, 0U);
    }
    else
    {
        u8g2_DrawStr(&g_u8g2, x, drv_display_baseline(y), txt);
    }
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

    if ((g_active_font == u8g2_font_tom_thumb_4x6_tr) &&
        drv_fastfont_can_render(txt))
    {
        return drv_font4x6_width(txt);
    }
    if ((g_active_font == u8g2_font_5x7_tr) && drv_fastfont_can_render(txt))
    {
        return drv_font5x7_width(txt);
    }
    if ((g_active_font == u8g2_font_6x13B_tf) && drv_fastfont_can_render(txt))
    {
        return drv_font6x13b_width(txt);
    }
    if ((g_active_font == u8g2_font_helvB14_tf) && drv_fastfont_can_render(txt))
    {
        return drv_fonthelvb14_width(txt);
    }
    return (uint8_t)u8g2_GetStrWidth(&g_u8g2, txt);
}

uint8_t drv_display_font_height(void)
{
    const int height = u8g2_GetAscent(&g_u8g2) - u8g2_GetDescent(&g_u8g2);
    return (height > 0) ? (uint8_t)height : 0U;
}
