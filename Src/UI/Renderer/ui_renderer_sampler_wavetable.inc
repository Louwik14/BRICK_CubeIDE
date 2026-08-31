static const char *ui_renderer_template_path_basename(const char *path)
{
    const char *base = path;

    if (path == NULL)
    {
        return NULL;
    }

    for (const char *p = path; *p != '\0'; ++p)
    {
        if ((*p == '/') || (*p == '\\') || (*p == ':'))
        {
            base = p + 1;
        }
    }

    return base;
}

static uint16_t ui_renderer_template_sampler_ram_selected_global_slot(void)
{
    const float sample_value = ui_renderer_template_get_param_display_value(PARAM_SAMPLER_SAMPLE);
    uint16_t sample_index = 0U;
    if (sample_value > 0.0f)
    {
        sample_index = (uint16_t)(sample_value + 0.5f);
    }
    return sample_index;
}

static void ui_renderer_template_sampler_ram_sample_label(char *out, uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    (void)snprintf(out, out_len, "NO SAMPLE");

    const uint16_t sample_index = ui_renderer_template_sampler_ram_selected_global_slot();
    const sample_global_slot_t *const sample_slot = sample_global_pool_get_slot(sample_index);
    if ((sample_slot == NULL)
            || (sample_slot->kind != SAMPLE_GLOBAL_KIND_RAM)
            || (sample_slot->state != SAMPLE_GLOBAL_STATE_READY)
            || (sample_slot->path[0] == '\0'))
    {
        return;
    }

    const char *const base = ui_renderer_template_path_basename(sample_slot->path);
    if ((base == NULL) || (base[0] == '\0'))
    {
        return;
    }

    uint32_t i = 0U;
    while ((base[i] != '\0') && (base[i] != '.') && ((i + 1U) < out_len))
    {
        out[i] = base[i];
        i++;
    }
    out[i] = '\0';
    ui_renderer_template_fit_text(out, 96U);
}

static uint8_t ui_renderer_template_is_wave_wavetable_subpage(const ui_template_subpage_t *subpage)
{
    if ((ui_get_track_family(ui_renderer_template_get_edit_owner_track()) != TRACK_FAMILY_SYNTH)
            || (ui_get_track_type(ui_renderer_template_get_edit_owner_track()) != TRACK_TYPE_WAVE)
            || (subpage == NULL))
    {
        return 0U;
    }

    return (uint8_t)(((subpage->param_bank.params[0] == PARAM_WAVE_OSC1_TABLE)
                && (subpage->param_bank.params[1] == PARAM_WAVE_OSC1_POS)
                && (subpage->param_bank.params[2] == PARAM_WAVE_OSC1_START)
                && (subpage->param_bank.params[3] == PARAM_WAVE_OSC1_LEN))
            || ((subpage->param_bank.params[0] == PARAM_WAVE_OSC2_TABLE)
                && (subpage->param_bank.params[1] == PARAM_WAVE_OSC2_POS)
                && (subpage->param_bank.params[2] == PARAM_WAVE_OSC2_START)
                && (subpage->param_bank.params[3] == PARAM_WAVE_OSC2_LEN)));
}

static uint8_t ui_renderer_template_is_wave_common_subpage(const ui_template_subpage_t *subpage)
{
    return (uint8_t)((subpage != NULL)
        && (ui_get_track_family(ui_renderer_template_get_edit_owner_track()) == TRACK_FAMILY_SYNTH)
        && (ui_get_track_type(ui_renderer_template_get_edit_owner_track()) == TRACK_TYPE_WAVE)
        && (subpage->param_bank.params[0] == PARAM_WAVE_VOLUME)
        && (subpage->param_bank.params[1] == PARAM_WAVE_BALANCE));
}

static uint8_t ui_renderer_template_is_prism_tone_subpage(
    const ui_template_subpage_t *subpage)
{
    if ((subpage == NULL)
            || (ui_get_track_family(ui_renderer_template_get_edit_owner_track()) != TRACK_FAMILY_SYNTH)
            || (ui_get_track_type(ui_renderer_template_get_edit_owner_track()) != TRACK_TYPE_PRISM))
    {
        return 0U;
    }

    const param_id_t first = subpage->param_bank.params[0];
    return (uint8_t)((first == PARAM_PRISM_OSC1_MODEL)
        || (first == PARAM_PRISM_OSC2_MODEL)
        || (first == PARAM_PRISM_VOLUME)
        || (first == PARAM_PRISM_PITCH_MOD1));
}

static int ui_renderer_template_sampler_ram_amp_to_y(int16_t value,
                                                     uint16_t peak,
                                                     int top,
                                                     int inner_h)
{
    const int center = top + (inner_h / 2);
    const int half = (inner_h > 1) ? ((inner_h - 1) / 2) : 0;
    if ((peak == 0U) || (half == 0))
    {
        return center;
    }

    int y = center - (int)(((int32_t)value * half) / (int32_t)peak);
    const int bottom = top + inner_h - 1;
    if (y < top)
    {
        y = top;
    }
    else if (y > bottom)
    {
        y = bottom;
    }
    return y;
}

