static const char *ui_renderer_template_filter_mode_short_label(uint8_t filter_mode)
{
    switch (filter_mode)
    {
        case 0U:
            return "OFF";
        case 1U:
            return "LOW";
        case 2U:
            return "HIGH";
        default:
            return NULL;
    }
}

static void ui_renderer_template_draw_filter_curve_segment(int x0, int y0, int x1, int y1, int baseline_y)
{
    const int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    const int dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);

    if ((dx == 0) && (dy == 0))
    {
        return;
    }
    (void)baseline_y;
    drv_display_draw_line(x0, y0, x1, y1);
}

static uint8_t ui_renderer_template_draw_filter_group_curve(float morph,
                                                            float cutoff_value,
                                                            float resonance_value,
                                                            int x,
                                                            int y,
                                                            int w,
                                                            int h)
{
    if ((w < 24) || (h < 10))
    {
        return 0U;
    }

    const uint8_t cutoff = ui_renderer_template_value_to_u7(cutoff_value);
    const uint8_t resonance = ui_renderer_template_value_to_u7(resonance_value);
    const int left = x + 2;
    const int right = x + w - 3;
    const int top = y + 2;
    const int bottom = y + h - 3;
    const int floor = bottom - 3;
    const int high = top + 5;
    const int lp_hp_high = high + 3;
    const int cutoff_x = ui_renderer_template_clamp_i32(left + 6 + (((right - left - 12) * (int)cutoff) / 127), left + 6, right - 6);
    const int lp_hp_peak = ui_renderer_template_clamp_i32(lp_hp_high - (((lp_hp_high - top - 1) * (int)resonance) / 127), top + 1, lp_hp_high);

    drv_display_draw_line(left, bottom, right, bottom);

    const float t = (morph <= 64.0f) ? (morph / 64.0f) : ((morph - 64.0f) / 63.0f);
    const uint8_t upper = (morph > 64.0f) ? 1U : 0U;
    const int bp_half_width = 16 - (((int)resonance * 8) / 127);
    const int bp_peak = ui_renderer_template_clamp_i32(floor - 3 - (((floor - top - 5) * (int)resonance) / 127), top + 2, floor - 3);
    int previous_x = left;
    int previous_y = floor;
    const int point_count = right - left;
    for (int point = 0; point <= point_count; ++point)
    {
        const int px = left + point;
        const int distance = (px >= cutoff_x) ? (px - cutoff_x) : (cutoff_x - px);
        const int slope = ui_renderer_template_clamp_i32(distance, 0, 8);
        const int lp_y = (px <= cutoff_x)
            ? (lp_hp_high + (((lp_hp_peak - lp_hp_high) * (8 - slope)) / 8))
            : (lp_hp_peak + (((floor - lp_hp_peak) * slope) / 8));
        const int hp_y = (px >= cutoff_x)
            ? (lp_hp_high + (((lp_hp_peak - lp_hp_high) * (8 - slope)) / 8))
            : (lp_hp_peak + (((floor - lp_hp_peak) * slope) / 8));
        const int bp_y = (distance >= bp_half_width)
            ? floor : (floor - (((floor - bp_peak) * (bp_half_width - distance)) / bp_half_width));
        const int py = upper ? (int)((1.0f - t) * (float)bp_y + t * (float)hp_y + 0.5f)
                             : (int)((1.0f - t) * (float)lp_y + t * (float)bp_y + 0.5f);
        if (point != 0)
        {
            ui_renderer_template_draw_filter_curve_segment(previous_x, previous_y, px, py, bottom);
        }
        previous_x = px;
        previous_y = py;
    }

    return 1U;
}

