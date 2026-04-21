#ifndef DRV_DISPLAY_H
#define DRV_DISPLAY_H

#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#include "UI/font.h"

#define OLED_WIDTH   128
#define OLED_HEIGHT  64

typedef enum
{
    DRV_DISPLAY_STATE_UNINIT = 0,
    DRV_DISPLAY_STATE_READY,
    DRV_DISPLAY_STATE_FAULT
} drv_display_state_t;

typedef struct
{
    uint32_t tx_ok;
    uint32_t tx_err;
    uint32_t timeout_err;
    uint32_t flush_count;
    uint32_t flush_fail;
} drv_display_stats_t;

void drv_display_init(void);
void drv_display_clear(void);
void drv_display_update(void);
uint8_t* drv_display_get_buffer(void);
drv_display_state_t drv_display_get_state(void);
const drv_display_stats_t* drv_display_get_stats(void);
uint8_t drv_display_flush_in_progress(void);

void drv_display_set_font(const font_t *font);

void drv_display_draw_char(uint8_t x, uint8_t y, char c);
void drv_display_draw_text(uint8_t x, uint8_t y, const char *txt);
void drv_display_draw_text_inverted(uint8_t x, uint8_t y, const char *txt);
void drv_display_draw_number(uint8_t x, uint8_t y, int num);
uint8_t drv_display_text_width(const char *txt);
uint8_t drv_display_font_height(void);

void drv_display_draw_pixel(int x, int y, bool on);
void drv_display_draw_rect(int x, int y, int w, int h);
void drv_display_draw_line(int x1, int y1, int x2, int y2);
void drv_display_fill_rect(int x, int y, int w, int h);
void drv_display_clear_rect(int x, int y, int w, int h);

#endif /* DRV_DISPLAY_H */