static int ui_renderer_template_sampler_ram_marker_x(float value,
                                                     uint32_t total_frames,
                                                     int inner_x,
                                                     int inner_w)
{
    if ((total_frames == 0U) || (inner_w <= 0))
    {
        return -1;
    }
    if (value < 0.0f)
    {
        value = 0.0f;
    }
    else if (value > 1.0f)
    {
        value = 1.0f;
    }

    const uint32_t max_frame = (total_frames > 1U) ? (total_frames - 1U) : 0U;
    uint32_t frame = (uint32_t)((value * (float)max_frame) + 0.5f);
    if (frame > max_frame)
    {
        frame = max_frame;
    }
    if (max_frame == 0U)
    {
        return inner_x;
    }
    return inner_x + (int)(((uint64_t)frame * (uint32_t)(inner_w - 1)) / max_frame);
}

static void ui_renderer_template_draw_sampler_ram_marker_label(int x, char label)
{
    char txt[2] = { label, '\0' };
    const uint8_t text_w = drv_display_text_width(txt);
    const int label_y = UI_TEMPLATE_SAMPLER_WAVE_Y + UI_TEMPLATE_SAMPLER_WAVE_H;
    int label_x = x - ((int)text_w / 2);
    label_x = ui_renderer_template_clamp_i32(label_x, 0, (int)OLED_WIDTH - (int)text_w);

    drv_display_set_font(&FONT_4X6);
    drv_display_draw_text((uint8_t)label_x, (uint8_t)label_y, txt);
}

static void ui_renderer_template_draw_sampler_ram_marker_line(int x,
                                                              int inner_y,
                                                              int inner_h,
                                                              uint8_t dotted)
{
    if (dotted != 0U)
    {
        const int bottom = inner_y + inner_h - 1;
        for (int y = inner_y; y <= bottom; y += 4)
        {
            const int y1 = ((y + 1) <= bottom) ? (y + 1) : bottom;
            drv_display_draw_line(x, y, x, y1);
        }
        return;
    }

    drv_display_draw_line(x, inner_y, x, inner_y + inner_h - 1);
}

static void ui_renderer_template_draw_sampler_ram_marker(float value,
                                                         uint32_t total_frames,
                                                         int inner_x,
                                                         int inner_y,
                                                         int inner_w,
                                                         int inner_h,
                                                         char label,
                                                         uint8_t dotted)
{
    const int x = ui_renderer_template_sampler_ram_marker_x(value, total_frames, inner_x, inner_w);
    if (x < 0)
    {
        return;
    }

    drv_display_set_draw_color(2U);
    ui_renderer_template_draw_sampler_ram_marker_line(x, inner_y, inner_h, dotted);
    drv_display_set_draw_color(1U);
    ui_renderer_template_draw_sampler_ram_marker_label(x, label);
}

static float ui_renderer_template_wave_wavetable_sample(const wavetable_slot_t *table,
                                                        float frame_position,
                                                        float sample_position)
{
    if ((table == NULL)
            || (table->data == NULL)
            || (table->frame_count == 0U)
            || (table->frame_sample_count == 0U))
    {
        return 0.0f;
    }

    const uint32_t max_frame = (table->frame_count > 1U) ? (table->frame_count - 1U) : 0U;
    if (frame_position < 0.0f)
    {
        frame_position = 0.0f;
    }
    else if (frame_position > (float)max_frame)
    {
        frame_position = (float)max_frame;
    }
    if (sample_position < 0.0f)
    {
        sample_position = 0.0f;
    }
    else if (sample_position > (float)(table->frame_sample_count - 1U))
    {
        sample_position = (float)(table->frame_sample_count - 1U);
    }

    uint32_t frame0 = (uint32_t)frame_position;
    uint32_t frame1 = (frame0 < max_frame) ? (frame0 + 1U) : frame0;
    const float frame_frac = frame_position - (float)frame0;
    uint32_t sample0 = (uint32_t)sample_position;
    uint32_t sample1 = (sample0 < (table->frame_sample_count - 1U)) ? (sample0 + 1U) : sample0;
    const float sample_frac = sample_position - (float)sample0;

    const float *const frame0_data = &table->data[frame0 * table->frame_sample_count];
    const float *const frame1_data = &table->data[frame1 * table->frame_sample_count];
    const float frame0_sample = frame0_data[sample0]
        + ((frame0_data[sample1] - frame0_data[sample0]) * sample_frac);
    const float frame1_sample = frame1_data[sample0]
        + ((frame1_data[sample1] - frame1_data[sample0]) * sample_frac);
    return (frame0_sample + ((frame1_sample - frame0_sample) * frame_frac))
        * 32767.0f;
}

static int ui_renderer_template_wave_wavetable_sample_to_y(float sample,
                                                            float peak,
                                                            int center_y,
                                                            int amplitude_half,
                                                            int top,
                                                            int bottom)
{
    int y = center_y;
    if (peak > 1.0f)
    {
        const float scaled = (sample * (float)amplitude_half) / peak;
        y = center_y - (int)((scaled >= 0.0f) ? (scaled + 0.5f) : (scaled - 0.5f));
    }
    return ui_renderer_template_clamp_i32(y, top, bottom);
}

static int ui_renderer_template_wave_wavetable_trace_x(int x_start,
                                                        int x_width,
                                                        uint8_t point)
{
    const uint32_t point_span = UI_TEMPLATE_WAVE_WT_TRACE_POINTS - 1U;
    const uint32_t x_span = (x_width > 1) ? (uint32_t)(x_width - 1) : 0U;
    return x_start + (int)(((uint32_t)point * x_span) / point_span);
}

