void sample_capture_model_init(void)
{
    memset(&g_sample_capture, 0, sizeof(g_sample_capture));
    sample_capture_debug_reset();
    g_sample_capture.state.view = SAMPLE_CAPTURE_VIEW_AUDIO_REC;
    g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
    g_sample_capture.state.len_bars = 1U;
    g_sample_capture.state.quant = SAMPLE_CAPTURE_QUANT_NOW;
    g_sample_capture.state.threshold_dbfs = SAMPLE_CAPTURE_THRESHOLD_DBFS_DEFAULT;
    g_sample_capture.trigger_threshold_peak_abs_pcm24 =
        sample_capture_threshold_to_peak_abs_pcm24(
            SAMPLE_CAPTURE_THRESHOLD_DBFS_DEFAULT);
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_IDLE;
    g_sample_capture.state.edit_vzoom = SAMPLE_CAPTURE_EDIT_VZOOM_DEFAULT;
    sample_capture_detail_reset();
}

void sample_capture_model_get_state(sample_capture_state_t *out_state)
{
    if(out_state == 0)
    {
        return;
    }
    *out_state = g_sample_capture.state;
    out_state->line_valid = g_sample_capture_line_hot.valid;
    out_state->line_start_frame = g_sample_capture_line_hot.start_frame;
    out_state->line_frames = g_sample_capture_line_hot.frames;
    out_state->line_count = g_sample_capture_line_hot.count;
    out_state->line_peak = g_sample_capture_line_hot.peak;
    memcpy(out_state->line, g_sample_capture_line_hot.points, sizeof(out_state->line));
    for(uint8_t track = 0U; track < SAMPLE_CAPTURE_TRACK_COUNT; ++track)
    {
        out_state->route_enabled[track] = g_sample_capture.route_mask[track];
    }
}

void sample_capture_model_get_ui_state(sample_capture_ui_state_t *out_state)
{
    if(out_state == 0)
    {
        return;
    }
    out_state->view = g_sample_capture.state.view;
    out_state->phase = g_sample_capture.state.phase;
    out_state->arm = g_sample_capture.state.arm;
    out_state->len_bars = g_sample_capture.state.len_bars;
    out_state->quant = g_sample_capture.state.quant;
    out_state->threshold_dbfs = g_sample_capture.state.threshold_dbfs;
    out_state->line_enabled = g_sample_capture.state.line_enabled;
    out_state->mic_enabled = g_sample_capture.state.mic_enabled;
    out_state->usb_enabled = g_sample_capture.state.usb_enabled;
    out_state->live_peak_abs_pcm24 = g_sample_capture.state.live_peak_abs_pcm24;
    out_state->take_valid = g_sample_capture.state.take_valid;
    out_state->recorded_frames = g_sample_capture.state.recorded_frames;
    out_state->edit_start_frame = g_sample_capture.state.edit_start_frame;
    out_state->edit_end_frame = g_sample_capture.state.edit_end_frame;
    out_state->edit_loop_start_frame = g_sample_capture.state.edit_loop_start_frame;
    out_state->edit_loop_end_frame = g_sample_capture.state.edit_loop_end_frame;
    out_state->edit_zoom = g_sample_capture.state.edit_zoom;
    out_state->edit_vzoom = g_sample_capture.state.edit_vzoom;
    out_state->edit_zcross_enabled = g_sample_capture.state.edit_zcross_enabled;
    out_state->edit_scroll_frame = g_sample_capture.state.edit_scroll_frame;
    out_state->line_valid = g_sample_capture_line_hot.valid;
    out_state->line_start_frame = g_sample_capture_line_hot.start_frame;
    out_state->line_frames = g_sample_capture_line_hot.frames;
    out_state->line_count = g_sample_capture_line_hot.count;
    out_state->line_peak = g_sample_capture_line_hot.peak;
    memcpy(out_state->line, g_sample_capture_line_hot.points, sizeof(out_state->line));
    out_state->error = g_sample_capture.state.error;
}

void sample_capture_model_set_view(sample_capture_view_t view)
{
    g_sample_capture.state.view = view;
}

uint8_t sample_capture_model_toggle_route(uint8_t track)
{
    if(track >= SAMPLE_CAPTURE_TRACK_COUNT)
    {
        return 0U;
    }

    g_sample_capture.route_mask[track] = (g_sample_capture.route_mask[track] == 0U) ? 1U : 0U;
    sample_capture_publish_rec_bus();
    return 1U;
}

uint8_t sample_capture_model_source_track_is_enabled(uint8_t track)
{
    if(track >= SAMPLE_CAPTURE_TRACK_COUNT)
    {
        return 0U;
    }
    return g_sample_capture.route_mask[track];
}

