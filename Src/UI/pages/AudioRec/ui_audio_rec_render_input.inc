static void ui_page_audio_rec_render(void)
{
#if SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS
    const uint32_t page_draw_start_ms = HAL_GetTick();
#endif
    sample_capture_state_t state;
    char line[32];
    sample_capture_model_get_state(&state);

    drv_display_set_font(&FONT_5X7);
    drv_display_draw_text(0U, 0U, "AUDIO REC");
    drv_display_draw_text(80U, 0U, ui_page_audio_rec_phase_label(state.phase));

    drv_display_set_font(&FONT_4X6);
    const uint8_t live_label_y = (uint8_t)(UI_AUDIO_REC_WAVE_Y - drv_display_font_height() - 2U);
    (void)snprintf(line, sizeof(line), "ARM %s", ui_page_audio_rec_arm_label(state.arm));
    drv_display_draw_text(0U, live_label_y, line);
    if(state.len_bars == 0U)
    {
        (void)snprintf(line, sizeof(line), "LEN FREE");
    }
    else
    {
        (void)snprintf(line, sizeof(line), "LEN %u", (unsigned)state.len_bars);
    }
    drv_display_draw_text(34U, live_label_y, line);
    (void)snprintf(line, sizeof(line), "Q %s", ui_page_audio_rec_quant_label(state.quant));
    drv_display_draw_text(68U, live_label_y, line);
    (void)snprintf(line, sizeof(line), "THR %d", (int)state.threshold_dbfs);
    drv_display_draw_text(96U, live_label_y, line);
    ui_page_audio_rec_draw_marker_bar(&state);
    ui_page_audio_rec_draw_live_meter(&state);

    (void)snprintf(line, sizeof(line), "LINE %s MIC %s",
                   state.line_enabled ? "ON" : "OFF",
                   state.mic_enabled ? "ON" : "OFF");
    drv_display_draw_text(UI_AUDIO_REC_STATUS_X, 50U, line);

    if(state.error != SAMPLE_CAPTURE_ERROR_NONE)
    {
        drv_display_draw_text(46U, 34U, ui_page_audio_rec_error_label(state.error));
    }
#if SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS
    if(state.view == SAMPLE_CAPTURE_VIEW_REC_EDIT)
    {
        sample_capture_model_debug_note_draw_cost(HAL_GetTick() - page_draw_start_ms,
                                                  g_ui_page_audio_rec_last_waveform_ms);
    }
#endif
}

static void ui_page_rec_edit_render(void)
{
#if SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS
    const uint32_t page_draw_start_ms = HAL_GetTick();
#endif
    sample_capture_state_t state;
    sample_capture_model_get_state(&state);
    sample_capture_model_note_rec_edit_first_render();

    drv_display_set_font(&FONT_5X7);
    drv_display_draw_text(ui_page_audio_rec_center_x("REC EDIT"), UI_REC_EDIT_TITLE_Y, "REC EDIT");
    const uint8_t title_height = drv_display_font_height();

    drv_display_set_font(&FONT_4X6);
    const uint8_t encoder_label_y =
        (uint8_t)(UI_REC_EDIT_TITLE_Y + title_height + UI_REC_EDIT_TITLE_GAP);
    const uint8_t page_label_y = (uint8_t)(OLED_HEIGHT - drv_display_font_height() + 1U);
    const uint8_t alt_held = button_down(BTN_SHIFT);
    if(alt_held != 0U)
    {
        ui_page_audio_rec_draw_label(0U, encoder_label_y, "VZOOM", 1U);
        ui_page_audio_rec_draw_label(38U, encoder_label_y, "FINE", 1U);
        ui_page_audio_rec_draw_label(76U, encoder_label_y, "L.ST", 1U);
        ui_page_audio_rec_draw_label(106U, encoder_label_y, "L.END", 1U);
    }
    else
    {
        drv_display_draw_text(0U, encoder_label_y, "ZOOM");
        drv_display_draw_text(38U, encoder_label_y, "POS");
        drv_display_draw_text(72U, encoder_label_y, "START");
        drv_display_draw_text(108U, encoder_label_y, "END");
    }

    ui_page_audio_rec_draw_marker_bar(&state);
    if(state.error != SAMPLE_CAPTURE_ERROR_NONE)
    {
        drv_display_draw_text(46U, 34U, ui_page_audio_rec_error_label(state.error));
    }
    if(g_ui_rec_edit_assign_popup != 0U)
    {
        drv_display_fill_rect(24U, 26U, 80U, 20U);
        drv_display_draw_text_inverted(42U, 30U, "ASSIGN?");
        drv_display_draw_text_inverted(30U, 40U, "NO");
        drv_display_draw_text_inverted(78U, 40U, "YES");
        drv_display_draw_text(0U, 54U, "NO");
        drv_display_draw_text(34U, 54U, "YES");
        drv_display_draw_text(72U, 54U, "-");
        drv_display_draw_text(120U, 54U, "-");
    }
    else
    {
        drv_display_draw_text(0U, page_label_y, "RETURN");
        drv_display_draw_text(34U, page_label_y, "SAVE");
        ui_page_audio_rec_draw_label(62U, page_label_y, "ZCROSS", state.edit_zcross_enabled);
        ui_page_audio_rec_draw_label(108U, page_label_y, "SHFT", alt_held);
    }
#if SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS
    sample_capture_model_debug_note_draw_cost(HAL_GetTick() - page_draw_start_ms,
                                              g_ui_page_audio_rec_last_waveform_ms);
#endif
}