static int ui_renderer_template_wave_wavetable_depth_y(int inner_y, float depth)
{
    if (depth < 0.0f)
    {
        depth = 0.0f;
    }
    else if (depth > 1.0f)
    {
        depth = 1.0f;
    }
    return inner_y + UI_TEMPLATE_WAVE_WT_CONTEXT_BASE_Y
        + (int)((depth * (float)UI_TEMPLATE_WAVE_WT_DEPTH_Y_PX) + 0.5f);
}

static uint8_t ui_renderer_template_build_wave_wavetable_cache(uint16_t global_slot,
                                                               float start_value,
                                                               float end_value,
                                                               float start_frame_position,
                                                               float end_frame_position,
                                                               const wavetable_slot_t *table,
                                                               const wavetable_preview_t *preview)
{
    if ((table == NULL)
            || (preview == NULL)
            || (table->data == NULL)
            || (table->frame_count == 0U)
            || (table->frame_sample_count != WAVETABLE_FRAME_SAMPLE_COUNT)
            || (preview->state != WAVETABLE_PREVIEW_READY)
            || (preview->global_peak <= 1U))
    {
        g_ui_renderer_template_wavetable_cache.valid = 0U;
        return 0U;
    }

    ui_renderer_template_wavetable_cache_t *const cache =
        &g_ui_renderer_template_wavetable_cache;
    const int top = UI_TEMPLATE_WAVE_WT_Y + 4;
    const int bottom = UI_TEMPLATE_WAVE_WT_Y + UI_TEMPLATE_WAVE_WT_H - 2;
    const float peak = (float)preview->global_peak;

    cache->valid = 0U;
    cache->global_slot = global_slot;
    cache->table_generation = table->generation;
    cache->preview_generation = preview->generation;
    cache->frame_count = table->frame_count;
    cache->global_peak = preview->global_peak;
    cache->layer_count = UI_TEMPLATE_WAVE_WT_LAYER_TARGET;
    cache->start_value = start_value;
    cache->end_value = end_value;
    cache->pos_valid = 0U;

    for (uint8_t layer = 0U; layer < cache->layer_count; ++layer)
    {
        const float depth = (float)layer / (float)(cache->layer_count - 1U);
        const int x_offset = (int)(((1.0f - depth) * (float)UI_TEMPLATE_WAVE_WT_DEPTH_X_PX) + 0.5f);
        const int x_start = UI_TEMPLATE_WAVE_WT_X + 1 + x_offset;
        const int center_y = ui_renderer_template_wave_wavetable_depth_y(UI_TEMPLATE_WAVE_WT_Y + 1,
                                                                          depth);
        const float frame_position = start_frame_position
            + ((end_frame_position - start_frame_position) * depth);

        cache->x_start[layer] = (uint8_t)x_start;
        cache->center_y[layer] = (int8_t)center_y;
        for (uint8_t point = 0U; point < UI_TEMPLATE_WAVE_WT_TRACE_POINTS; ++point)
        {
            const float sample_position = ((float)(table->frame_sample_count - 1U)
                                           * (float)point)
                / (float)(UI_TEMPLATE_WAVE_WT_TRACE_POINTS - 1U);
            const float sample = ui_renderer_template_wave_wavetable_sample(table,
                                                                             frame_position,
                                                                             sample_position);
            cache->y[layer][point] = (int8_t)ui_renderer_template_wave_wavetable_sample_to_y(
                sample,
                peak,
                center_y,
                UI_TEMPLATE_WAVE_WT_CONTEXT_AMP_HALF_PX,
                top,
                bottom);
        }
    }

    cache->valid = 1U;
    return 1U;
}

static uint8_t ui_renderer_template_wave_wavetable_cache_matches(uint16_t global_slot,
                                                                 float start_value,
                                                                 float end_value,
                                                                 const wavetable_slot_t *table,
                                                                 const wavetable_preview_t *preview)
{
    const ui_renderer_template_wavetable_cache_t *const cache =
        &g_ui_renderer_template_wavetable_cache;
    return (uint8_t)((cache->valid != 0U)
            && (cache->global_slot == global_slot)
            && (cache->table_generation == table->generation)
            && (cache->preview_generation == preview->generation)
            && (cache->frame_count == table->frame_count)
            && (cache->global_peak == preview->global_peak)
            && (cache->start_value == start_value)
            && (cache->end_value == end_value));
}

static uint8_t ui_renderer_template_build_wave_wavetable_pos_cache(float pos_value,
                                                                    float start_frame_position,
                                                                    float end_frame_position,
                                                                    const wavetable_slot_t *table,
                                                                    const wavetable_preview_t *preview)
{
    ui_renderer_template_wavetable_cache_t *const cache =
        &g_ui_renderer_template_wavetable_cache;
    if ((cache->valid == 0U)
            || (table == NULL)
            || (preview == NULL)
            || (preview->global_peak <= 1U))
    {
        cache->pos_valid = 0U;
        return 0U;
    }

    const int top = UI_TEMPLATE_WAVE_WT_Y + 1;
    const int bottom = UI_TEMPLATE_WAVE_WT_Y + UI_TEMPLATE_WAVE_WT_H - 2;
    const float peak = (float)preview->global_peak;
    const int x_offset = (int)(((1.0f - pos_value) * (float)UI_TEMPLATE_WAVE_WT_DEPTH_X_PX) + 0.5f);
    const int x_start = UI_TEMPLATE_WAVE_WT_X + 1 + x_offset;
    const int center_y = ui_renderer_template_wave_wavetable_depth_y(UI_TEMPLATE_WAVE_WT_Y + 1,
                                                                      pos_value);
    const float frame_position = start_frame_position
        + ((end_frame_position - start_frame_position) * pos_value);

    cache->pos_value = pos_value;
    cache->pos_x_start = (uint8_t)x_start;
    for (uint8_t point = 0U; point < UI_TEMPLATE_WAVE_WT_TRACE_POINTS; ++point)
    {
        const float sample_position = ((float)(table->frame_sample_count - 1U)
                                       * (float)point)
            / (float)(UI_TEMPLATE_WAVE_WT_TRACE_POINTS - 1U);
        const float sample = ui_renderer_template_wave_wavetable_sample(table,
                                                                         frame_position,
                                                                         sample_position);
        cache->pos_y[point] = (int8_t)ui_renderer_template_wave_wavetable_sample_to_y(
            sample,
            peak,
            center_y,
            UI_TEMPLATE_WAVE_WT_POS_AMP_HALF_PX,
            top,
            bottom);
    }
    cache->pos_valid = 1U;
    return 1U;
}

