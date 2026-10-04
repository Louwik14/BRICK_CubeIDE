#ifndef UI_THEME_H
#define UI_THEME_H

#include <stdint.h>

#include "font.h"

typedef enum
{
    UI_THEME_CLASSIC = 0,
    UI_THEME_DECK,
    UI_THEME_HALO,
    UI_THEME_MINIMALIST,
    UI_THEME_STRIP,
    UI_THEME_AXIS,
    UI_THEME_COUNT
} ui_theme_id_t;

typedef enum
{
    UI_THEME_BASE_CLASSIC = 0,
    UI_THEME_BASE_MINIMALIST
} ui_theme_base_style_t;

typedef enum
{
    UI_THEME_FRAME_NONE = 0,
    UI_THEME_FRAME_CLASSIC,
    UI_THEME_FRAME_LINE
} ui_theme_frame_style_t;

typedef enum
{
    UI_THEME_FOCUS_CLASSIC = 0,
    UI_THEME_FOCUS_UNDERLINE
} ui_theme_focus_style_t;

typedef enum
{
    UI_THEME_HEADER_CLASSIC = 0,
    UI_THEME_HEADER_MINIMALIST,
    UI_THEME_HEADER_DECK,
    UI_THEME_HEADER_HALO,
    UI_THEME_HEADER_STRIP,
    UI_THEME_HEADER_AXIS
} ui_theme_header_style_t;

typedef struct
{
    const char *name;
    ui_theme_base_style_t base_style;
    ui_theme_header_style_t header;
} ui_theme_t;

typedef struct
{
    ui_theme_frame_style_t page_frame;
    ui_theme_frame_style_t card_frame;
    ui_theme_focus_style_t focus;
    const font_t *title_font;
    const font_t *header_font;
    const font_t *label_font;
    uint8_t title_x_pad;
    uint8_t content_x_pad;
    uint8_t separator;
} ui_theme_base_t;

typedef struct
{
    const char *track;
    const char *track_name;
    const char *hall_mode;
    const char *hall_suffix;
    const char *ensemble;
    const char *bpm;
    const char *cpu_load;
    const char *pattern;
    uint8_t bpm_external;
    uint8_t ensemble_page_index;
    uint8_t ensemble_page_count;
} ui_theme_header_data_t;

void ui_theme_init(void);
const ui_theme_t *ui_theme_get(void);
const ui_theme_base_t *ui_theme_get_base(void);
ui_theme_id_t ui_theme_get_id(void);
const char *ui_theme_name(ui_theme_id_t id);
uint8_t ui_theme_set(ui_theme_id_t id);
void ui_theme_preview(ui_theme_id_t id);
uint8_t ui_theme_commit_preview(void);
uint8_t ui_theme_show_cpu_load(void);
uint8_t ui_theme_set_show_cpu_load(uint8_t show);

void ui_theme_draw_frame(int x, int y, int w, int h, ui_theme_frame_style_t style);
void ui_theme_draw_card_frame(int x, int y, int w, int h);
void ui_theme_draw_focus(int x, int y, int w, int h, const char *text);
void ui_theme_draw_focus_at(int x, int y, int w, int h, const char *text,
                            uint8_t text_x, uint8_t text_y);
void ui_theme_draw_header(const ui_theme_header_data_t *data);
void ui_theme_draw_page_title(const char *title, const char *context, uint8_t line_y);

#endif /* UI_THEME_H */
