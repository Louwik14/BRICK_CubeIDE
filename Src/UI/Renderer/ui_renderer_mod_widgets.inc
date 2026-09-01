static int ui_renderer_template_clamp_i32(int value, int min_value, int max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

static uint8_t ui_renderer_template_value_to_u7(float value)
{
    int32_t v = (int32_t)(value + 0.5f);
    if (v < 0)
    {
        v = 0;
    }
    if (v > 127)
    {
        v = 127;
    }
    return (uint8_t)v;
}

typedef struct
{
    uint8_t mode;
    float cutoff;
    float resonance;
    float morph;
} ui_renderer_template_filter_snapshot_t;

static uint8_t ui_renderer_template_filter_snapshot_capture(
    const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
    ui_renderer_template_filter_snapshot_t *out)
{
    float mode = 0.0f;
    if ((out == NULL)
            || (ui_renderer_template_get_visible_param_value(plock_frame_ctx, PARAM_FILTER_MODE, &mode, 0) == 0U)
            || (ui_renderer_template_get_visible_param_value(plock_frame_ctx, PARAM_FILTER_CUTOFF, &out->cutoff, 0) == 0U)
            || (ui_renderer_template_get_visible_param_value(plock_frame_ctx, PARAM_FILTER_RESONANCE, &out->resonance, 0) == 0U)
            || (ui_renderer_template_get_visible_param_value(plock_frame_ctx, PARAM_FILTER_MORPH, &out->morph, 0) == 0U))
    {
        return 0U;
    }
    out->mode = (mode < 0.5f) ? 0U : ((mode < 1.5f) ? 1U : 2U);
    out->cutoff = (out->cutoff < 0.0f) ? 0.0f : ((out->cutoff > 127.0f) ? 127.0f : out->cutoff);
    out->resonance = (out->resonance < 0.0f) ? 0.0f : ((out->resonance > 127.0f) ? 127.0f : out->resonance);
    out->morph = (out->morph < 0.0f) ? 0.0f : ((out->morph > 127.0f) ? 127.0f : out->morph);
    return 1U;
}

static uint8_t ui_renderer_template_filter_param_supported(uint8_t active_track, param_id_t id)
{
    const track_runtime_param_status_t status = track_runtime_get_effective_param_status(active_track, id);
    return (uint8_t)((status == TRACK_RUNTIME_PARAM_ALLOWED) || (status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED));
}

static uint8_t ui_renderer_template_draw_filter_text(int x, int y, int w, int h, const char *txt, const font_t *font, int y_offset)
{
    if ((txt == 0) || (font == 0) || (w <= 0) || (h <= 0))
    {
        return 0U;
    }

    drv_display_set_font(font);
    const int text_w = (int)drv_display_text_width(txt);
    const int text_h = (int)drv_display_font_height();
    int text_x = x + ((w - text_w) / 2);
    int text_y = y + ((h - text_h) / 2) + y_offset;
    if (text_x < x)
    {
        text_x = x;
    }
    if (text_y < y)
    {
        text_y = y;
    }
    drv_display_draw_text((uint8_t)text_x, (uint8_t)text_y, txt);
    drv_display_set_font(&FONT_4X6);
    return 1U;
}

static uint8_t ui_renderer_template_draw_filter_big_text(int x, int y, int w, int h, const char *txt, const font_t *font)
{
    return ui_renderer_template_draw_filter_text(x, y, w, h, txt, font, 4);
}

static uint8_t ui_renderer_template_draw_lfo_dest_text(int x, int y, int w, int h, float value)
{
    if ((w <= 0) || (h <= 0))
    {
        return 0U;
    }

    char label[20];
    if (mod_lfo_v1_dest_short_label(ui_renderer_template_get_edit_owner_track(), (uint16_t)(value + 0.5f), label, (uint32_t)sizeof(label)) == 0U)
    {
        (void)snprintf(label, sizeof(label), "-");
    }

    drv_display_set_font(&FONT_OFF_COMPACT);
    ui_renderer_template_fit_text(label, (uint8_t)((w > 2) ? (w - 2) : w));
    return ui_renderer_template_draw_filter_text(x, y, w, h, label, &FONT_OFF_COMPACT, 4);
}

static uint8_t ui_renderer_template_lfo_shape_from_value(float value)
{
    int32_t shape = (int32_t)(value + 0.5f);
    if (shape < 0)
    {
        shape = 0;
    }
    if (shape >= (int32_t)MOD_LFO_SHAPE_COUNT)
    {
        shape = (int32_t)MOD_LFO_SHAPE_COUNT - 1;
    }
    return (uint8_t)shape;
}

static int8_t ui_renderer_template_lfo_sine_q7(uint8_t phase, uint8_t positive)
{
    static const int8_t sine_q7[17] = {
        0, 24, 45, 58, 63, 58, 45, 24, 0, -24, -45, -58, -63, -58, -45, -24, 0
    };
    const uint8_t segment = (uint8_t)(phase >> 4);
    const uint8_t frac = (uint8_t)(phase & 0x0FU);
    const int16_t y0 = sine_q7[segment];
    const int16_t y1 = sine_q7[segment + 1U];
    int16_t y = (int16_t)(y0 + (((y1 - y0) * (int16_t)frac) / 16));
    if (positive != 0U)
    {
        y = (int16_t)((y + 63) / 2);
    }
    return (int8_t)y;
}

static int8_t ui_renderer_template_lfo_wave_q7(uint8_t shape, uint8_t x, uint8_t width)
{
    static const int8_t rnd_q7[8] = { -36, 22, 52, -12, -56, 4, 40, -24 };
    const uint16_t denom = (width > 1U) ? (uint16_t)(width - 1U) : 1U;
    const uint8_t phase = (uint8_t)(((uint16_t)x * 255U) / denom);

    switch ((mod_lfo_shape_t)shape)
    {
        case MOD_LFO_SHAPE_SINE:
            return ui_renderer_template_lfo_sine_q7(phase, 0U);
        case MOD_LFO_SHAPE_SINE_POS:
            return ui_renderer_template_lfo_sine_q7(phase, 1U);
        case MOD_LFO_SHAPE_TRIANGLE:
            if (phase < 128U)
            {
                return (int8_t)(-63 + (((int16_t)phase * 126) / 128));
            }
            return (int8_t)(63 - ((((int16_t)phase - 128) * 126) / 127));
        case MOD_LFO_SHAPE_TRIANGLE_POS:
            if (phase < 128U)
            {
                return (int8_t)(((uint16_t)phase * 63U) / 128U);
            }
            return (int8_t)(63U - ((((uint16_t)phase - 128U) * 63U) / 127U));
        case MOD_LFO_SHAPE_SAW:
            return (int8_t)(-63 + (((int16_t)phase * 126) / 255));
        case MOD_LFO_SHAPE_REVERSE_SAW:
            return (int8_t)(63 - (((int16_t)phase * 126) / 255));
        case MOD_LFO_SHAPE_SQUARE:
            return (phase < 128U) ? 63 : -63;
        case MOD_LFO_SHAPE_SQUARE_POS:
            return (phase < 128U) ? 63 : 0;
        case MOD_LFO_SHAPE_RANDOM_SH:
            return rnd_q7[((uint16_t)x * 8U) / ((width > 0U) ? width : 1U)];
        default:
            return 0;
    }
}

static uint8_t ui_renderer_template_lfo_shape_is_step(uint8_t shape)
{
    return ((shape == (uint8_t)MOD_LFO_SHAPE_SQUARE)
            || (shape == (uint8_t)MOD_LFO_SHAPE_SQUARE_POS)
            || (shape == (uint8_t)MOD_LFO_SHAPE_RANDOM_SH)) ? 1U : 0U;
}

static int ui_renderer_template_lfo_wave_y(int y, int h, int8_t q7)
{
    const int mid = y + (h / 2);
    const int amp = (h > 4) ? ((h - 4) / 2) : 1;
    return mid - (((int)q7 * amp) / 63);
}

static void ui_renderer_template_draw_lfo_baseline(int x, int y, int w, int h)
{
    const int mid = y + (h / 2);
    for (int px = x + 1; px < (x + w - 1); px += 2)
    {
        drv_display_draw_pixel(px, mid, true);
    }
}

static uint8_t ui_renderer_template_draw_lfo_wave_shape(int x, int y, int w, int h, uint8_t shape)
{
    if ((w <= 2) || (h <= 2))
    {
        return 0U;
    }

    const uint8_t plot_w = (uint8_t)((w > 2) ? (w - 2) : w);
    ui_renderer_template_draw_lfo_baseline(x, y, w, h);

    int prev_x = x + 1;
    int prev_y = ui_renderer_template_lfo_wave_y(y, h, ui_renderer_template_lfo_wave_q7(shape, 0U, plot_w));
    const uint8_t step_shape = ui_renderer_template_lfo_shape_is_step(shape);
    for (uint8_t px = 1U; px < plot_w; ++px)
    {
        const int xx = x + 1 + (int)px;
        const int yy = ui_renderer_template_lfo_wave_y(y, h, ui_renderer_template_lfo_wave_q7(shape, px, plot_w));
        if (step_shape != 0U)
        {
            drv_display_draw_line(prev_x, prev_y, xx, prev_y);
            if (yy != prev_y)
            {
                drv_display_draw_line(xx, prev_y, xx, yy);
            }
        }
        else
        {
            drv_display_draw_line(prev_x, prev_y, xx, yy);
        }
        prev_x = xx;
        prev_y = yy;
    }
    return 1U;
}

static uint8_t ui_renderer_template_draw_lfo_shape_widget(int x, int y, int w, int h, float value)
{
    return ui_renderer_template_draw_lfo_wave_shape(x, y, w, h, ui_renderer_template_lfo_shape_from_value(value));
}

static uint16_t ui_renderer_template_float_to_q15(float value)
{
    if (value <= 0.0f)
    {
        return 0U;
    }
    if (value >= 1.0f)
    {
        return 32767U;
    }
    return (uint16_t)(value * 32767.0f + 0.5f);
}

static uint8_t ui_renderer_template_stack_slot_param(param_id_t id, uint8_t *out_slot, uint8_t *out_param)
{
    if ((out_slot == 0) || (out_param == 0))
    {
        return 0U;
    }
    if ((id >= PARAM_STACK_OSC1_MODEL) && (id <= PARAM_STACK_OSC3_COLOR))
    {
        const uint8_t rel = (uint8_t)(id - PARAM_STACK_OSC1_MODEL);
        *out_slot = (uint8_t)(rel / 4U);
        *out_param = (uint8_t)((rel % 4U) + 1U);
        return (*out_slot < BRICK6_STACK_SLOT_COUNT) ? 1U : 0U;
    }
    return 0U;
}

static int ui_renderer_template_stack_wave_y(int y, int h, int16_t sample)
{
    const int top = y + 1;
    const int bottom = y + h - 2;
    const int span = bottom - top;
    const uint32_t normalized = (uint32_t)((int32_t)sample + 32768);
    int yy = bottom - (int)(((uint64_t)normalized * (uint64_t)span + 32767ULL) / 65535ULL);
    if (yy < top)
    {
        yy = top;
    }
    if (yy > bottom)
    {
        yy = bottom;
    }
    return yy;
}

typedef struct
{
    uint8_t valid;
    brick6_stack_model_t model;
    uint16_t timbre_q15;
    uint16_t color_q15;
    int plot_w;
    int16_t samples[UI_TEMPLATE_STACK_WAVE_CACHE_MAX_W];
} ui_renderer_template_stack_wave_cache_t;

static ui_renderer_template_stack_wave_cache_t g_ui_template_stack_wave_cache[BRICK6_STACK_SLOT_COUNT];

static void ui_renderer_template_rebuild_stack_wave_cache(ui_renderer_template_stack_wave_cache_t *cache,
                                                          uint16_t timbre_q15,
                                                          uint16_t color_q15,
                                                          int plot_w)
{
    if ((cache == NULL) || (plot_w <= 0))
    {
        return;
    }

    cache->valid = 1U;
    cache->model = BRICK6_STACK_MODEL_SHAPE;
    cache->timbre_q15 = timbre_q15;
    cache->color_q15 = color_q15;
    cache->plot_w = plot_w;

    const uint64_t denom = (uint64_t)plot_w * 2ULL;
    for (int px = 0; px < plot_w; ++px)
    {
        const uint32_t phase = (uint32_t)(((uint64_t)((px * 2) + 1) * 0xFFFFFFFFULL) / denom);
        cache->samples[px] = brick6_stack_waveform_shape(phase, timbre_q15, color_q15);
    }
}

static uint8_t ui_renderer_template_draw_stack_waveform_widget(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                               int x,
                                                               int y,
                                                               int w,
                                                               int h,
                                                               param_id_t id,
                                                               float value)
{
    if ((w <= 2) || (h <= 2))
    {
        return 0U;
    }

    uint8_t slot = 0U;
    uint8_t slot_param = 0U;
    if (ui_renderer_template_stack_slot_param(id, &slot, &slot_param) == 0U)
    {
        return 0U;
    }

    const param_id_t model_param = (param_id_t)(PARAM_STACK_OSC1_MODEL + (slot * 4U));
    const param_id_t timbre_param = (param_id_t)(PARAM_STACK_OSC1_TIMBRE + (slot * 4U));
    const param_id_t color_param = (param_id_t)(PARAM_STACK_OSC1_COLOR + (slot * 4U));
    float model_value = 0.0f;
    float timbre_value = 0.0f;
    float color_value = 0.0f;
    (void)slot_param;
    if ((ui_renderer_template_get_visible_param_value(plock_frame_ctx, model_param, &model_value, 0) == 0U)
            || (ui_renderer_template_get_visible_param_value(plock_frame_ctx, timbre_param, &timbre_value, 0) == 0U)
            || (ui_renderer_template_get_visible_param_value(plock_frame_ctx, color_param, &color_value, 0) == 0U)
            )
    {
        return 0U;
    }

    if (id == timbre_param)
    {
        timbre_value = value;
    }
    else if (id == color_param)
    {
        color_value = value;
    }
    const brick6_stack_model_t model = (brick6_stack_model_t)(uint8_t)(model_value + 0.5f);
    if (!((model == BRICK6_STACK_MODEL_SHAPE) && (id == color_param)))
    {
        return 0U;
    }

    const uint16_t timbre_q15 = ui_renderer_template_float_to_q15(timbre_value);
    const uint16_t color_q15 = ui_renderer_template_float_to_q15(color_value);
    const int plot_w = (w > 2) ? (w - 2) : w;
    if (plot_w > UI_TEMPLATE_STACK_WAVE_CACHE_MAX_W)
    {
        return 0U;
    }

    ui_renderer_template_stack_wave_cache_t *const cache = &g_ui_template_stack_wave_cache[slot];
    if ((cache->valid == 0U)
            || (cache->model != model)
            || (cache->timbre_q15 != timbre_q15)
            || (cache->color_q15 != color_q15)
            || (cache->plot_w != plot_w))
    {
        ui_renderer_template_rebuild_stack_wave_cache(cache, timbre_q15, color_q15, plot_w);
    }

    ui_renderer_template_draw_lfo_baseline(x, y, w, h);

    int prev_x = x + 1;
    int prev_y = 0;
    uint8_t have_prev = 0U;
    for (int px = 0; px < plot_w; ++px)
    {
        const int xx = x + 1 + (int)px;
        const int yy = ui_renderer_template_stack_wave_y(y, h, cache->samples[px]);
        if (have_prev != 0U)
        {
            if (model == BRICK6_STACK_MODEL_SHAPE)
            {
                const int dy = yy - prev_y;
                if ((dy > (h / 2)) || (dy < -(h / 2)))
                {
                    drv_display_draw_line(prev_x, prev_y, xx, prev_y);
                    drv_display_draw_line(xx, prev_y, xx, yy);
                }
                else
                {
                    drv_display_draw_line(prev_x, prev_y, xx, yy);
                }
            }
            else
            {
                drv_display_draw_line(prev_x, prev_y, xx, yy);
            }
        }
        prev_x = xx;
        prev_y = yy;
        have_prev = 1U;
    }
    return 1U;
}

static uint8_t ui_renderer_template_draw_lfo_phase(int x, int y, int w, int h, param_id_t id, float value)
{
    if ((w <= 2) || (h <= 2))
    {
        return 0U;
    }

    const uint8_t lfo = (uint8_t)(id - PARAM_LFO1_PHASE) / (uint8_t)MOD_LFO_PARAM_COUNT;
    const uint8_t track = ui_renderer_template_get_edit_owner_track();
    float shape_value = 0.0f;
    (void)mod_lfo_v1_get_track_param(track, lfo, MOD_LFO_PARAM_SHAPE, &shape_value);
    const uint8_t shape = ui_renderer_template_lfo_shape_from_value(shape_value);
    const uint8_t plot_w = (uint8_t)((w > 2) ? (w - 2) : w);
    if (ui_renderer_template_draw_lfo_wave_shape(x, y, w, h, shape) == 0U)
    {
        return 0U;
    }

    if (shape != (uint8_t)MOD_LFO_SHAPE_RANDOM_SH)
    {
        const float phase = (value < 0.0f) ? 0.0f : ((value > 360.0f) ? 360.0f : value);
        const int cursor_x = x + 1 + (int)((phase * (float)(plot_w - 1U)) / 360.0f);
        drv_display_draw_line(cursor_x, y + 1, cursor_x, y + h - 2);
    }
    return 1U;
}

static uint8_t ui_renderer_template_render_lfo_shape_phase_group(param_id_t shape_id,
                                                                 param_id_t phase_id,
                                                                 float shape_value,
                                                                 float phase_value,
                                                                 int x,
                                                                 int y,
                                                                 int w,
                                                                 int h)
{
    if ((w < 24) || (h < 10))
    {
        return 0U;
    }

    (void)shape_id;
    (void)phase_id;
    const uint8_t shape = ui_renderer_template_lfo_shape_from_value(shape_value);
    const int half_w = w / 2;
    const int phase_x = x + half_w;
    const int phase_w = w - half_w;

    if (shape == (uint8_t)MOD_LFO_SHAPE_RANDOM_SH)
    {
        if (ui_renderer_template_draw_lfo_wave_shape(x, y, half_w, h, shape) == 0U)
        {
            return 0U;
        }

        const int bar_y = y + ((h - 5) / 2);
        const int bar_x = phase_x + 3;
        const int bar_w = (phase_w > 6) ? (phase_w - 6) : phase_w;
        float norm = phase_value / 360.0f;
        if (norm < 0.0f)
        {
            norm = 0.0f;
        }
        else if (norm > 1.0f)
        {
            norm = 1.0f;
        }
        drv_display_draw_rect(bar_x, bar_y, bar_w, 5);
        const int fill_w = (bar_w > 2) ? (int)((norm * (float)(bar_w - 2)) + 0.5f) : 0;
        if (fill_w > 0)
        {
            drv_display_fill_rect(bar_x + 1, bar_y + 1, fill_w, 3);
        }
    }
    else
    {
        if (ui_renderer_template_draw_lfo_wave_shape(x, y, w, h, shape) == 0U)
        {
            return 0U;
        }

        float phase = phase_value;
        if (phase < 0.0f)
        {
            phase = 0.0f;
        }
        else if (phase > 360.0f)
        {
            phase = 360.0f;
        }
        const uint8_t plot_w = (uint8_t)((w > 2) ? (w - 2) : w);
        const int cursor_x = x + 1 + (int)((phase * (float)(plot_w - 1U)) / 360.0f);
        drv_display_draw_line(cursor_x, y + 1, cursor_x, y + h - 2);
    }
    return 1U;
}

static void ui_renderer_template_draw_lfo_center_indicator(int x, int y, int w, int h, float value, float max_abs)
{
    uiw_draw_bipolar_bar(x, y, w, h, value, -max_abs, max_abs);
}

static void ui_renderer_template_draw_lfo_unipolar_bar(int x, int y, int w, int h, float value, float min_value, float max_value)
{
    uiw_draw_value_bar(x, y, w, h, value, min_value, max_value);
}

static uint8_t ui_renderer_template_draw_lfo_custom_widget(ui_template_custom_widget_kind_t kind,
                                                           int x,
                                                           int y,
                                                           int w,
                                                           int h,
                                                           param_id_t id,
                                                           float value)
{
    switch (kind)
    {
        case UI_TEMPLATE_CUSTOM_WIDGET_LFO_RATE:
        {
            const float max_abs = (value > 0.0001f) ? (float)MOD_LFO_SYNC_RATE_COUNT : LFO_FREE_MAX_HZ;
            ui_renderer_template_draw_lfo_center_indicator(x, y, w, h, value, max_abs);
            return 1U;
        }
        case UI_TEMPLATE_CUSTOM_WIDGET_LFO_DEPTH:
            ui_renderer_template_draw_lfo_unipolar_bar(x, y, w, h, value, 0.0f, 127.0f);
            return 1U;
        case UI_TEMPLATE_CUSTOM_WIDGET_LFO_SHAPE:
            return ui_renderer_template_draw_lfo_shape_widget(x, y, w, h, value);
        case UI_TEMPLATE_CUSTOM_WIDGET_LFO_PHASE:
            return ui_renderer_template_draw_lfo_phase(x, y, w, h, id, value);
        case UI_TEMPLATE_CUSTOM_WIDGET_LFO_SHAPE_PHASE_GROUP:
            return 0U;
        default:
            return 0U;
    }
}

static uint8_t ui_renderer_template_matrix_slot_is_filled(uint8_t track, uint8_t slot)
{
    float source = 0.0f;
    float destination = 0.0f;
    float depth = 0.0f;
    if ((slot >= MOD_MATRIX_SLOT_COUNT)
            || (mod_matrix_get_slot_source(track, slot, &source) == 0U)
            || (mod_matrix_get_slot_destination_index(track, slot, &destination) == 0U)
            || (mod_matrix_get_slot_depth(track, slot, &depth) == 0U))
    {
        return 0U;
    }
    return ((source > (float)MOD_MATRIX_SOURCE_NONE)
            && (source < (float)MOD_MATRIX_SOURCE_COUNT)
            && (destination > 0.0f)
            && (depth != 0.0f)) ? 1U : 0U;
}

static void ui_renderer_template_draw_matrix_rows(int x, int y, const uint32_t rows[24])
{
    for (uint8_t row = 0U; row < 24U; ++row)
    {
        const uint32_t bits = rows[row];
        for (uint8_t col = 0U; col < 24U; ++col)
        {
            if ((bits & (1UL << (23U - col))) != 0UL)
            {
                drv_display_draw_pixel(x + (int)col, y + (int)row, true);
            }
        }
    }
}

static uint32_t ui_renderer_template_matrix_slot_mask(uint8_t slot, uint8_t row)
{
    static const uint8_t slot_x[MOD_MATRIX_SLOT_COUNT] = { 0U, 7U, 15U, 15U, 15U, 7U, 0U, 0U };
    static const uint8_t slot_y[MOD_MATRIX_SLOT_COUNT] = { 0U, 0U, 0U, 7U, 16U, 15U, 16U, 7U };
    static const uint8_t slot_w[MOD_MATRIX_SLOT_COUNT] = { 7U, 9U, 9U, 9U, 9U, 9U, 7U, 9U };
    static const uint8_t slot_h[MOD_MATRIX_SLOT_COUNT] = { 7U, 9U, 7U, 9U, 7U, 9U, 7U, 9U };

    if ((slot >= MOD_MATRIX_SLOT_COUNT) || (row < slot_y[slot]) || (row >= (uint8_t)(slot_y[slot] + slot_h[slot])))
    {
        return 0U;
    }

    return ((1UL << slot_w[slot]) - 1UL) << (24U - slot_x[slot] - slot_w[slot]);
}

static void ui_renderer_template_matrix_set_bit(uint32_t rows[24], uint8_t col, uint8_t row, uint8_t on)
{
    if ((col >= 24U) || (row >= 24U))
    {
        return;
    }

    const uint32_t bit = 1UL << (23U - col);
    rows[row] = (on != 0U) ? (rows[row] | bit) : (rows[row] & ~bit);
}

static void ui_renderer_template_matrix_clear_slot(uint32_t rows[24], uint8_t slot)
{
    for (uint8_t row = 0U; row < 24U; ++row)
    {
        rows[row] &= ~ui_renderer_template_matrix_slot_mask(slot, row);
    }
}

static void ui_renderer_template_matrix_draw_digit(uint32_t rows[24], uint8_t slot, uint8_t on)
{
    static const uint8_t digit_x[MOD_MATRIX_SLOT_COUNT] = { 3U, 10U, 18U, 18U, 18U, 10U, 3U, 3U };
    static const uint8_t digit_y[MOD_MATRIX_SLOT_COUNT] = { 1U, 1U, 1U, 9U, 17U, 17U, 17U, 9U };
    static const uint8_t glyphs[8][5] = {
        { 0x2U, 0x6U, 0x2U, 0x2U, 0x7U },
        { 0x6U, 0x1U, 0x2U, 0x4U, 0x7U },
        { 0x6U, 0x1U, 0x2U, 0x1U, 0x6U },
        { 0x5U, 0x5U, 0x7U, 0x1U, 0x1U },
        { 0x7U, 0x4U, 0x6U, 0x1U, 0x6U },
        { 0x3U, 0x4U, 0x7U, 0x5U, 0x7U },
        { 0x7U, 0x1U, 0x2U, 0x4U, 0x4U },
        { 0x7U, 0x5U, 0x7U, 0x5U, 0x7U }
    };

    if (slot >= MOD_MATRIX_SLOT_COUNT)
    {
        return;
    }

    for (uint8_t row = 0U; row < 5U; ++row)
    {
        for (uint8_t col = 0U; col < 3U; ++col)
        {
            if ((glyphs[slot][row] & (uint8_t)(1U << (2U - col))) != 0U)
            {
                ui_renderer_template_matrix_set_bit(rows,
                                                    (uint8_t)(digit_x[slot] + col),
                                                    (uint8_t)(digit_y[slot] + row),
                                                    on);
            }
        }
    }
}

static void ui_renderer_template_matrix_draw_digit_at(uint32_t rows[24],
                                                      uint8_t slot,
                                                      uint8_t x,
                                                      uint8_t y,
                                                      uint8_t on)
{
    static const uint8_t glyphs[8][5] = {
        { 0x2U, 0x6U, 0x2U, 0x2U, 0x7U },
        { 0x6U, 0x1U, 0x2U, 0x4U, 0x7U },
        { 0x6U, 0x1U, 0x2U, 0x1U, 0x6U },
        { 0x5U, 0x5U, 0x7U, 0x1U, 0x1U },
        { 0x7U, 0x4U, 0x6U, 0x1U, 0x6U },
        { 0x3U, 0x4U, 0x7U, 0x5U, 0x7U },
        { 0x7U, 0x1U, 0x2U, 0x4U, 0x4U },
        { 0x7U, 0x5U, 0x7U, 0x5U, 0x7U }
    };

    if (slot >= MOD_MATRIX_SLOT_COUNT)
    {
        return;
    }

    for (uint8_t row = 0U; row < 5U; ++row)
    {
        for (uint8_t col = 0U; col < 3U; ++col)
        {
            if ((glyphs[slot][row] & (uint8_t)(1U << (2U - col))) != 0U)
            {
                ui_renderer_template_matrix_set_bit(rows, (uint8_t)(x + col), (uint8_t)(y + row), on);
            }
        }
    }
}

static void ui_renderer_template_matrix_apply_inactive_slot(uint32_t rows[24], uint8_t slot)
{
    static const uint8_t cross_x[MOD_MATRIX_SLOT_COUNT] = { 1U, 9U, 17U, 17U, 17U, 9U, 1U, 1U };
    static const uint8_t cross_y[MOD_MATRIX_SLOT_COUNT] = { 1U, 1U, 1U, 9U, 17U, 17U, 17U, 9U };

    if (slot >= MOD_MATRIX_SLOT_COUNT)
    {
        return;
    }

    ui_renderer_template_matrix_draw_digit(rows, slot, 0U);
    for (uint8_t row = 0U; row < 5U; ++row)
    {
        for (uint8_t col = 0U; col < 5U; ++col)
        {
            ui_renderer_template_matrix_set_bit(rows,
                                                (uint8_t)(cross_x[slot] + col),
                                                (uint8_t)(cross_y[slot] + row),
                                                0U);
        }
    }
    for (uint8_t i = 0U; i < 5U; ++i)
    {
        ui_renderer_template_matrix_set_bit(rows, (uint8_t)(cross_x[slot] + i), (uint8_t)(cross_y[slot] + i), 1U);
        ui_renderer_template_matrix_set_bit(rows, (uint8_t)(cross_x[slot] + 4U - i), (uint8_t)(cross_y[slot] + i), 1U);
    }
}

static void ui_renderer_template_matrix_apply_focus_slot(uint32_t rows[24], uint8_t slot)
{
    static const uint8_t odd_x[MOD_MATRIX_SLOT_COUNT] = { 0U, 0U, 15U, 0U, 15U, 0U, 0U, 0U };
    static const uint8_t odd_y[MOD_MATRIX_SLOT_COUNT] = { 0U, 0U, 0U, 0U, 16U, 0U, 16U, 0U };
    static const uint8_t even_x[MOD_MATRIX_SLOT_COUNT] = { 0U, 7U, 0U, 15U, 0U, 7U, 0U, 0U };
    static const uint8_t even_y[MOD_MATRIX_SLOT_COUNT] = { 0U, 0U, 0U, 7U, 0U, 15U, 0U, 7U };
    static const uint16_t arrow_down[9] = {
        0x1FFU, 0x1FFU, 0x1FFU, 0x1FFU, 0x1FFU, 0x1FFU, 0x0FEU, 0x07CU, 0x038U
    };

    if (slot >= MOD_MATRIX_SLOT_COUNT)
    {
        return;
    }

    ui_renderer_template_matrix_clear_slot(rows, slot);
    if ((slot & 1U) == 0U)
    {
        const uint8_t x = odd_x[slot];
        const uint8_t y = odd_y[slot];
        for (uint8_t row = 0U; row < 7U; ++row)
        {
            for (uint8_t col = 0U; col < 7U; ++col)
            {
                ui_renderer_template_matrix_set_bit(rows, (uint8_t)(x + col), (uint8_t)(y + row), 1U);
            }
        }
    }
    else
    {
        uint8_t x = even_x[slot];
        uint8_t y = even_y[slot];
        if (slot == 3U)
        {
            x--;
        }
        else if (slot == 5U)
        {
            y--;
        }
        for (uint8_t row = 0U; row < 9U; ++row)
        {
            for (uint8_t col = 0U; col < 9U; ++col)
            {
                uint8_t on = 0U;
                if (slot == 1U)
                {
                    on = ((arrow_down[row] & (uint16_t)(1U << (8U - col))) != 0U) ? 1U : 0U;
                }
                else if (slot == 5U)
                {
                    on = ((arrow_down[8U - row] & (uint16_t)(1U << (8U - col))) != 0U) ? 1U : 0U;
                }
                else if (slot == 3U)
                {
                    on = ((arrow_down[8U - col] & (uint16_t)(1U << (8U - row))) != 0U) ? 1U : 0U;
                }
                else
                {
                    on = ((arrow_down[col] & (uint16_t)(1U << row)) != 0U) ? 1U : 0U;
                }
                if (on != 0U)
                {
                    if ((slot == 7U) && (col == 8U) && ((row == 3U) || (row == 5U)))
                    {
                        continue;
                    }
                    ui_renderer_template_matrix_set_bit(rows, (uint8_t)(x + col), (uint8_t)(y + row), 1U);
                }
            }
        }
        if (slot == 1U)
        {
            ui_renderer_template_matrix_set_bit(rows, (uint8_t)(x + 1U), (uint8_t)(y + 8U), 1U);
            ui_renderer_template_matrix_set_bit(rows, (uint8_t)(x + 7U), (uint8_t)(y + 8U), 1U);
        }
    }
    if (slot == 3U)
    {
        ui_renderer_template_matrix_draw_digit_at(rows, slot, 17U, 9U, 0U);
    }
    else if (slot == 5U)
    {
        ui_renderer_template_matrix_draw_digit_at(rows, slot, 10U, 16U, 0U);
    }
    else
    {
        ui_renderer_template_matrix_draw_digit(rows, slot, 0U);
    }
}

static uint8_t ui_renderer_template_draw_matrix_slot_widget(int x, int y, int w, int h, float value)
{
    static const uint32_t all_active_rows[24] = {
        0x000000U, 0x103030U, 0x300808U, 0x101010U, 0x102008U, 0x383830U,
        0x000000U, 0x010100U, 0x008200U, 0x384428U, 0x283828U, 0x382838U,
        0x283808U, 0x384408U, 0x008200U, 0x010100U, 0x000000U, 0x383838U,
        0x082020U, 0x103838U, 0x202808U, 0x203838U, 0x000000U, 0x000000U
    };
    uint32_t rows[24];

    const int ox = x + ((w > 24) ? ((w - 24) / 2) : 0);
    const int oy = y + ((h > 24) ? ((h - 24) / 2) : 0);
    uint8_t selected = (uint8_t)(value + 0.5f);
    if (selected >= MOD_MATRIX_SLOT_COUNT)
    {
        selected = MOD_MATRIX_SLOT_COUNT - 1U;
    }

    const uint8_t track = ui_renderer_template_get_edit_owner_track();
    for (uint8_t row = 0U; row < 24U; ++row)
    {
        rows[row] = all_active_rows[row];
    }

    for (uint8_t slot = 0U; slot < MOD_MATRIX_SLOT_COUNT; ++slot)
    {
        if (ui_renderer_template_matrix_slot_is_filled(track, slot) == 0U)
        {
            ui_renderer_template_matrix_apply_inactive_slot(rows, slot);
        }
    }

    ui_renderer_template_matrix_apply_focus_slot(rows, selected);

    ui_renderer_template_draw_matrix_rows(ox, oy, rows);
    return 1U;
}

static uint8_t ui_renderer_template_draw_matrix_source_text(int x, int y, int w, int h, float value)
{
    char label[12];

    if ((w <= 0) || (h <= 0))
    {
        return 0U;
    }

    (void)snprintf(label, sizeof(label), "%s", ui_renderer_template_matrix_source_label(value));
    drv_display_set_font(&FONT_OFF_COMPACT);
    ui_renderer_template_fit_text(label, (uint8_t)((w > 2) ? (w - 2) : w));
    return ui_renderer_template_draw_filter_text(x, y, w, h, label, &FONT_OFF_COMPACT, 4);
}