static void ui_renderer_template_draw_wave_wavetable_cache(void)
{
    const ui_renderer_template_wavetable_cache_t *const cache =
        &g_ui_renderer_template_wavetable_cache;
    if ((cache->valid == 0U) || (cache->layer_count != UI_TEMPLATE_WAVE_WT_LAYER_TARGET))
    {
        return;
    }

    for (uint8_t layer = 0U; layer < cache->layer_count; ++layer)
    {
        const int x_width = UI_TEMPLATE_WAVE_WT_INNER_W - UI_TEMPLATE_WAVE_WT_DEPTH_X_PX;
        int prev_x = ui_renderer_template_wave_wavetable_trace_x(cache->x_start[layer],
                                                                 x_width,
                                                                 0U);
        int prev_y = cache->y[layer][0];
        for (uint8_t point = 1U; point < UI_TEMPLATE_WAVE_WT_TRACE_POINTS; ++point)
        {
            const int x = ui_renderer_template_wave_wavetable_trace_x(cache->x_start[layer],
                                                                        x_width,
                                                                        point);
            const int y = cache->y[layer][point];
            if (((point + layer) & 1U) == 0U)
            {
                drv_display_draw_line(prev_x, prev_y, x, y);
            }
            prev_x = x;
            prev_y = y;
        }
    }
}

static void ui_renderer_template_clear_wave_wavetable_pos_halo(int x_start,
                                                               int x_width,
                                                               const int8_t *y)
{
    const int top = UI_TEMPLATE_WAVE_WT_Y + 1;
    const int bottom = UI_TEMPLATE_WAVE_WT_Y + UI_TEMPLATE_WAVE_WT_H - 2;
    drv_display_set_draw_color(0U);
    int prev_x = ui_renderer_template_wave_wavetable_trace_x(x_start, x_width, 0U);
    int prev_y = y[0];
    for (uint8_t point = 1U; point < UI_TEMPLATE_WAVE_WT_TRACE_POINTS; ++point)
    {
        const int x = ui_renderer_template_wave_wavetable_trace_x(x_start, x_width, point);
        const int value_y = y[point];
        const int y0 = ui_renderer_template_clamp_i32(prev_y - 2, top, bottom);
        const int y1 = ui_renderer_template_clamp_i32(value_y - 2, top, bottom);
        const int y2 = ui_renderer_template_clamp_i32(prev_y - 1, top, bottom);
        const int y3 = ui_renderer_template_clamp_i32(value_y - 1, top, bottom);
        const int y4 = ui_renderer_template_clamp_i32(prev_y + 1, top, bottom);
        const int y5 = ui_renderer_template_clamp_i32(value_y + 1, top, bottom);
        const int y6 = ui_renderer_template_clamp_i32(prev_y + 2, top, bottom);
        const int y7 = ui_renderer_template_clamp_i32(value_y + 2, top, bottom);
        drv_display_draw_line(prev_x, y0, x, y1);
        drv_display_draw_line(prev_x, y2, x, y3);
        drv_display_draw_line(prev_x, y4, x, y5);
        drv_display_draw_line(prev_x, y6, x, y7);
        prev_x = x;
        prev_y = value_y;
    }
    drv_display_set_draw_color(1U);
}

static void ui_renderer_template_draw_wave_wavetable_pos_cache(void)
{
    const ui_renderer_template_wavetable_cache_t *const cache =
        &g_ui_renderer_template_wavetable_cache;
    if ((cache->valid == 0U) || (cache->pos_valid == 0U))
    {
        return;
    }

    const int x_width = UI_TEMPLATE_WAVE_WT_INNER_W - UI_TEMPLATE_WAVE_WT_DEPTH_X_PX;
    ui_renderer_template_clear_wave_wavetable_pos_halo(cache->pos_x_start,
                                                       x_width,
                                                       cache->pos_y);

    int prev_x = ui_renderer_template_wave_wavetable_trace_x(cache->pos_x_start, x_width, 0U);
    int prev_y = cache->pos_y[0];
    for (uint8_t point = 1U; point < UI_TEMPLATE_WAVE_WT_TRACE_POINTS; ++point)
    {
        const int x = ui_renderer_template_wave_wavetable_trace_x(cache->pos_x_start, x_width, point);
        const int y = cache->pos_y[point];
        drv_display_draw_line(prev_x, prev_y, x, y);
        prev_x = x;
        prev_y = y;
    }
}

