#include "UI/ui_theme.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "drv_display.h"
#include "Storage/persistent_fatfs_io.h"
#include "Storage/sd_access_gate.h"
#include "UI/ui_renderer_oled.h"

#define UI_THEME_PREF_PATH "0:/BRICK/UI_PREFS.BIN"
#define UI_THEME_PREF_TMP  "0:/BRICK/UI_PREFS.TMP"
#define UI_THEME_PREF_BAK  "0:/BRICK/UI_PREFS.BAK"
#define UI_THEME_PREF_MAGIC 0x46504955UL
#define UI_THEME_PREF_VERSION 1U

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t theme_id;
    uint8_t show_cpu_load;
    uint8_t reserved[10];
    uint32_t crc32;
} ui_theme_preferences_t;

static const ui_theme_t g_ui_themes[UI_THEME_COUNT] = {
    {"CLASSIC",  UI_THEME_FRAME_CLASSIC, UI_THEME_FRAME_NONE,     UI_THEME_FOCUS_CLASSIC,   UI_THEME_HEADER_CLASSIC,  &FONT_5X7,        &FONT_4X6,        &FONT_4X6, 2U, 1U, 0U},
    {"MINIMAL",  UI_THEME_FRAME_LINE,    UI_THEME_FRAME_LINE,     UI_THEME_FOCUS_UNDERLINE, UI_THEME_HEADER_MINIMAL,  &FONT_5X7,        &FONT_4X6,        &FONT_4X6, 1U, 2U, 0U},
    {"GRID",     UI_THEME_FRAME_GRID,    UI_THEME_FRAME_GRID,     UI_THEME_FOCUS_BLOCK,     UI_THEME_HEADER_GRID,     &FONT_5X7,        &FONT_4X6,        &FONT_4X6, 2U, 1U, 1U},
    {"TERMINAL", UI_THEME_FRAME_BRACKETS,UI_THEME_FRAME_BRACKETS, UI_THEME_FOCUS_BRACKETS,  UI_THEME_HEADER_TERMINAL, &FONT_4X6,        &FONT_4X6,        &FONT_4X6, 1U, 1U, 0U},
    {"MODERN",   UI_THEME_FRAME_RAIL,    UI_THEME_FRAME_RAIL,     UI_THEME_FOCUS_FLAG,      UI_THEME_HEADER_MODERN,   &FONT_5X7,        &FONT_4X6,        &FONT_4X6, 3U, 2U, 1U},
    {"STUDIO",   UI_THEME_FRAME_TICKS,   UI_THEME_FRAME_GRID,     UI_THEME_FOCUS_SIDEBAR,   UI_THEME_HEADER_STUDIO,   &FONT_4X6,        &FONT_4X6,        &FONT_4X6, 1U, 1U, 1U},
    {"BRUTAL",   UI_THEME_FRAME_HEAVY,   UI_THEME_FRAME_HEAVY,    UI_THEME_FOCUS_OUTLINE,   UI_THEME_HEADER_BRUTAL,   &FONT_5X7,        &FONT_5X7,        &FONT_4X6, 2U, 1U, 1U},
    {"NINETIES", UI_THEME_FRAME_DOUBLE,  UI_THEME_FRAME_DOUBLE,   UI_THEME_FOCUS_CHEVRON,   UI_THEME_HEADER_NINETIES, &FONT_4X6,        &FONT_4X6,        &FONT_4X6, 1U, 1U, 1U},
    {"CONTRAST", UI_THEME_FRAME_BLOCKS,  UI_THEME_FRAME_BLOCKS,   UI_THEME_FOCUS_TOP,       UI_THEME_HEADER_CONTRAST, &FONT_5X7,        &FONT_4X6,        &FONT_4X6, 2U, 1U, 1U},
    {"AIR",      UI_THEME_FRAME_DOTS,    UI_THEME_FRAME_DOTS,     UI_THEME_FOCUS_SPACED,    UI_THEME_HEADER_AIR,      &FONT_5X7,        &FONT_4X6,        &FONT_4X6, 4U, 3U, 0U},
};

