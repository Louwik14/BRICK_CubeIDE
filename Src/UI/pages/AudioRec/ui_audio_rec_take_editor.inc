static uint16_t ui_page_audio_rec_take_peak(const sample_capture_state_t *state)
{
    uint16_t peak = 0U;
    if(state == 0)
    {
        return 0U;
    }
    waveform_source_t source;
    const uint16_t global_peak =
        (sample_capture_model_rec_waveform_source(&source) != 0U)
            ? waveform_rec_peak(&source) : 0U;
    if(global_peak > peak)
    {
        peak = global_peak;
    }
    return peak;
}

static uint16_t ui_page_audio_rec_edit_vertical_ref(const sample_capture_state_t *state)
{
    uint16_t ref = ui_page_audio_rec_take_peak(state);
    if(ref < 4096U)
    {
        ref = 4096U;
    }
    if(ref > SAMPLE_CAPTURE_WAVEFORM_FULL_SCALE)
    {
        ref = SAMPLE_CAPTURE_WAVEFORM_FULL_SCALE;
    }
    return ref;
}

static void ui_page_audio_rec_draw_minmax_column(int x,
                                                 int inner_y,
                                                 int inner_h,
                                                 int cy,
                                                 int16_t min_v,
                                                 int16_t max_v,
                                                 uint16_t vertical_ref,
                                                 uint16_t vzoom_q8)
{
    const int max_amp = (inner_h / 2) - 2;
    if((max_amp <= 0) || (vertical_ref == 0U))
    {
        return;
    }

    int y0 = cy - (int)(((int64_t)max_v * (int64_t)max_amp * (int64_t)vzoom_q8)
        / ((int64_t)vertical_ref * 256LL));
    int y1 = cy - (int)(((int64_t)min_v * (int64_t)max_amp * (int64_t)vzoom_q8)
        / ((int64_t)vertical_ref * 256LL));
    if(y0 > y1)
    {
        const int tmp = y0;
        y0 = y1;
        y1 = tmp;
    }
    const int clip_top = inner_y;
    const int clip_bottom = inner_y + inner_h - 1;
    if((y1 < clip_top) || (y0 > clip_bottom))
    {
        return;
    }
    if(y0 < inner_y)
    {
        y0 = inner_y;
    }
    if(y1 > clip_bottom)
    {
        y1 = clip_bottom;
    }
    drv_display_draw_line(x, y0, x, y1);
}

static void ui_page_audio_rec_draw_zero_line(int x, int y, int w, int h)
{
    if((w <= 2) || (h <= 2))
    {
        return;
    }
    const int cy = y + (h / 2);
    drv_display_draw_line(x + 1, cy, x + w - 2, cy);
}

static uint8_t ui_page_audio_rec_draw_rec_service_overview(int x,
                                                          int y,
                                                          int w,
                                                          int h,
                                                          uint32_t view_start_frame,
                                                          uint32_t view_frames,
                                                          uint16_t vertical_ref,
                                                          uint16_t vzoom_q8)
{
    const int inner_w = w - 2;
    const int inner_h = h - 2;
    if((inner_w <= 0) || (inner_w > 126) || (inner_h <= 2)
            || (vertical_ref == 0U))
    {
        return 0U;
    }
    waveform_source_t source;
    if(sample_capture_model_rec_waveform_source(&source) == 0U)
    {
        return 0U;
    }
    waveform_column_t columns[126];
    if(waveform_request(&source, view_start_frame, view_frames,
                        (uint8_t)inner_w, columns) != WAVEFORM_RESULT_READY)
    {
        return 0U;
    }
    const int cy = y + (h / 2);
    for(int col = 0; col < inner_w; ++col)
    {
        ui_page_audio_rec_draw_minmax_column(x + 1 + col, y + 1,
            inner_h, cy, columns[col].min, columns[col].max,
            vertical_ref, vzoom_q8);
    }
    return 1U;
}