uint8_t sample_capture_model_set_arm(sample_capture_arm_t arm)
{
    if(arm >= SAMPLE_CAPTURE_ARM_COUNT)
    {
        return 0U;
    }
    if(arm == SAMPLE_CAPTURE_ARM_OFF)
    {
        g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
        g_sample_capture.state.armed_pending = 0U;
        g_sample_capture.state.trigger_latched = 0U;
        if(g_sample_capture.state.recording != 0U)
        {
            sample_capture_set_capture_enabled(0U);
            (void)audio_recorder_request_stop_client(
                AUDIO_RECORDER_CLIENT_AUDIO_REC);
            g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_STOPPING;
        }
        else
        {
            (void)audio_recorder_cancel_prepared_client(
                AUDIO_RECORDER_CLIENT_AUDIO_REC);
            if(g_sample_capture.state.take_valid == 0U)
                g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_IDLE;
        }
    sample_capture_publish_rec_bus();
        return 1U;
    }

    if(sample_capture_looper_record_conflict() != 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_LOOPER_ACTIVE);
        return 0U;
    }
    if(sample_capture_has_route() == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_NO_ROUTE);
        return 0U;
    }

    if (audio_recorder_client_is_active(AUDIO_RECORDER_CLIENT_AUDIO_REC) != 0U)
        (void)audio_recorder_cancel_prepared_client(
            AUDIO_RECORDER_CLIENT_AUDIO_REC);
    if (sample_capture_prepare_session() == 0U)
        return 0U;

    g_sample_capture.state.arm = arm;
    g_sample_capture.state.armed_pending =
        ((arm == SAMPLE_CAPTURE_ARM_REC)
            && (seq_runtime_rec_is_armed() != 0U)) ? 1U : 0U;
    g_sample_capture.state.trigger_latched = 0U;
    if(arm == SAMPLE_CAPTURE_ARM_TRIG)
    {
        audio_rec_level_snapshot_t level;
        g_sample_capture.trigger_last_level_generation =
            (audio_rec_level_reader_read(&level) != 0U)
                ? level.generation : 0U;
    }
    g_sample_capture.state.error = SAMPLE_CAPTURE_ERROR_NONE;
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_ARMED;
    sample_capture_publish_rec_bus();
    return 1U;
}

uint8_t sample_capture_model_step_arm(int16_t delta)
{
    if(delta == 0)
    {
        return 0U;
    }
    int16_t next = (int16_t)g_sample_capture.state.arm + ((delta > 0) ? 1 : -1);
    if(next < (int16_t)SAMPLE_CAPTURE_ARM_OFF)
    {
        next = SAMPLE_CAPTURE_ARM_OFF;
    }
    if(next >= (int16_t)SAMPLE_CAPTURE_ARM_COUNT)
    {
        next = (int16_t)SAMPLE_CAPTURE_ARM_COUNT - 1;
    }
    if(next == (int16_t)g_sample_capture.state.arm)
    {
        return 0U;
    }
    return sample_capture_model_set_arm((sample_capture_arm_t)next);
}

uint8_t sample_capture_model_step_len(int16_t delta)
{
    if(delta == 0)
    {
        return 0U;
    }
    if((g_sample_capture.state.recording != 0U)
            || (g_sample_capture.state.arm != SAMPLE_CAPTURE_ARM_OFF))
    {
        return 0U;
    }

    int16_t next = (int16_t)g_sample_capture.state.len_bars + ((delta > 0) ? 1 : -1);
    if(next < 0)
    {
        next = 0;
    }
    if(next > (int16_t)SAMPLE_CAPTURE_LEN_FIXED_MAX)
    {
        next = SAMPLE_CAPTURE_LEN_FIXED_MAX;
    }
    g_sample_capture.state.len_bars = (uint8_t)next;
    return 1U;
}

uint8_t sample_capture_model_step_quant(int16_t delta)
{
    if(delta == 0)
    {
        return 0U;
    }
    if((g_sample_capture.state.recording != 0U)
            || (g_sample_capture.state.arm != SAMPLE_CAPTURE_ARM_OFF))
    {
        return 0U;
    }

    int16_t next = (int16_t)g_sample_capture.state.quant + ((delta > 0) ? 1 : -1);
    if(next < 0)
    {
        next = 0;
    }
    if(next >= (int16_t)SAMPLE_CAPTURE_QUANT_COUNT)
    {
        next = (int16_t)SAMPLE_CAPTURE_QUANT_COUNT - 1;
    }
    g_sample_capture.state.quant = (sample_capture_quant_t)next;
    return 1U;
}

uint8_t sample_capture_model_set_threshold_dbfs(int8_t threshold_dbfs)
{
    if((threshold_dbfs < SAMPLE_CAPTURE_THRESHOLD_DBFS_MIN)
            || (threshold_dbfs > SAMPLE_CAPTURE_THRESHOLD_DBFS_MAX))
    {
        return 0U;
    }
    g_sample_capture.state.threshold_dbfs = threshold_dbfs;
    g_sample_capture.trigger_threshold_peak_abs_pcm24 =
        sample_capture_threshold_to_peak_abs_pcm24(threshold_dbfs);
    return 1U;
}

