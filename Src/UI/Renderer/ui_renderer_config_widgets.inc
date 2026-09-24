enum
{
    UI_CFG_ICON_W = 30,
    UI_CFG_ICON_H = 29
};

static const uint32_t g_cfg_external_icon_1bpp[UI_CFG_ICON_H] = {
    0x00000070U, 0x00000088U, 0x00000088U, 0x00000070U, 0x00000088U,
    0x000000F8U, 0x00000088U, 0x00000088U, 0x0FFE00F8U, 0x0BBA0088U,
    0x0BBA0088U, 0x0BBA0088U, 0x0BBA21FCU, 0x0BBA2104U, 0x0912F904U,
    0x091223FEU, 0x09122202U, 0x09120202U, 0x09120282U, 0x09120282U,
    0x09120282U, 0x0FFE0282U, 0x00000282U, 0x00000242U, 0x00000222U,
    0x00000306U, 0x0000018CU, 0x000000F8U, 0x00000070U,
};

static const uint32_t g_cfg_midi_icon_1bpp[UI_CFG_ICON_H] = {
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x000FFE00U, 0x000BBA00U, 0x000BBA00U,
    0x000BBA00U, 0x000BBA00U, 0x000BBA00U, 0x00091200U, 0x00091200U,
    0x00091200U, 0x00091200U, 0x00091200U, 0x00091200U, 0x00091200U,
    0x000FFE00U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
};

static const uint32_t g_cfg_synth_icon_1bpp[UI_CFG_ICON_H] = {
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
    0x00000000U, 0x3FFFFFFFU, 0x20000881U, 0x2AAAAFBDU, 0x20000881U,
    0x2AAAAFB5U, 0x200008B5U, 0x20000881U, 0x20000881U, 0x3FFFFFFFU,
    0x20000001U, 0x2A8A2A29U, 0x2A8A2A29U, 0x20000001U, 0x2DB6DB6DU,
    0x2DB6DB6DU, 0x2DB6DB6DU, 0x1FFFFFFEU, 0x00000000U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
};

static const uint32_t g_cfg_wave_icon_1bpp[UI_CFG_ICON_H] = {
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x003A9280U, 0x000AAA80U,
    0x003ABA80U, 0x000AAB80U, 0x00392A80U, 0x00000000U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x01010100U, 0x00C2C2C0U, 0x002C2C20U,
    0x02121210U, 0x01858580U, 0x00585840U, 0x00202020U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
};

static const uint32_t g_cfg_sampler_icon_1bpp[UI_CFG_ICON_H] = {
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x0001F000U,
    0x0007FC00U, 0x001FFF00U, 0x003BFF80U, 0x0037FF80U, 0x006DFFC0U,
    0x006BFFC0U, 0x00BBFFE0U, 0x00BF1FE0U, 0x00BF5FE0U, 0x00DF1FE0U,
    0x00EFFAE0U, 0x0077FAC0U, 0x007BF6C0U, 0x003C3D80U, 0x003E3B80U,
    0x001FFF00U, 0x0007FC00U, 0x0001F000U, 0x00000000U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
};

static const uint32_t g_cfg_stream_icon_1bpp[UI_CFG_ICON_H] = {
    0x00000000U, 0x0003FFFCU, 0x00060006U, 0x000DB6C6U, 0x0019B6C6U,
    0x0031B6C6U, 0x0021B6C6U, 0x00200006U, 0x00200006U, 0x00200306U,
    0x00300706U, 0x00101F06U, 0x00107F06U, 0x0030FF06U, 0x0021FF06U,
    0x0020FF06U, 0x00207F06U, 0x00201F06U, 0x00200706U, 0x00200306U,
    0x00200006U, 0x00200006U, 0x00200006U, 0x00200006U, 0x00200006U,
    0x00200006U, 0x003FFFFEU, 0x001FFFFCU, 0x00000000U,
};

static const uint32_t g_cfg_ram_icon_1bpp[UI_CFG_ICON_H] = {
    0x00000000U, 0x00000000U, 0x00666660U, 0x00666660U, 0x01FFFFF0U,
    0x07000010U, 0x0700001CU, 0x0100001CU, 0x01000010U, 0x07000010U,
    0x0700001CU, 0x0100001CU, 0x01226390U, 0x07369490U, 0x072AF39CU,
    0x0122929CU, 0x01229490U, 0x07000010U, 0x0700001CU, 0x0100001CU,
    0x01000010U, 0x07000010U, 0x0700001CU, 0x0100001CU, 0x01FFFFF0U,
    0x00CCCCC0U, 0x00CCCCC0U, 0x00000000U, 0x00000000U,
};