static void ui_renderer_template_invert_wave_cycle_interval_projected(
    float begin,
    float end,
    int x_start,
    int x_width,
    int y,
    int h)
{
    if ((end <= begin) || (x_width <= 1) || (h <= 0)) return;
    const float x_span = (float)(x_width - 1);
    const int x0 = x_start + (int)(begin * x_span + 0.5f);
    int x1 = x_start + (int)(end * x_span + 0.5f);
    if (end >= 1.0f) x1++;
    if (x1 > x0) drv_display_fill_rect(x0, y, x1 - x0, h);
}

static void ui_renderer_template_draw_wave_cycle_window_projected(
    const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
    param_id_t start_id,
    param_id_t len_id,
    const int *layer_x,
    const int *layer_width,
    const int *layer_center_y,
    uint8_t layer_count,
    int top,
    int bottom)
{
    if ((layer_x == NULL) || (layer_width == NULL) || (layer_center_y == NULL)
            || (layer_count == 0U)) return;
    float start = 0.0f;
    float len = 1.0f;
    (void)ui_renderer_template_get_visible_param_value(plock_frame_ctx,
                                                       start_id, &start, 0);
    (void)ui_renderer_template_get_visible_param_value(plock_frame_ctx,
                                                       len_id, &len, 0);
    if (start < 0.0f) start = 0.0f;
    if (start > 1.0f) start = 1.0f;
    if (len < 0.0f) len = 0.0f;
    if (len > 1.0f) len = 1.0f;
    float end = start + len;
    if (end > 1.0f) end = 1.0f;
    drv_display_set_draw_color(2U);
    for (uint8_t layer = 0U; layer < layer_count; ++layer)
    {
        int band_top = (layer == 0U)
            ? (layer_center_y[layer] - 3)
            : ((layer_center_y[layer - 1U] + layer_center_y[layer] + 1) / 2);
        int band_bottom = (layer + 1U == layer_count)
            ? (layer_center_y[layer] + 3)
            : ((layer_center_y[layer] + layer_center_y[layer + 1U] - 1) / 2);
        band_top = ui_renderer_template_clamp_i32(band_top, top, bottom);
        band_bottom = ui_renderer_template_clamp_i32(band_bottom, top, bottom);
        const int band_h = band_bottom - band_top + 1;
        ui_renderer_template_invert_wave_cycle_interval_projected(
            0.0f, start, layer_x[layer], layer_width[layer], band_top, band_h);
        ui_renderer_template_invert_wave_cycle_interval_projected(
            end, 1.0f, layer_x[layer], layer_width[layer], band_top, band_h);
    }
    drv_display_set_draw_color(1U);
}

static uint16_t ui_renderer_template_sampler_ram_slice_divisions(float slice_value)
{
    static const uint16_t k_slice_counts[] = {1U, 2U, 4U, 8U, 16U, 32U, 64U};
    int32_t index = (int32_t)(slice_value + 0.5f);
    if (index < 0)
    {
        index = 0;
    }
    if (index >= (int32_t)(sizeof(k_slice_counts) / sizeof(k_slice_counts[0])))
    {
        index = (int32_t)((sizeof(k_slice_counts) / sizeof(k_slice_counts[0])) - 1U);
    }
    return k_slice_counts[index];
}

static void ui_renderer_template_draw_sampler_ram_slice_divisions(uint16_t divisions,
                                                                  float start_value,
                                                                  float end_value,
                                                                  uint32_t frame_count,
                                                                  int inner_x,
                                                                  int inner_y,
                                                                  int inner_w,
                                                                  int inner_h)
{
    if ((divisions <= 1U) || (inner_w <= 1) || (inner_h <= 2))
    {
        return;
    }

    if (start_value < 0.0f)
    {
        start_value = 0.0f;
    }
    else if (start_value > 1.0f)
    {
        start_value = 1.0f;
    }
    if (end_value < 0.0f)
    {
        end_value = 0.0f;
    }
    else if (end_value > 1.0f)
    {
        end_value = 1.0f;
    }

    uint32_t region_begin = 0U;
    uint32_t region_end = frame_count;
    if (frame_count != 0U)
    {
        region_begin = (uint32_t)(start_value * (float)frame_count);
        region_end = (uint32_t)(end_value * (float)frame_count);
        if ((region_end == 0U) || (region_end > frame_count))
        {
            region_end = frame_count;
        }
        if (region_begin >= frame_count)
        {
            region_begin = frame_count - 1U;
        }
        if (region_end <= region_begin)
        {
            region_begin = 0U;
            region_end = frame_count;
        }
    }
    const uint32_t region_frames = region_end - region_begin;
    const int bottom = inner_y + inner_h - 2;
    drv_display_set_draw_color(2U);
    for (uint16_t i = 1U; i < divisions; ++i)
    {
        const uint32_t frame =
            region_begin + (uint32_t)(((uint64_t)region_frames * (uint64_t)i)
                                      / (uint64_t)divisions);
        const int x = inner_x + (int)(((uint64_t)frame * (uint32_t)(inner_w - 1))
                                      / (uint64_t)frame_count);
        if ((x <= inner_x) || (x >= (inner_x + inner_w)))
        {
            continue;
        }
        for (int y = inner_y + 1; y <= bottom; y += 5)
        {
            drv_display_draw_pixel(x, y, true);
        }
    }
    drv_display_set_draw_color(1U);
}