static ui_theme_id_t g_ui_theme_id = UI_THEME_CLASSIC;
static uint8_t g_ui_show_cpu_load = 1U;

static uint32_t ui_theme_crc(const uint8_t *data, uint32_t size)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0U; i < size; ++i)
    {
        crc ^= data[i];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1) ^ ((0U - (crc & 1U)) & 0xEDB88320UL);
    }
    return ~crc;
}

static uint8_t ui_theme_preferences_valid(const ui_theme_preferences_t *prefs)
{
    return (uint8_t)((prefs != NULL)
        && (prefs->magic == UI_THEME_PREF_MAGIC)
        && (prefs->version == UI_THEME_PREF_VERSION)
        && (prefs->size == sizeof(*prefs))
        && (prefs->theme_id < (uint8_t)UI_THEME_COUNT)
        && (prefs->show_cpu_load <= 1U)
        && (prefs->crc32 == ui_theme_crc((const uint8_t *)prefs,
                                         offsetof(ui_theme_preferences_t, crc32))));
}

static uint8_t ui_theme_preferences_save(void)
{
    ui_theme_preferences_t prefs = {
        .magic = UI_THEME_PREF_MAGIC,
        .version = UI_THEME_PREF_VERSION,
        .size = sizeof(ui_theme_preferences_t),
        .theme_id = (uint8_t)g_ui_theme_id,
        .show_cpu_load = g_ui_show_cpu_load,
        .reserved = {0},
        .crc32 = 0U,
    };
    prefs.crc32 = ui_theme_crc((const uint8_t *)&prefs,
                               offsetof(ui_theme_preferences_t, crc32));
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_BACKGROUND) == 0U) return 0U;
    uint8_t ok = 0U;
    persistent_fatfs_file_t file;
    if (sd_access_fs_mount_if_needed() != 0U)
    {
        (void)f_mkdir("0:/BRICK");
        (void)f_unlink(UI_THEME_PREF_TMP);
        if (persistent_fatfs_open_write(&file, UI_THEME_PREF_TMP) != 0U)
        {
            UINT written = 0U;
            ok = (uint8_t)((f_write(&file.file, &prefs, sizeof(prefs), &written) == FR_OK)
                && (written == sizeof(prefs)) && (f_sync(&file.file) == FR_OK)
                && (persistent_fatfs_close_result(&file) == FR_OK));
            if (ok != 0U)
                ok = (uint8_t)(persistent_fatfs_commit_replace(UI_THEME_PREF_PATH,
                    UI_THEME_PREF_TMP, UI_THEME_PREF_BAK) == FR_OK);
        }
        if (ok == 0U) (void)f_unlink(UI_THEME_PREF_TMP);
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_BACKGROUND);
    return ok;
}

void ui_theme_init(void)
{
    g_ui_theme_id = UI_THEME_CLASSIC;
    g_ui_show_cpu_load = 1U;
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_BACKGROUND) == 0U) return;
    ui_theme_preferences_t prefs;
    uint8_t loaded = 0U;
    persistent_fatfs_file_t file;
    if (sd_access_fs_mount_if_needed() != 0U)
    {
        (void)persistent_fatfs_recover_replace(UI_THEME_PREF_PATH,
                                               UI_THEME_PREF_TMP,
                                               UI_THEME_PREF_BAK);
        if ((persistent_fatfs_open_read(&file, UI_THEME_PREF_PATH) != 0U)
            && (file.size == sizeof(prefs)))
        {
            UINT read = 0U;
            loaded = (uint8_t)((f_read(&file.file, &prefs, sizeof(prefs), &read) == FR_OK)
                && (read == sizeof(prefs)));
            persistent_fatfs_close(&file);
        }
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_BACKGROUND);
    if ((loaded != 0U) && (ui_theme_preferences_valid(&prefs) != 0U))
    {
        g_ui_theme_id = (ui_theme_id_t)prefs.theme_id;
        g_ui_show_cpu_load = prefs.show_cpu_load;
    }
}

