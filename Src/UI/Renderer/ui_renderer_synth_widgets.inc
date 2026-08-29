static void ui_renderer_template_draw_synth_waveform_large(const int8_t *points)
{
    ui_renderer_template_draw_synth_waveform(points, 1, 17, 126, 29);
}

static void ui_renderer_template_draw_synth_waveform_half(const int8_t *points,
                                                          uint8_t osc)
{
    ui_renderer_template_draw_synth_waveform(points, (osc == 0U) ? 1 : 65,
                                             16, 62, 30);
}

static uint8_t ui_renderer_template_synth_live_snapshot(
    synth_waveform_snapshot_t *snapshot, synth_waveform_engine_t engine)
{
    return (uint8_t)((synth_waveform_control_read(snapshot) != 0U)
        && (snapshot->entity_id == ui_renderer_template_get_edit_owner_track())
        && (snapshot->engine == engine));
}

static void ui_renderer_template_draw_wave_live_cursor(
    const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
    param_id_t pos_id)
{
    float pos = 0.0f;
    (void)ui_renderer_template_get_visible_param_value(plock_frame_ctx, pos_id, &pos, 0);
    if (pos < 0.0f) pos = 0.0f;
    if (pos > 1.0f) pos = 1.0f;
    const int left = 1;
    const int right = (int)OLED_WIDTH - 2;
    const int marker = left + (int)(pos * (float)(right - left) + 0.5f);
    drv_display_draw_line(marker, 16, marker, 20);
}

static void ui_renderer_template_draw_synth_live_placeholder(
    const ui_template_page_state_t *state,
    const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
    const ui_template_subpage_t *subpage,
    uint8_t osc_mask,
    uint8_t show_cursor,
    synth_waveform_engine_t engine)
{
    synth_waveform_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    (void)ui_renderer_template_synth_live_snapshot(&snapshot, engine);
    if (osc_mask == 0x03U)
    {
        ui_renderer_template_draw_synth_waveform_half(snapshot.points[0], 0U);
        ui_renderer_template_draw_synth_waveform_half(snapshot.points[1], 1U);
    }
    else
    {
        const uint8_t osc = (osc_mask == 0x02U) ? 1U : 0U;
        ui_renderer_template_draw_synth_waveform_large(snapshot.points[osc]);
        if (show_cursor != 0U)
            ui_renderer_template_draw_wave_live_cursor(plock_frame_ctx,
                                                       (osc == 0U) ? PARAM_WAVE_OSC1_POS : PARAM_WAVE_OSC2_POS);
    }
    for (uint8_t slot = 0U; slot < 4U; ++slot)
        ui_renderer_template_draw_sampler_ram_slot_text(state, plock_frame_ctx, slot,
                                                        subpage->param_bank.params[slot]);
}

static uint8_t ui_renderer_template_wave_table_for_param(
    const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
    param_id_t table_id,
    const wavetable_slot_t **out_table,
    const wavetable_preview_t **out_preview)
{
    float table_value = 0.0f;
    if ((out_table == NULL) || (out_preview == NULL)
            || (ui_renderer_template_get_visible_param_value(plock_frame_ctx,
                                                             table_id, &table_value, 0) == 0U)) return 0U;
    const uint16_t logical = (uint16_t)((table_value < 0.0f) ? 0.0f : table_value + 0.5f);
    uint16_t global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    uint16_t slot = WAVETABLE_POOL_INVALID_SLOT;
    if ((project_control_resolve_wavetable_runtime(logical, &global) == 0U)
            || (sample_global_pool_resolve_backend(global, SAMPLE_GLOBAL_KIND_WAVETABLE, &slot) == 0U)) return 0U;
    *out_table = wavetable_pool_get_slot(slot);
    *out_preview = wavetable_pool_get_preview_for_global(global);
    return (uint8_t)((*out_table != NULL) && ((*out_table)->state == WAVETABLE_SLOT_READY)
        && ((*out_table)->data != NULL) && ((*out_table)->frame_count != 0U)
        && (*out_preview != NULL) && ((*out_preview)->state == WAVETABLE_PREVIEW_READY)
        && ((*out_preview)->global_peak > 1U));
}