static int ui_renderer_template_sampler_ram_frame_to_x(uint32_t frame,
                                                       uint32_t frame_count,
                                                       int inner_x,
                                                       int inner_w)
{
    if ((frame_count == 0U) || (inner_w <= 0))
    {
        return -1;
    }
    if (frame >= frame_count)
    {
        frame = frame_count - 1U;
    }
    return inner_x + (int)(((uint64_t)frame * (uint32_t)(inner_w - 1)) / frame_count);
}

static void ui_renderer_template_draw_sampler_ram_playhead(uint16_t global_slot,
                                                           uint32_t frame_count,
                                                           int inner_x,
                                                           int inner_y,
                                                           int inner_w,
                                                           int inner_h)
{
    float start = ui_renderer_template_get_param_display_value(PARAM_SAMPLER_START);
    const float length = ui_renderer_template_get_param_display_value(PARAM_SAMPLER_LENGTH);
    float end = start + length;
    const float mode_value =
        ui_renderer_template_get_param_display_value(PARAM_SAMPLER_MODE);
    if (start < 0.0f) start = 0.0f;
    if (end > 1.0f) end = 1.0f;
    if (end <= start)
        return;
    uint32_t source_rate = BOARD_AUDIO_SAMPLE_RATE_HZ;
    uint16_t ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
    if (sample_global_pool_resolve_backend(global_slot, SAMPLE_GLOBAL_KIND_RAM,
                                           &ram_slot) != 0U)
    {
        const sampler_ram_slot_t *const ram = sampler_ram_pool_get_slot(ram_slot);
        if ((ram != NULL) && (ram->sample_rate != 0U))
            source_rate = ram->sample_rate;
    }
    const uint64_t region_frames =
        (uint64_t)((end - start) * (float)frame_count);
    const uint32_t duration = (uint32_t)(
        (region_frames * BOARD_AUDIO_SAMPLE_RATE_HZ) / source_rate);
    const ui_sampler_playhead_view_t view = ui_sampler_playhead_view(
        ui_renderer_template_get_edit_owner_track(), live_clock_control_sample(),
        duration, (uint8_t)(mode_value + 0.5f));
    if (view.active == 0U)
    {
        return;
    }

    const float normalized = start + ((end - start) * view.normalized_position);
    const uint32_t frame = (uint32_t)(normalized * (float)frame_count);
    const int x = ui_renderer_template_sampler_ram_frame_to_x(
        frame, frame_count, inner_x, inner_w);
    if (x < 0)
    {
        return;
    }

    const int bottom = inner_y + inner_h - 1;
    drv_display_set_draw_color(2U);
    for (int y = inner_y; y <= bottom; y += 3)
    {
        drv_display_draw_pixel(x, y, true);
    }
    drv_display_set_draw_color(1U);
}

static void ui_renderer_template_draw_sampler_ram_waveform(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx)
{
    const int wave_x = UI_TEMPLATE_SAMPLER_WAVE_X;
    const int wave_y = UI_TEMPLATE_SAMPLER_WAVE_Y;
    const int wave_w = UI_TEMPLATE_SAMPLER_WAVE_W;
    const int wave_h = UI_TEMPLATE_SAMPLER_WAVE_H;
    const int inner_x = wave_x + 1;
    const int inner_y = wave_y + 1;
    const int inner_w = UI_TEMPLATE_SAMPLER_WAVE_INNER_W;
    const int inner_h = UI_TEMPLATE_SAMPLER_WAVE_INNER_H;
    const int center_y = inner_y + (inner_h / 2);
    const uint16_t global_slot = ui_renderer_template_sampler_ram_selected_global_slot();
    const sample_ram_waveform_overview_t *const overview =
        sampler_ram_pool_get_waveform_for_global(global_slot);

    drv_display_draw_rect(wave_x, wave_y, wave_w, wave_h);

    for (int x = inner_x; x < (inner_x + inner_w); x += 2)
    {
        drv_display_draw_pixel(x, center_y, true);
    }

    if (overview == NULL)
    {
        return;
    }
    if (!((overview->state == SAMPLE_RAM_WAVEFORM_READY)
          || (overview->state == SAMPLE_RAM_WAVEFORM_BUILDING))
        || (overview->columns == 0U)
        || (overview->frame_count == 0U))
    {
        return;
    }

    const uint16_t peak = overview->global_peak;
    if (peak > 1U)
    {
        uint16_t columns = (overview->state == SAMPLE_RAM_WAVEFORM_READY)
                               ? overview->columns
                               : overview->ready_columns;
        if (columns > (uint16_t)inner_w)
        {
            columns = (uint16_t)inner_w;
        }
        for (uint16_t col = 0U; col < columns; ++col)
        {
            const int y_top = ui_renderer_template_sampler_ram_amp_to_y(overview->max[col],
                                                                        peak,
                                                                        inner_y,
                                                                        inner_h);
            const int y_bottom = ui_renderer_template_sampler_ram_amp_to_y(overview->min[col],
                                                                           peak,
                                                                           inner_y,
                                                                           inner_h);
            drv_display_draw_line(inner_x + (int)col, y_top, inner_x + (int)col, y_bottom);
        }
    }

    float start_value = 0.0f;
    float length_value = 1.0f;
    float loop_value = 0.0f;
    float slice_value = 0.0f;
    (void)ui_renderer_template_get_visible_param_value(plock_frame_ctx, PARAM_SAMPLER_START, &start_value, 0);
    (void)ui_renderer_template_get_visible_param_value(plock_frame_ctx, PARAM_SAMPLER_LENGTH, &length_value, 0);
    float end_value = start_value + length_value;
    if (end_value > 1.0f)
    {
        end_value = 1.0f;
    }
    (void)ui_renderer_template_get_visible_param_value(plock_frame_ctx, PARAM_SAMPLER_LOOP_START, &loop_value, 0);
    (void)ui_renderer_template_get_visible_param_value(plock_frame_ctx, PARAM_SAMPLER_SLICE_COUNT, &slice_value, 0);
    ui_renderer_template_draw_sampler_ram_slice_divisions(
        ui_renderer_template_sampler_ram_slice_divisions(slice_value),
        start_value,
        end_value,
        overview->frame_count,
        inner_x,
        inner_y,
        inner_w,
        inner_h);
    ui_renderer_template_draw_sampler_ram_playhead(global_slot,
                                                  overview->frame_count,
                                                  inner_x,
                                                  inner_y,
                                                  inner_w,
                                                  inner_h);
    ui_renderer_template_draw_sampler_ram_marker(start_value,
                                                 overview->frame_count,
                                                 inner_x,
                                                 inner_y,
                                                 inner_w,
                                                 inner_h,
                                                 'S',
                                                 0U);
    ui_renderer_template_draw_sampler_ram_marker(end_value,
                                                 overview->frame_count,
                                                 inner_x,
                                                 inner_y,
                                                 inner_w,
                                                 inner_h,
                                                 'E',
                                                 0U);
    if (slice_value < 0.5f)
    {
        ui_renderer_template_draw_sampler_ram_marker(loop_value,
                                                     overview->frame_count,
                                                     inner_x,
                                                     inner_y,
                                                     inner_w,
                                                     inner_h,
                                                     'L',
                                                     1U);
    }
}