uint8_t sample_capture_model_step_threshold(int16_t delta)
{
    if(delta == 0)
    {
        return 0U;
    }
    int16_t next = (int16_t)g_sample_capture.state.threshold_dbfs
        + ((delta > 0) ? 1 : -1);
    if(next < SAMPLE_CAPTURE_THRESHOLD_DBFS_MIN)
    {
        next = SAMPLE_CAPTURE_THRESHOLD_DBFS_MIN;
    }
    if(next > SAMPLE_CAPTURE_THRESHOLD_DBFS_MAX)
    {
        next = SAMPLE_CAPTURE_THRESHOLD_DBFS_MAX;
    }
    if(next == (int16_t)g_sample_capture.state.threshold_dbfs)
    {
        return 0U;
    }
    g_sample_capture.state.threshold_dbfs = (int8_t)next;
    g_sample_capture.trigger_threshold_peak_abs_pcm24 =
        sample_capture_threshold_to_peak_abs_pcm24((int8_t)next);
    return 1U;
}

uint8_t sample_capture_model_set_line_enabled(uint8_t enabled)
{
    g_sample_capture.state.line_enabled = (enabled != 0U) ? 1U : 0U;
    sample_capture_publish_rec_bus();
    return 1U;
}

uint8_t sample_capture_model_toggle_line(void)
{
    return sample_capture_model_set_line_enabled(
        (uint8_t)(g_sample_capture.state.line_enabled == 0U));
}

uint8_t sample_capture_model_set_mic_enabled(uint8_t enabled)
{
    g_sample_capture.state.mic_enabled = (enabled != 0U) ? 1U : 0U;
    sample_capture_publish_rec_bus();
    return 1U;
}

uint8_t sample_capture_model_toggle_mic(void)
{
    return sample_capture_model_set_mic_enabled(
        (uint8_t)(g_sample_capture.state.mic_enabled == 0U));
}

uint8_t sample_capture_model_set_usb_enabled(uint8_t enabled)
{
    g_sample_capture.state.usb_enabled = (enabled != 0U) ? 1U : 0U;
    sample_capture_publish_rec_bus();
    return 1U;
}

uint8_t sample_capture_model_toggle_usb(void)
{
    return sample_capture_model_set_usb_enabled(
        (uint8_t)(g_sample_capture.state.usb_enabled == 0U));
}