static uint8_t ui_renderer_template_draw_custom_filter(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                       ui_template_custom_widget_kind_t kind,
                                                       int x,
                                                       int y,
                                                       int w,
                                                       int h,
                                                       float value,
                                                       uint8_t allow_group_curve)
{
    ui_renderer_template_filter_snapshot_t snapshot;
    if (ui_renderer_template_filter_snapshot_capture(plock_frame_ctx, &snapshot) == 0U)
    {
        return 0U;
    }

    if (kind == UI_TEMPLATE_CUSTOM_WIDGET_FILTER_TYPE)
    {
        const char *label = ui_renderer_template_filter_mode_short_label(snapshot.mode);
        const font_t *font = (snapshot.mode == 0U) ? &FONT_OFF_COMPACT : &FONT_HELVB14;
        return (label != NULL) ? ui_renderer_template_draw_filter_big_text(x, y, w, h, label, font) : 0U;
    }

    if ((kind == UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CUTOFF)
            && (ui_renderer_template_filter_param_supported(ui_renderer_template_get_edit_owner_track(), PARAM_FILTER_CUTOFF) == 0U))
    {
        return 0U;
    }
    if ((kind == UI_TEMPLATE_CUSTOM_WIDGET_FILTER_RESONANCE)
            && (ui_renderer_template_filter_param_supported(ui_renderer_template_get_edit_owner_track(), PARAM_FILTER_RESONANCE) == 0U))
    {
        return 0U;
    }

    if ((snapshot.mode == 0U) && (kind == UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP))
    {
        const int baseline = y + (h / 2);
        drv_display_draw_line(x + 2, baseline, x + w - 3, baseline);
        return 1U;
    }
    if (snapshot.mode == 0U)
    {
        return ui_renderer_template_draw_filter_text(x, y, w, h, "-", &FONT_4X6, 0);
    }

    if ((kind == UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP) && (allow_group_curve != 0U))
    {
        return ui_renderer_template_draw_filter_group_curve(snapshot.morph,
                                                             snapshot.cutoff,
                                                             snapshot.resonance,
                                                             x, y, w, h);
    }

    (void)value;
    return 0U;
}

static uint8_t ui_renderer_template_custom_adsr_params(ui_template_custom_widget_kind_t kind,
                                                       param_id_t *out_attack,
                                                       param_id_t *out_decay,
                                                       param_id_t *out_sustain,
                                                       param_id_t *out_release)
{
    if ((out_attack == 0) || (out_decay == 0) || (out_sustain == 0) || (out_release == 0))
    {
        return 0U;
    }

    switch (kind)
    {
        case UI_TEMPLATE_CUSTOM_WIDGET_ADSR_FILTER:
            *out_attack = PARAM_FILTER_ATTACK;
            *out_decay = PARAM_FILTER_DECAY;
            *out_sustain = PARAM_FILTER_SUSTAIN;
            *out_release = PARAM_FILTER_RELEASE;
            return 1U;

        case UI_TEMPLATE_CUSTOM_WIDGET_ADSR_VCA:
            *out_attack = PARAM_VCA_ATTACK;
            *out_decay = PARAM_VCA_DECAY;
            *out_sustain = PARAM_VCA_SUSTAIN;
            *out_release = PARAM_VCA_RELEASE;
            return 1U;

        case UI_TEMPLATE_CUSTOM_WIDGET_ADSR_ENV3:
            *out_attack = PARAM_ENV3_ATTACK;
            *out_decay = PARAM_ENV3_DECAY;
            *out_sustain = PARAM_ENV3_SUSTAIN;
            *out_release = PARAM_ENV3_RELEASE;
            return 1U;

        default:
            return 0U;
    }
}