static void ui_renderer_template_draw_cfg_bitmap(int x,
                                                 int y,
                                                 int w,
                                                 int h,
                                                 int x_offset,
                                                 int y_offset,
                                                 const uint32_t bitmap[UI_CFG_ICON_H])
{
    const uint32_t prof_start = DWT->CYCCNT;
    const int icon_x = x + ((w - UI_CFG_ICON_W) / 2) + x_offset;
    const int icon_y = y + ((h - UI_CFG_ICON_H) / 2) + y_offset;

    drv_display_draw_bitmap_1bpp(icon_x,
                                icon_y,
                                UI_CFG_ICON_W,
                                UI_CFG_ICON_H,
                                (const uint8_t *)bitmap);
    ui_render_prof_add_cycles(&g_ui_render_prof.renderer.widget.track_cfg_bitmap,
                              DWT->CYCCNT - prof_start);
}

static uint8_t ui_renderer_template_draw_play_note_text(param_id_t id, int x, int y, int w, int h, float value)
{
    if ((w <= 0) || (h <= 0))
    {
        return 0U;
    }

    char label[8];
    ui_renderer_template_format_value(id, value, label, (uint32_t)sizeof(label));

    const font_t *font = &FONT_HELVB14;
    drv_display_set_font(font);
    if (drv_display_text_width(label) > (uint8_t)((w > 2) ? (w - 2) : w))
    {
        font = &FONT_OFF_COMPACT;
        drv_display_set_font(font);
        ui_renderer_template_fit_text(label, (uint8_t)((w > 2) ? (w - 2) : w));
    }

    return ui_renderer_template_draw_filter_text(x, y, w, h, label, font, 4);
}

static void ui_renderer_template_draw_cfg_piano_icon(int x, int y, int w, int h)
{
    const uint32_t prof_start = DWT->CYCCNT;
    const int key_w = 26;
    const int key_h = 15;
    const int key_x = x + ((w - key_w) / 2);
    const int key_y = y + ((h - key_h) / 2) + 2;
    static const int8_t div_x[] = {4, 8, 12, 15, 19, 23};
    static const int8_t black_x[] = {3, 7, 14, 18, 22};

    drv_display_draw_rect(key_x, key_y, key_w, key_h);
    for (uint8_t i = 0U; i < 6U; ++i)
    {
        drv_display_draw_line(key_x + div_x[i], key_y + 7, key_x + div_x[i], key_y + key_h - 1);
    }
    for (uint8_t i = 0U; i < 5U; ++i)
    {
        drv_display_fill_rect(key_x + black_x[i], key_y + 1, 3, 7);
    }
    ui_render_prof_add_cycles(&g_ui_render_prof.renderer.widget.track_cfg_bitmap,
                              DWT->CYCCNT - prof_start);
}

static void ui_renderer_template_draw_cfg_external_icon(int x, int y, int w, int h)
{
    ui_renderer_template_draw_cfg_bitmap(x, y, w, h, 0, 0, g_cfg_external_icon_1bpp);
}

static uint8_t ui_renderer_template_draw_cfg_midi_text(int x, int y, int w, int h)
{
    const uint32_t prof_start = DWT->CYCCNT;
    static const char label[] = "Midi";

    drv_display_set_font(&FONT_OFF_COMPACT);
    const int text_w = (int)drv_display_text_width(label);
    const int text_h = (int)drv_display_font_height();
    int text_x = x + ((w - text_w) / 2);
    int text_y = y + ((h - text_h) / 2);

    if (text_x < x)
    {
        text_x = x;
    }
    if (text_y < y)
    {
        text_y = y;
    }

    drv_display_draw_text((uint8_t)text_x, (uint8_t)text_y, label);
    drv_display_set_font(&FONT_4X6);
    ui_render_prof_add_cycles(&g_ui_render_prof.renderer.widget.track_cfg_text,
                              DWT->CYCCNT - prof_start);
    return 1U;
}

static void ui_renderer_template_draw_cfg_midi_icon(int x, int y, int w, int h)
{
    ui_renderer_template_draw_cfg_bitmap(x, y, w, h, 0, 0, g_cfg_midi_icon_1bpp);
}

static void ui_renderer_template_draw_cfg_synth_icon(int x, int y, int w, int h)
{
    ui_renderer_template_draw_cfg_bitmap(x, y, w, h, 0, 0, g_cfg_synth_icon_1bpp);
}

