#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#define DISPLAY_WIDTH  128U
#define DISPLAY_HEIGHT 64U
#define DISPLAY_PAGES  (DISPLAY_HEIGHT / 8U)
#define DISPLAY_BUFFER_SIZE (DISPLAY_WIDTH * DISPLAY_PAGES)

void ui_display_init(void);

void display_clear(void);
void set_pixel(uint8_t x, uint8_t y, uint8_t on);
void draw_char(uint8_t x, uint8_t y, char c);
void draw_text(uint8_t x, uint8_t y, const char *text);
void draw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h);
void fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h);

void display_update(void);

const uint8_t *display_get_buffer(void);

/* Backward compatibility wrappers used by renderer code. */
void ui_display_begin_frame(void);
void ui_display_end_frame(void);
void ui_display_set_font_default(void);

#endif /* UI_DISPLAY_H */