uint8_t sample_capture_model_step_edit(uint8_t encoder, int16_t delta, uint8_t alt_held)
{
    if((delta == 0) || (g_sample_capture.state.take_valid == 0U))
    {
        return 0U;
    }

    const uint32_t coarse = (delta > 0) ? (uint32_t)delta : (uint32_t)(-delta);
    uint32_t marker_step = sample_capture_edit_marker_step();
    if((alt_held != 0U) && (encoder == 1U))
    {
        marker_step /= 8U;
        if(marker_step == 0U)
        {
            marker_step = 1U;
        }
    }
    const uint32_t step = coarse * marker_step;
    uint8_t changed = 0U;

    if((alt_held != 0U) && (encoder == 0U))
    {
        int16_t next = (int16_t)g_sample_capture.state.edit_vzoom + ((delta > 0) ? 1 : -1);
        if(next < 0)
        {
            next = 0;
        }
        if(next > (int16_t)SAMPLE_CAPTURE_EDIT_VZOOM_MAX)
        {
            next = (int16_t)SAMPLE_CAPTURE_EDIT_VZOOM_MAX;
        }
        g_sample_capture.state.edit_vzoom = (uint8_t)next;
        changed = 1U;
    }
    else if((alt_held != 0U) && (encoder == 2U))
    {
        const uint32_t current = g_sample_capture.state.edit_loop_start_frame;
        uint32_t next = current;
        if(delta > 0)
        {
            next = (step > (0xFFFFFFFFUL - next)) ? 0xFFFFFFFFUL : (next + step);
        }
        else
        {
            next = (step < next) ? (next - step) : 0U;
        }
        next = sample_capture_clamp_u32(next,
                                        g_sample_capture.state.edit_start_frame,
                                        g_sample_capture.state.edit_loop_end_frame - 1U);
        g_sample_capture.state.edit_loop_start_frame =
            sample_capture_snap_to_zcross(current,
                                          next,
                                          delta,
                                          g_sample_capture.state.edit_start_frame,
                                          g_sample_capture.state.edit_loop_end_frame - 1U);
        changed = 1U;
    }
    else if((alt_held != 0U) && (encoder == 3U))
    {
        const uint32_t current = g_sample_capture.state.edit_loop_end_frame;
        uint32_t next = current;
        if(delta > 0)
        {
            next = (step > (0xFFFFFFFFUL - next)) ? 0xFFFFFFFFUL : (next + step);
        }
        else
        {
            next = (step < next) ? (next - step) : 0U;
        }
        next = sample_capture_clamp_u32(next,
                                        g_sample_capture.state.edit_loop_start_frame + 1U,
                                        g_sample_capture.state.edit_end_frame);
        g_sample_capture.state.edit_loop_end_frame =
            sample_capture_snap_to_zcross(current,
                                          next,
                                          delta,
                                          g_sample_capture.state.edit_loop_start_frame + 1U,
                                          g_sample_capture.state.edit_end_frame);
        changed = 1U;
    }
    else if(encoder == 2U)
    {
        const uint32_t current = g_sample_capture.state.edit_start_frame;
        if(delta > 0)
        {
            if(step > (0xFFFFFFFFUL - g_sample_capture.state.edit_start_frame))
            {
                g_sample_capture.state.edit_start_frame = 0xFFFFFFFFUL;
            }
            else
            {
                g_sample_capture.state.edit_start_frame += step;
            }
            if(g_sample_capture.state.edit_start_frame >= g_sample_capture.state.edit_end_frame)
            {
                g_sample_capture.state.edit_start_frame = g_sample_capture.state.edit_end_frame - 1U;
            }
        }
        else if(step < g_sample_capture.state.edit_start_frame)
        {
            g_sample_capture.state.edit_start_frame -= step;
        }
        else
        {
            g_sample_capture.state.edit_start_frame = 0U;
        }
        g_sample_capture.state.edit_start_frame =
            sample_capture_snap_to_zcross(current,
                                          g_sample_capture.state.edit_start_frame,
                                          delta,
                                          0U,
                                          g_sample_capture.state.edit_end_frame - 1U);
        changed = 1U;
    }
    else if(encoder == 3U)
    {
        const uint32_t current = g_sample_capture.state.edit_end_frame;
        if(delta > 0)
        {
            if(step > (0xFFFFFFFFUL - g_sample_capture.state.edit_end_frame))
            {
                g_sample_capture.state.edit_end_frame = 0xFFFFFFFFUL;
            }
            else
            {
                g_sample_capture.state.edit_end_frame += step;
            }
            if(g_sample_capture.state.edit_end_frame > g_sample_capture.state.recorded_frames)
            {
                g_sample_capture.state.edit_end_frame = g_sample_capture.state.recorded_frames;
            }
        }
        else if((g_sample_capture.state.edit_end_frame > g_sample_capture.state.edit_start_frame + 1U)
                && (step < (g_sample_capture.state.edit_end_frame - g_sample_capture.state.edit_start_frame)))
        {
            g_sample_capture.state.edit_end_frame -= step;
        }
        else
        {
            g_sample_capture.state.edit_end_frame = g_sample_capture.state.edit_start_frame + 1U;
        }
        g_sample_capture.state.edit_end_frame =
            sample_capture_snap_to_zcross(current,
                                          g_sample_capture.state.edit_end_frame,
                                          delta,
                                          g_sample_capture.state.edit_start_frame + 1U,
                                          g_sample_capture.state.recorded_frames);
        changed = 1U;
    }
    else if(encoder == 0U)
    {
        uint8_t next_zoom = g_sample_capture.state.edit_zoom;
        uint32_t zoom_step = coarse * 2U;
        if(coarse >= 4U)
        {
            zoom_step += coarse;
        }
        if(zoom_step == 0U)
        {
            zoom_step = 1U;
        }
        if(delta > 0)
        {
            if(zoom_step >= ((uint32_t)SAMPLE_CAPTURE_EDIT_ZOOM_MAX - (uint32_t)next_zoom))
            {
                next_zoom = SAMPLE_CAPTURE_EDIT_ZOOM_MAX;
            }
            else
            {
                next_zoom = (uint8_t)((uint32_t)next_zoom + zoom_step);
            }
        }
        else if(zoom_step >= next_zoom)
        {
            next_zoom = 0U;
        }
        else
        {
            next_zoom = (uint8_t)((uint32_t)next_zoom - zoom_step);
        }
        sample_capture_set_zoom_preserve_center(next_zoom);
        changed = 1U;
    }
    else if(encoder == 1U)
    {
        uint32_t scroll_unit = sample_capture_edit_scroll_step();
        if(alt_held != 0U)
        {
            scroll_unit /= 8U;
            if(scroll_unit == 0U)
            {
                scroll_unit = 1U;
            }
        }
        const uint32_t scroll_step = coarse * scroll_unit;
        if(delta > 0)
        {
            if(scroll_step > (0xFFFFFFFFUL - g_sample_capture.state.edit_scroll_frame))
            {
                g_sample_capture.state.edit_scroll_frame = 0xFFFFFFFFUL;
            }
            else
            {
                g_sample_capture.state.edit_scroll_frame += scroll_step;
            }
        }
        else if(scroll_step < g_sample_capture.state.edit_scroll_frame)
        {
            g_sample_capture.state.edit_scroll_frame -= scroll_step;
        }
        else
        {
            g_sample_capture.state.edit_scroll_frame = 0U;
        }
        changed = 1U;
    }

    if(changed != 0U)
    {
        sample_capture_clamp_edit_markers();
        if(g_sample_capture.state.final_path[0] != '\0')
        {
            g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_REC_EDIT;
            g_sample_capture.state.final_path[0] = '\0';
        }
        else if(g_sample_capture.state.phase == SAMPLE_CAPTURE_PHASE_ERROR)
        {
            g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_REC_EDIT;
            g_sample_capture.state.error = SAMPLE_CAPTURE_ERROR_NONE;
        }
    }
    return 1U;
}

