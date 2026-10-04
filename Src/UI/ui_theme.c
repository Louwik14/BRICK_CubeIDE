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
#define UI_THEME_PREF_VERSION 2U
#define UI_THEME_PREF_LEGACY_VERSION 1U

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

static const ui_theme_base_t g_ui_theme_bases[] = {
    {UI_THEME_FRAME_CLASSIC, UI_THEME_FRAME_NONE, UI_THEME_FOCUS_CLASSIC,
     &FONT_5X7, &FONT_4X6, &FONT_4X6, 2U, 1U, 0U},
    {UI_THEME_FRAME_LINE, UI_THEME_FRAME_LINE, UI_THEME_FOCUS_UNDERLINE,
     &FONT_5X7, &FONT_4X6, &FONT_4X6, 1U, 2U, 0U},
};

static const ui_theme_t g_ui_themes[UI_THEME_COUNT] = {
    {"CLASSIC",    UI_THEME_BASE_CLASSIC,    UI_THEME_HEADER_CLASSIC},
    {"DECK",       UI_THEME_BASE_CLASSIC,    UI_THEME_HEADER_DECK},
    {"HALO",       UI_THEME_BASE_CLASSIC,    UI_THEME_HEADER_HALO},
    {"MINIMALIST", UI_THEME_BASE_MINIMALIST, UI_THEME_HEADER_MINIMALIST},
    {"STRIP",      UI_THEME_BASE_MINIMALIST, UI_THEME_HEADER_STRIP},
    {"AXIS",       UI_THEME_BASE_MINIMALIST, UI_THEME_HEADER_AXIS},
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
        && ((prefs->version == UI_THEME_PREF_VERSION)
            || (prefs->version == UI_THEME_PREF_LEGACY_VERSION))
        && (prefs->size == sizeof(*prefs))
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
        if (prefs.version == UI_THEME_PREF_VERSION)
            g_ui_theme_id = (prefs.theme_id < (uint8_t)UI_THEME_COUNT)
                ? (ui_theme_id_t)prefs.theme_id : UI_THEME_CLASSIC;
        else
            g_ui_theme_id = (prefs.theme_id == 1U)
                ? UI_THEME_MINIMALIST : UI_THEME_CLASSIC;
        g_ui_show_cpu_load = prefs.show_cpu_load;
    }
}

const ui_theme_t *ui_theme_get(void) { return &g_ui_themes[g_ui_theme_id]; }
const ui_theme_base_t *ui_theme_get_base(void)
{
    return &g_ui_theme_bases[ui_theme_get()->base_style];
}
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
        default: break;
    }
}

void ui_theme_draw_card_frame(int x, int y, int w, int h)
{
    ui_theme_draw_frame(x, y, w, h, ui_theme_get_base()->card_frame);
}

void ui_theme_draw_focus_at(int x, int y, int w, int h, const char *text,
                            uint8_t text_x, uint8_t text_y)
{
    const ui_theme_base_t *base = ui_theme_get_base();
    drv_display_set_font(base->label_font);
    int tx = text_x;
    switch (base->focus)
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
    }
}

void ui_theme_draw_focus(int x, int y, int w, int h, const char *text)
{
    drv_display_set_font(ui_theme_get_base()->label_font);
    ui_theme_draw_focus_at(x, y, w, h, text,
                           (uint8_t)ui_theme_center_x(x, w, text), (uint8_t)(y + 2));
}

static uint8_t ui_theme_right_x(const char *text)
{
    const uint8_t width = drv_display_text_width(text);
    return (width < OLED_WIDTH) ? (uint8_t)(OLED_WIDTH - width) : 0U;
}

static void ui_theme_fit_copy(char *out, uint32_t out_len, const char *text,
                              uint8_t max_px, const font_t *font)
{
    if ((out == NULL) || (out_len == 0U)) return;
    (void)snprintf(out, out_len, "%s", (text != NULL) ? text : "");
    drv_display_set_font(font);
    uint32_t len = (uint32_t)strlen(out);
    while ((len > 1U) && (drv_display_text_width(out) > max_px))
    {
        if (len > 2U) out[len - 2U] = '.';
        out[len - 1U] = '\0';
        len--;
    }
}