uint8_t ui_page_audio_rec_handle_encoder(uint8_t encoder, int16_t delta)
{
    if(delta == 0)
    {
        return 1U;
    }

    sample_capture_state_t state;
    sample_capture_model_get_state(&state);
    if(state.view == SAMPLE_CAPTURE_VIEW_REC_EDIT)
    {
        if(g_ui_rec_edit_assign_popup != 0U)
        {
            return 1U;
        }
        return control_domain_request_audio_rec(&(control_audio_rec_intent_t){
            .operation = CONTROL_AUDIO_REC_STEP_EDIT,
            .value0 = encoder,
            .value1 = button_down(BTN_SHIFT),
            .delta = delta
        });
    }

    switch(encoder)
    {
        case 0U:
            return control_domain_request_audio_rec(&(control_audio_rec_intent_t){
                .operation = CONTROL_AUDIO_REC_STEP_ARM, .delta = delta
            });
        case 1U:
            return control_domain_request_audio_rec(&(control_audio_rec_intent_t){
                .operation = CONTROL_AUDIO_REC_STEP_LENGTH, .delta = delta
            });
        case 2U:
            return control_domain_request_audio_rec(&(control_audio_rec_intent_t){
                .operation = CONTROL_AUDIO_REC_STEP_QUANTIZATION, .delta = delta
            });
        case 3U:
            return control_domain_request_audio_rec(&(control_audio_rec_intent_t){
                .operation = CONTROL_AUDIO_REC_STEP_THRESHOLD, .delta = delta
            });
        default:
            return 0U;
    }
}

uint8_t ui_page_audio_rec_is_open(void)
{
    const uint8_t page = ui_page_get_id();
    return (uint8_t)((page == UI_PAGE_AUDIO_REC) || (page == UI_PAGE_REC_EDIT));
}

const ui_page_t g_ui_page_audio_rec = {
    .enter = ui_page_audio_rec_enter,
    .leave = 0,
    .handle_event = ui_page_audio_rec_handle_event,
    .tick = ui_page_audio_rec_tick,
    .sync_active_context = 0,
    .render = ui_page_audio_rec_render,
    .context = 0,
};

const ui_page_t g_ui_page_rec_edit = {
    .enter = ui_page_rec_edit_enter,
    .leave = 0,
    .handle_event = ui_page_rec_edit_handle_event,
    .tick = ui_page_audio_rec_tick,
    .sync_active_context = 0,
    .render = ui_page_rec_edit_render,
    .context = 0,
};