void sample_capture_model_request_detail_waveform(uint32_t start_frame,
                                                  uint32_t frame_count,
                                                  uint16_t columns)
{
    if(SAMPLE_CAPTURE_DETAIL_VISIBLE_POINTS > SAMPLE_CAPTURE_DETAIL_POINTS)
    {
        return;
    }
    if((g_sample_capture.state.take_valid == 0U)
            || (frame_count == 0U)
            || (start_frame >= g_sample_capture.state.recorded_frames)
            || ((g_sample_capture.state.recorded_frames - start_frame) < frame_count))
    {
        return;
    }

    uint32_t margin = frame_count * SAMPLE_CAPTURE_DETAIL_CACHE_MARGIN_MULT;
    if(margin == 0U)
    {
        margin = 1U;
    }
    uint32_t cache_start = (start_frame > margin) ? (start_frame - margin) : 0U;
    uint32_t cache_end = start_frame + frame_count;
    if((0xFFFFFFFFUL - cache_end) > margin)
    {
        cache_end += margin;
    }
    if(cache_end > g_sample_capture.state.recorded_frames)
    {
        cache_end = g_sample_capture.state.recorded_frames;
    }
    if(cache_end <= cache_start)
    {
        return;
    }

    uint32_t request_columns = (uint32_t)columns;
    if(request_columns == 0U)
    {
        request_columns = SAMPLE_CAPTURE_DETAIL_VISIBLE_POINTS;
    }
    request_columns *= (1U + (2U * SAMPLE_CAPTURE_DETAIL_CACHE_MARGIN_MULT));
    if(request_columns < SAMPLE_CAPTURE_DETAIL_VISIBLE_POINTS)
    {
        request_columns = SAMPLE_CAPTURE_DETAIL_VISIBLE_POINTS;
    }
    if(request_columns > SAMPLE_CAPTURE_DETAIL_POINTS)
    {
        request_columns = SAMPLE_CAPTURE_DETAIL_POINTS;
    }

    if((g_sample_capture.state.detail_valid != 0U)
            && (g_sample_capture.state.detail_start_frame <= start_frame)
            && ((g_sample_capture.state.detail_start_frame + g_sample_capture.state.detail_frames)
                >= (start_frame + frame_count))
            && (g_sample_capture.state.detail_count >= request_columns))
    {
        return;
    }
    if((g_sample_capture.detail_requested != 0U)
            && (g_sample_capture.detail_request_start_frame <= start_frame)
            && ((g_sample_capture.detail_request_start_frame + g_sample_capture.detail_request_frames)
                >= (start_frame + frame_count))
            && (g_sample_capture.detail_request_columns >= request_columns))
    {
        return;
    }

    g_sample_capture.detail_requested = 1U;
    g_sample_capture.detail_building = 0U;
    g_sample_capture.detail_request_start_frame = cache_start;
    g_sample_capture.detail_request_frames = cache_end - cache_start;
    g_sample_capture.detail_request_columns = (uint16_t)request_columns;
    g_sample_capture.detail_build_next_frame = 0U;
}

uint8_t sample_capture_model_global_overview_ready(void)
{
    return (uint8_t)((g_sample_capture_global_overview.state == SAMPLE_CAPTURE_GLOBAL_OVERVIEW_READY)
            && (sample_capture_global_overview_path_matches() != 0U)
            && (g_sample_capture_global_overview.frame_count == g_sample_capture.state.recorded_frames)
            && (g_sample_capture_global_overview.point_count != 0U));
}

uint16_t sample_capture_model_global_overview_peak(void)
{
    return sample_capture_model_global_overview_ready()
        ? g_sample_capture_global_overview.peak
        : 0U;
}