const ui_theme_t *ui_theme_get(void) { return &g_ui_themes[g_ui_theme_id]; }
ui_theme_id_t ui_theme_get_id(void) { return g_ui_theme_id; }
const char *ui_theme_name(ui_theme_id_t id)
{
    return (id < UI_THEME_COUNT) ? g_ui_themes[id].name : g_ui_themes[UI_THEME_CLASSIC].name;
}

uint8_t ui_theme_set(ui_theme_id_t id)
{
    if (id >= UI_THEME_COUNT) id = UI_THEME_CLASSIC;
    if (id == g_ui_theme_id) return 1U;
    g_ui_theme_id = id;
    ui_renderer_oled_invalidate();
    return ui_theme_preferences_save();
}

void ui_theme_preview(ui_theme_id_t id)
{
    if (id >= UI_THEME_COUNT) id = UI_THEME_CLASSIC;
    if (id == g_ui_theme_id) return;
    g_ui_theme_id = id;
    ui_renderer_oled_invalidate();
}

uint8_t ui_theme_commit_preview(void)
{
    return ui_theme_preferences_save();
}

uint8_t ui_theme_show_cpu_load(void) { return g_ui_show_cpu_load; }
uint8_t ui_theme_set_show_cpu_load(uint8_t show)
{
    show = (show != 0U) ? 1U : 0U;
    if (show == g_ui_show_cpu_load) return 1U;
    g_ui_show_cpu_load = show;
    ui_renderer_oled_invalidate();
    return ui_theme_preferences_save();
}

static int ui_theme_center_x(int x, int w, const char *text)
{
    const int tw = (int)drv_display_text_width(text);
    return x + ((w > tw) ? ((w - tw) / 2) : 0);
}

