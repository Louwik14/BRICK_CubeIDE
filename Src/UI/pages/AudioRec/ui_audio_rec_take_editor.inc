static uint16_t ui_page_audio_rec_take_peak(const sample_capture_state_t *state)
{
    uint16_t peak = 0U;
    if(state == 0)
    {
        return 0U;
    }
    if(state->line_peak > peak)
    {
        peak = state->line_peak;
    }
    const uint16_t global_peak = sample_capture_model_global_overview_peak();
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

static int ui_page_audio_rec_sample_to_y(int16_t sample,
                                         int inner_y,
                                         int inner_h,
                                         int cy,
                                         uint16_t vertical_ref,
                                         uint16_t vzoom_q8)
{
    const int max_amp = (inner_h / 2) - 3;
    if((max_amp <= 0) || (vertical_ref == 0U))
    {
        return cy;
    }

    (void)inner_y;
    return cy - (int)(((int64_t)sample * (int64_t)max_amp * (int64_t)vzoom_q8)
        / ((int64_t)vertical_ref * 256LL));
}

static uint8_t ui_page_audio_rec_clip_line_y(int *x0,
                                             int *y0,
                                             int *x1,
                                             int *y1,
                                             int top,
                                             int bottom)
{
    if((*y0 < top && *y1 < top) || (*y0 > bottom && *y1 > bottom))
    {
        return 0U;
    }

    if(*y0 < top || *y0 > bottom)
    {
        const int bound = (*y0 < top) ? top : bottom;
        const int dy = *y1 - *y0;
        if(dy == 0)
        {
            return 0U;
        }
        *x0 = *x0 + (int)(((int64_t)(*x1 - *x0) * (int64_t)(bound - *y0)) / (int64_t)dy);
        *y0 = bound;
    }

    if(*y1 < top || *y1 > bottom)
    {
        const int bound = (*y1 < top) ? top : bottom;
        const int dy = *y1 - *y0;
        if(dy == 0)
        {
            return 0U;
        }
        *x1 = *x0 + (int)(((int64_t)(*x1 - *x0) * (int64_t)(bound - *y0)) / (int64_t)dy);
        *y1 = bound;
    }
    return 1U;
}

static void ui_page_audio_rec_draw_line_waveform(const sample_capture_state_t *state,
                                                 int x,
                                                 int y,
                                                 int w,
                                                 int h,
                                                 uint16_t vertical_ref,
                                                 uint16_t vzoom_q8,
                                                 uint16_t *out_segments)
{
    const int inner_x = x + 1;
    const int inner_y = y + 1;
    const int inner_w = w - 2;
    const int inner_h = h - 2;
    const int cy = y + (h / 2);

    if((inner_w <= 1) || (inner_h <= 2) || (state->line_valid == 0U)
            || (state->line_count < 2U) || (vertical_ref == 0U))
    {
        if(out_segments != 0)
        {
            *out_segments = 0U;
        }
        return;
    }

    uint16_t segments = 0U;
    int prev_x = inner_x;
    int prev_y = ui_page_audio_rec_sample_to_y(state->line[0],
                                               inner_y,
                                               inner_h,
                                               cy,
                                               vertical_ref,
                                               vzoom_q8);

    for(uint16_t point = 1U; point < state->line_count; ++point)
    {
        const int draw_x = inner_x
            + (int)(((uint32_t)point * (uint32_t)(inner_w - 1))
                / (uint32_t)(state->line_count - 1U));
        const int draw_y = ui_page_audio_rec_sample_to_y(state->line[point],
                                                         inner_y,
                                                         inner_h,
                                                         cy,
                                                         vertical_ref,
                                                         vzoom_q8);
        if(draw_x != prev_x || draw_y != prev_y)
        {
            int x0 = prev_x;
            int y0 = prev_y;
            int x1 = draw_x;
            int y1 = draw_y;
            if(ui_page_audio_rec_clip_line_y(&x0,
                                             &y0,
                                             &x1,
                                             &y1,
                                             inner_y,
                                             inner_y + inner_h - 1) != 0U)
            {
                drv_display_draw_line(x0, y0, x1, y1);
                segments++;
            }
        }
        prev_x = draw_x;
        prev_y = draw_y;
    }
    if(out_segments != 0)
    {
        *out_segments = segments;
    }
}

static uint8_t ui_page_audio_rec_draw_global_overview(int x,
                                                      int y,
                                                      int w,
                                                      int h,
                                                      uint32_t view_start_frame,
                                                      uint32_t view_frames,
                                                      uint16_t vertical_ref,
                                                      uint16_t vzoom_q8)
{
    const int inner_x = x + 1;
    const int inner_y = y + 1;
    const int inner_w = w - 2;
    const int inner_h = h - 2;
    const int cy = y + (h / 2);

    if((inner_w <= 0) || (inner_h <= 2) || (vertical_ref == 0U)
            || (sample_capture_model_global_overview_ready() == 0U))
    {
        return 0U;
    }
    if(view_frames == 0U)
    {
        view_frames = 1U;
    }

    for(int col = 0; col < inner_w; ++col)
    {
        const uint32_t frame0 = view_start_frame
            + (uint32_t)(((uint64_t)col * (uint64_t)view_frames) / (uint64_t)inner_w);
        uint32_t frame1 = view_start_frame
            + (uint32_t)(((uint64_t)(col + 1) * (uint64_t)view_frames) / (uint64_t)inner_w);
        if(frame1 <= frame0)
        {
            frame1 = frame0 + 1U;
        }

        int16_t min_v = 0;
        int16_t max_v = 0;
        if(sample_capture_model_global_overview_minmax(frame0,
                                                       frame1 - frame0,
                                                       &min_v,
                                                       &max_v) == 0U)
        {
            continue;
        }
        ui_page_audio_rec_draw_minmax_column(inner_x + col,
                                             inner_y,
                                             inner_h,
                                             cy,
                                             min_v,
                                             max_v,
                                             vertical_ref,
                                             vzoom_q8);
    }
    return 1U;
}

static uint8_t ui_page_audio_rec_wavecache_request_visible(const waveform_cache_handle_t *handle,
                                                           waveform_cache_level_id_t level,
                                                           uint32_t view_start_frame,
                                                           uint32_t view_frames,
                                                           uint16_t inner_w,
                                                           uint32_t *out_tile_start,
                                                           uint32_t *out_tile_count)
{
    uint32_t frames_per_column = 0U;
    if((handle == 0)
            || (inner_w == 0U)
            || (waveform_cache_level_frames_per_column(level, &frames_per_column) == 0U)
            || (frames_per_column == 0U))
    {
        return 0U;
    }

    const uint32_t view_end = view_start_frame + view_frames;
    uint32_t col0 = view_start_frame / frames_per_column;
    uint32_t col1 = (view_end + frames_per_column - 1U) / frames_per_column;
    if(col1 <= col0)
    {
        col1 = col0 + 1U;
    }
    const uint32_t max_cols =
        (handle->frame_count + frames_per_column - 1U) / frames_per_column;
    if(max_cols == 0U)
    {
        return 0U;
    }
    if(col1 > max_cols)
    {
        col1 = max_cols;
    }
    const uint32_t tile0 = col0 / WAVEFORM_CACHE_TILE_COLUMNS;
    uint32_t tile1 = (col1 + WAVEFORM_CACHE_TILE_COLUMNS - 1U)
        / WAVEFORM_CACHE_TILE_COLUMNS;
    if(tile1 <= tile0)
    {
        tile1 = tile0 + 1U;
    }
    const uint32_t max_tiles =
        (max_cols + WAVEFORM_CACHE_TILE_COLUMNS - 1U) / WAVEFORM_CACHE_TILE_COLUMNS;
    if(tile1 > max_tiles)
    {
        tile1 = max_tiles;
    }

    uint32_t req_start = tile0;
    if(req_start > 0U)
    {
        req_start--;
    }
    uint32_t req_end = tile1;
    if(req_end < max_tiles)
    {
        req_end++;
    }
    const uint32_t req_count = req_end - req_start;

    (void)waveform_cache_request_tiles(handle,
                                       level,
                                       req_start,
                                       req_count,
                                       WAVEFORM_CACHE_REASON_EDITOR_VISIBLE);
    if(out_tile_start != 0)
    {
        *out_tile_start = tile0;
    }
    if(out_tile_count != 0)
    {
        *out_tile_count = tile1 - tile0;
    }
    return waveform_cache_tiles_ready(handle, level, tile0, tile1 - tile0);
}

static uint8_t ui_page_audio_rec_draw_wavecache(int x,
                                                int y,
                                                int w,
                                                int h,
                                                uint32_t view_start_frame,
                                                uint32_t view_frames,
                                                uint32_t frames_per_pixel,
                                                uint16_t vertical_ref,
                                                uint16_t vzoom_q8,
                                                uint32_t *out_frames_per_column)
{
    const int inner_x = x + 1;
    const int inner_y = y + 1;
    const int inner_w = w - 2;
    const int inner_h = h - 2;
    const int cy = y + (h / 2);

    if((inner_w <= 0) || (inner_h <= 2) || (vertical_ref == 0U))
    {
        return 0U;
    }

    waveform_cache_handle_t handle;
    if(sample_capture_model_waveform_cache_get_handle(&handle) == 0U)
    {
        return 0U;
    }

    waveform_cache_level_id_t level = WAVEFORM_CACHE_LEVEL_L3_FINE;
    if(waveform_cache_choose_level(frames_per_pixel, &level) == 0U)
    {
        return 0U;
    }
    uint32_t visible_tile_start = 0U;
    uint32_t visible_tile_count = 0U;
    if(ui_page_audio_rec_wavecache_request_visible(&handle,
                                                   level,
                                                   view_start_frame,
                                                   view_frames,
                                                   (uint16_t)inner_w,
                                                   &visible_tile_start,
                                                   &visible_tile_count) == 0U)
    {
        for(int fallback = (int)level - 1; fallback >= 0; --fallback)
        {
            waveform_cache_level_id_t fb_level = (waveform_cache_level_id_t)fallback;
            uint32_t fb_tile_start = 0U;
            uint32_t fb_tile_count = 0U;
            if(ui_page_audio_rec_wavecache_request_visible(&handle,
                                                           fb_level,
                                                           view_start_frame,
                                                           view_frames,
                                                           (uint16_t)inner_w,
                                                           &fb_tile_start,
                                                           &fb_tile_count) != 0U)
            {
                level = fb_level;
                visible_tile_start = fb_tile_start;
                visible_tile_count = fb_tile_count;
                break;
            }
        }
        if(waveform_cache_tiles_ready(&handle, level, visible_tile_start, visible_tile_count) == 0U)
        {
            return 0U;
        }
    }

    uint32_t frames_per_column = 0U;
    if((waveform_cache_level_frames_per_column(level, &frames_per_column) == 0U)
            || (frames_per_column == 0U))
    {
        return 0U;
    }
    if(out_frames_per_column != 0)
    {
        *out_frames_per_column = frames_per_column;
    }

    for(int col = 0; col < inner_w; ++col)
    {
        uint32_t frame0 = view_start_frame
            + (uint32_t)(((uint64_t)col * (uint64_t)view_frames) / (uint64_t)inner_w);
        uint32_t frame1 = view_start_frame
            + (uint32_t)(((uint64_t)(col + 1) * (uint64_t)view_frames) / (uint64_t)inner_w);
        if(frame1 <= frame0)
        {
            frame1 = frame0 + 1U;
        }
        uint32_t column0 = frame0 / frames_per_column;
        uint32_t column1 = (frame1 + frames_per_column - 1U) / frames_per_column;
        if(column1 <= column0)
        {
            column1 = column0 + 1U;
        }
        int16_t min_v = 0;
        int16_t max_v = 0;
        if(waveform_cache_minmax_from_ram(&handle,
                                          level,
                                          column0,
                                          column1 - column0,
                                          &min_v,
                                          &max_v) == 0U)
        {
            return 0U;
        }
        ui_page_audio_rec_draw_minmax_column(inner_x + col,
                                             inner_y,
                                             inner_h,
                                             cy,
                                             min_v,
                                             max_v,
                                             vertical_ref,
                                             vzoom_q8);
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

    const uint16_t overview_peak = sample_capture_model_global_overview_peak();
    if((overview_peak != 0U) && (sample_capture_model_global_overview_ready() != 0U))
    {
        const int cy = y + (h / 2);
        for(int col = 0; col < inner_w; ++col)
        {
            const uint32_t frame0 =
                (uint32_t)(((uint64_t)col * (uint64_t)state->recorded_frames)
                    / (uint64_t)inner_w);
            uint32_t frame1 =
                (uint32_t)(((uint64_t)(col + 1) * (uint64_t)state->recorded_frames)
                    / (uint64_t)inner_w);
            if(frame1 <= frame0)
            {
                frame1 = frame0 + 1U;
            }

            int16_t min_v = 0;
            int16_t max_v = 0;
            if(sample_capture_model_global_overview_minmax(frame0,
                                                           frame1 - frame0,
                                                           &min_v,
                                                           &max_v) != 0U)
            {
                ui_page_audio_rec_draw_minmax_column(inner_x + col,
                                                     inner_y,
                                                     inner_h,
                                                     cy,
                                                     min_v,
                                                     max_v,
                                                     overview_peak,
                                                     256U);
            }
        }
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
    uint16_t draw_line_segments = 0U;
    uint32_t wavecache_frames_per_column = 0U;
    uint8_t fallback_reason = 0U;
    if((state->view == SAMPLE_CAPTURE_VIEW_REC_EDIT)
            && (state->take_valid != 0U)
            && (state->recorded_frames != 0U))
    {
        const uint16_t line_columns = (uint16_t)((w > 2) ? (w - 2) : 0);
        uint32_t samples_per_pixel = view_frames;
        if(line_columns != 0U)
        {
            samples_per_pixel = (view_frames + (uint32_t)line_columns - 1U)
                / (uint32_t)line_columns;
        }
        const uint8_t prefer_old_audio_tile =
            (uint8_t)((samples_per_pixel < 128U)
                && (sample_capture_model_view_uses_tile_cache(view_frames) != 0U));

        sample_capture_state_t draw_state = *state;
        if(prefer_old_audio_tile != 0U)
        {
            sample_capture_model_request_line_waveform(view_start_frame,
                                                       view_frames,
                                                       line_columns);
            sample_capture_model_get_state(&draw_state);
        }

        if((prefer_old_audio_tile != 0U)
                && (draw_state.line_valid != 0U)
                && (draw_state.line_start_frame == view_start_frame)
                && (draw_state.line_frames == view_frames))
        {
            ui_page_audio_rec_draw_line_waveform(&draw_state, x, y, w, h,
                                                 ui_page_audio_rec_edit_vertical_ref(&draw_state),
                                                 ui_page_audio_rec_vzoom_scale_q8(draw_state.edit_vzoom),
                                                 &draw_line_segments);
            waveform_drawn = 1U;
            renderer = SAMPLE_CAPTURE_RENDERER_OLD_AUDIO_TILE;
        }

        if((waveform_drawn == 0U)
                && ui_page_audio_rec_draw_wavecache(x,
                                                    y,
                                                    w,
                                                    h,
                                                    view_start_frame,
                                                    view_frames,
                                                    samples_per_pixel,
                                                    ui_page_audio_rec_edit_vertical_ref(state),
                                                    ui_page_audio_rec_vzoom_scale_q8(state->edit_vzoom),
                                                    &wavecache_frames_per_column) != 0U)
        {
            waveform_drawn = 1U;
            renderer = SAMPLE_CAPTURE_RENDERER_BRKWAVE_TILE;
        }

        if((waveform_drawn == 0U)
                && (prefer_old_audio_tile != 0U)
                && (draw_state.line_valid != 0U)
                && (((draw_state.line_start_frame <= view_start_frame)
                     && ((draw_state.line_start_frame + draw_state.line_frames)
                         >= (view_start_frame + view_frames)))
                    || ((draw_state.line_frames == view_frames)
                        && (((draw_state.line_start_frame > view_start_frame)
                                ? (draw_state.line_start_frame - view_start_frame)
                                : (view_start_frame - draw_state.line_start_frame))
                            <= (view_frames / 4U)))))
        {
            ui_page_audio_rec_draw_line_waveform(&draw_state, x, y, w, h,
                                                 ui_page_audio_rec_edit_vertical_ref(&draw_state),
                                                 ui_page_audio_rec_vzoom_scale_q8(draw_state.edit_vzoom),
                                                 &draw_line_segments);
            waveform_drawn = 1U;
            renderer = SAMPLE_CAPTURE_RENDERER_OLD_LINE;
            fallback_reason = 2U;
        }

        if(waveform_drawn == 0U
                && ui_page_audio_rec_draw_global_overview(x, y, w, h,
                                                          view_start_frame,
                                                          view_frames,
                                                          ui_page_audio_rec_edit_vertical_ref(state),
                                                          ui_page_audio_rec_vzoom_scale_q8(state->edit_vzoom)) != 0U)
        {
            waveform_drawn = 1U;
            renderer = SAMPLE_CAPTURE_RENDERER_GLOBAL_OVERVIEW;
            fallback_reason = (prefer_old_audio_tile != 0U) ? 3U : 1U;
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
                                                 wavecache_frames_per_column,
                                                 state->line_valid,
                                                 state->line_count,
                                                 draw_line_segments,
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