static void ui_renderer_template_draw_wave_wavetable_compact(
    const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
    param_id_t table_id,
    param_id_t start_id,
    param_id_t len_id,
    int x)
{
    const wavetable_slot_t *table = NULL;
    const wavetable_preview_t *preview = NULL;
    const int y = 15;
    const int w = 62;
    const int h = 31;
    drv_display_draw_rect(x, y, w, h);
    if (ui_renderer_template_wave_table_for_param(plock_frame_ctx, table_id,
                                                  &table, &preview) == 0U) return;
    const uint8_t layers = 5U;
    const uint8_t points = 40U;
    const int top = y + 2;
    const int bottom = y + h - 2;
    int layer_x[5U];
    int layer_width[5U];
    int layer_center_y[5U];
    for (uint8_t layer = 0U; layer < layers; ++layer)
    {
        const float depth = (float)layer / (float)(layers - 1U);
        const int x0 = x + 2 + (int)((1.0f - depth) * 5.0f + 0.5f);
        const int width = w - 9;
        const int center = y + 8 + (int)(depth * 11.0f + 0.5f);
        layer_x[layer] = x0;
        layer_width[layer] = width;
        layer_center_y[layer] = center;
        const float frame = depth * (float)(table->frame_count - 1U);
        int px0 = x0;
        float sample = ui_renderer_template_wave_wavetable_sample(table, frame, 0.0f);
        int py0 = ui_renderer_template_wave_wavetable_sample_to_y(
            sample, (float)preview->global_peak, center, 2, top, bottom);
        for (uint8_t point = 1U; point < points; ++point)
        {
            const float sample_pos = ((float)(table->frame_sample_count - 1U) * point)
                / (float)(points - 1U);
            sample = ui_renderer_template_wave_wavetable_sample(table, frame, sample_pos);
            const int px = x0 + (int)(((uint32_t)point * (uint32_t)(width - 1)) / (points - 1U));
            const int py = ui_renderer_template_wave_wavetable_sample_to_y(
                sample, (float)preview->global_peak, center, 2, top, bottom);
            drv_display_draw_line(px0, py0, px, py);
            px0 = px;
            py0 = py;
        }
    }
    ui_renderer_template_draw_wave_cycle_window_projected(
        plock_frame_ctx, start_id, len_id,
        layer_x, layer_width, layer_center_y, layers, top, bottom);
}

static void ui_renderer_template_draw_wave_classic_common(
    const ui_template_page_state_t *state,
    const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
    const ui_template_subpage_t *subpage)
{
    ui_renderer_template_draw_wave_wavetable_compact(
        plock_frame_ctx, PARAM_WAVE_OSC1_TABLE,
        PARAM_WAVE_OSC1_START, PARAM_WAVE_OSC1_LEN, 1);
    ui_renderer_template_draw_wave_wavetable_compact(
        plock_frame_ctx, PARAM_WAVE_OSC2_TABLE,
        PARAM_WAVE_OSC2_START, PARAM_WAVE_OSC2_LEN, 65);
    for (uint8_t slot = 0U; slot < 4U; ++slot)
        ui_renderer_template_draw_sampler_ram_slot_text(state, plock_frame_ctx, slot,
                                                        subpage->param_bank.params[slot]);
}

static uint8_t ui_renderer_template_widget_type_is_bar(uiw_widget_type_t widget_type)
{
    return (uint8_t)((widget_type == UIW_WIDGET_BAR) || (widget_type == UIW_WIDGET_BIPOLAR_BAR));
}

static uint8_t ui_renderer_template_custom_widget_is_bar(ui_template_custom_widget_kind_t kind)
{
    return (uint8_t)((kind == UI_TEMPLATE_CUSTOM_WIDGET_LFO_RATE)
            || (kind == UI_TEMPLATE_CUSTOM_WIDGET_LFO_DEPTH));
}

