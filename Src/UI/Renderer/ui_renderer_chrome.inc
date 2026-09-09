static void ui_renderer_template_draw_header(const ui_template_page_state_t *state)
{
    const ui_template_family_t *family = ui_template_page_get_active_family(state);
    const char *family_title = ((family != NULL) && (family->family_title != NULL)) ? family->family_title : "TEMPLATE";
    const uint8_t active_track = ui_renderer_template_get_selected_entity();
    char track_label[8];
    char runtime_label[12];
    char cpu_avg_label[40];
    char bpm_label[12];

    const uint8_t track_display_id = (uint8_t)(active_track + 1U);
    (void)snprintf(track_label, sizeof(track_label), "%u", (unsigned int)track_display_id);
    ui_get_track_runtime_header_label(active_track, runtime_label, (uint32_t)sizeof(runtime_label));
    ui_renderer_template_format_cpu_avg(cpu_avg_label, (uint32_t)sizeof(cpu_avg_label));
    uint8_t draw_bpm = 0U;
    uint8_t bpm_inverted = 0U;
    uint32_t bpm_milli = 0U;

    if (seq_runtime_get_clock_source() == SEQ_CLOCK_SRC_INTERNAL)
    {
        bpm_milli = seq_runtime_get_tempo_bpm_milli();
        draw_bpm = 1U;
        bpm_inverted = 0U;
    }
    else if (seq_runtime_is_external_tempo_valid() != 0U)
    {
        bpm_milli = seq_runtime_get_external_tempo_bpm_milli();
        draw_bpm = 1U;
        bpm_inverted = 1U;
    }

    if (draw_bpm != 0U)
    {
        (void)snprintf(bpm_label,
                       sizeof(bpm_label),
                       "%lu.%01lu",
                       (unsigned long)(bpm_milli / 1000U),
                       (unsigned long)((bpm_milli % 1000U) / 100U));
    }

    const ui_hall_mode_t hall_mode = ui_get_hall_mode();
    const char *hall_mode_label = ui_get_hall_mode_short_label();
    if (ui_hall_mode_resolve_effective_view(active_track, hall_mode) == UI_HALL_MODE_VIEW_KEYBOARD)
    {
        hall_mode_label = keyboard_runtime_get_omnichord() ? "CHORD" : "KBD";
    }
    const char *hall_mode_suffix = ui_get_hall_mode_suffix_label();

    ui_renderer_template_draw_brick_frame(0, 0, 15, 15);
    ui_renderer_template_draw_brick_frame(UI_TEMPLATE_HEADER_TITLE_X, 0, UI_TEMPLATE_HEADER_TITLE_W, 15);

    ui_renderer_template_draw_track_badge(2U, 2U, track_label);

    drv_display_set_font(&FONT_4X6);
    char runtime_fit[12];
    char hall_fit[12];
    (void)snprintf(runtime_fit, sizeof(runtime_fit), "%s", runtime_label);
    (void)snprintf(hall_fit, sizeof(hall_fit), "%s", hall_mode_label);
    ui_renderer_template_fit_text(runtime_fit, 25U);
    ui_renderer_template_fit_text(hall_fit, 20U);
    drv_display_draw_text(17U, 1U, runtime_fit);
    drv_display_draw_text(17U, 9U, hall_fit);
    if ((hall_mode_suffix != NULL) && (hall_mode_suffix[0] != '\0'))
    {
        char suffix_fit[8];
        (void)snprintf(suffix_fit, sizeof(suffix_fit), "%s", hall_mode_suffix);
        ui_renderer_template_fit_text(suffix_fit, 9U);
        drv_display_draw_text(33U, 9U, suffix_fit);
    }

    char title_fit[16];
    (void)snprintf(title_fit, sizeof(title_fit), "%s", family_title);
    drv_display_set_font(&FONT_5X7);
    if (drv_display_text_width(title_fit) > 38U)
    {
        drv_display_set_font(&FONT_4X6);
    }
    ui_renderer_template_fit_text(title_fit, 38U);
    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(UI_TEMPLATE_HEADER_TITLE_X,
                                                                  UI_TEMPLATE_HEADER_TITLE_W,
                                                                  title_fit),
                          4U,
                          title_fit);

    drv_display_set_font(&FONT_4X6);
    ui_renderer_template_fit_text(cpu_avg_label, 30U);
    if (draw_bpm != 0U)
    {
        drv_display_set_font(&FONT_4X6);
        const uint8_t bpm_text_w = drv_display_text_width(bpm_label);
        if (bpm_inverted != 0U)
        {
            const uint8_t bpm_box_w = (uint8_t)(bpm_text_w + 2U);
            ui_renderer_template_draw_inverted_label(ui_renderer_template_right_x(0U, bpm_box_w),
                                                     1U,
                                                     bpm_label,
                                                     &FONT_4X6);
        }
        else
        {
            drv_display_draw_text(ui_renderer_template_right_x(0U, bpm_text_w), 1U, bpm_label);
        }
    }
    char pattern_label[6];
    ui_renderer_template_format_active_pattern_label(pattern_label, sizeof(pattern_label));
    drv_display_set_font(&FONT_4X6);
    const uint8_t cpu_text_w = drv_display_text_width(cpu_avg_label);
    const uint8_t bpm_w = (draw_bpm != 0U) ? drv_display_text_width(bpm_label) : 0U;
    const uint8_t bpm_x = (draw_bpm != 0U) ? ui_renderer_template_right_x(0U, bpm_w) : OLED_WIDTH;
    uint8_t cpu_x = (uint8_t)(108U - cpu_text_w);
    if ((draw_bpm != 0U) && ((uint8_t)(cpu_x + cpu_text_w + 1U) > bpm_x))
    {
        cpu_x = (bpm_x > (uint8_t)(cpu_text_w + 1U)) ? (uint8_t)(bpm_x - cpu_text_w - 1U) : 0U;
    }
    if (cpu_x >= 2U)
    {
        cpu_x = (uint8_t)(cpu_x - 2U);
    }
    drv_display_draw_text(cpu_x, 1U, cpu_avg_label);

    drv_display_draw_text(ui_renderer_template_right_x(0U, drv_display_text_width(pattern_label)), 8U, pattern_label);
}