static void ui_renderer_template_draw_wave_wavetable_preview(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                             const ui_template_subpage_t *subpage)
{
    float table_value = 0.0f;
    float pos_value = 0.0f;
    const float first_frame_value = 0.0f;
    const float last_frame_value = 1.0f;

    if ((subpage == NULL)
            || (ui_renderer_template_get_visible_param_value(plock_frame_ctx, subpage->param_bank.params[0], &table_value, 0) == 0U))
    {
        return;
    }

    (void)ui_renderer_template_get_visible_param_value(plock_frame_ctx, subpage->param_bank.params[1], &pos_value, 0);
    if (pos_value < 0.0f) { pos_value = 0.0f; }
    if (pos_value > 1.0f) { pos_value = 1.0f; }
    const uint16_t logical_slot = (uint16_t)((table_value < 0.0f) ? 0.0f : (table_value + 0.5f));
    uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (project_control_resolve_wavetable_runtime(
            logical_slot, &global_slot) == 0U)
    {
        return;
    }
    const wavetable_preview_t *const preview = wavetable_pool_get_preview_for_global(global_slot);

    if ((preview == 0)
            || (preview->state != WAVETABLE_PREVIEW_READY)
            || (preview->columns == 0U)
            || (preview->frame_count == 0U))
    {
        return;
    }

    uint16_t wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
    if (sample_global_pool_resolve_backend(global_slot,
                                           SAMPLE_GLOBAL_KIND_WAVETABLE,
                                           &wavetable_slot) == 0U)
    {
        return;
    }

    const wavetable_slot_t *const table = wavetable_pool_get_slot(wavetable_slot);
    if ((table == NULL)
            || (table->state != WAVETABLE_SLOT_READY)
            || (table->data == NULL)
            || (table->frame_count == 0U)
            || (table->frame_sample_count != WAVETABLE_FRAME_SAMPLE_COUNT))
    {
        return;
    }

    const uint32_t max_frame = (table->frame_count > 1U) ? (table->frame_count - 1U) : 0U;
    const float first_frame_position = 0.0f;
    const float last_frame_position = (float)max_frame;

    if (ui_renderer_template_wave_wavetable_cache_matches(global_slot,
                                                           first_frame_value,
                                                           last_frame_value,
                                                           table,
                                                           preview) == 0U)
    {
        (void)ui_renderer_template_build_wave_wavetable_cache(global_slot,
                                                              first_frame_value,
                                                              last_frame_value,
                                                              first_frame_position,
                                                              last_frame_position,
                                                              table,
                                                              preview);
    }

    if ((g_ui_renderer_template_wavetable_cache.pos_valid == 0U)
            || (g_ui_renderer_template_wavetable_cache.pos_value != pos_value))
    {
        (void)ui_renderer_template_build_wave_wavetable_pos_cache(pos_value,
                                                                   first_frame_position,
                                                                   last_frame_position,
                                                                   table,
                                                                   preview);
    }

    ui_renderer_template_draw_wave_wavetable_cache();
    ui_renderer_template_draw_wave_wavetable_pos_cache();
    int layer_x[UI_TEMPLATE_WAVE_WT_LAYER_TARGET];
    int layer_width[UI_TEMPLATE_WAVE_WT_LAYER_TARGET];
    int layer_center_y[UI_TEMPLATE_WAVE_WT_LAYER_TARGET];
    for (uint8_t layer = 0U; layer < UI_TEMPLATE_WAVE_WT_LAYER_TARGET; ++layer)
    {
        layer_x[layer] = g_ui_renderer_template_wavetable_cache.x_start[layer];
        layer_width[layer] = UI_TEMPLATE_WAVE_WT_INNER_W
            - UI_TEMPLATE_WAVE_WT_DEPTH_X_PX;
        layer_center_y[layer] = g_ui_renderer_template_wavetable_cache.center_y[layer];
    }
    ui_renderer_template_draw_wave_cycle_window_projected(
        plock_frame_ctx,
        subpage->param_bank.params[2],
        subpage->param_bank.params[3],
        layer_x,
        layer_width,
        layer_center_y,
        UI_TEMPLATE_WAVE_WT_LAYER_TARGET,
        UI_TEMPLATE_WAVE_WT_Y + 4,
        UI_TEMPLATE_WAVE_WT_Y + UI_TEMPLATE_WAVE_WT_H - 2);
}