static void ui_renderer_template_draw_cfg_wave_icon(int x, int y, int w, int h)
{
    ui_renderer_template_draw_cfg_bitmap(x, y, w, h, 0, 0, g_cfg_wave_icon_1bpp);
}

static void ui_renderer_template_draw_cfg_drum_icon(int x, int y, int w, int h)
{
    const uint32_t prof_start = DWT->CYCCNT;
    const int cx = x + (w / 2);
    const int top = y + 12;
    const int left = cx - 10;
    const int right = cx + 10;
    const int bottom = top + 11;

    drv_display_draw_line(cx - 11, y + 4, cx - 2, y + 10);
    drv_display_draw_line(cx + 11, y + 4, cx + 2, y + 10);
    drv_display_draw_line(cx - 12, y + 4, cx - 10, y + 4);
    drv_display_draw_line(cx + 10, y + 4, cx + 12, y + 4);
    drv_display_draw_line(left + 2, top, right - 2, top);
    drv_display_draw_line(left, top + 2, left, bottom - 3);
    drv_display_draw_line(right, top + 2, right, bottom - 3);
    drv_display_draw_line(left + 2, bottom, right - 2, bottom);
    drv_display_draw_line(left, top + 2, left + 2, top);
    drv_display_draw_line(right - 2, top, right, top + 2);
    drv_display_draw_line(left, bottom - 3, left + 2, bottom);
    drv_display_draw_line(right - 2, bottom, right, bottom - 3);
    drv_display_draw_line(left + 1, top + 5, right - 1, top + 5);
    ui_render_prof_add_cycles(&g_ui_render_prof.renderer.widget.track_cfg_bitmap,
                              DWT->CYCCNT - prof_start);
}

static void ui_renderer_template_draw_cfg_sampler_icon(int x, int y, int w, int h)
{
    ui_renderer_template_draw_cfg_bitmap(x, y, w, h, 0, 1, g_cfg_sampler_icon_1bpp);
}

static void ui_renderer_template_draw_cfg_stream_icon(int x, int y, int w, int h)
{
    ui_renderer_template_draw_cfg_bitmap(x, y, w, h, 4, 0, g_cfg_stream_icon_1bpp);
}

static void ui_renderer_template_draw_cfg_ram_icon(int x, int y, int w, int h)
{
    ui_renderer_template_draw_cfg_bitmap(x, y, w, h, 0, 0, g_cfg_ram_icon_1bpp);
}

static uint8_t ui_renderer_template_draw_cfg_stacked_text(int x,
                                                          int y,
                                                          int w,
                                                          int h,
                                                          const char *top,
                                                          const char *bottom)
{
    const uint32_t prof_start = DWT->CYCCNT;
    if ((top == 0) || (bottom == 0))
    {
        return 0U;
    }

    drv_display_set_font(&FONT_4X6);
    const int font_h = (int)drv_display_font_height();
    const int gap = 1;
    int text_y = y + ((h - ((2 * font_h) + gap)) / 2);
    if (text_y < y)
    {
        text_y = y;
    }
    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, w, top), (uint8_t)text_y, top);
    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, w, bottom), (uint8_t)(text_y + font_h + gap), bottom);
    ui_render_prof_add_cycles(&g_ui_render_prof.renderer.widget.track_cfg_text,
                              DWT->CYCCNT - prof_start);
    return 1U;
}

static track_family_t ui_renderer_template_cfg_visible_family(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx)
{
    (void)plock_frame_ctx;
    return ui_get_track_family(ui_renderer_template_get_edit_owner_track());
}