static void ui_renderer_template_draw_footer(const ui_template_page_state_t *state)
{
    const ui_template_family_t *family = ui_template_page_get_active_family(state);

    drv_display_set_font(&FONT_4X6);

    for (uint8_t i = 0U; i < 4U; i++)
    {
        const int bx = g_ui_template_footer_x[i];
        const int bw = g_ui_template_footer_w[i];
        const uint8_t subpage_selectable = ui_template_page_is_subpage_selectable(state, i);
        const char *label = family->nav_labels[i];

        if (subpage_selectable == 0U)
        {
            label = "-";
        }

        if ((label == NULL) || (label[0] == '\0'))
        {
            label = "-";
        }


        char label_fit[12];
        (void)snprintf(label_fit, sizeof(label_fit), "%s", label);
        ui_renderer_template_fit_text(label_fit, 28U);

        const int x_label = ui_renderer_template_center_x(bx, bw, label_fit);

        if (i == state->active_subpage)
        {
            drv_display_fill_rect(bx, UI_TEMPLATE_FOOTER_Y, bw, UI_TEMPLATE_FOOTER_H);

            drv_display_draw_pixel(bx,                 UI_TEMPLATE_FOOTER_Y,                 false);
            drv_display_draw_pixel(bx + 1,             UI_TEMPLATE_FOOTER_Y,                 false);
            drv_display_draw_pixel(bx,                 UI_TEMPLATE_FOOTER_Y + 1,             false);

            drv_display_draw_pixel(bx + bw - 1,        UI_TEMPLATE_FOOTER_Y,                 false);
            drv_display_draw_pixel(bx + bw - 2,        UI_TEMPLATE_FOOTER_Y,                 false);
            drv_display_draw_pixel(bx + bw - 1,        UI_TEMPLATE_FOOTER_Y + 1,             false);

            drv_display_draw_pixel(bx,                 UI_TEMPLATE_FOOTER_Y + UI_TEMPLATE_FOOTER_H - 1, false);
            drv_display_draw_pixel(bx + 1,             UI_TEMPLATE_FOOTER_Y + UI_TEMPLATE_FOOTER_H - 1, false);
            drv_display_draw_pixel(bx,                 UI_TEMPLATE_FOOTER_Y + UI_TEMPLATE_FOOTER_H - 2, false);

            drv_display_draw_pixel(bx + bw - 1,        UI_TEMPLATE_FOOTER_Y + UI_TEMPLATE_FOOTER_H - 1, false);
            drv_display_draw_pixel(bx + bw - 2,        UI_TEMPLATE_FOOTER_Y + UI_TEMPLATE_FOOTER_H - 1, false);
            drv_display_draw_pixel(bx + bw - 1,        UI_TEMPLATE_FOOTER_Y + UI_TEMPLATE_FOOTER_H - 2, false);

            drv_display_draw_text_inverted((uint8_t)x_label, UI_TEMPLATE_FOOTER_TEXT_Y, label_fit);
        }
        else
        {
            ui_renderer_template_draw_brick_frame(bx, UI_TEMPLATE_FOOTER_Y, bw, UI_TEMPLATE_FOOTER_H);
            drv_display_draw_text((uint8_t)x_label, UI_TEMPLATE_FOOTER_TEXT_Y, label_fit);
        }
    }
}

