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

static void ui_renderer_template_draw_point(int x, int y)
{
    drv_display_draw_line(x, y, x, y);
}

static void ui_renderer_template_draw_circle_points(int cx, int cy, int x, int y)
{
    ui_renderer_template_draw_point(cx + x, cy + y);
    ui_renderer_template_draw_point(cx - x, cy + y);
    ui_renderer_template_draw_point(cx + x, cy - y);
    ui_renderer_template_draw_point(cx - x, cy - y);
    ui_renderer_template_draw_point(cx + y, cy + x);
    ui_renderer_template_draw_point(cx - y, cy + x);
    ui_renderer_template_draw_point(cx + y, cy - x);
    ui_renderer_template_draw_point(cx - y, cy - x);
}

static void ui_renderer_template_draw_circle_outline(int cx, int cy, int radius)
{
    int px = 0;
    int py = radius;
    int d = 1 - radius;

    while (px <= py)
    {
        ui_renderer_template_draw_circle_points(cx, cy, px, py);
        px++;
        if (d < 0)
        {
            d += (2 * px) + 1;
        }
        else
        {
            py--;
            d += (2 * (px - py)) + 1;
        }
    }
}

static void ui_renderer_template_draw_cfg_piano_icon(int x, int y, int w, int h)
{
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
}

static void ui_renderer_template_draw_cfg_external_icon(int x, int y, int w, int h)
{
    static const char *const external[29] = {
        "WWWWBBBWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWBWWWBWWWWWWWWWWWWWWWWWWWWWW",
        "WWWBWWWBWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWBBBWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWBWWWBWWWWWWWWWWWWWWWWWWWWWW",
        "WWWBBBBBWWWWWWWWWWWWWWWWWWWWWW",
        "WWWBWWWBWWWWWWWWWWWWWWWWWWWWWW",
        "WWWBWWWBWWWWWWWWWWWWWWWWWWWWWW",
        "WWWBBBBBWWWWWWWWWBBBBBBBBBBBWW",
        "WWWBWWWBWWWWWWWWWBWBBBWBBBWBWW",
        "WWWBWWWBWWWWWWWWWBWBBBWBBBWBWW",
        "WWWBWWWBWWWWWWWWWBWBBBWBBBWBWW",
        "WWBBBBBBBWWWWBWWWBWBBBWBBBWBWW",
        "WWBWWWWWBWWWWBWWWBWBBBWBBBWBWW",
        "WWBWWWWWBWWBBBBBWBWWBWWWBWWBWW",
        "WBBBBBBBBBWWWBWWWBWWBWWWBWWBWW",
        "WBWWWWWWWBWWWBWWWBWWBWWWBWWBWW",
        "WBWWWWWWWBWWWWWWWBWWBWWWBWWBWW",
        "WBWWWWWBWBWWWWWWWBWWBWWWBWWBWW",
        "WBWWWWWBWBWWWWWWWBWWBWWWBWWBWW",
        "WBWWWWWBWBWWWWWWWBWWBWWWBWWBWW",
        "WBWWWWWBWBWWWWWWWBBBBBBBBBBBWW",
        "WBWWWWWBWBWWWWWWWWWWWWWWWWWWWW",
        "WBWWWWBWWBWWWWWWWWWWWWWWWWWWWW",
        "WBWWWBWWWBWWWWWWWWWWWWWWWWWWWW",
        "WBBWWWWWBBWWWWWWWWWWWWWWWWWWWW",
        "WWBBWWWBBWWWWWWWWWWWWWWWWWWWWW",
        "WWWBBBBBWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWBBBWWWWWWWWWWWWWWWWWWWWWWW",
    };
    const int icon_w = 30;
    const int icon_h = 29;
    const int icon_x = x + ((w - icon_w) / 2);
    const int icon_y = y + ((h - icon_h) / 2);

    for (uint8_t row = 0U; row < 29U; ++row)
    {
        for (uint8_t col = 0U; col < 30U; ++col)
        {
            if (external[row][col] == 'B')
            {
                drv_display_draw_line(icon_x + (int)col, icon_y + (int)row, icon_x + (int)col, icon_y + (int)row);
            }
        }
    }
}

static uint8_t ui_renderer_template_draw_cfg_midi_text(int x, int y, int w, int h)
{
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
    return 1U;
}