uint8_t sample_capture_model_global_overview_minmax(uint32_t start_frame,
                                                    uint32_t frame_count,
                                                    int16_t *out_min,
                                                    int16_t *out_max)
{
    if(out_min != 0)
    {
        *out_min = 0;
    }
    if(out_max != 0)
    {
        *out_max = 0;
    }
    if((sample_capture_model_global_overview_ready() == 0U)
            || (frame_count == 0U)
            || (start_frame >= g_sample_capture_global_overview.frame_count))
    {
        return 0U;
    }

    uint32_t end_frame = start_frame + frame_count;
    if((end_frame < start_frame) || (end_frame > g_sample_capture_global_overview.frame_count))
    {
        end_frame = g_sample_capture_global_overview.frame_count;
    }
    if(end_frame <= start_frame)
    {
        return 0U;
    }

    uint32_t idx0 = (uint32_t)(((uint64_t)start_frame
            * (uint64_t)g_sample_capture_global_overview.point_count)
        / (uint64_t)g_sample_capture_global_overview.frame_count);
    uint32_t idx1 = (uint32_t)(((uint64_t)end_frame
            * (uint64_t)g_sample_capture_global_overview.point_count
            + (uint64_t)g_sample_capture_global_overview.frame_count - 1ULL)
        / (uint64_t)g_sample_capture_global_overview.frame_count);
    if(idx0 >= g_sample_capture_global_overview.point_count)
    {
        idx0 = g_sample_capture_global_overview.point_count - 1U;
    }
    if(idx1 <= idx0)
    {
        idx1 = idx0 + 1U;
    }
    if(idx1 > g_sample_capture_global_overview.point_count)
    {
        idx1 = g_sample_capture_global_overview.point_count;
    }

    int16_t min_v = g_sample_capture_global_overview_points[idx0].min;
    int16_t max_v = g_sample_capture_global_overview_points[idx0].max;
    for(uint32_t idx = idx0 + 1U; idx < idx1; ++idx)
    {
        if(g_sample_capture_global_overview_points[idx].min < min_v)
        {
            min_v = g_sample_capture_global_overview_points[idx].min;
        }
        if(g_sample_capture_global_overview_points[idx].max > max_v)
        {
            max_v = g_sample_capture_global_overview_points[idx].max;
        }
    }
    if(out_min != 0)
    {
        *out_min = min_v;
    }
    if(out_max != 0)
    {
        *out_max = max_v;
    }
    return 1U;
}

uint8_t sample_capture_model_waveform_cache_ready(void)
{
    return g_sample_capture.wave_cache_ready;
}

uint8_t sample_capture_model_waveform_cache_get_handle(waveform_cache_handle_t *out_handle)
{
    if((out_handle == 0) || (g_sample_capture.wave_cache_ready == 0U))
    {
        return 0U;
    }
    *out_handle = g_sample_capture.wave_cache_handle;
    return 1U;
}

void sample_capture_model_note_rec_edit_first_render(void)
{
    if(g_sample_capture.rec_edit_first_render_pending == 0U)
    {
        return;
    }
    g_sample_capture.rec_edit_first_render_pending = 0U;

}

void sample_capture_model_request_line_waveform(uint32_t start_frame,
                                                uint32_t frame_count,
                                                uint16_t columns)
{
    if((g_sample_capture.state.take_valid == 0U)
            || (frame_count == 0U)
            || (start_frame >= g_sample_capture.state.recorded_frames)
            || ((g_sample_capture.state.recorded_frames - start_frame) < frame_count))
    {
        return;
    }

    uint16_t points = columns;
    if(points == 0U)
    {
        points = SAMPLE_CAPTURE_DETAIL_VISIBLE_POINTS;
    }
    if(points > SAMPLE_CAPTURE_LINE_POINTS)
    {
        points = SAMPLE_CAPTURE_LINE_POINTS;
    }

    sample_capture_global_overview_request();
    if(sample_capture_model_view_uses_tile_cache(frame_count) == 0U)
    {
        return;
    }

    if(sample_capture_editor_cache_covers(start_frame, frame_count) != 0U)
    {
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
        g_sample_capture_debug.cache_hit_count++;
        const uint32_t hit_now = HAL_GetTick();
        if((g_sample_capture_debug.last_miss_start != start_frame)
                || (g_sample_capture_debug.last_miss_frames != frame_count)
                || ((hit_now - g_sample_capture_debug.last_miss_ms) >= 500U))
        {
            sample_capture_debug_log("OLD_AUDIO_TILE_HIT vs=%lu vf=%lu\r\n",
                                     (unsigned long)start_frame,
                                     (unsigned long)frame_count);
            g_sample_capture_debug.last_miss_start = start_frame;
            g_sample_capture_debug.last_miss_frames = frame_count;
            g_sample_capture_debug.last_miss_ms = hit_now;
        }
#endif
        if((g_sample_capture_line_hot.valid != 0U)
                && (g_sample_capture_line_hot.start_frame == start_frame)
                && (g_sample_capture_line_hot.frames == frame_count)
                && (g_sample_capture_line_hot.count == points))
        {
            return;
        }
        sample_capture_line_publish_from_editor_cache(start_frame,
                                                      frame_count,
                                                      points);
        return;
    }

#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    g_sample_capture_debug.cache_miss_count++;
    const uint32_t miss_now = HAL_GetTick();
    if((g_sample_capture_debug.last_miss_valid == 0U)
            || (g_sample_capture_debug.last_miss_start != start_frame)
            || (g_sample_capture_debug.last_miss_frames != frame_count)
            || ((miss_now - g_sample_capture_debug.last_miss_ms) >= 500U))
    {
        sample_capture_debug_log("OLD_AUDIO_TILE_MISS vs=%lu vf=%lu\r\n",
                                 (unsigned long)start_frame,
                                 (unsigned long)frame_count);
        g_sample_capture_debug.last_miss_start = start_frame;
        g_sample_capture_debug.last_miss_frames = frame_count;
        g_sample_capture_debug.last_miss_ms = miss_now;
        g_sample_capture_debug.last_miss_valid = 1U;
    }
#endif
    sample_capture_editor_cache_request(start_frame, frame_count);

    if((g_sample_capture_line_hot.valid != 0U)
            && (g_sample_capture_line_hot.start_frame == start_frame)
            && (g_sample_capture_line_hot.frames == frame_count)
            && (g_sample_capture_line_hot.count == points))
    {
        return;
    }
}