static void ui_renderer_template_draw_mod_link_arrows(const ui_template_subpage_t *subpage)
{
    if (subpage == NULL)
    {
        return;
    }

    (void)subpage;
}

static uint8_t ui_renderer_template_draw_audio_fx_group(
    const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx)
{
    (void)plock_frame_ctx;
    const int x = (int)AUDIO_WAVEFORM_CAPTURE_GRAPH_X;
    const int top = UI_TEMPLATE_WAVE_WT_Y + 4;
    const int bottom = UI_TEMPLATE_WAVE_WT_Y + UI_TEMPLATE_WAVE_WT_H - 2;
    const int w = (int)AUDIO_WAVEFORM_CAPTURE_GRAPH_WIDTH;
    const int center = top + ((bottom - top) / 2);
    const int positive_height = center - top;
    const int negative_height = bottom - center;
    typedef struct
    {
        brick_entity_id_t entity_id;
        uint32_t generation;
        uint8_t valid;
        uint8_t triggered;
        int8_t y_top[AUDIO_WAVEFORM_CAPTURE_COLUMNS];
        int8_t y_bottom[AUDIO_WAVEFORM_CAPTURE_COLUMNS];
    } ui_audio_scope_cache_t;
    static audio_waveform_capture_snapshot_t snapshot;
    static ui_audio_scope_cache_t cache;
    static brick_entity_id_t scale_entity = BRICK_ENTITY_INVALID_ID;
    static uint8_t display_peak = 32U;
    const brick_entity_id_t selected_entity =
        (brick_entity_id_t)ui_renderer_template_get_selected_entity();
    const uint8_t snapshot_read = audio_waveform_capture_read(&snapshot);
    if ((snapshot_read != 0U) && (snapshot.entity_id != selected_entity))
    {
        cache.valid = 0U;
        scale_entity = BRICK_ENTITY_INVALID_ID;
        display_peak = 32U;
    }
    else if ((snapshot_read != 0U)
            && (snapshot.entity_id == selected_entity)
            && (snapshot.valid != 0U)
            && ((cache.valid == 0U) || (cache.generation != snapshot.generation)))
    {
        uint8_t peak = 0U;
        for (uint16_t column = 0U; column < AUDIO_WAVEFORM_CAPTURE_COLUMNS; ++column)
        {
            int8_t column_min;
            int8_t column_max;
            if (snapshot.triggered != 0U)
            {
                const uint32_t position_q8 =
                    (((uint32_t)column
                      * (AUDIO_WAVEFORM_CAPTURE_FRAME_SAMPLES - 1U)) << 8)
                    / (AUDIO_WAVEFORM_CAPTURE_COLUMNS - 1U)
                    + snapshot.trigger_fraction_q8;
                uint32_t source = position_q8 >> 8;
                const uint16_t fraction = (uint16_t)(position_q8 & 0xffU);
                if (source >= AUDIO_WAVEFORM_CAPTURE_FRAME_SAMPLES)
                    source = AUDIO_WAVEFORM_CAPTURE_FRAME_SAMPLES - 1U;
                const uint32_t next = (source + 1U < AUDIO_WAVEFORM_CAPTURE_FRAME_SAMPLES)
                    ? source + 1U : source;
                const int32_t a = snapshot.samples[source];
                const int32_t b = snapshot.samples[next];
                const int8_t value = (int8_t)(a
                    + (((b - a) * fraction) >> 8));
                column_min = value;
                column_max = value;
            }
            else
            {
                const uint16_t begin = (uint16_t)(column * 16U);
                const uint16_t end = (uint16_t)(begin + 16U);
                column_min = INT8_MAX;
                column_max = INT8_MIN;
                for (uint16_t sample = begin; sample < end; ++sample)
                {
                    const int8_t value = snapshot.samples[sample];
                    if (value < column_min) column_min = value;
                    if (value > column_max) column_max = value;
                }
            }
            const uint8_t min_peak = (column_min == INT8_MIN)
                ? 128U : (uint8_t)((column_min < 0) ? -column_min : column_min);
            const uint8_t max_peak = (column_max == INT8_MIN)
                ? 128U : (uint8_t)((column_max < 0) ? -column_max : column_max);
            if (min_peak > peak) peak = min_peak;
            if (max_peak > peak) peak = max_peak;
            cache.y_top[column] = column_max;
            cache.y_bottom[column] = column_min;
        }
        const uint8_t target_peak = (peak > 32U) ? peak : 32U;
        if (scale_entity != selected_entity)
        {
            scale_entity = selected_entity;
            display_peak = 32U;
        }
        if (target_peak > display_peak)
            display_peak = target_peak;
        else
            display_peak = (uint8_t)(((uint16_t)display_peak * 7U + target_peak) / 8U);
        if (display_peak < 32U) display_peak = 32U;
        for (uint16_t column = 0U; column < AUDIO_WAVEFORM_CAPTURE_COLUMNS; ++column)
        {
            const int8_t column_max = cache.y_top[column];
            const int8_t column_min = cache.y_bottom[column];
            const int max_height = (column_max >= 0) ? positive_height : negative_height;
            const int min_height = (column_min >= 0) ? positive_height : negative_height;
            int32_t max_scaled = ((int32_t)column_max * max_height) / display_peak;
            int32_t min_scaled = ((int32_t)column_min * min_height) / display_peak;
            if (max_scaled > positive_height) max_scaled = positive_height;
            if (max_scaled < -negative_height) max_scaled = -negative_height;
            if (min_scaled > positive_height) min_scaled = positive_height;
            if (min_scaled < -negative_height) min_scaled = -negative_height;
            cache.y_top[column] = (int8_t)(center - (int)max_scaled);
            cache.y_bottom[column] = (int8_t)(center - (int)min_scaled);
        }
        cache.entity_id = selected_entity;
        cache.generation = snapshot.generation;
        cache.triggered = snapshot.triggered;
        cache.valid = 1U;
    }
    const uint8_t snapshot_valid = (uint8_t)(
        (cache.valid != 0U)
        && (cache.entity_id == selected_entity));

    for (int px = x; px < (x + w); px += 2)
    {
        drv_display_draw_pixel(px, center, true);
    }

    if (snapshot_valid == 0U) return 1U;
    if (cache.triggered != 0U)
    {
        int previous_y = cache.y_top[0];
        for (uint16_t column = 1U; column < AUDIO_WAVEFORM_CAPTURE_COLUMNS; ++column)
        {
            const int next_y = cache.y_top[column];
            drv_display_draw_line(x + (int)column - 1,
                                  previous_y,
                                  x + (int)column,
                                  next_y);
            previous_y = next_y;
        }
    }
    else
    {
        for (uint16_t column = 0U; column < AUDIO_WAVEFORM_CAPTURE_COLUMNS; ++column)
        {
            drv_display_draw_line(x + (int)column,
                                  cache.y_top[column],
                                  x + (int)column,
                                  cache.y_bottom[column]);
        }
    }
    return 1U;
}