static void ui_renderer_template_draw_cfg_midi_icon(int x, int y, int w, int h)
{
    static const char *const midi[29] = {
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWBBBBBBBBBBBWWWWWWWWWW",
        "WWWWWWWWWBWBBBWBBBWBWWWWWWWWWW",
        "WWWWWWWWWBWBBBWBBBWBWWWWWWWWWW",
        "WWWWWWWWWBWBBBWBBBWBWWWWWWWWWW",
        "WWWWWWWWWBWBBBWBBBWBWWWWWWWWWW",
        "WWWWWWWWWBWBBBWBBBWBWWWWWWWWWW",
        "WWWWWWWWWBWWBWWWBWWBWWWWWWWWWW",
        "WWWWWWWWWBWWBWWWBWWBWWWWWWWWWW",
        "WWWWWWWWWBWWBWWWBWWBWWWWWWWWWW",
        "WWWWWWWWWBWWBWWWBWWBWWWWWWWWWW",
        "WWWWWWWWWBWWBWWWBWWBWWWWWWWWWW",
        "WWWWWWWWWBWWBWWWBWWBWWWWWWWWWW",
        "WWWWWWWWWBWWBWWWBWWBWWWWWWWWWW",
        "WWWWWWWWWBBBBBBBBBBBWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
    };
    const int icon_w = 30;
    const int icon_h = 29;
    const int icon_x = x + ((w - icon_w) / 2);
    const int icon_y = y + ((h - icon_h) / 2);

    for (uint8_t row = 0U; row < 29U; ++row)
    {
        for (uint8_t col = 0U; col < 30U; ++col)
        {
            if (midi[row][col] == 'B')
            {
                drv_display_draw_line(icon_x + (int)col, icon_y + (int)row, icon_x + (int)col, icon_y + (int)row);
            }
        }
    }
}

static void ui_renderer_template_draw_cfg_synth_icon(int x, int y, int w, int h)
{
    static const char *const synth[29] = {
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
        "BWWWWWWBWWWBWWWWWWWWWWWWWWWWWB",
        "BWBBBBWBBBBBWBWBWBWBWBWBWBWBWB",
        "BWWWWWWBWWWBWWWWWWWWWWWWWWWWWB",
        "BWBWBBWBBBBBWBWBWBWBWBWBWBWBWB",
        "BWBWBBWBWWWBWWWWWWWWWWWWWWWWWB",
        "BWWWWWWBWWWBWWWWWWWWWWWWWWWWWB",
        "BWWWWWWBWWWBWWWWWWWWWWWWWWWWWB",
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
        "BWWWWWWWWWWWWWWWWWWWWWWWWWWWWB",
        "BWWBWBWWWBWBWBWWWBWBWWWBWBWBWB",
        "BWWBWBWWWBWBWBWWWBWBWWWBWBWBWB",
        "BWWWWWWWWWWWWWWWWWWWWWWWWWWWWB",
        "BWBBWBBWBBWBBWBBWBBWBBWBBWBBWB",
        "BWBBWBBWBBWBBWBBWBBWBBWBBWBBWB",
        "BWBBWBBWBBWBBWBBWBBWBBWBBWBBWB",
        "WBBBBBBBBBBBBBBBBBBBBBBBBBBBBW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
    };
    const int icon_w = 30;
    const int icon_h = 29;
    const int icon_x = x + ((w - icon_w) / 2);
    const int icon_y = y + ((h - icon_h) / 2);

    for (uint8_t row = 0U; row < 29U; ++row)
    {
        for (uint8_t col = 0U; col < 30U; ++col)
        {
            if (synth[row][col] == 'B')
            {
                drv_display_draw_line(icon_x + (int)col, icon_y + (int)row, icon_x + (int)col, icon_y + (int)row);
            }
        }
    }
}