#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
static uint8_t sample_capture_editor_count_tiles(sample_capture_editor_cache_state_t state)
{
    uint8_t count = 0U;
    for(uint8_t i = 0U; i < SAMPLE_CAPTURE_EDITOR_TILE_COUNT; ++i)
    {
        if(g_sample_capture_editor_cache.tiles[i].state == state)
        {
            count++;
        }
    }
    return count;
}
#endif

void sample_capture_model_debug_note_renderer(sample_capture_renderer_debug_t renderer,
                                              uint8_t zoom,
                                              uint32_t view_start_frame,
                                              uint32_t view_frames,
                                              uint16_t inner_w,
                                              uint32_t samples_per_pixel,
                                              uint32_t wavecache_frames_per_column,
                                              uint8_t line_valid,
                                              uint16_t line_points,
                                              uint16_t draw_line_segments,
                                              uint8_t fallback_reason)
{
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    const uint8_t cache_valid =
        sample_capture_editor_count_tiles(SAMPLE_CAPTURE_EDITOR_CACHE_READY);
    const uint8_t cache_loading =
        sample_capture_editor_count_tiles(SAMPLE_CAPTURE_EDITOR_CACHE_LOADING);
    const uint8_t covered = sample_capture_editor_cache_covers(view_start_frame, view_frames);
    const uint32_t now = HAL_GetTick();

    g_sample_capture_debug.draw_count++;
    if((uint8_t)renderer < 8U)
    {
        g_sample_capture_debug.renderer_count[(uint8_t)renderer]++;
    }
    g_sample_capture_debug.last_draw_segments = draw_line_segments;
    if(draw_line_segments > g_sample_capture_debug.max_draw_segments)
    {
        g_sample_capture_debug.max_draw_segments = draw_line_segments;
    }

    const uint8_t renderer_changed =
        ((g_sample_capture_debug.last_renderer_valid == 0U)
         || (g_sample_capture_debug.last_renderer != renderer)) ? 1U : 0U;
    const uint8_t view_changed =
        ((g_sample_capture_debug.last_source_change_valid == 0U)
         || (g_sample_capture_debug.last_zoom != zoom)
         || (g_sample_capture_debug.last_view_start_frame != view_start_frame)
         || (g_sample_capture_debug.last_view_frames != view_frames)
         || (g_sample_capture_debug.last_samples_per_pixel != samples_per_pixel)
         || (g_sample_capture_debug.last_wavecache_frames_per_column
             != wavecache_frames_per_column)
         || (g_sample_capture_debug.last_fallback_reason != fallback_reason)) ? 1U : 0U;
    const uint8_t summary_due =
        ((now - g_sample_capture_debug.last_summary_ms) >= 1000U) ? 1U : 0U;

    if((renderer_changed == 0U) && (view_changed == 0U) && (summary_due == 0U))
    {
        return;
    }

    const uint32_t sec_draws = g_sample_capture_debug.draw_count
        - g_sample_capture_debug.last_summary_draw_count;
    const uint32_t sec_elines = g_sample_capture_debug.eline_count
        - g_sample_capture_debug.last_summary_eline_count;
    const uint32_t sec_flushes = g_sample_capture_debug.flush_count
        - g_sample_capture_debug.last_summary_flush_count;

    if(wavecache_frames_per_column != 0U)
    {
        sample_capture_debug_log("RECEDIT R=%s z=%u vs=%lu vf=%lu w=%u spp=%lu wc=%lu fb=%u line=%u lp=%u old_ready=%u old_load=%u old_cov=%u seg=%u hit=%lu miss=%lu d/e/f=%lu/%lu/%lu\r\n",
                                 sample_capture_debug_renderer_label(renderer),
                                 (unsigned)zoom,
                                 (unsigned long)view_start_frame,
                                 (unsigned long)view_frames,
                                 (unsigned)inner_w,
                                 (unsigned long)samples_per_pixel,
                                 (unsigned long)wavecache_frames_per_column,
                                 (unsigned)fallback_reason,
                                 (unsigned)line_valid,
                                 (unsigned)line_points,
                                 (unsigned)cache_valid,
                                 (unsigned)cache_loading,
                                 (unsigned)covered,
                                 (unsigned)draw_line_segments,
                                 (unsigned long)g_sample_capture_debug.cache_hit_count,
                                 (unsigned long)g_sample_capture_debug.cache_miss_count,
                                 (unsigned long)sec_draws,
                                 (unsigned long)sec_elines,
                                 (unsigned long)sec_flushes);
    }
    else
    {
        sample_capture_debug_log("RECEDIT R=%s z=%u vs=%lu vf=%lu w=%u spp=%lu wc=NONE fb=%u line=%u lp=%u old_ready=%u old_load=%u old_cov=%u seg=%u hit=%lu miss=%lu d/e/f=%lu/%lu/%lu\r\n",
                                 sample_capture_debug_renderer_label(renderer),
                                 (unsigned)zoom,
                                 (unsigned long)view_start_frame,
                                 (unsigned long)view_frames,
                                 (unsigned)inner_w,
                                 (unsigned long)samples_per_pixel,
                                 (unsigned)fallback_reason,
                                 (unsigned)line_valid,
                                 (unsigned)line_points,
                                 (unsigned)cache_valid,
                                 (unsigned)cache_loading,
                                 (unsigned)covered,
                                 (unsigned)draw_line_segments,
                                 (unsigned long)g_sample_capture_debug.cache_hit_count,
                                 (unsigned long)g_sample_capture_debug.cache_miss_count,
                                 (unsigned long)sec_draws,
                                 (unsigned long)sec_elines,
                                 (unsigned long)sec_flushes);
    }

    g_sample_capture_debug.last_renderer = renderer;
    g_sample_capture_debug.last_renderer_valid = 1U;
    g_sample_capture_debug.last_zoom = zoom;
    g_sample_capture_debug.last_view_start_frame = view_start_frame;
    g_sample_capture_debug.last_view_frames = view_frames;
    g_sample_capture_debug.last_samples_per_pixel = samples_per_pixel;
    g_sample_capture_debug.last_wavecache_frames_per_column = wavecache_frames_per_column;
    g_sample_capture_debug.last_fallback_reason = fallback_reason;
    g_sample_capture_debug.last_source_change_valid = 1U;
    if(summary_due != 0U)
    {
        g_sample_capture_debug.last_summary_ms = now;
        g_sample_capture_debug.last_summary_draw_count = g_sample_capture_debug.draw_count;
        g_sample_capture_debug.last_summary_eline_count = g_sample_capture_debug.eline_count;
        g_sample_capture_debug.last_summary_flush_count = g_sample_capture_debug.flush_count;
    }
#else
    (void)renderer;
    (void)zoom;
    (void)view_start_frame;
    (void)view_frames;
    (void)inner_w;
    (void)samples_per_pixel;
    (void)wavecache_frames_per_column;
    (void)line_valid;
    (void)line_points;
    (void)draw_line_segments;
    (void)fallback_reason;
#endif
}

void sample_capture_model_debug_note_draw_cost(uint32_t page_ms, uint32_t waveform_ms)
{
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    g_sample_capture_debug.draw_last_ms = page_ms;
    if(page_ms > g_sample_capture_debug.draw_max_ms)
    {
        g_sample_capture_debug.draw_max_ms = page_ms;
    }
    g_sample_capture_debug.waveform_last_ms = waveform_ms;
    if(waveform_ms > g_sample_capture_debug.waveform_max_ms)
    {
        g_sample_capture_debug.waveform_max_ms = waveform_ms;
    }
#else
    (void)page_ms;
    (void)waveform_ms;
#endif
}

void sample_capture_model_debug_note_flush_cost(uint32_t flush_ms, uint8_t continued_flush)
{
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    g_sample_capture_debug.flush_count++;
    if(continued_flush != 0U)
    {
        g_sample_capture_debug.flush_cont_count++;
    }
    g_sample_capture_debug.flush_last_ms = flush_ms;
    if(flush_ms > g_sample_capture_debug.flush_max_ms)
    {
        g_sample_capture_debug.flush_max_ms = flush_ms;
    }
#else
    (void)flush_ms;
    (void)continued_flush;
#endif
}