typedef enum
{
    UI_TEMPLATE_RENDER_IDLE = 0,
    UI_TEMPLATE_RENDER_HEADER,
    UI_TEMPLATE_RENDER_PREPARE,
    UI_TEMPLATE_RENDER_SPECIAL,
    UI_TEMPLATE_RENDER_AUDIO_FX,
    UI_TEMPLATE_RENDER_SLOT_0,
    UI_TEMPLATE_RENDER_SLOT_1,
    UI_TEMPLATE_RENDER_SLOT_2,
    UI_TEMPLATE_RENDER_SLOT_3,
    UI_TEMPLATE_RENDER_GROUP,
    UI_TEMPLATE_RENDER_FOOTER,
    UI_TEMPLATE_RENDER_FINALIZE
} ui_renderer_template_phase_t;

typedef enum
{
    UI_TEMPLATE_BODY_SLOTS = 0,
    UI_TEMPLATE_BODY_SAMPLER_RAM,
    UI_TEMPLATE_BODY_SYNTH_LIVE,
    UI_TEMPLATE_BODY_WAVE_WAVETABLE,
    UI_TEMPLATE_BODY_WAVE_CLASSIC
} ui_renderer_template_body_t;

typedef struct
{
    ui_renderer_template_phase_t phase;
    const ui_template_page_state_t *source_state;
    const ui_template_family_t *source_family;
    ui_template_page_state_t state;
    ui_template_family_t family;
    ui_param_seq_plock_feedback_frame_t plock_frame;
    ui_renderer_template_body_t body;
    ui_template_custom_widget_kind_t grouped_widget;
    ui_renderer_template_adsr_shape_t grouped_adsr_shape;
    uint8_t grouped_widget_drawn;
    uint8_t grouped_filter_first_slot;
    uint8_t osc_mask;
    uint8_t large_wave;
    uint8_t selected_entity;
    param_id_t grouped_filter_position;
    param_id_t grouped_filter_width;
    param_id_t grouped_lfo_shape_id;
    param_id_t grouped_lfo_phase_id;
    synth_waveform_engine_t synth_engine;
} ui_renderer_template_job_t;