static void ui_renderer_template_draw_cfg_wave_icon(int x, int y, int w, int h)
{
    static const char *const pixels[29] = {
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWBWBWWBWWBWBWBBBWWWWWWWW",
        "WWWWWWWBWBWBWBWBWBWBWWWWWWWWWW",
        "WWWWWWWBWBWBBBWBWBWBBBWWWWWWWW",
        "WWWWWWWBBBWBWBWBWBWBWWWWWWWWWW",
        "WWWWWWWBWBWBWBWWBWWBBBWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWBWWWWWWWBWWWWWWWBWWWWW",
        "WWWWWWBBWBWWWWBBWBWWWWBBWWWWWW",
        "WWWWWBWWWWBBWBWWWWBBWBWWWWWWWW",
        "WWWWBWWWWBWWBWWWWBWWBWWWWBWWWW",
        "WWWWWWWBBWBWWWWBBWBWWWWBBWWWWW",
        "WWWWWWBWWWWBBWBWWWWBBWBWWWWWWW",
        "WWWWWBWWWWWWWBWWWWWWWBWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
    };
    const int icon_w = 30;
    const int icon_h = 29;
    const int icon_x = x + ((w - icon_w) / 2);
    const int icon_y = y + ((h - icon_h) / 2);

    for (uint8_t row = 0U; row < 29U; ++row)
    {
        for (uint8_t col = 0U; col < 30U; ++col)
        {
            if (pixels[row][col] == 'B')
            {
                drv_display_draw_line(icon_x + (int)col, icon_y + (int)row, icon_x + (int)col, icon_y + (int)row);
            }
        }
    }
}

static void ui_renderer_template_draw_cfg_drum_icon(int x, int y, int w, int h)
{
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
}

static void ui_renderer_template_draw_cfg_sampler_icon(int x, int y, int w, int h)
{
    static const char *const vinyl[29] = {
        "..............................",
        "..............................",
        "..............................",
        "..............................",
        "............#####.............",
        "..........#########...........",
        "........#############.........",
        ".......###########W###........",
        ".......############W##........",
        "......###########W##W##.......",
        "......############W#W##.......",
        ".....#############W###W#......",
        ".....########WWW######W#......",
        ".....########W#W######W#......",
        ".....########WWW#####W##......",
        ".....###W#W#########W###......",
        "......##W#W########W###.......",
        "......##W##W######W####.......",
        ".......##W####WWWW####........",
        ".......###W###WWW#####........",
        "........#############.........",
        "..........#########...........",
        "............#####.............",
        "..............................",
        "..............................",
        "..............................",
        "..............................",
        "..............................",
        "..............................",
    };
    const int icon_w = 30;
    const int icon_h = 29;
    const int icon_x = x + ((w - icon_w) / 2) ;
    const int icon_y = y + ((h - icon_h) / 2) + 1;

    for (uint8_t row = 0U; row < 29U; ++row)
    {
        for (uint8_t col = 0U; col < 30U; ++col)
        {
            if (vinyl[row][col] == '#')
            {
                drv_display_draw_line(icon_x + (int)col, icon_y + (int)row, icon_x + (int)col, icon_y + (int)row);
            }
        }
    }
}

static void ui_renderer_template_draw_cfg_stream_icon(int x, int y, int w, int h)
{
    static const char *const stream[29] = {
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWBBBBBBBBBBBBBBBBWWWWWWWWWWWW",
        "WBBWWWWWWWWWWWWWWBBWWWWWWWWWWW",
        "WBBWWWBBWBBWBBWBBWBBWWWWWWWWWW",
        "WBBWWWBBWBBWBBWBBWWBBWWWWWWWWW",
        "WBBWWWBBWBBWBBWBBWWWBBWWWWWWWW",
        "WBBWWWBBWBBWBBWBBWWWWBWWWWWWWW",
        "WBBWWWWWWWWWWWWWWWWWWBWWWWWWWW",
        "WBBWWWWWWWWWWWWWWWWWWBWWWWWWWW",
        "WBBWWWWWBBWWWWWWWWWWWBWWWWWWWW",
        "WBBWWWWWBBBWWWWWWWWWBBWWWWWWWW",
        "WBBWWWWWBBBBBWWWWWWWBWWWWWWWWW",
        "WBBWWWWWBBBBBBBWWWWWBWWWWWWWWW",
        "WBBWWWWWBBBBBBBBWWWWBBWWWWWWWW",
        "WBBWWWWWBBBBBBBBBWWWWBWWWWWWWW",
        "WBBWWWWWBBBBBBBBWWWWWBWWWWWWWW",
        "WBBWWWWWBBBBBBBWWWWWWBWWWWWWWW",
        "WBBWWWWWBBBBBWWWWWWWWBWWWWWWWW",
        "WBBWWWWWBBBWWWWWWWWWWBWWWWWWWW",
        "WBBWWWWWBBWWWWWWWWWWWBWWWWWWWW",
        "WBBWWWWWWWWWWWWWWWWWWBWWWWWWWW",
        "WBBWWWWWWWWWWWWWWWWWWBWWWWWWWW",
        "WBBWWWWWWWWWWWWWWWWWWBWWWWWWWW",
        "WBBWWWWWWWWWWWWWWWWWWBWWWWWWWW",
        "WBBWWWWWWWWWWWWWWWWWWBWWWWWWWW",
        "WBBWWWWWWWWWWWWWWWWWWBWWWWWWWW",
        "WBBBBBBBBBBBBBBBBBBBBBWWWWWWWW",
        "WWBBBBBBBBBBBBBBBBBBBWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
    };
    const int icon_w = 30;
    const int icon_h = 29;
    const int icon_x = x + ((w - icon_w) / 2) + 4;
    const int icon_y = y + ((h - icon_h) / 2);

    for (uint8_t row = 0U; row < 29U; ++row)
    {
        for (uint8_t col = 0U; col < 30U; ++col)
        {
            if (stream[row][col] == 'B')
            {
                drv_display_draw_line(icon_x + (int)col, icon_y + (int)row, icon_x + (int)col, icon_y + (int)row);
            }
        }
    }
}