void ui_theme_draw_frame(int x, int y, int w, int h, ui_theme_frame_style_t style)
{
    if ((w < 2) || (h < 2) || (style == UI_THEME_FRAME_NONE)) return;
    switch (style)
    {
        case UI_THEME_FRAME_CLASSIC:
            drv_display_draw_line(x + 2, y, x + w - 3, y);
            drv_display_draw_line(x + 2, y + h - 1, x + w - 3, y + h - 1);
            drv_display_draw_line(x, y + 2, x, y + h - 3);
            drv_display_draw_line(x + w - 1, y + 2, x + w - 1, y + h - 3);
            drv_display_draw_line(x, y + 2, x + 2, y);
            drv_display_draw_line(x + w - 3, y, x + w - 1, y + 2);
            drv_display_draw_line(x, y + h - 3, x + 2, y + h - 1);
            drv_display_draw_line(x + w - 3, y + h - 1, x + w - 1, y + h - 3);
            break;
        case UI_THEME_FRAME_LINE:
            drv_display_draw_line(x + 2, y, x + w - 3, y);
            drv_display_draw_pixel(x, y, true);
            drv_display_draw_pixel(x + w - 1, y, true);
            break;
        case UI_THEME_FRAME_GRID:
            drv_display_draw_rect(x, y, w, h);
            if (h > 9) drv_display_draw_line(x, y + h - 8, x + w - 1, y + h - 8);
            break;
        case UI_THEME_FRAME_BRACKETS:
            drv_display_draw_line(x, y, x + 4, y);
            drv_display_draw_line(x, y, x, y + 4);
            drv_display_draw_line(x + w - 5, y, x + w - 1, y);
            drv_display_draw_line(x + w - 1, y, x + w - 1, y + 4);
            drv_display_draw_line(x, y + h - 5, x, y + h - 1);
            drv_display_draw_line(x, y + h - 1, x + 4, y + h - 1);
            drv_display_draw_line(x + w - 5, y + h - 1, x + w - 1, y + h - 1);
            drv_display_draw_line(x + w - 1, y + h - 5, x + w - 1, y + h - 1);
            break;
        case UI_THEME_FRAME_RAIL:
            drv_display_fill_rect(x, y + 2, 2, h - 4);
            drv_display_draw_line(x + 2, y, x + (w / 2), y);
            drv_display_draw_line(x + 2, y + h - 1, x + w - 5, y + h - 1);
            drv_display_draw_pixel(x + w - 2, y + h - 1, true);
            break;
        case UI_THEME_FRAME_TICKS:
            drv_display_draw_line(x, y, x + w - 1, y);
            drv_display_draw_line(x, y + h - 1, x + w - 1, y + h - 1);
            drv_display_draw_line(x, y, x, y + 3);
            drv_display_draw_line(x + w - 1, y, x + w - 1, y + 3);
            drv_display_draw_pixel(x + (w / 2), y + h - 2, true);
            break;
        case UI_THEME_FRAME_HEAVY:
            drv_display_fill_rect(x, y, w, 2);
            drv_display_fill_rect(x, y + h - 2, w, 2);
            drv_display_draw_line(x, y + 2, x, y + h - 3);
            drv_display_draw_line(x + w - 1, y + 2, x + w - 1, y + h - 3);
            break;
        case UI_THEME_FRAME_DOUBLE:
            drv_display_draw_rect(x, y, w, h);
            if ((w > 5) && (h > 5)) drv_display_draw_rect(x + 2, y + 2, w - 4, h - 4);
            break;
        case UI_THEME_FRAME_BLOCKS:
            drv_display_fill_rect(x, y, 5, 2);
            drv_display_fill_rect(x + w - 5, y, 5, 2);
            drv_display_fill_rect(x, y + h - 2, 9, 2);
            drv_display_fill_rect(x + w - 3, y + h - 2, 3, 2);
            break;
        case UI_THEME_FRAME_DOTS:
            for (int px = x + 2; px < x + w - 1; px += 4)
                drv_display_draw_pixel(px, y, true);
            drv_display_draw_pixel(x, y + h - 1, true);
            drv_display_draw_pixel(x + w - 1, y + h - 1, true);
            break;
        default: break;
    }
}

void ui_theme_draw_card_frame(int x, int y, int w, int h)
{
    ui_theme_draw_frame(x, y, w, h, ui_theme_get()->card_frame);
}