static void ui_theme_draw_bpm(uint8_t x, uint8_t y,
                              const ui_theme_header_data_t *data)
{
    if ((data->bpm == NULL) || (data->bpm[0] == '\0')) return;
    if (data->bpm_external != 0U)
    {
        const uint8_t width = drv_display_text_width(data->bpm);
        drv_display_fill_rect((x > 0U) ? (uint8_t)(x - 1U) : 0U,
                              (y > 0U) ? (uint8_t)(y - 1U) : 0U,
                              (uint8_t)(width + 2U), 8U);
        drv_display_draw_text_inverted(x, y, data->bpm);
    }
    else
    {
        drv_display_draw_text(x, y, data->bpm);
    }
}

void ui_theme_draw_header(const ui_theme_header_data_t *d)
{
    if (d == NULL) return;
    const ui_theme_t *theme = ui_theme_get();
    drv_display_set_font(ui_theme_get_base()->header_font);
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
        case UI_THEME_HEADER_MINIMALIST:
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
        case UI_THEME_HEADER_DECK:
        {
            char track_name[12], hall[12], ensemble[16], cpu[12];
            ui_theme_fit_copy(track_name, sizeof(track_name), d->track_name, 28U, &FONT_4X6);
            ui_theme_fit_copy(hall, sizeof(hall), d->hall_mode, 28U, &FONT_4X6);
            ui_theme_fit_copy(ensemble, sizeof(ensemble), d->ensemble, 34U, &FONT_5X7);
            ui_theme_fit_copy(cpu, sizeof(cpu), d->cpu_load, 21U, &FONT_4X6);
            drv_display_draw_rect(0, 0, 128, 15);
            drv_display_draw_line(14, 0, 14, 14);
            drv_display_draw_line(47, 0, 47, 14);
            drv_display_draw_line(86, 0, 86, 14);
            drv_display_fill_rect(1, 1, 13, 13);
            drv_display_set_font(&FONT_4X6);
            drv_display_draw_text_inverted(4U, 4U, d->track);
            drv_display_draw_text(17U, 1U, track_name);
            drv_display_draw_text(17U, 8U, hall);
            drv_display_set_font(&FONT_5X7);
            drv_display_draw_text((uint8_t)ui_theme_center_x(48, 38, ensemble), 4U, ensemble);
            drv_display_set_font(&FONT_4X6);
            ui_theme_draw_bpm(89U, 1U, d);
            if (cpu[0] != '\0') drv_display_draw_text(89U, 8U, cpu);
            drv_display_draw_text(ui_theme_right_x(d->pattern), 8U, d->pattern);
            break;
        }
        case UI_THEME_HEADER_HALO:
        {
            char track_name[12], hall[12], ensemble[16], cpu[12];
            ui_theme_fit_copy(track_name, sizeof(track_name), d->track_name, 25U, &FONT_4X6);
            ui_theme_fit_copy(hall, sizeof(hall), d->hall_mode, 32U, &FONT_4X6);
            ui_theme_fit_copy(ensemble, sizeof(ensemble), d->ensemble, 52U, &FONT_5X7);
            ui_theme_fit_copy(cpu, sizeof(cpu), d->cpu_load, 20U, &FONT_4X6);
            drv_display_draw_line(0, 0, 35, 0);
            drv_display_draw_line(92, 0, 127, 0);
            drv_display_draw_line(0, 14, 35, 14);
            drv_display_draw_line(92, 14, 127, 14);
            drv_display_set_font(&FONT_5X7);
            drv_display_draw_text((uint8_t)ui_theme_center_x(36, 56, ensemble), 1U, ensemble);
            drv_display_set_font(&FONT_4X6);
            drv_display_draw_text(0U, 2U, d->track);
            drv_display_draw_text(9U, 2U, track_name);
            drv_display_draw_text(0U, 9U, hall);
            ui_theme_draw_bpm(ui_theme_right_x(d->bpm), 2U, d);
            if (cpu[0] != '\0') drv_display_draw_text(72U, 9U, cpu);
            drv_display_draw_text(ui_theme_right_x(d->pattern), 9U, d->pattern);
            break;
        }
        case UI_THEME_HEADER_STRIP:
        {
            char track_name[12], hall[12], ensemble[16], cpu[12];
            ui_theme_fit_copy(track_name, sizeof(track_name), d->track_name, 34U, &FONT_4X6);
            ui_theme_fit_copy(hall, sizeof(hall), d->hall_mode, 35U, &FONT_4X6);
            ui_theme_fit_copy(ensemble, sizeof(ensemble), d->ensemble, 32U, &FONT_4X6);
            ui_theme_fit_copy(cpu, sizeof(cpu), d->cpu_load, 24U, &FONT_4X6);
            drv_display_set_font(&FONT_4X6);
            drv_display_draw_text(0U, 1U, d->track);
            drv_display_draw_text(8U, 1U, track_name);
            drv_display_draw_text(48U, 1U, ensemble);
            ui_theme_draw_bpm(ui_theme_right_x(d->bpm), 1U, d);
            drv_display_draw_line(0, 8, 127, 8);
            drv_display_draw_text(0U, 10U, hall);
            if (cpu[0] != '\0')
            {
                drv_display_draw_text(42U, 10U, d->pattern);
                drv_display_draw_text(ui_theme_right_x(cpu), 10U, cpu);
            }
            else
            {
                drv_display_draw_text(ui_theme_right_x(d->pattern), 10U, d->pattern);
            }
            break;
        }
        case UI_THEME_HEADER_AXIS:
        {
            char track_name[12], hall[12], ensemble[16], cpu[12];
            ui_theme_fit_copy(track_name, sizeof(track_name), d->track_name, 38U, &FONT_4X6);
            ui_theme_fit_copy(hall, sizeof(hall), d->hall_mode, 28U, &FONT_4X6);
            ui_theme_fit_copy(ensemble, sizeof(ensemble), d->ensemble, 38U, &FONT_5X7);
            ui_theme_fit_copy(cpu, sizeof(cpu), d->cpu_load, 24U, &FONT_4X6);
            drv_display_fill_rect(0, 0, 2, 15);
            drv_display_set_font(&FONT_5X7);
            drv_display_draw_text(6U, 1U, ensemble);
            drv_display_set_font(&FONT_4X6);
            drv_display_draw_text(6U, 9U, track_name);
            drv_display_draw_line(47, 0, 47, 14);
            drv_display_draw_text(52U, 1U, d->track);
            drv_display_draw_text(62U, 1U, hall);
            ui_theme_draw_bpm(ui_theme_right_x(d->bpm), 1U, d);
            if (cpu[0] != '\0')
            {
                drv_display_draw_text(52U, 9U, d->pattern);
                drv_display_draw_text(ui_theme_right_x(cpu), 9U, cpu);
            }
            else
            {
                drv_display_draw_text(ui_theme_right_x(d->pattern), 9U, d->pattern);
            }
            break;
        }
    }
}

void ui_theme_draw_page_title(const char *title, const char *context, uint8_t line_y)
{
    const ui_theme_base_t *base = ui_theme_get_base();
    if (ui_theme_get()->base_style == UI_THEME_BASE_CLASSIC)
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
    drv_display_set_font(base->title_font);
    drv_display_draw_text(base->title_x_pad, 0U, title);
    if ((context != NULL) && (context[0] != '\0'))
    {
        drv_display_set_font(base->header_font);
        drv_display_draw_text(ui_theme_right_x(context), 0U, context);
    }
    ui_theme_draw_frame(0, 0, OLED_WIDTH, (int)line_y + 1, base->page_frame);
    if (base->separator != 0U || base->page_frame == UI_THEME_FRAME_LINE)
        drv_display_draw_line(0, line_y, 127, line_y);
}