static void ui_renderer_template_draw_cfg_ram_icon(int x, int y, int w, int h)
{
    static const char *const ram[29] = {
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWBBWWBBWWBBWWBBWWBBWWWWWWW",
        "WWWWWBBWWBBWWBBWWBBWWBBWWWWWWW",
        "WWWWBBBBBBBBBBBBBBBBBBBBBWWWWW",
        "WWWWBWWWWWWWWWWWWWWWWWWWBBBWWW",
        "WWBBBWWWWWWWWWWWWWWWWWWWBBBWWW",
        "WWBBBWWWWWWWWWWWWWWWWWWWBWWWWW",
        "WWWWBWWWWWWWWWWWWWWWWWWWBWWWWW",
        "WWWWBWWWWWWWWWWWWWWWWWWWBBBWWW",
        "WWBBBWWWWWWWWWWWWWWWWWWWBBBWWW",
        "WWBBBWWWWWWWWWWWWWWWWWWWBWWWWW",
        "WWWWBWWBBBWWWBBWWBWWWBWWBWWWWW",
        "WWWWBWWBWWBWBWWBWBBWBBWWBBBWWW",
        "WWBBBWWBBBWWBBBBWBWBWBWWBBBWWW",
        "WWBBBWWBWBWWBWWBWBWWWBWWBWWWWW",
        "WWWWBWWBWWBWBWWBWBWWWBWWBWWWWW",
        "WWWWBWWWWWWWWWWWWWWWWWWWBBBWWW",
        "WWBBBWWWWWWWWWWWWWWWWWWWBBBWWW",
        "WWBBBWWWWWWWWWWWWWWWWWWWBWWWWW",
        "WWWWBWWWWWWWWWWWWWWWWWWWBWWWWW",
        "WWWWBWWWWWWWWWWWWWWWWWWWBBBWWW",
        "WWBBBWWWWWWWWWWWWWWWWWWWBBBWWW",
        "WWBBBWWWWWWWWWWWWWWWWWWWBWWWWW",
        "WWWWBBBBBBBBBBBBBBBBBBBBBWWWWW",
        "WWWWWWBBWWBBWWBBWWBBWWBBWWWWWW",
        "WWWWWWBBWWBBWWBBWWBBWWBBWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
    };
    const int icon_w = 30;
    const int icon_h = 29;
    const int icon_x = x + ((w - icon_w) / 2);
    const int icon_y = y + ((h - icon_h) / 2);

    for (uint8_t row = 0U; row < 29U; ++row)
    {
        for (uint8_t col = 0U; col < 30U; ++col)
        {
            if (ram[row][col] == 'B')
            {
                drv_display_draw_line(icon_x + (int)col, icon_y + (int)row, icon_x + (int)col, icon_y + (int)row);
            }
        }
    }
}