void ui_theme_draw_focus_at(int x, int y, int w, int h, const char *text,
                            uint8_t text_x, uint8_t text_y)
{
    const ui_theme_t *theme = ui_theme_get();
    drv_display_set_font(theme->label_font);
    int tx = text_x;
    switch (theme->focus)
    {
        case UI_THEME_FOCUS_CLASSIC:
            drv_display_fill_rect(x, y, w, h);
            drv_display_draw_pixel(x, y, false);
            drv_display_draw_pixel(x + 1, y, false);
            drv_display_draw_pixel(x, y + 1, false);
            drv_display_draw_pixel(x + w - 1, y, false);
            drv_display_draw_pixel(x + w - 2, y, false);
            drv_display_draw_pixel(x + w - 1, y + 1, false);
            drv_display_draw_pixel(x, y + h - 1, false);
            drv_display_draw_pixel(x + 1, y + h - 1, false);
            drv_display_draw_pixel(x, y + h - 2, false);
            drv_display_draw_pixel(x + w - 1, y + h - 1, false);
            drv_display_draw_pixel(x + w - 2, y + h - 1, false);
            drv_display_draw_pixel(x + w - 1, y + h - 2, false);
            drv_display_draw_text_inverted((uint8_t)tx, text_y, text);
            break;
        case UI_THEME_FOCUS_UNDERLINE:
            drv_display_draw_text((uint8_t)tx, text_y, text);
            drv_display_draw_line(x + 3, y + h - 1, x + w - 4, y + h - 1);
            drv_display_draw_pixel(x + 1, y + h - 2, true);
            drv_display_draw_pixel(x + w - 2, y + h - 2, true);
            break;
        case UI_THEME_FOCUS_BLOCK:
            drv_display_fill_rect(x, y, w, h);
            drv_display_draw_text_inverted((uint8_t)tx, text_y, text);
            break;
        case UI_THEME_FOCUS_BRACKETS:
            ui_theme_draw_frame(x, y, w, h, UI_THEME_FRAME_BRACKETS);
            drv_display_draw_text((uint8_t)tx, text_y, text);
            break;
        case UI_THEME_FOCUS_FLAG:
            drv_display_fill_rect(x, y, w - 4, h);
            drv_display_draw_line(x + w - 4, y, x + w - 1, y + (h / 2));
            drv_display_draw_line(x + w - 1, y + (h / 2), x + w - 4, y + h - 1);
            tx = ui_theme_center_x(x, w - 4, text);
            drv_display_draw_text_inverted((uint8_t)tx, text_y, text);
            break;
        case UI_THEME_FOCUS_SIDEBAR:
            drv_display_fill_rect(x, y, 3, h);
            drv_display_draw_text((uint8_t)((tx < x + 5) ? x + 5 : tx), text_y, text);
            break;
        case UI_THEME_FOCUS_OUTLINE:
            drv_display_draw_rect(x, y, w, h);
            drv_display_draw_rect(x + 2, y + 2, w - 4, h - 4);
            drv_display_draw_text((uint8_t)tx, text_y, text);
            break;
        case UI_THEME_FOCUS_CHEVRON:
            drv_display_draw_text((uint8_t)x, text_y, ">");
            drv_display_draw_text((uint8_t)((tx < x + 7) ? x + 7 : tx), text_y, text);
            drv_display_draw_text((uint8_t)(x + w - 5), text_y, "<");
            break;
        case UI_THEME_FOCUS_TOP:
            drv_display_fill_rect(x, y, w, 3);
            drv_display_draw_text((uint8_t)tx, text_y, text);
            break;
        case UI_THEME_FOCUS_SPACED:
            drv_display_draw_pixel(x + 1, y + (h / 2), true);
            drv_display_draw_pixel(x + w - 2, y + (h / 2), true);
            drv_display_draw_line(x + 7, y + h - 1, x + w - 8, y + h - 1);
            drv_display_draw_text((uint8_t)tx, text_y, text);
            break;
    }
}

void ui_theme_draw_focus(int x, int y, int w, int h, const char *text)
{
    drv_display_set_font(ui_theme_get()->label_font);
    ui_theme_draw_focus_at(x, y, w, h, text,
                           (uint8_t)ui_theme_center_x(x, w, text), (uint8_t)(y + 2));
}

static uint8_t ui_theme_right_x(const char *text)
{
    const uint8_t width = drv_display_text_width(text);
    return (width < OLED_WIDTH) ? (uint8_t)(OLED_WIDTH - width) : 0U;
}