static uint8_t ui_renderer_template_custom_adsr_supported(uint8_t active_track,
                                                          param_id_t attack,
                                                          param_id_t decay,
                                                          param_id_t sustain,
                                                          param_id_t release)
{
    const track_runtime_param_status_t attack_status = track_runtime_get_effective_param_status(active_track, attack);
    const track_runtime_param_status_t decay_status = track_runtime_get_effective_param_status(active_track, decay);
    const track_runtime_param_status_t sustain_status = track_runtime_get_effective_param_status(active_track, sustain);
    const track_runtime_param_status_t release_status = track_runtime_get_effective_param_status(active_track, release);

    return (uint8_t)(((attack_status == TRACK_RUNTIME_PARAM_ALLOWED) || (attack_status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
            && ((decay_status == TRACK_RUNTIME_PARAM_ALLOWED) || (decay_status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
            && ((sustain_status == TRACK_RUNTIME_PARAM_ALLOWED) || (sustain_status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
            && ((release_status == TRACK_RUNTIME_PARAM_ALLOWED) || (release_status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)));
}

static uint8_t ui_renderer_template_prepare_custom_adsr(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                        ui_template_custom_widget_kind_t kind,
                                                        ui_renderer_template_adsr_shape_t *out_shape)
{
    if (out_shape == 0)
    {
        return 0U;
    }

    param_id_t attack_param = PARAM_COUNT;
    param_id_t decay_param = PARAM_COUNT;
    param_id_t sustain_param = PARAM_COUNT;
    param_id_t release_param = PARAM_COUNT;
    if (ui_renderer_template_custom_adsr_params(kind, &attack_param, &decay_param, &sustain_param, &release_param) == 0U)
    {
        return 0U;
    }

    if (ui_renderer_template_custom_adsr_supported(ui_renderer_template_get_edit_owner_track(), attack_param, decay_param, sustain_param, release_param) == 0U)
    {
        return 0U;
    }

    float attack_value = 0.0f;
    float decay_value = 0.0f;
    float sustain_value = 0.0f;
    float release_value = 0.0f;
    if ((ui_renderer_template_get_visible_param_value(plock_frame_ctx, attack_param, &attack_value, 0) == 0U)
            || (ui_renderer_template_get_visible_param_value(plock_frame_ctx, decay_param, &decay_value, 0) == 0U)
            || (ui_renderer_template_get_visible_param_value(plock_frame_ctx, sustain_param, &sustain_value, 0) == 0U)
            || (ui_renderer_template_get_visible_param_value(plock_frame_ctx, release_param, &release_value, 0) == 0U))
    {
        return 0U;
    }

    const uint8_t attack = ui_renderer_template_value_to_u7(attack_value);
    const uint8_t decay = ui_renderer_template_value_to_u7(decay_value);
    const uint8_t sustain = ui_renderer_template_value_to_u7(sustain_value);
    const uint8_t release = ui_renderer_template_value_to_u7(release_value);

    out_shape->attack = attack;
    out_shape->decay = decay;
    out_shape->sustain = sustain;
    out_shape->release = release;
    out_shape->locked[0] = ui_macro_interaction_param_is_locked(attack_param);
    out_shape->locked[1] = ui_macro_interaction_param_is_locked(decay_param);
    out_shape->locked[2] = ui_macro_interaction_param_is_locked(sustain_param);
    out_shape->locked[3] = ui_macro_interaction_param_is_locked(release_param);
    return 1U;
}

static int ui_renderer_template_lerp_i32(int x, int x0, int y0, int x1, int y1)
{
    if (x1 == x0)
    {
        return y1;
    }

    const int dx = x1 - x0;
    const int dy = y1 - y0;
    const int num = (dy * (x - x0));
    if (num >= 0)
    {
        return y0 + ((num + (dx / 2)) / dx);
    }
    return y0 + ((num - (dx / 2)) / dx);
}

static uint8_t ui_renderer_template_slot_for_x(int x)
{
    if (x < 32)
    {
        return 0U;
    }
    if (x < 64)
    {
        return 1U;
    }
    if (x < 96)
    {
        return 2U;
    }
    return 3U;
}

static int ui_renderer_template_adsr_y_for_x(int x,
                                             int x0,
                                             int x3,
                                             int x4,
                                             int ax,
                                             int dx,
                                             int rx,
                                             int peak_y,
                                             int sustain_y,
                                             int bottom)
{
    if (x <= ax)
    {
        return ui_renderer_template_lerp_i32(x, x0, bottom, ax, peak_y);
    }
    if (x <= dx)
    {
        return ui_renderer_template_lerp_i32(x, ax, peak_y, dx, sustain_y);
    }
    if (x <= x3)
    {
        return sustain_y;
    }
    if (x <= rx)
    {
        return ui_renderer_template_lerp_i32(x, x3, sustain_y, rx, bottom);
    }
    (void)x4;
    return bottom;
}

static uint8_t ui_renderer_template_draw_custom_adsr_shape(const ui_renderer_template_adsr_shape_t *shape,
                                                           int x,
                                                           int y,
                                                           int w,
                                                           int h,
                                                           uint8_t segment_by_slot_lock)
{
    if ((shape == 0) || (w < 8) || (h < 8))
    {
        return 0U;
    }

    const int left = x + 2;
    const int right = x + w - 3;
    const int top = y + 2;
    const int bottom = y + h - 3;
    if ((right <= left) || (bottom <= top))
    {
        return 0U;
    }

    const int plot_w = right - left;
    const int zone_w = plot_w / 4;
    if (zone_w < 3)
    {
        return 0U;
    }

    const int amp_h = bottom - top;
    const int x0 = left;
    const int x1 = left + zone_w;
    const int x2 = left + (2 * zone_w);
    const int x3 = left + (3 * zone_w);
    const int x4 = right;
    const int peak_y = top + 1;
    const int sustain_y = ui_renderer_template_clamp_i32((bottom - 1) - (((amp_h - 2) * (int)shape->sustain) / 127), top + 1, bottom - 1);
    const int attack_x = ui_renderer_template_clamp_i32(x0 + 1 + (((zone_w - 2) * (int)shape->attack) / 127), x0 + 1, x1 - 1);
    const int decay_x = ui_renderer_template_clamp_i32(x1 + 1 + (((zone_w - 2) * (int)shape->decay) / 127), x1 + 1, x2 - 1);
    const int release_x = ui_renderer_template_clamp_i32(x3 + 1 + (((x4 - x3 - 2) * (int)shape->release) / 127), x3 + 1, x4 - 1);

    if (segment_by_slot_lock == 0U)
    {
        drv_display_draw_line(x0, bottom, attack_x, peak_y);
        drv_display_draw_line(attack_x, peak_y, decay_x, sustain_y);
        drv_display_draw_line(decay_x, sustain_y, x3, sustain_y);
        drv_display_draw_line(x3, sustain_y, release_x, bottom);
        drv_display_draw_line(release_x, bottom, x4, bottom);

        if ((bottom - top) >= 10)
        {
            drv_display_draw_pixel(x0, bottom, true);
            drv_display_draw_pixel(x4, bottom, true);
        }

        return 1U;
    }

    int prev_x = x0;
    int prev_y = bottom;
    for (int draw_x = x0 + 1; draw_x <= x4; ++draw_x)
    {
        const int draw_y = ui_renderer_template_adsr_y_for_x(draw_x,
                                                             x0,
                                                             x3,
                                                             x4,
                                                             attack_x,
                                                             decay_x,
                                                             release_x,
                                                             peak_y,
                                                             sustain_y,
                                                             bottom);
        const uint8_t slot = ui_renderer_template_slot_for_x(draw_x);
        drv_display_set_draw_color((shape->locked[slot] != 0U) ? 0U : 1U);
        drv_display_draw_line(prev_x, prev_y, draw_x, draw_y);
        prev_x = draw_x;
        prev_y = draw_y;
    }

    if ((bottom - top) >= 10)
    {
        drv_display_set_draw_color((shape->locked[ui_renderer_template_slot_for_x(x0)] != 0U) ? 0U : 1U);
        drv_display_draw_pixel(x0, bottom, true);
        drv_display_set_draw_color((shape->locked[ui_renderer_template_slot_for_x(x4)] != 0U) ? 0U : 1U);
        drv_display_draw_pixel(x4, bottom, true);
    }

    drv_display_set_draw_color(1U);
    return 1U;
}

static uint8_t ui_renderer_template_draw_custom_adsr(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                     ui_template_custom_widget_kind_t kind,
                                                     int x,
                                                     int y,
                                                     int w,
                                                     int h)
{
    ui_renderer_template_adsr_shape_t shape;
    if (ui_renderer_template_prepare_custom_adsr(plock_frame_ctx, kind, &shape) == 0U)
    {
        return 0U;
    }

    (void)ui_renderer_template_draw_custom_adsr_shape(&shape, x, y, w, h, 0U);
    return 1U;
}

static ui_template_custom_widget_kind_t ui_renderer_template_resolve_custom_widget(const ui_template_page_state_t *state,
                                                                                   const ui_template_subpage_t *subpage,
                                                                                   uint8_t slot,
                                                                                   param_id_t id)
{
    if ((state == 0) || (state->custom_widget_picker == 0))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
    }

    return state->custom_widget_picker(slot, subpage, id);
}

static ui_template_custom_widget_kind_t ui_renderer_template_resolve_grouped_custom_widget(const ui_template_page_state_t *state,
                                                                                           const ui_template_subpage_t *subpage)
{
    if (subpage == 0)
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
    }

    const ui_template_custom_widget_kind_t kind =
        ui_renderer_template_resolve_custom_widget(state, subpage, 0U, subpage->param_bank.params[0]);
    if (kind == UI_TEMPLATE_CUSTOM_WIDGET_NONE)
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
    }
    if (kind == UI_TEMPLATE_CUSTOM_WIDGET_MATRIX_SOURCE)
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
    }

    if (kind == UI_TEMPLATE_CUSTOM_WIDGET_AUDIO_FX_GROUP)
    {
        return (ui_renderer_template_resolve_custom_widget(
                    state, subpage, 1U, subpage->param_bank.params[1])
                == kind)
            && (ui_renderer_template_resolve_custom_widget(
                    state, subpage, 2U, subpage->param_bank.params[2])
                == kind)
            && (ui_renderer_template_resolve_custom_widget(
                    state, subpage, 3U, subpage->param_bank.params[3])
                == UI_TEMPLATE_CUSTOM_WIDGET_NONE)
            ? kind : UI_TEMPLATE_CUSTOM_WIDGET_NONE;
    }

    for (uint8_t slot = 1U; slot < 4U; ++slot)
    {
        if (ui_renderer_template_resolve_custom_widget(state, subpage, slot, subpage->param_bank.params[slot]) != kind)
        {
            return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
        }
    }

    return kind;
}

static uint8_t ui_renderer_template_filter_curve_group_is_active(const ui_template_page_state_t *state,
                                                                 const ui_template_subpage_t *subpage,
                                                                 const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx)
{
    if ((subpage == 0)
            || (subpage->param_bank.params[UI_TEMPLATE_FILTER_GROUP_SLOT_FIRST] != PARAM_FILTER_CUTOFF)
            || (subpage->param_bank.params[UI_TEMPLATE_FILTER_GROUP_SLOT_FIRST + 1U] != PARAM_FILTER_RESONANCE)
            || (ui_renderer_template_resolve_custom_widget(state,
                                                           subpage,
                                                           UI_TEMPLATE_FILTER_GROUP_SLOT_FIRST,
                                                           PARAM_FILTER_CUTOFF) != UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP)
            || (ui_renderer_template_resolve_custom_widget(state,
                                                           subpage,
                                                           UI_TEMPLATE_FILTER_GROUP_SLOT_FIRST + 1U,
                                                           PARAM_FILTER_RESONANCE) != UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP))
    {
        return 0U;
    }

    ui_renderer_template_filter_snapshot_t snapshot;
    if (ui_renderer_template_filter_snapshot_capture(plock_frame_ctx, &snapshot) == 0U)
    {
        return 0U;
    }
    (void)snapshot;
    if ((ui_renderer_template_filter_param_supported(ui_renderer_template_get_edit_owner_track(), PARAM_FILTER_CUTOFF) == 0U)
            || (ui_renderer_template_filter_param_supported(ui_renderer_template_get_edit_owner_track(), PARAM_FILTER_RESONANCE) == 0U))
    {
        return 0U;
    }
    return 1U;
}

static uint8_t ui_renderer_template_spectral_window_group_is_active(const ui_template_page_state_t *state,
                                                                     const ui_template_subpage_t *subpage,
                                                                     uint8_t *out_first_slot,
                                                                     param_id_t *out_position,
                                                                     param_id_t *out_width)
{
    if ((subpage == NULL) || (out_first_slot == NULL) || (out_position == NULL) || (out_width == NULL))
    {
        return 0U;
    }

    for(uint8_t slot = 0U; slot < 3U; ++slot)
    {
        const param_id_t position = subpage->param_bank.params[slot];
        const param_id_t width = subpage->param_bank.params[slot + 1U];
        const uint8_t recognized = (uint8_t)((position == PARAM_MIX_DELAY_SPECTRAL_POSITION)
                && (width == PARAM_MIX_DELAY_SPECTRAL_WIDTH));
        if((recognized != 0U)
                && (ui_renderer_template_resolve_custom_widget(state, subpage, slot, position)
                        == UI_TEMPLATE_CUSTOM_WIDGET_SPECTRAL_WINDOW_GROUP)
                && (ui_renderer_template_resolve_custom_widget(state, subpage, slot + 1U, width)
                        == UI_TEMPLATE_CUSTOM_WIDGET_SPECTRAL_WINDOW_GROUP))
        {
            *out_first_slot = slot;
            *out_position = position;
            *out_width = width;
            return 1U;
        }
    }
    return 0U;
}

static void ui_renderer_template_draw_spectral_window_group(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                            uint8_t first_slot,
                                                            param_id_t position_id,
                                                            param_id_t width_id)
{
    float position = 0.5f;
    float width_value = 1.0f;
    (void)ui_renderer_template_get_visible_param_value(plock_frame_ctx, position_id, &position, 0);
    (void)ui_renderer_template_get_visible_param_value(plock_frame_ctx, width_id, &width_value, 0);

    const int x = g_ui_template_frame_x[first_slot] + UI_TEMPLATE_CARD_WIDGET_X_PAD;
    const int y = UI_TEMPLATE_FRAME_Y + UI_TEMPLATE_CARD_WIDGET_Y;
    const int w = (UI_TEMPLATE_FRAME_W * 2) - (2 * UI_TEMPLATE_CARD_WIDGET_X_PAD);
    const int h = UI_TEMPLATE_CARD_WIDGET_H;
    const spectral_window_limits_t limits = spectral_window_delay_limits();
    spectral_window_result_t result;
    spectral_window_calculate(position, width_value, limits, &result);
    const float left_edge = spectral_window_log_position(result.low_cut_hz, limits);
    const float right_edge = spectral_window_log_position(result.high_cut_hz, limits);
    const float slope = 0.055f;
    int prev_y = y + h - 2;

    for(int col = 0; col < w; ++col)
    {
        const float f = (float)col / (float)(w - 1);
        float gain = 1.0f;
        if (f < left_edge)
        {
            const float t = (f - (left_edge - slope)) / slope;
            const float clamped = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
            gain = 0.5f - (0.5f * cosf(clamped * 3.14159265359f));
        }
        else if (f > right_edge)
        {
            const float t = ((right_edge + slope) - f) / slope;
            const float clamped = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
            gain = 0.5f - (0.5f * cosf(clamped * 3.14159265359f));
        }
        const int yy = y + h - 2 - (int)(gain * (float)(h - 4));
        if(col > 0)
            drv_display_draw_line(x + col - 1, prev_y, x + col, yy);
        prev_y = yy;
    }
}

static uint8_t ui_renderer_template_draw_filter_curve_group(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx)
{
    const int x = g_ui_template_frame_x[UI_TEMPLATE_FILTER_GROUP_SLOT_FIRST] + UI_TEMPLATE_CARD_WIDGET_X_PAD;
    const int y = UI_TEMPLATE_FRAME_Y + UI_TEMPLATE_CARD_WIDGET_Y;
    const int w = (UI_TEMPLATE_FRAME_W * UI_TEMPLATE_FILTER_GROUP_SLOT_COUNT) - (2 * UI_TEMPLATE_CARD_WIDGET_X_PAD);
    const int h = UI_TEMPLATE_CARD_WIDGET_H;
    return ui_renderer_template_draw_custom_filter(plock_frame_ctx,
                                                   UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP,
                                                   x,
                                                   y,
                                                   w,
                                                   h,
                                                   0.0f,
                                                   1U);
}

static uint8_t ui_renderer_template_lfo_shape_phase_group_is_active(const ui_template_page_state_t *state,
                                                                    const ui_template_subpage_t *subpage,
                                                                    param_id_t *out_shape_id,
                                                                    param_id_t *out_phase_id)
{
    if ((subpage == 0)
            || (out_shape_id == 0)
            || (out_phase_id == 0)
            || (ui_renderer_template_resolve_custom_widget(state,
                                                           subpage,
                                                           UI_TEMPLATE_LFO_GROUP_SLOT_FIRST,
                                                           subpage->param_bank.params[UI_TEMPLATE_LFO_GROUP_SLOT_FIRST]) != UI_TEMPLATE_CUSTOM_WIDGET_LFO_SHAPE_PHASE_GROUP)
            || (ui_renderer_template_resolve_custom_widget(state,
                                                           subpage,
                                                           UI_TEMPLATE_LFO_GROUP_SLOT_FIRST + 1U,
                                                           subpage->param_bank.params[UI_TEMPLATE_LFO_GROUP_SLOT_FIRST + 1U]) != UI_TEMPLATE_CUSTOM_WIDGET_LFO_SHAPE_PHASE_GROUP))
    {
        return 0U;
    }

    const param_id_t shape_param = subpage->param_bank.params[UI_TEMPLATE_LFO_GROUP_SLOT_FIRST];
    const param_id_t phase_param = subpage->param_bank.params[UI_TEMPLATE_LFO_GROUP_SLOT_FIRST + 1U];
    if (!(((shape_param == PARAM_LFO1_SHAPE) && (phase_param == PARAM_LFO1_PHASE))
            || ((shape_param == PARAM_LFO2_SHAPE) && (phase_param == PARAM_LFO2_PHASE))
            || ((shape_param == PARAM_LFO3_SHAPE) && (phase_param == PARAM_LFO3_PHASE))))
    {
        return 0U;
    }

    *out_shape_id = shape_param;
    *out_phase_id = phase_param;
    return 1U;
}

static uint8_t ui_renderer_template_draw_lfo_shape_phase_group(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                               param_id_t shape_id,
                                                               param_id_t phase_id)
{
    float shape_value = 0.0f;
    float phase_value = 0.0f;
    if ((ui_renderer_template_get_visible_param_value(plock_frame_ctx, shape_id, &shape_value, 0) == 0U)
            || (ui_renderer_template_get_visible_param_value(plock_frame_ctx, phase_id, &phase_value, 0) == 0U))
    {
        return 0U;
    }

    const int x = g_ui_template_frame_x[UI_TEMPLATE_LFO_GROUP_SLOT_FIRST] + UI_TEMPLATE_CARD_WIDGET_X_PAD;
    const int y = UI_TEMPLATE_FRAME_Y + UI_TEMPLATE_CARD_WIDGET_Y;
    const int w = (UI_TEMPLATE_FRAME_W * UI_TEMPLATE_LFO_GROUP_SLOT_COUNT) - (2 * UI_TEMPLATE_CARD_WIDGET_X_PAD);
    const int h = UI_TEMPLATE_CARD_WIDGET_H;
    return ui_renderer_template_render_lfo_shape_phase_group(shape_id,
                                                             phase_id,
                                                             shape_value,
                                                             phase_value,
                                                             x,
                                                             y,
                                                             w,
                                                             h);
}

static uint8_t ui_renderer_template_fm_pitch_eg_group_is_active(const ui_template_page_state_t *state,
                                                                  const ui_template_subpage_t *subpage)
{
    if ((state == NULL) || (subpage == NULL))
    {
        return 0U;
    }

    const param_id_t *const params = subpage->param_bank.params;
    const uint8_t rates = (uint8_t)((params[0] == PARAM_FM_PITCH_R1)
            && (params[1] == PARAM_FM_PITCH_R2)
            && (params[2] == PARAM_FM_PITCH_R3)
            && (params[3] == PARAM_FM_PITCH_R4));
    const uint8_t levels = (uint8_t)((params[0] == PARAM_FM_PITCH_L1)
            && (params[1] == PARAM_FM_PITCH_L2)
            && (params[2] == PARAM_FM_PITCH_L3)
            && (params[3] == PARAM_FM_PITCH_L4));
    if ((rates == 0U) && (levels == 0U))
    {
        return 0U;
    }

    return (uint8_t)(ui_renderer_template_resolve_custom_widget(
                         state,
                         subpage,
                         0U,
                         params[0]) == UI_TEMPLATE_CUSTOM_WIDGET_FM_PITCH_EG_GROUP);
}

static uint8_t ui_renderer_template_draw_fm_pitch_eg_group(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx)
{
    static const param_id_t rate_ids[4] = {
        PARAM_FM_PITCH_R1, PARAM_FM_PITCH_R2, PARAM_FM_PITCH_R3, PARAM_FM_PITCH_R4
    };
    static const param_id_t level_ids[4] = {
        PARAM_FM_PITCH_L1, PARAM_FM_PITCH_L2, PARAM_FM_PITCH_L3, PARAM_FM_PITCH_L4
    };
    float rates[4];
    float levels[4];
    float total_time = 0.0f;
    float time[4];

    for (uint8_t i = 0U; i < 4U; ++i)
    {
        rates[i] = 0.0f;
        levels[i] = 49.0f;
        (void)ui_renderer_template_get_visible_param_value(plock_frame_ctx, rate_ids[i], &rates[i], 0);
        (void)ui_renderer_template_get_visible_param_value(plock_frame_ctx, level_ids[i], &levels[i], 0);
        if (rates[i] < 0.0f) rates[i] = 0.0f;
        if (rates[i] > 99.0f) rates[i] = 99.0f;
        levels[i] += 49.0f;
        if (levels[i] < 0.0f) levels[i] = 0.0f;
        if (levels[i] > 99.0f) levels[i] = 99.0f;
        time[i] = 100.0f - rates[i] + 1.0f;
        total_time += time[i];
    }

    const int x = UI_TEMPLATE_GROUP_WIDGET_X;
    const int y = UI_TEMPLATE_GROUP_WIDGET_Y;
    const int w = UI_TEMPLATE_GROUP_WIDGET_W;
    const int h = UI_TEMPLATE_GROUP_WIDGET_H;
    const int neutral_y = y + h - 2 - (int)((49.0f * (float)(h - 4)) / 99.0f + 0.5f);
    drv_display_draw_line(x, neutral_y, x + w - 1, neutral_y);
    int previous_x = x;
    int previous_y = neutral_y;
    float accumulated = 0.0f;
    for (uint8_t i = 0U; i < 4U; ++i)
    {
        accumulated += time[i];
        int next_x = x + (int)(((accumulated / total_time) * (float)(w - 1)) + 0.5f);
        if (next_x > (x + w - 1)) next_x = x + w - 1;
        const int next_y = y + h - 2 - (int)((levels[i] * (float)(h - 4)) / 99.0f + 0.5f);
        drv_display_draw_line(previous_x, previous_y, next_x, next_y);
        previous_x = next_x;
        previous_y = next_y;
    }
    return 1U;
}

static uint8_t ui_renderer_template_is_sampler_ram_tone(const ui_template_page_state_t *state,
                                                        const ui_template_family_t *family)
{
    const uint8_t active_track = ui_renderer_template_get_edit_owner_track();

    if ((state == NULL) || (family == NULL) || (family->family_title == NULL))
    {
        return 0U;
    }
    if (strcmp(family->family_title, "TONE") != 0)
    {
        return 0U;
    }

    return (uint8_t)(((ui_get_track_family(active_track) == TRACK_FAMILY_SAMPLER)
                      && (ui_get_track_type(active_track) == TRACK_TYPE_RAM)) ? 1U : 0U);
}

