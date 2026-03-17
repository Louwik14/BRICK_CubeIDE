#ifndef DRV_DISPLAY_H
#define DRV_DISPLAY_H

#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#include "UI/font.h"
/* Taille écran issue de brick_config */
#define OLED_WIDTH   128
#define OLED_HEIGHT  64

/* ====================================================================== */
/*                              API PUBLIQUE                              */
/* ====================================================================== */

void drv_display_init(void);
void drv_display_clear(void);
void drv_display_update(void);
uint8_t* drv_display_get_buffer(void);

/* ====================================================================== */
/*                           GESTION DES POLICES                          */
/* ====================================================================== */

void drv_display_set_font(const font_t *font);

/* ====================================================================== */
/*                           DESSIN ET TEXTE                              */
/* ====================================================================== */

void drv_display_draw_char(uint8_t x, uint8_t y, char c);
void drv_display_draw_text(uint8_t x, uint8_t y, const char *txt);
void drv_display_draw_number(uint8_t x, uint8_t y, int num);

/* ====================================================================== */
/*                        PRIMITIVES GRAPHIQUES                           */
/* ====================================================================== */

/* Pixel */
void drv_display_draw_pixel(int x, int y, bool on);

/* Rectangles */
void drv_display_draw_rect(int x, int y, int w, int h);
void drv_display_draw_line(int x1, int y1, int x2, int y2);
void drv_display_fill_rect(int x, int y, int w, int h);
void drv_display_clear_rect(int x, int y, int w, int h);

#endif /* DRV_DISPLAY_H */