static void ui_renderer_template_draw_cfg_loop_icon(int x, int y, int w, int h)
{
    const int bx = x + 4;
    const int by = y + 6;
    const int bw = w - 8;
    const int bh = h - 10;
    const int r1x = bx + 7;
    const int r2x = bx + bw - 8;
    const int ry = by + 9;

    drv_display_draw_rect(bx, by, bw, bh);
    drv_display_draw_line(bx + 3, by + 3, bx + bw - 4, by + 3);
    ui_renderer_template_draw_circle_outline(r1x, ry, 3);
    ui_renderer_template_draw_circle_outline(r2x, ry, 3);
    drv_display_draw_line(r1x + 3, ry, r2x - 3, ry);
    drv_display_draw_line(bx + 5, by + bh - 4, bx + bw - 6, by + bh - 4);
    drv_display_draw_line(bx + 8, by + bh - 7, bx + bw - 9, by + bh - 7);
}

static uint8_t ui_renderer_template_draw_cfg_stacked_text(int x,
                                                          int y,
                                                          int w,
                                                          int h,
                                                          const char *top,
                                                          const char *bottom)
{
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
    return 1U;
}

static track_family_t ui_renderer_template_cfg_visible_family(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx)
{
    float family_value = 0.0f;
    if (ui_renderer_template_get_visible_param_value(plock_frame_ctx, PARAM_CFG_TRACK, &family_value, 0) == 0U)
    {
        return ui_get_track_family(ui_renderer_template_get_edit_owner_track());
    }

    int32_t family = (int32_t)(family_value + 0.5f);
    if (family < 0)
    {
        family = 0;
    }
    if (family >= (int32_t)TRACK_FAMILY_COUNT)
    {
        family = (int32_t)TRACK_FAMILY_COUNT - 1;
    }
    return (track_family_t)family;
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

    if (kind == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_INACTIVE)
    {
        return ui_renderer_template_draw_filter_text(x, y, w, h, "-", &FONT_4X6, 0);
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
        return ui_renderer_template_draw_filter_big_text(x, y, w, h, label, &FONT_HELVB14);
    }

    if (kind == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_MIDI_SOURCE)
    {
        static const char *const labels[] = {"Int", "Ext", "All"};
        uint8_t source = (uint8_t)(value + 0.5f);
        if (source >= 3U)
        {
            source = 2U;
        }
        return ui_renderer_template_draw_filter_big_text(x, y, w, h, labels[source], &FONT_OFF_COMPACT);
    }

    const track_family_t family = ui_renderer_template_cfg_visible_family(plock_frame_ctx);
    if (kind == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_TRACK)
    {
        if (family == TRACK_FAMILY_OFF)
        {
            return ui_renderer_template_draw_filter_big_text(x, y, w, h, "OFF", &FONT_OFF_COMPACT);
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
            return ui_renderer_template_draw_filter_big_text(x, y, w, h, "EXT", &FONT_OFF_COMPACT);
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
        return ui_renderer_template_draw_filter_big_text(x, y, w, h, ui_get_track_family_display_name(family), &FONT_OFF_COMPACT);
    }

    if (kind == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_TYPE)
    {
        const track_type_t type = ui_get_track_type_from_family_index(family, (uint8_t)(value + 0.5f));
        if (family == TRACK_FAMILY_OFF)
        {
            return ui_renderer_template_draw_filter_text(x, y, w, h, "-", &FONT_4X6, 0);
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

            case TRACK_TYPE_LOOPER:
                ui_renderer_template_draw_cfg_loop_icon(x, y, w, h);
                return 1U;

            case TRACK_TYPE_DRUM_MD:
                return ui_renderer_template_draw_cfg_stacked_text(x, y, w, h, "DRUM", "MD");

            case TRACK_TYPE_DRUM_BD_ANALOG:
                return ui_renderer_template_draw_cfg_stacked_text(x, y, w, h, "BD", "Ana");

                return ui_renderer_template_draw_filter_big_text(x, y, w, h, "FX", &FONT_OFF_COMPACT);

            default:
                break;
        }

        return ui_renderer_template_draw_filter_big_text(x, y, w, h, ui_get_track_type_short_name(family, type), &FONT_OFF_COMPACT);
    }

    return 0U;
}