static void ui_renderer_template_draw_sampler_ram_slot_text(const ui_template_page_state_t *state,
                                                           const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                           uint8_t slot,
                                                           param_id_t id)
{
    const int x = g_ui_template_frame_x[slot];
    char name_txt[24] = "-";
    char value_txt[20] = "---";
    char bottom_txt[24] = "-";
    uint8_t draw_name_inverted = 0U;
    float value = 0.0f;

    (void)ui_renderer_template_prepare_param_slot_texts(state,
                                                        plock_frame_ctx,
                                                        slot,
                                                        id,
                                                        &value,
                                                        &draw_name_inverted,
                                                        name_txt,
                                                        (uint32_t)sizeof(name_txt),
                                                        value_txt,
                                                        (uint32_t)sizeof(value_txt),
                                                        bottom_txt,
                                                        (uint32_t)sizeof(bottom_txt),
                                                        NULL);
    (void)value;

    drv_display_set_font(&FONT_4X6);
    ui_renderer_template_fit_text(bottom_txt, UI_TEMPLATE_SAMPLER_TEXT_MAX_PX);

    const uint8_t text_x = (uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, bottom_txt);
    drv_display_draw_text(text_x, UI_TEMPLATE_SAMPLER_LABEL_Y, bottom_txt);

    if ((draw_name_inverted != 0U) && (drv_display_text_width(bottom_txt) != 0U))
    {
        const uint8_t text_w = drv_display_text_width(bottom_txt);
        ui_renderer_template_fill_lock_label_frame(text_x,
                                                   UI_TEMPLATE_SAMPLER_LABEL_Y - 1,
                                                   text_w,
                                                   8);
        drv_display_draw_text_inverted(text_x, UI_TEMPLATE_SAMPLER_LABEL_Y, bottom_txt);
    }
}

static void ui_renderer_template_draw_sampler_ram_wave_placeholder(const ui_template_page_state_t *state,
                                                                   const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                                   const ui_template_subpage_t *subpage)
{
    char sample_label[40];
    ui_renderer_template_sampler_ram_sample_label(sample_label, (uint32_t)sizeof(sample_label));

    drv_display_set_font(&FONT_4X6);
    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(0, OLED_WIDTH, sample_label),
                          UI_TEMPLATE_SAMPLER_NAME_Y,
                          sample_label);
    ui_renderer_template_draw_sampler_ram_waveform(plock_frame_ctx);

    if (subpage == NULL)
    {
        return;
    }

    for (uint8_t slot = 0U; slot < 4U; ++slot)
    {
        ui_renderer_template_draw_sampler_ram_slot_text(state,
                                                        plock_frame_ctx,
                                                        slot,
                                                        subpage->param_bank.params[slot]);
    }
}

static void ui_renderer_template_draw_wave_wavetable_placeholder(const ui_template_page_state_t *state,
                                                                 const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                                 const ui_template_subpage_t *subpage)
{
    ui_renderer_template_draw_wave_wavetable_preview(plock_frame_ctx, subpage);

    if (subpage == NULL)
    {
        return;
    }

    for (uint8_t slot = 0U; slot < 4U; ++slot)
    {
        ui_renderer_template_draw_sampler_ram_slot_text(state,
                                                        plock_frame_ctx,
                                                        slot,
                                                        subpage->param_bank.params[slot]);
    }
}

static void ui_renderer_template_draw_synth_waveform(const int8_t *points,
                                                     int x, int y, int w, int h)
{
    if ((points == NULL) || (w < 3) || (h < 3)) return;
    drv_display_draw_rect(x, y, w, h);
    const int inner_x = x + 1;
    const int inner_y = y + 1;
    const int inner_w = w - 2;
    const int inner_h = h - 2;
    const int center = inner_y + (inner_h / 2);
    for (int px = inner_x; px < inner_x + inner_w; px += 3)
        drv_display_draw_pixel(px, center, true);
    int previous_x = inner_x;
    int previous_y = center - (((int)points[0] * (inner_h - 2)) / 254);
    for (uint8_t i = 1U; i < SYNTH_WAVEFORM_POINT_COUNT; ++i)
    {
        const int px = inner_x + (int)(((uint32_t)i * (uint32_t)(inner_w - 1))
                                      / (SYNTH_WAVEFORM_POINT_COUNT - 1U));
        const int py = center - (((int)points[i] * (inner_h - 2)) / 254);
        drv_display_draw_line(previous_x, previous_y, px, py);
        previous_x = px;
        previous_y = py;
    }
}