static ui_renderer_template_job_t g_ui_renderer_template_job;

static uint8_t ui_renderer_template_job_is_current(const ui_renderer_template_job_t *job,
                                                    const ui_template_page_state_t *state)
{
    if (state == NULL)
    {
        return 0U;
    }
    const ui_template_family_t *family = ui_template_page_get_active_family(state);
    if ((family == NULL)
            || (state != job->source_state) || (family != job->source_family))
    {
        return 0U;
    }

    if ((ui_renderer_template_get_selected_entity() != job->selected_entity)
            || (state->active_subpage != job->state.active_subpage)
            || (state->navigation_subset != job->state.navigation_subset)
            || (state->resolved_navigation_subset != job->state.resolved_navigation_subset)
            || (memcmp(family, &job->family, sizeof(job->family)) != 0))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t ui_renderer_template_job_begin(const ui_template_page_state_t *state)
{
    const ui_template_family_t *family = ui_template_page_get_active_family(state);

    if ((state == NULL) || (family == NULL))
    {
        g_ui_renderer_template_job.phase = UI_TEMPLATE_RENDER_IDLE;
        return 0U;
    }

    memset(&g_ui_renderer_template_job, 0, sizeof(g_ui_renderer_template_job));
    g_ui_renderer_template_job.source_state = state;
    g_ui_renderer_template_job.source_family = family;
    g_ui_renderer_template_job.state = *state;
    g_ui_renderer_template_job.family = *family;
    g_ui_renderer_template_job.selected_entity = ui_renderer_template_get_selected_entity();
    /* Freeze navigation/layout for the duration of the frame. */
    g_ui_renderer_template_job.state.family_resolver = NULL;
    g_ui_renderer_template_job.state.family = &g_ui_renderer_template_job.family;
    g_ui_renderer_template_job.state.resolved_family = &g_ui_renderer_template_job.family;
    g_ui_renderer_template_job.phase = UI_TEMPLATE_RENDER_HEADER;
    return 1U;
}

static void ui_renderer_template_job_prepare(ui_renderer_template_job_t *job)
{
    const ui_template_page_state_t *state = &job->state;
    const ui_template_family_t *family = &job->family;
    const ui_template_subpage_t *subpage = ui_template_page_get_active_subpage(state);

    ui_param_seq_plock_feedback_frame_begin(&job->plock_frame);
    job->body = UI_TEMPLATE_BODY_SLOTS;
    job->grouped_widget = UI_TEMPLATE_CUSTOM_WIDGET_NONE;
    job->grouped_filter_position = PARAM_COUNT;
    job->grouped_filter_width = PARAM_COUNT;
    job->grouped_lfo_shape_id = PARAM_COUNT;
    job->grouped_lfo_phase_id = PARAM_COUNT;

    if (subpage == NULL)
    {
        return;
    }

    const uint8_t wave_wavetable_subpage =
        ui_renderer_template_is_wave_wavetable_subpage(subpage);
    const uint8_t wave_classic_page = (uint8_t)((wave_wavetable_subpage != 0U)
        && (family->family_title != NULL)
        && (strcmp(family->family_title, "TONE 2/2") == 0));
    if (ui_renderer_template_is_sampler_ram_tone(state, family) != 0U)
    {
        job->body = UI_TEMPLATE_BODY_SAMPLER_RAM;
        return;
    }
    if ((wave_wavetable_subpage != 0U) && (wave_classic_page == 0U))
    {
        job->body = UI_TEMPLATE_BODY_SYNTH_LIVE;
        job->osc_mask = (subpage->param_bank.params[0] == UI_PARAM_LOCAL_WAVE_OSC2)
            ? 0x02U : 0x01U;
        job->large_wave = 1U;
        job->synth_engine = SYNTH_WAVEFORM_ENGINE_WAVE;
        return;
    }
    if ((state->navigation_subset == 0U)
            && (ui_renderer_template_is_wave_common_subpage(subpage) != 0U))
    {
        job->body = UI_TEMPLATE_BODY_SYNTH_LIVE;
        job->osc_mask = 0x03U;
        job->synth_engine = SYNTH_WAVEFORM_ENGINE_WAVE;
        return;
    }
    if ((state->navigation_subset == 0U)
            && (ui_renderer_template_is_prism_tone_subpage(subpage) != 0U))
    {
        const param_id_t first = subpage->param_bank.params[0];
        job->body = UI_TEMPLATE_BODY_SYNTH_LIVE;
        job->osc_mask = (first == PARAM_PRISM_OSC1_MODEL) ? 0x01U
            : ((first == PARAM_PRISM_OSC2_MODEL) ? 0x02U : 0x03U);
        job->synth_engine = SYNTH_WAVEFORM_ENGINE_PRISM;
        return;
    }
    if (wave_classic_page != 0U)
    {
        job->body = UI_TEMPLATE_BODY_WAVE_WAVETABLE;
        return;
    }
    if ((state->navigation_subset != 0U)
            && (ui_renderer_template_is_wave_common_subpage(subpage) != 0U))
    {
        job->body = UI_TEMPLATE_BODY_WAVE_CLASSIC;
        return;
    }

    job->grouped_widget = ui_renderer_template_resolve_grouped_custom_widget(state, subpage);
    job->grouped_widget_drawn =
        (uint8_t)((job->grouped_widget != UI_TEMPLATE_CUSTOM_WIDGET_NONE)
                && (ui_renderer_template_prepare_custom_adsr(&job->plock_frame,
                        job->grouped_widget, &job->grouped_adsr_shape) != 0U));
    if (job->grouped_widget == UI_TEMPLATE_CUSTOM_WIDGET_AUDIO_FX_GROUP)
    {
        job->grouped_widget_drawn = 1U;
    }
    if (ui_renderer_template_filter_curve_group_is_active(state, subpage,
                                                           &job->plock_frame) != 0U)
    {
        job->grouped_widget = UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP;
        job->grouped_widget_drawn = 1U;
    }
    if (ui_renderer_template_spectral_window_group_is_active(state, subpage,
            &job->grouped_filter_first_slot, &job->grouped_filter_position,
            &job->grouped_filter_width) != 0U)
    {
        job->grouped_widget = UI_TEMPLATE_CUSTOM_WIDGET_SPECTRAL_WINDOW_GROUP;
        job->grouped_widget_drawn = 1U;
    }
    if (ui_renderer_template_lfo_shape_phase_group_is_active(state, subpage,
            &job->grouped_lfo_shape_id, &job->grouped_lfo_phase_id) != 0U)
    {
        job->grouped_widget = UI_TEMPLATE_CUSTOM_WIDGET_LFO_SHAPE_PHASE_GROUP;
        job->grouped_widget_drawn = 1U;
    }
    if (ui_renderer_template_fm_pitch_eg_group_is_active(state, subpage) != 0U)
    {
        job->grouped_widget = UI_TEMPLATE_CUSTOM_WIDGET_FM_PITCH_EG_GROUP;
        job->grouped_widget_drawn = 1U;
    }
}

static void ui_renderer_template_job_draw_special(ui_renderer_template_job_t *job)
{
    const ui_template_page_state_t *state = &job->state;
    const ui_template_subpage_t *subpage = ui_template_page_get_active_subpage(state);
    if (subpage == NULL) return;

    switch (job->body)
    {
        case UI_TEMPLATE_BODY_SAMPLER_RAM:
            ui_renderer_template_draw_sampler_ram_wave_placeholder(state, &job->plock_frame, subpage);
            break;
        case UI_TEMPLATE_BODY_SYNTH_LIVE:
            ui_renderer_template_draw_synth_live_placeholder(state, &job->plock_frame,
                subpage, job->osc_mask, job->large_wave, job->synth_engine);
            break;
        case UI_TEMPLATE_BODY_WAVE_WAVETABLE:
            ui_renderer_template_draw_wave_wavetable_placeholder(state, &job->plock_frame, subpage);
            break;
        case UI_TEMPLATE_BODY_WAVE_CLASSIC:
            ui_renderer_template_draw_wave_classic_common(state, &job->plock_frame, subpage);
            break;
        default:
            break;
    }
}

static void ui_renderer_template_job_draw_group(ui_renderer_template_job_t *job)
{
    if (job->grouped_widget_drawn == 0U) return;
    switch (job->grouped_widget)
    {
        case UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP:
            (void)ui_renderer_template_draw_filter_curve_group(&job->plock_frame);
            break;
        case UI_TEMPLATE_CUSTOM_WIDGET_LFO_SHAPE_PHASE_GROUP:
            (void)ui_renderer_template_draw_lfo_shape_phase_group(&job->plock_frame,
                job->grouped_lfo_shape_id, job->grouped_lfo_phase_id);
            break;
        case UI_TEMPLATE_CUSTOM_WIDGET_SPECTRAL_WINDOW_GROUP:
            ui_renderer_template_draw_spectral_window_group(&job->plock_frame,
                job->grouped_filter_first_slot, job->grouped_filter_position,
                job->grouped_filter_width);
            break;
        case UI_TEMPLATE_CUSTOM_WIDGET_FM_PITCH_EG_GROUP:
            (void)ui_renderer_template_draw_fm_pitch_eg_group(&job->plock_frame);
            break;
        case UI_TEMPLATE_CUSTOM_WIDGET_AUDIO_FX_GROUP:
            break;
        default:
            (void)ui_renderer_template_draw_custom_adsr_shape(&job->grouped_adsr_shape,
                UI_TEMPLATE_GROUP_WIDGET_X, UI_TEMPLATE_GROUP_WIDGET_Y,
                UI_TEMPLATE_GROUP_WIDGET_W, UI_TEMPLATE_GROUP_WIDGET_H, 1U);
            break;
    }
}

uint8_t ui_renderer_template_draw_step(const ui_template_page_state_t *state)
{
    ui_renderer_template_job_t *job = &g_ui_renderer_template_job;
    if ((job->phase == UI_TEMPLATE_RENDER_IDLE)
            && (ui_renderer_template_job_begin(state) == 0U))
    {
        return 1U;
    }
    if (ui_renderer_template_job_is_current(job, state) == 0U)
    {
        drv_display_clear();
        if (ui_renderer_template_job_begin(state) == 0U) return 1U;
    }

    const ui_template_subpage_t *subpage = ui_template_page_get_active_subpage(&job->state);
    switch (job->phase)
    {
        case UI_TEMPLATE_RENDER_HEADER:
            ui_renderer_template_draw_header(&job->state);
            job->phase = UI_TEMPLATE_RENDER_PREPARE;
            break;
        case UI_TEMPLATE_RENDER_PREPARE:
            ui_renderer_template_job_prepare(job);
            job->phase = (job->body == UI_TEMPLATE_BODY_SLOTS)
                ? UI_TEMPLATE_RENDER_AUDIO_FX : UI_TEMPLATE_RENDER_SPECIAL;
            break;
        case UI_TEMPLATE_RENDER_SPECIAL:
            ui_renderer_template_job_draw_special(job);
            job->phase = UI_TEMPLATE_RENDER_FOOTER;
            break;
        case UI_TEMPLATE_RENDER_AUDIO_FX:
            if (job->grouped_widget == UI_TEMPLATE_CUSTOM_WIDGET_AUDIO_FX_GROUP)
                (void)ui_renderer_template_draw_audio_fx_group(&job->plock_frame);
            job->phase = UI_TEMPLATE_RENDER_SLOT_0;
            break;
        case UI_TEMPLATE_RENDER_SLOT_0:
        case UI_TEMPLATE_RENDER_SLOT_1:
        case UI_TEMPLATE_RENDER_SLOT_2:
        case UI_TEMPLATE_RENDER_SLOT_3:
        {
            const uint8_t slot = (uint8_t)(job->phase - UI_TEMPLATE_RENDER_SLOT_0);
            if (subpage != NULL)
                ui_renderer_template_draw_param_slot(&job->state, &job->plock_frame,
                    subpage, job->grouped_widget, job->grouped_widget_drawn,
                    slot, subpage->param_bank.params[slot]);
            job->phase = (ui_renderer_template_phase_t)(job->phase + 1);
            break;
        }
        case UI_TEMPLATE_RENDER_GROUP:
            ui_renderer_template_draw_mod_link_arrows(subpage);
            ui_renderer_template_job_draw_group(job);
            job->phase = UI_TEMPLATE_RENDER_FOOTER;
            break;
        case UI_TEMPLATE_RENDER_FOOTER:
            ui_renderer_template_draw_footer(&job->state);
            job->phase = UI_TEMPLATE_RENDER_FINALIZE;
            break;
        case UI_TEMPLATE_RENDER_FINALIZE:
            drv_display_set_font(&FONT_5X7);
            job->phase = UI_TEMPLATE_RENDER_IDLE;
            return 1U;
        default:
            job->phase = UI_TEMPLATE_RENDER_IDLE;
            return 1U;
    }
    return 0U;
}

uint8_t ui_renderer_template_draw_pending(void)
{
    return (g_ui_renderer_template_job.phase != UI_TEMPLATE_RENDER_IDLE) ? 1U : 0U;
}

void ui_renderer_template_draw_cancel(void)
{
    g_ui_renderer_template_job.phase = UI_TEMPLATE_RENDER_IDLE;
}