static void ui_renderer_template_draw_bar_value_text(int widget_x,
                                                     int widget_y,
                                                     int widget_w,
                                                     int widget_h,
                                                     const char *value_txt)
{
    if ((value_txt == NULL) || (value_txt[0] == '\0'))
    {
        return;
    }

    char fitted_value[20];
    (void)snprintf(fitted_value, (uint32_t)sizeof(fitted_value), "%s", value_txt);
    drv_display_set_font(&FONT_4X6);
    ui_renderer_template_fit_text(fitted_value, UI_TEMPLATE_CARD_LABEL_MAX_PX);
    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(widget_x, widget_w, fitted_value),
                          (uint8_t)(widget_y + 6),
                          fitted_value);
}

static void ui_renderer_template_draw_param_slot(const ui_template_page_state_t *state,
                                                 const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                 const ui_template_subpage_t *subpage,
                                                 ui_template_custom_widget_kind_t grouped_widget,
                                                 uint8_t grouped_widget_drawn,
                                                 uint8_t slot,
                                                 param_id_t id)
{
    const int x = g_ui_template_frame_x[slot];
    const int y = UI_TEMPLATE_FRAME_Y;
    const int widget_x = x + UI_TEMPLATE_CARD_WIDGET_X_PAD;
    const int widget_y = y + UI_TEMPLATE_CARD_WIDGET_Y;
    const uint8_t audio_fx_shared_slot = (uint8_t)(
        (grouped_widget == UI_TEMPLATE_CUSTOM_WIDGET_AUDIO_FX_GROUP)
        && (slot < 3U));
    const uint8_t slot_locked = ui_macro_interaction_param_is_locked(id);

    if ((slot_locked != 0U) && (audio_fx_shared_slot == 0U))
    {
        drv_display_fill_rect(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H);
        drv_display_set_draw_color(0U);
    }

    if (audio_fx_shared_slot == 0U)
    {
        ui_renderer_template_draw_param_frame(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H);
    }

    if ((state != NULL) && (state->virtual_slot_text != NULL))
    {
        char virt_name[24];
        char virt_value[20];
        if (state->virtual_slot_text(slot,
                                     virt_name,
                                     (uint32_t)sizeof(virt_name),
                                     virt_value,
                                     (uint32_t)sizeof(virt_value)) != 0U)
        {
            drv_display_set_font(&FONT_4X6);
            ui_renderer_template_fit_text(virt_name, UI_TEMPLATE_CARD_LABEL_MAX_PX);
            ui_renderer_template_fit_text(virt_value, UI_TEMPLATE_CARD_LABEL_MAX_PX);
            uiw_draw_enum_text(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H, virt_value);
            drv_display_set_font(&FONT_4X6);
            drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, virt_name),
                                  (uint8_t)(y + UI_TEMPLATE_CARD_LABEL_Y),
                                  virt_name);
            return;
        }
    }

    if (id >= PARAM_COUNT)
    {
        if (slot_locked != 0U)
        {
            drv_display_set_draw_color(1U);
        }

        if ((state != NULL) && (state->virtual_slot_text != NULL))
        {
            char virt_name[24];
            char virt_value[20];
            const uint8_t has_virtual = state->virtual_slot_text(slot,
                                                                 virt_name,
                                                                 (uint32_t)sizeof(virt_name),
                                                                 virt_value,
                                                                 (uint32_t)sizeof(virt_value));
            if (has_virtual != 0U)
            {
                drv_display_set_font(&FONT_4X6);
                ui_renderer_template_fit_text(virt_name, UI_TEMPLATE_CARD_LABEL_MAX_PX);
                ui_renderer_template_fit_text(virt_value, UI_TEMPLATE_CARD_LABEL_MAX_PX);
                uiw_draw_enum_text(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H, virt_value);
                drv_display_set_font(&FONT_4X6);
                drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, virt_name),
                                      (uint8_t)(y + UI_TEMPLATE_CARD_LABEL_Y),
                                      virt_name);
                return;
            }
        }

        drv_display_set_font(&FONT_4X6);
        drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, "-"), (uint8_t)(y + 14), "-");
        return;
    }

    const param_desc_t *desc = &param_registry[id];
    float value = 0.0f;
    uint8_t draw_name_inverted = 0U;
    const char *enum_label = NULL;
    char value_txt[20];
    char name_txt[24];
    char bottom_txt[24];
    uint8_t flash_active = 0U;

    if (ui_renderer_template_prepare_param_slot_texts(state,
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
                                                      &flash_active) == 0U)
    {
        return;
    }

    if (audio_fx_shared_slot != 0U)
    {
        drv_display_set_font(&FONT_4X6);
        ui_renderer_template_fit_text(bottom_txt, UI_TEMPLATE_CARD_LABEL_MAX_PX);
        goto draw_bottom_label;
    }

    if ((desc->display_type == PARAM_DISPLAY_ENUM) && (desc->labels != NULL))
    {
        const int32_t index = (int32_t)(value + 0.5f);
        if (index >= 0)
        {
            enum_label = desc->labels[index];
        }
    }

    drv_display_set_font(&FONT_4X6);
    ui_renderer_template_fit_text(value_txt, UI_TEMPLATE_CARD_LABEL_MAX_PX);
    const ui_template_custom_widget_kind_t custom_widget =
        ui_renderer_template_resolve_custom_widget(state, subpage, slot, id);
    if ((flash_active == 0U) && (custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP))
    {
        ui_renderer_template_filter_snapshot_t snapshot;
        if ((ui_renderer_template_filter_snapshot_capture(plock_frame_ctx, &snapshot) != 0U)
                && (snapshot.mode == 0U))
        {
            bottom_txt[0] = '\0';
        }
    }
    if ((flash_active == 0U) && (custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_INACTIVE))
    {
        bottom_txt[0] = '\0';
    }
    if (ui_renderer_template_custom_widget_is_bar(custom_widget) != 0U)
    {
        (void)snprintf(bottom_txt, (uint32_t)sizeof(bottom_txt), "%s", name_txt);
    }
    ui_renderer_template_fit_text(bottom_txt, UI_TEMPLATE_CARD_LABEL_MAX_PX);

    if (custom_widget != UI_TEMPLATE_CUSTOM_WIDGET_NONE)
    {
        if (custom_widget == grouped_widget)
        {
            if (grouped_widget_drawn != 0U)
            {
                goto draw_bottom_label;
            }
        }
        else
        {
        if (slot_locked != 0U)
        {
            drv_display_set_draw_color(0U);
        }
        if (ui_renderer_template_draw_lfo_custom_widget(custom_widget,
                                                        widget_x,
                                                        widget_y,
                                                        UI_TEMPLATE_CARD_WIDGET_W,
                                                        UI_TEMPLATE_CARD_WIDGET_H,
                                                        id,
                                                        value) != 0U)
        {
            if (ui_renderer_template_custom_widget_is_bar(custom_widget) != 0U)
            {
                ui_renderer_template_draw_bar_value_text(widget_x,
                                                         widget_y,
                                                         UI_TEMPLATE_CARD_WIDGET_W,
                                                         UI_TEMPLATE_CARD_WIDGET_H,
                                                         value_txt);
            }
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(0U);
            }
            goto draw_bottom_label;
        }
        if ((custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_PLAY_NOTE)
                ? (ui_renderer_template_draw_play_note_text(id,
                                                            widget_x,
                                                            widget_y,
                                                            UI_TEMPLATE_CARD_WIDGET_W,
                                                            UI_TEMPLATE_CARD_WIDGET_H,
                                                            value) != 0U)
                : ((custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_LFO_DEST)
                ? (ui_renderer_template_draw_lfo_dest_text(widget_x,
                                                           widget_y,
                                                           UI_TEMPLATE_CARD_WIDGET_W,
                                                           UI_TEMPLATE_CARD_WIDGET_H,
                                                           value) != 0U)
                : ((custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_LFO_PHASE)
                ? (ui_renderer_template_draw_lfo_phase(widget_x,
                                                            widget_y,
                                                            UI_TEMPLATE_CARD_WIDGET_W,
                                                            UI_TEMPLATE_CARD_WIDGET_H,
                                                            id,
                                                            value) != 0U)
                : ((custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_MATRIX_SLOT)
                ? (ui_renderer_template_draw_matrix_slot_widget(widget_x,
                                                                widget_y,
                                                                UI_TEMPLATE_CARD_WIDGET_W,
                                                                UI_TEMPLATE_CARD_WIDGET_H,
                                                                value) != 0U)
                : ((custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_MATRIX_SOURCE)
                ? (ui_renderer_template_draw_matrix_source_text(widget_x,
                                                                widget_y,
                                                                UI_TEMPLATE_CARD_WIDGET_W,
                                                                UI_TEMPLATE_CARD_WIDGET_H,
                                                                value) != 0U)
                : ((custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_STACK_WAVEFORM)
                ? (ui_renderer_template_draw_stack_waveform_widget(plock_frame_ctx,
                                                                   widget_x,
                                                                   widget_y,
                                                                   UI_TEMPLATE_CARD_WIDGET_W,
                                                                   UI_TEMPLATE_CARD_WIDGET_H,
                                                                   id,
                                                                   value) != 0U)
                : (((custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_TRACK)
                    || (custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_TYPE)
                    || (custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_INACTIVE)
                    || (custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_MIDI_CHANNEL)
                    || (custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_MIDI_SOURCE))
                ? (ui_renderer_template_draw_custom_track_cfg(plock_frame_ctx,
                                                              custom_widget,
                                                              widget_x,
                                                              widget_y,
                                                              UI_TEMPLATE_CARD_WIDGET_W,
                                                              UI_TEMPLATE_CARD_WIDGET_H,
                                                              value) != 0U)
                : (((custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_FILTER_TYPE)
                    || (custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CUTOFF)
                    || (custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_FILTER_RESONANCE)
                    || (custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP))
                ? (ui_renderer_template_draw_custom_filter(plock_frame_ctx,
                                                           custom_widget,
                                                           widget_x,
                                                           widget_y,
                                                           UI_TEMPLATE_CARD_WIDGET_W,
                                                           UI_TEMPLATE_CARD_WIDGET_H,
                                                           value,
                                                           0U) != 0U)
                : (ui_renderer_template_draw_custom_adsr(plock_frame_ctx,
                                                         custom_widget,
                                                         widget_x,
                                                         widget_y,
                                                         UI_TEMPLATE_CARD_WIDGET_W,
                                                         UI_TEMPLATE_CARD_WIDGET_H) != 0U)))))))))
        {
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(0U);
            }
            goto draw_bottom_label;
        }
        if (slot_locked != 0U)
        {
            drv_display_set_draw_color(1U);
        }
        }
    }

    const uiw_widget_type_t widget_type = ui_renderer_template_resolve_widget_type(state, slot, id, desc, enum_label, value_txt);
    const uint8_t widget_is_bar = ui_renderer_template_widget_type_is_bar(widget_type);
    if (widget_is_bar != 0U)
    {
        (void)snprintf(bottom_txt, (uint32_t)sizeof(bottom_txt), "%s", name_txt);
        ui_renderer_template_fit_text(bottom_txt, UI_TEMPLATE_CARD_LABEL_MAX_PX);
    }

    switch (widget_type)
    {
        case UIW_WIDGET_EMPTY:
            break;

        case UIW_WIDGET_SWITCH:
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(0U);
            }
            uiw_draw_switch(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H, (value >= 0.5f) ? 1U : 0U);
            ui_renderer_template_draw_bar_value_text(widget_x,
                                                     widget_y,
                                                     UI_TEMPLATE_CARD_WIDGET_W,
                                                     UI_TEMPLATE_CARD_WIDGET_H,
                                                     value_txt);
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(1U);
            }
            break;

        case UIW_WIDGET_ENUM_TEXT:
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(1U);
            }
            uiw_draw_enum_text(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H, (enum_label != NULL) ? enum_label : value_txt);
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(0U);
            }
            break;

        case UIW_WIDGET_JACK:
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(0U);
            }
            uiw_draw_jack_icon(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H);
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(1U);
            }
            break;

        case UIW_WIDGET_KEYBOARD:
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(0U);
            }
            uiw_draw_keyboard_icon(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H);
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(1U);
            }
            break;

        case UIW_WIDGET_WAVE_ICON:
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(0U);
            }
            uiw_draw_wave_icon(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H, enum_label);
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(1U);
            }
            break;

        case UIW_WIDGET_FILTER_ICON:
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(0U);
            }
            uiw_draw_filter_icon(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H, enum_label);
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(1U);
            }
            break;

        case UIW_WIDGET_ALGO_ICON:
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(0U);
            }
            uiw_draw_algo_icon(widget_x,
                               widget_y,
                               UI_TEMPLATE_CARD_WIDGET_W,
                               UI_TEMPLATE_CARD_WIDGET_H,
                               (uint8_t)(value + 0.5f));
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(1U);
            }
            break;

        case UIW_WIDGET_BAR:
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(0U);
            }
            uiw_draw_value_bar(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H, value, desc->min, desc->max);
            ui_renderer_template_draw_bar_value_text(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H, value_txt);
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(1U);
            }
            break;

        case UIW_WIDGET_BIPOLAR_BAR:
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(0U);
            }
            if (!((desc->min < 0.0f) && (desc->max > 0.0f)))
            {
                const uint8_t authority_track = ui_renderer_template_get_param_authority_track(id);
                uiw_draw_bipolar_bar(
                    widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H,
                    param_value_policy_canonical_to_display(id, authority_track, value),
                    param_value_policy_canonical_to_display(id, authority_track, desc->min),
                    param_value_policy_canonical_to_display(id, authority_track, desc->max));
            }
            else
            {
                uiw_draw_bipolar_bar(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H, value, desc->min, desc->max);
            }
            ui_renderer_template_draw_bar_value_text(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H, value_txt);
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(1U);
            }
            break;

        default:
        {
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(0U);
            }
            uiw_draw_value_bar(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H, value, desc->min, desc->max);
            ui_renderer_template_draw_bar_value_text(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H, value_txt);
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(1U);
            }
            break;
        }
    }

draw_bottom_label:
    if (slot_locked != 0U)
    {
        drv_display_set_draw_color(0U);
    }

    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, bottom_txt),
                          (uint8_t)(y + UI_TEMPLATE_CARD_LABEL_Y),
                          bottom_txt);
    if ((draw_name_inverted != 0U)
            || ((audio_fx_shared_slot != 0U) && (slot_locked != 0U)))
    {
        const uint8_t text_x = (uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, bottom_txt);
        const uint8_t text_w = drv_display_text_width(bottom_txt);
        if (text_w == 0U)
        {
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(1U);
            }
            return;
        }
        ui_renderer_template_fill_lock_label_frame(text_x,
                                                   (uint8_t)(y + UI_TEMPLATE_CARD_LABEL_Y - 1),
                                                   text_w,
                                                   UI_TEMPLATE_CARD_LABEL_H + 1);
        drv_display_draw_text_inverted(text_x,
                                       (uint8_t)(y + UI_TEMPLATE_CARD_LABEL_Y),
                                       bottom_txt);
    }

    if (slot_locked != 0U)
    {
        drv_display_set_draw_color(1U);
    }

}