void ui_theme_draw_header(const ui_theme_header_data_t *d)
{
    if (d == NULL) return;
    const ui_theme_t *theme = ui_theme_get();
    drv_display_set_font(theme->header_font);
    switch (theme->header)
    {
        case UI_THEME_HEADER_CLASSIC:
            ui_theme_draw_frame(0, 0, 15, 15, UI_THEME_FRAME_CLASSIC);
            drv_display_draw_pixel(2, 12, true);
            drv_display_draw_pixel(12, 12, true);
            drv_display_fill_rect(2, 2, 11, 9);
            drv_display_set_font(&FONT_5X7);
            drv_display_draw_text_inverted((uint8_t)(ui_theme_center_x(2, 11, d->track) - 1), 3U, d->track);
            drv_display_set_font(&FONT_4X6);
            drv_display_draw_text(17U, 1U, d->track_name);
            drv_display_draw_text(17U, 9U, d->hall_mode);
            if ((d->hall_suffix != NULL) && (d->hall_suffix[0] != '\0')) drv_display_draw_text(33U, 9U, d->hall_suffix);
            ui_theme_draw_frame(43, 0, 42, 15, UI_THEME_FRAME_CLASSIC);
            drv_display_set_font(&FONT_5X7);
            if (drv_display_text_width(d->ensemble) > 38U) drv_display_set_font(&FONT_4X6);
            drv_display_draw_text((uint8_t)ui_theme_center_x(43, 42, d->ensemble), 4U, d->ensemble);
            drv_display_set_font(&FONT_4X6);
            if ((d->bpm != NULL) && (d->bpm[0] != '\0'))
            {
                const uint8_t x = ui_theme_right_x(d->bpm);
                if (d->bpm_external != 0U) { drv_display_fill_rect(x - 1, 0, drv_display_text_width(d->bpm) + 2, 9); drv_display_draw_text_inverted(x, 1, d->bpm); }
                else drv_display_draw_text(x, 1U, d->bpm);
            }
            if ((d->cpu_load != NULL) && (d->cpu_load[0] != '\0'))
            {
                const uint8_t cpu_w = drv_display_text_width(d->cpu_load);
                const uint8_t bpm_x = ((d->bpm != NULL) && (d->bpm[0] != '\0')) ? ui_theme_right_x(d->bpm) : OLED_WIDTH;
                uint8_t cpu_x = (uint8_t)(108U - cpu_w);
                if ((uint8_t)(cpu_x + cpu_w + 1U) > bpm_x) cpu_x = (bpm_x > cpu_w + 1U) ? (uint8_t)(bpm_x - cpu_w - 1U) : 0U;
                if (cpu_x >= 2U) cpu_x -= 2U;
                drv_display_draw_text(cpu_x, 1U, d->cpu_load);
            }
            drv_display_draw_text(ui_theme_right_x(d->pattern), 8U, d->pattern);
            break;
        case UI_THEME_HEADER_MINIMAL:
            drv_display_set_font(&FONT_5X7);
            drv_display_draw_text(0U, 1U, d->track);
            drv_display_draw_text(10U, 1U, d->ensemble);
            drv_display_set_font(&FONT_4X6);
            drv_display_draw_text(0U, 9U, d->track_name);
            drv_display_draw_text(50U, 9U, d->hall_mode);
            if ((d->cpu_load != NULL) && d->cpu_load[0] != '\0') drv_display_draw_text(86U, 9U, d->cpu_load);
            drv_display_draw_text(ui_theme_right_x(d->bpm), 1U, d->bpm);
            drv_display_draw_text(ui_theme_right_x(d->pattern), 9U, d->pattern);
            break;
        case UI_THEME_HEADER_GRID:
            drv_display_draw_rect(0, 0, 128, 15);
            drv_display_draw_line(14, 0, 14, 14);
            drv_display_draw_line(47, 0, 47, 14);
            drv_display_draw_line(86, 0, 86, 14);
            drv_display_fill_rect(1, 1, 13, 13);
            drv_display_draw_text_inverted(4U, 4U, d->track);
            drv_display_draw_text(17U, 1U, d->track_name);
            drv_display_draw_text(17U, 8U, d->hall_mode);
            drv_display_set_font(&FONT_5X7);
            drv_display_draw_text((uint8_t)ui_theme_center_x(48, 38, d->ensemble), 4U, d->ensemble);
            drv_display_set_font(&FONT_4X6);
            drv_display_draw_text(89U, 1U, d->bpm);
            if ((d->cpu_load != NULL) && d->cpu_load[0] != '\0') drv_display_draw_text(89U, 8U, d->cpu_load);
            drv_display_draw_text(ui_theme_right_x(d->pattern), 8U, d->pattern);
            break;
        case UI_THEME_HEADER_TERMINAL:
            drv_display_set_font(&FONT_4X6);
            drv_display_draw_text(0U, 0U, "[");
            drv_display_draw_text(5U, 0U, d->track);
            drv_display_draw_text(11U, 0U, "]");
            drv_display_draw_text(17U, 0U, d->track_name);
            drv_display_draw_text(0U, 8U, ">");
            drv_display_draw_text(6U, 8U, d->ensemble);
            drv_display_draw_text(54U, 8U, d->hall_mode);
            if ((d->cpu_load != NULL) && d->cpu_load[0] != '\0') drv_display_draw_text(86U, 8U, d->cpu_load);
            drv_display_draw_text(ui_theme_right_x(d->bpm), 0U, d->bpm);
            drv_display_draw_text(ui_theme_right_x(d->pattern), 8U, d->pattern);
            drv_display_draw_line(0, 15, 127, 15);
            break;
        case UI_THEME_HEADER_MODERN:
            drv_display_fill_rect(0, 0, 4, 15);
            drv_display_set_font(&FONT_5X7);
            drv_display_draw_text(8U, 1U, d->ensemble);
            drv_display_set_font(&FONT_4X6);
            drv_display_draw_text(8U, 9U, d->track_name);
            drv_display_fill_rect(52, 0, 2, 15);
            drv_display_draw_text(58U, 1U, d->hall_mode);
            drv_display_draw_text(58U, 9U, d->track);
            drv_display_draw_text(ui_theme_right_x(d->bpm), 1U, d->bpm);
            if ((d->cpu_load != NULL) && d->cpu_load[0] != '\0') drv_display_draw_text(ui_theme_right_x(d->cpu_load), 9U, d->cpu_load);
            drv_display_draw_text(82U, 9U, d->pattern);
            break;
        case UI_THEME_HEADER_STUDIO:
            drv_display_draw_line(0, 0, 127, 0);
            drv_display_draw_line(0, 15, 127, 15);
            drv_display_draw_line(19, 0, 19, 15);
            drv_display_draw_line(82, 0, 82, 15);
            drv_display_fill_rect(1, 2, 17, 11);
            drv_display_draw_text_inverted(4U, 4U, d->track);
            drv_display_draw_text(23U, 1U, d->ensemble);
            drv_display_draw_text(23U, 8U, d->track_name);
            drv_display_draw_text(59U, 8U, d->hall_mode);
            drv_display_draw_text(85U, 1U, d->bpm);
            if ((d->cpu_load != NULL) && d->cpu_load[0] != '\0')
                drv_display_draw_text(85U, 8U, d->cpu_load);
            drv_display_draw_text(ui_theme_right_x(d->pattern), 8U, d->pattern);
            break;
        case UI_THEME_HEADER_BRUTAL:
            drv_display_fill_rect(0, 0, 128, 15);
            drv_display_set_draw_color(0U);
            drv_display_draw_text(2U, 2U, d->ensemble);
            drv_display_draw_text(43U, 2U, d->track_name);
            drv_display_set_font(&FONT_4X6);
            drv_display_draw_text(2U, 9U, d->track);
            drv_display_draw_text(15U, 9U, d->hall_mode);
            drv_display_draw_text(69U, 9U, d->pattern);
            drv_display_draw_text(86U, 2U, d->bpm);
            if ((d->cpu_load != NULL) && d->cpu_load[0] != '\0')
                drv_display_draw_text(86U, 9U, d->cpu_load);
            drv_display_set_draw_color(1U);
            break;
        case UI_THEME_HEADER_NINETIES:
            drv_display_draw_rect(0, 0, 128, 15);
            drv_display_draw_rect(2, 2, 25, 11);
            drv_display_draw_text(5U, 4U, d->track);
            drv_display_draw_text(12U, 4U, d->hall_mode);
            drv_display_draw_text(31U, 1U, d->ensemble);
            drv_display_draw_text(31U, 8U, d->track_name);
            drv_display_draw_line(78, 1, 78, 13);
            drv_display_draw_text(82U, 1U, d->bpm);
            if ((d->cpu_load != NULL) && d->cpu_load[0] != '\0')
                drv_display_draw_text(82U, 8U, d->cpu_load);
            drv_display_draw_text(ui_theme_right_x(d->pattern), 8U, d->pattern);
            break;
        case UI_THEME_HEADER_CONTRAST:
            drv_display_fill_rect(0, 0, 35, 15);
            drv_display_draw_text_inverted(3U, 1U, d->ensemble);
            drv_display_draw_text_inverted(3U, 8U, d->track);
            drv_display_draw_line(38, 0, 38, 14);
            drv_display_draw_text(43U, 1U, d->track_name);
            drv_display_draw_text(43U, 8U, d->hall_mode);
            drv_display_fill_rect(88, 0, 40, 7);
            drv_display_draw_text_inverted(91U, 1U, d->bpm);
            if ((d->cpu_load != NULL) && d->cpu_load[0] != '\0')
                drv_display_draw_text(88U, 9U, d->cpu_load);
            drv_display_draw_text(ui_theme_right_x(d->pattern), 9U, d->pattern);
            break;
        case UI_THEME_HEADER_AIR:
            drv_display_set_font(&FONT_5X7);
            drv_display_draw_text(2U, 1U, d->ensemble);
            drv_display_set_font(&FONT_4X6);
            drv_display_draw_text(2U, 10U, d->track_name);
            drv_display_draw_pixel(39, 7, true);
            drv_display_draw_text(45U, 1U, d->track);
            drv_display_draw_text(54U, 1U, d->hall_mode);
            drv_display_draw_text(ui_theme_right_x(d->bpm), 1U, d->bpm);
            if ((d->cpu_load != NULL) && d->cpu_load[0] != '\0')
                drv_display_draw_text(ui_theme_right_x(d->cpu_load), 10U, d->cpu_load);
            drv_display_draw_text(83U, 10U, d->pattern);
            break;
    }
}