static void ui_page_audio_rec_invert_active_range(const sample_capture_state_t *state,
                                                  int x,
                                                  int y,
                                                  int w,
                                                  int h,
                                                  uint32_t view_start_frame,
                                                  uint32_t view_frames)
{
    if((state == 0) || (state->view != SAMPLE_CAPTURE_VIEW_REC_EDIT)
            || (state->take_valid == 0U) || (state->recorded_frames == 0U)
            || (state->edit_end_frame <= state->edit_start_frame)
            || (view_frames == 0U) || (w <= 2) || (h <= 2))
    {
        return;
    }

    const uint32_t view_end_frame = view_start_frame + view_frames;
    uint32_t active_start = state->edit_start_frame;
    uint32_t active_end = state->edit_end_frame;
    if(active_start < view_start_frame)
    {
        active_start = view_start_frame;
    }
    if(active_end > view_end_frame)
    {
        active_end = view_end_frame;
    }
    if(active_end <= active_start)
    {
        return;
    }

    const int inner_x = x + 1;
    const int inner_y = y + 1;
    const int inner_w = w - 2;
    const int inner_h = h - 2;
    int x0 = inner_x
        + (int)(((uint64_t)(active_start - view_start_frame) * (uint64_t)inner_w)
            / (uint64_t)view_frames);
    int x1 = inner_x
        + (int)((((uint64_t)(active_end - view_start_frame) * (uint64_t)inner_w)
            + (uint64_t)view_frames - 1ULL) / (uint64_t)view_frames);

    if(x0 < inner_x)
    {
        x0 = inner_x;
    }
    if(x1 > (inner_x + inner_w))
    {
        x1 = inner_x + inner_w;
    }
    if(x1 <= x0)
    {
        x1 = x0 + 1;
    }
    if(x1 > (inner_x + inner_w))
    {
        x1 = inner_x + inner_w;
    }
    if(x1 <= x0)
    {
        return;
    }

    drv_display_set_draw_color(2U);
    drv_display_fill_rect(x0, inner_y, x1 - x0, inner_h);
    drv_display_set_draw_color(1U);
}

static void ui_page_audio_rec_draw_edit_overview(const sample_capture_state_t *state,
                                                 uint32_t view_start_frame,
                                                 uint32_t view_frames)
{
    if((state == 0) || (state->view != SAMPLE_CAPTURE_VIEW_REC_EDIT)
            || (state->take_valid == 0U) || (state->recorded_frames == 0U))
    {
        return;
    }

    const int x = UI_REC_EDIT_OVERVIEW_X;
    const int y = UI_REC_EDIT_OVERVIEW_Y;
    const int w = UI_REC_EDIT_OVERVIEW_W;
    const int h = UI_REC_EDIT_OVERVIEW_H;
    const int inner_x = x + 1;
    const int inner_y = y + 1;
    const int inner_w = w - 2;
    const int inner_h = h - 2;

    if((inner_w <= 0) || (inner_h <= 2))
    {
        return;
    }

    drv_display_clear_rect(x, y, w, h);
    drv_display_draw_rect(x, y, w, h);

    waveform_source_t source;
    if(sample_capture_model_rec_waveform_source(&source) != 0U)
    {
        const uint16_t vertical_ref = waveform_rec_peak(&source);
        (void)ui_page_audio_rec_draw_rec_service_overview(x, y, w, h,
            0U, state->recorded_frames, vertical_ref, 256U);
    }

    if(state->edit_end_frame > state->edit_start_frame)
    {
        uint32_t active_start = state->edit_start_frame;
        uint32_t active_end = state->edit_end_frame;
        if(active_start > state->recorded_frames)
        {
            active_start = state->recorded_frames;
        }
        if(active_end > state->recorded_frames)
        {
            active_end = state->recorded_frames;
        }
        if(active_end > active_start)
        {
            int ax0 = inner_x
                + (int)(((uint64_t)active_start * (uint64_t)inner_w)
                    / (uint64_t)state->recorded_frames);
            int ax1 = inner_x
                + (int)((((uint64_t)active_end * (uint64_t)inner_w)
                    + (uint64_t)state->recorded_frames - 1ULL)
                    / (uint64_t)state->recorded_frames);
            if(ax0 < inner_x)
            {
                ax0 = inner_x;
            }
            if(ax1 > (inner_x + inner_w))
            {
                ax1 = inner_x + inner_w;
            }
            if(ax1 <= ax0)
            {
                ax1 = ax0 + 1;
            }
            if(ax1 > (inner_x + inner_w))
            {
                ax1 = inner_x + inner_w;
            }
            if(ax1 > ax0)
            {
                drv_display_set_draw_color(2U);
                drv_display_fill_rect(ax0, inner_y, ax1 - ax0, inner_h);
                drv_display_set_draw_color(1U);
            }
        }
    }

    if(view_frames == 0U)
    {
        view_frames = 1U;
    }
    uint32_t view_end_frame = view_start_frame + view_frames;
    if(view_end_frame > state->recorded_frames)
    {
        view_end_frame = state->recorded_frames;
    }
    if(view_start_frame > state->recorded_frames)
    {
        view_start_frame = state->recorded_frames;
    }
    if(view_end_frame <= view_start_frame)
    {
        view_end_frame = view_start_frame + 1U;
        if(view_end_frame > state->recorded_frames)
        {
            view_end_frame = state->recorded_frames;
        }
    }
    if(view_end_frame <= view_start_frame)
    {
        return;
    }

    int vx0 = inner_x
        + (int)(((uint64_t)view_start_frame * (uint64_t)inner_w)
            / (uint64_t)state->recorded_frames);
    int vx1 = inner_x
        + (int)((((uint64_t)view_end_frame * (uint64_t)inner_w)
            + (uint64_t)state->recorded_frames - 1ULL)
            / (uint64_t)state->recorded_frames);
    if(vx0 < inner_x)
    {
        vx0 = inner_x;
    }
    if(vx1 > (inner_x + inner_w))
    {
        vx1 = inner_x + inner_w;
    }
    if(vx1 <= vx0)
    {
        vx1 = vx0 + 1;
    }
    if(vx1 > (inner_x + inner_w))
    {
        vx1 = inner_x + inner_w;
    }

    drv_display_draw_line(x, y - 1, x + w - 1, y - 1);
    drv_display_set_draw_color(2U);
    if((vx1 - vx0) <= 1)
    {
        const uint64_t focus_center =
            ((uint64_t)view_start_frame + (uint64_t)view_end_frame) / 2ULL;
        int focus_x = inner_x
            + (int)((focus_center * (uint64_t)inner_w)
                / (uint64_t)state->recorded_frames);
        if(focus_x < inner_x)
        {
            focus_x = inner_x;
        }
        if(focus_x >= (inner_x + inner_w))
        {
            focus_x = inner_x + inner_w - 1;
        }
        drv_display_draw_line(focus_x, y, focus_x, y + h - 2);
    }
    else
    {
        drv_display_draw_rect(vx0, y, vx1 - vx0, h - 1);
        drv_display_draw_pixel(vx0, y, true);
        drv_display_draw_pixel(vx1 - 1, y, true);
        drv_display_draw_pixel(vx0, y + h - 2, true);
        drv_display_draw_pixel(vx1 - 1, y + h - 2, true);
    }
    drv_display_set_draw_color(1U);
}