static uint8_t ui_renderer_template_draw_custom_track_cfg(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                          ui_template_custom_widget_kind_t kind,
                                                          int x,
                                                          int y,
                                                          int w,
                                                          int h,
                                                          float value)
{
    if ((w <= 0) || (h <= 0))
    {
        return 0U;
    }

#define UI_CFG_RETURN_TEXT(...)                                                     \
    do                                                                              \
    {                                                                               \
        const uint32_t cfg_text_prof_start = DWT->CYCCNT;                           \
        const uint8_t cfg_text_result = (__VA_ARGS__);                              \
        ui_render_prof_add_cycles(&g_ui_render_prof.renderer.widget.track_cfg_text, \
                                  DWT->CYCCNT - cfg_text_prof_start);                \
        return cfg_text_result;                                                      \
    } while (0)

    if (kind == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_INACTIVE)
    {
        UI_CFG_RETURN_TEXT(ui_renderer_template_draw_filter_text(x, y, w, h, "-", &FONT_4X6, 0));
    }

    if (kind == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_MIDI_CHANNEL)
    {
        uint8_t channel = (uint8_t)(value + 0.5f);
        if (channel < 1U)
        {
            channel = 1U;
        }
        if (channel > 16U)
        {
            channel = 16U;
        }

        char label[4];
        (void)snprintf(label, sizeof(label), "%u", (unsigned int)channel);
        UI_CFG_RETURN_TEXT(ui_renderer_template_draw_filter_big_text(x, y, w, h, label, &FONT_HELVB14));
    }

    if (kind == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_MIDI_SOURCE)
    {
        static const char *const labels[] = {"Int", "Ext", "All"};
        uint8_t source = (uint8_t)(value + 0.5f);
        if (source >= 3U)
        {
            source = 2U;
        }
        UI_CFG_RETURN_TEXT(ui_renderer_template_draw_filter_big_text(x, y, w, h, labels[source], &FONT_OFF_COMPACT));
    }

    const track_family_t family = ui_renderer_template_cfg_visible_family(plock_frame_ctx);
    if (kind == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_TRACK)
    {
        if (family == TRACK_FAMILY_OFF)
        {
            UI_CFG_RETURN_TEXT(ui_renderer_template_draw_filter_big_text(x, y, w, h, "OFF", &FONT_OFF_COMPACT));
        }
        if (family == TRACK_FAMILY_SYNTH)
        {
            ui_renderer_template_draw_cfg_synth_icon(x, y, w, h);
            return 1U;
        }
        if (family == TRACK_FAMILY_MIDI)
        {
            return ui_renderer_template_draw_cfg_midi_text(x, y, w, h);
        }
        if (family == TRACK_FAMILY_EXTERNAL)
        {
            UI_CFG_RETURN_TEXT(ui_renderer_template_draw_filter_big_text(x, y, w, h, "EXT", &FONT_OFF_COMPACT));
        }
        if (family == TRACK_FAMILY_DRUM)
        {
            ui_renderer_template_draw_cfg_drum_icon(x, y, w, h);
            return 1U;
        }
        if (family == TRACK_FAMILY_SAMPLER)
        {
            ui_renderer_template_draw_cfg_sampler_icon(x, y, w, h);
            return 1U;
        }
        UI_CFG_RETURN_TEXT(ui_renderer_template_draw_filter_big_text(x, y, w, h, ui_get_track_family_display_name(family), &FONT_OFF_COMPACT));
    }

    if (kind == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_TYPE)
    {
        const track_type_t type = ui_get_track_type_from_family_index(family, (uint8_t)(value + 0.5f));
        if (family == TRACK_FAMILY_OFF)
        {
            UI_CFG_RETURN_TEXT(ui_renderer_template_draw_filter_text(x, y, w, h, "-", &FONT_4X6, 0));
        }

        switch (type)
        {
            case TRACK_TYPE_WAVE:
                ui_renderer_template_draw_cfg_wave_icon(x, y, w, h);
                return 1U;

            case TRACK_TYPE_MULTI:
                ui_renderer_template_draw_cfg_piano_icon(x, y, w, h);
                return 1U;

            case TRACK_TYPE_MIDI:
                ui_renderer_template_draw_cfg_midi_icon(x, y, w, h);
                return 1U;

            case TRACK_TYPE_EXTERNAL:
                ui_renderer_template_draw_cfg_external_icon(x, y, w, h);
                return 1U;

            case TRACK_TYPE_NONE:
                break;

            case TRACK_TYPE_RAM:
                ui_renderer_template_draw_cfg_ram_icon(x, y, w, h);
                return 1U;

            case TRACK_TYPE_STREAM:
                ui_renderer_template_draw_cfg_stream_icon(x, y, w, h);
                return 1U;

            case TRACK_TYPE_DRUM_MD:
                return ui_renderer_template_draw_cfg_stacked_text(x, y, w, h, "DRUM", "MD");

                UI_CFG_RETURN_TEXT(ui_renderer_template_draw_filter_big_text(x, y, w, h, "FX", &FONT_OFF_COMPACT));

            default:
                break;
        }

        UI_CFG_RETURN_TEXT(ui_renderer_template_draw_filter_big_text(x, y, w, h, ui_get_track_type_short_name(family, type), &FONT_OFF_COMPACT));
    }

    return 0U;

#undef UI_CFG_RETURN_TEXT
}