void ui_theme_draw_page_title(const char *title, const char *context, uint8_t line_y)
{
    const ui_theme_t *theme = ui_theme_get();
    if (g_ui_theme_id == UI_THEME_CLASSIC)
    {
        drv_display_set_font(&FONT_5X7);
        drv_display_draw_text(0U, 0U, title);
        if ((context != NULL) && (context[0] != '\0'))
        {
            drv_display_set_font(&FONT_4X6);
            drv_display_draw_text(ui_theme_right_x(context), 0U, context);
        }
        drv_display_draw_line(0, line_y, 127, line_y);
        return;
    }
    drv_display_set_font(theme->title_font);
    if (theme->header == UI_THEME_HEADER_TERMINAL)
    {
        drv_display_draw_text(0U, 0U, ">");
        drv_display_draw_text(7U, 0U, title);
    }
    else
    {
        drv_display_draw_text(theme->title_x_pad, 0U, title);
    }
    if ((context != NULL) && (context[0] != '\0'))
    {
        drv_display_set_font(theme->header_font);
        drv_display_draw_text(ui_theme_right_x(context), 0U, context);
    }
    if (theme->page_frame == UI_THEME_FRAME_GRID)
        drv_display_draw_rect(0, 0, OLED_WIDTH, (int)line_y + 1);
    else
        ui_theme_draw_frame(0, 0, OLED_WIDTH, (int)line_y + 1, theme->page_frame);
    if (theme->separator != 0U || theme->page_frame == UI_THEME_FRAME_LINE)
        drv_display_draw_line(0, line_y, 127, line_y);
}