static void ui_page_audio_rec_draw_marker_bar(const sample_capture_state_t *state)
{
#if SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS
    const uint32_t waveform_draw_start_ms = HAL_GetTick();
#endif
    const int x = UI_AUDIO_REC_WAVE_X;
    const int y = UI_AUDIO_REC_WAVE_Y;
    const int w = UI_AUDIO_REC_WAVE_W;
    const int h = UI_AUDIO_REC_WAVE_H;
    drv_display_clear_rect(x, y, w, h);
    drv_display_draw_rect(x, y, w, h);
    ui_page_audio_rec_draw_zero_line(x, y, w, h);
    if(!((state->view == SAMPLE_CAPTURE_VIEW_REC_EDIT)
                && (state->take_valid != 0U)
                && (state->recorded_frames != 0U)))
    {
        if((state->view == SAMPLE_CAPTURE_VIEW_REC_EDIT) && (state->take_valid != 0U))
        {
            drv_display_draw_text(52U, 34U, "BUILD");
        }
        return;
    }

    uint32_t view_start_frame = 0U;
    uint32_t view_frames = state->recorded_frames;
    if(state->view == SAMPLE_CAPTURE_VIEW_REC_EDIT)
    {
        if(view_frames == 0U)
        {
            view_frames = 1U;
        }
        view_frames =
            sample_capture_model_visible_frames_for_zoom(state->recorded_frames, state->edit_zoom);
        if(view_frames >= state->recorded_frames)
        {
            view_start_frame = 0U;
            view_frames = state->recorded_frames;
        }
        else
        {
            view_start_frame = state->edit_scroll_frame;
            if(view_start_frame > (state->recorded_frames - view_frames))
            {
                view_start_frame = state->recorded_frames - view_frames;
            }
        }
    }
    if(view_frames == 0U)
    {
        view_frames = 1U;
    }

    uint8_t waveform_drawn = 0U;
    sample_capture_renderer_debug_t renderer = SAMPLE_CAPTURE_RENDERER_EMPTY;
    uint8_t fallback_reason = 0U;
    if((state->view == SAMPLE_CAPTURE_VIEW_REC_EDIT)
            && (state->take_valid != 0U)
            && (state->recorded_frames != 0U))
    {
        if(ui_page_audio_rec_draw_rec_service_overview(x, y, w, h,
            view_start_frame, view_frames,
            ui_page_audio_rec_edit_vertical_ref(state),
            ui_page_audio_rec_vzoom_scale_q8(state->edit_vzoom)) != 0U)
        {
            waveform_drawn = 1U;
            renderer = SAMPLE_CAPTURE_RENDERER_SERVICE;
        }
    }

    if(waveform_drawn == 0U)
    {
        if(state->view == SAMPLE_CAPTURE_VIEW_REC_EDIT)
        {
            renderer = SAMPLE_CAPTURE_RENDERER_EMPTY;
            fallback_reason = 4U;
        }
    }

    if(state->view == SAMPLE_CAPTURE_VIEW_REC_EDIT)
    {
#if SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS
        const uint32_t waveform_ms = HAL_GetTick() - waveform_draw_start_ms;
        g_ui_page_audio_rec_last_waveform_ms = waveform_ms;
#else
        const uint32_t waveform_ms = 0U;
#endif
        if(state->error != SAMPLE_CAPTURE_ERROR_NONE)
        {
            renderer = SAMPLE_CAPTURE_RENDERER_ERROR;
        }
#if SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS
        const uint16_t inner_w_dbg = (uint16_t)((w > 2) ? (w - 2) : 0);
        uint32_t samples_per_pixel = view_frames;
        if(inner_w_dbg != 0U)
        {
            samples_per_pixel = (view_frames + (uint32_t)inner_w_dbg - 1U) / (uint32_t)inner_w_dbg;
        }
        sample_capture_model_debug_note_renderer(renderer,
                                                 state->edit_zoom,
                                                 view_start_frame,
                                                 view_frames,
                                                 inner_w_dbg,
                                                 samples_per_pixel,
                                                 0U,
                                                 0U,
                                                 0U,
                                                 0U,
                                                 fallback_reason);
#endif
        (void)waveform_ms;
#if !(SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS)
        (void)renderer;
        (void)fallback_reason;
#endif
    }

    ui_page_audio_rec_invert_active_range(state, x, y, w, h, view_start_frame, view_frames);

    if((state->take_valid != 0U) && (state->recorded_frames != 0U))
    {
        if((state->edit_loop_start_frame >= view_start_frame)
                && (state->edit_loop_start_frame <= (view_start_frame + view_frames)))
        {
            int loop_start_x = x + 1 + (int)(((uint64_t)(state->edit_loop_start_frame - view_start_frame)
                    * (uint64_t)(w - 2)) / view_frames);
            drv_display_set_draw_color(2U);
            for(int yy = y + 1; yy < y + h - 1; yy += 4)
            {
                drv_display_draw_line(loop_start_x, yy, loop_start_x, yy + 1);
            }
            drv_display_set_draw_color(1U);
        }
        if((state->edit_loop_end_frame >= view_start_frame)
                && (state->edit_loop_end_frame <= (view_start_frame + view_frames)))
        {
            int loop_end_x = x + 1 + (int)(((uint64_t)(state->edit_loop_end_frame - view_start_frame)
                    * (uint64_t)(w - 2)) / view_frames);
            if(loop_end_x >= x + w)
            {
                loop_end_x = x + w - 1;
            }
            drv_display_set_draw_color(2U);
            for(int yy = y + 2; yy < y + h - 1; yy += 4)
            {
                drv_display_draw_line(loop_end_x, yy, loop_end_x, yy + 1);
            }
            drv_display_set_draw_color(1U);
        }
        if((state->edit_start_frame >= view_start_frame)
                && (state->edit_start_frame <= (view_start_frame + view_frames)))
        {
            int start_x = x + 1 + (int)(((uint64_t)(state->edit_start_frame - view_start_frame)
                    * (uint64_t)(w - 2)) / view_frames);
            drv_display_draw_line(start_x, y, start_x, y + h - 1);
        }
        if((state->edit_end_frame >= view_start_frame)
                && (state->edit_end_frame <= (view_start_frame + view_frames)))
        {
            int end_x = x + 1 + (int)(((uint64_t)(state->edit_end_frame - view_start_frame)
                    * (uint64_t)(w - 2)) / view_frames);
            if(end_x >= x + w)
            {
                end_x = x + w - 1;
            }
            drv_display_draw_line(end_x, y, end_x, y + h - 1);
        }
    }

    ui_page_audio_rec_draw_edit_overview(state, view_start_frame, view_frames);
}

