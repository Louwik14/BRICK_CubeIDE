static uint8_t sample_capture_looper_record_conflict(void)
{
    return audio_recorder_client_is_active(AUDIO_RECORDER_CLIENT_LOOPER);
}

static uint32_t sample_capture_edit_marker_step(void)
{
    const uint8_t zoom = (g_sample_capture.state.edit_zoom > SAMPLE_CAPTURE_EDIT_ZOOM_MAX)
        ? SAMPLE_CAPTURE_EDIT_ZOOM_MAX
        : g_sample_capture.state.edit_zoom;
    if(g_sample_capture.state.recorded_frames == 0U)
    {
        return 1U;
    }

    uint32_t visible_frames =
        sample_capture_model_visible_frames_for_zoom(g_sample_capture.state.recorded_frames, zoom);

    uint32_t step = visible_frames / 96U;
    if(step == 0U)
    {
        step = 1U;
    }
    return step;
}

static uint32_t sample_capture_edit_scroll_step(void)
{
    if(g_sample_capture.state.recorded_frames == 0U)
    {
        return 1U;
    }

    uint32_t visible_frames =
        sample_capture_model_visible_frames_for_zoom(g_sample_capture.state.recorded_frames,
                                                     g_sample_capture.state.edit_zoom);

    uint32_t step = visible_frames / 8U;
    if(step == 0U)
    {
        step = 1U;
    }
    return step;
}

static uint32_t sample_capture_clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    if(v < lo)
    {
        return lo;
    }
    if(v > hi)
    {
        return hi;
    }
    return v;
}

static void sample_capture_clamp_edit_markers(void)
{
    if(g_sample_capture.state.recorded_frames == 0U)
    {
        g_sample_capture.state.edit_start_frame = 0U;
        g_sample_capture.state.edit_end_frame = 0U;
        g_sample_capture.state.edit_loop_start_frame = 0U;
        g_sample_capture.state.edit_loop_end_frame = 0U;
        g_sample_capture.state.edit_scroll_frame = 0U;
        return;
    }

    if(g_sample_capture.state.edit_end_frame == 0U)
    {
        g_sample_capture.state.edit_end_frame = 1U;
    }
    if(g_sample_capture.state.edit_end_frame > g_sample_capture.state.recorded_frames)
    {
        g_sample_capture.state.edit_end_frame = g_sample_capture.state.recorded_frames;
    }
    if(g_sample_capture.state.edit_start_frame >= g_sample_capture.state.edit_end_frame)
    {
        g_sample_capture.state.edit_start_frame = g_sample_capture.state.edit_end_frame - 1U;
    }
    if(g_sample_capture.state.edit_loop_end_frame == 0U)
    {
        g_sample_capture.state.edit_loop_end_frame = g_sample_capture.state.edit_end_frame;
    }
    g_sample_capture.state.edit_loop_start_frame =
        sample_capture_clamp_u32(g_sample_capture.state.edit_loop_start_frame,
                                 g_sample_capture.state.edit_start_frame,
                                 g_sample_capture.state.edit_end_frame - 1U);
    g_sample_capture.state.edit_loop_end_frame =
        sample_capture_clamp_u32(g_sample_capture.state.edit_loop_end_frame,
                                 g_sample_capture.state.edit_loop_start_frame + 1U,
                                 g_sample_capture.state.edit_end_frame);

    uint32_t visible_frames =
        sample_capture_model_visible_frames_for_zoom(g_sample_capture.state.recorded_frames,
                                                     g_sample_capture.state.edit_zoom);
    if(visible_frames >= g_sample_capture.state.recorded_frames)
    {
        g_sample_capture.state.edit_scroll_frame = 0U;
    }
    else if(g_sample_capture.state.edit_scroll_frame > (g_sample_capture.state.recorded_frames - visible_frames))
    {
        g_sample_capture.state.edit_scroll_frame = g_sample_capture.state.recorded_frames - visible_frames;
    }
}

static uint32_t sample_capture_clamp_scroll_for_visible(uint32_t start_frame,
                                                        uint32_t visible_frames)
{
    if((g_sample_capture.state.recorded_frames == 0U)
            || (visible_frames >= g_sample_capture.state.recorded_frames))
    {
        return 0U;
    }
    const uint32_t max_start = g_sample_capture.state.recorded_frames - visible_frames;
    return (start_frame > max_start) ? max_start : start_frame;
}

static void sample_capture_set_zoom_preserve_center(uint8_t next_zoom)
{
    const uint32_t old_visible =
        sample_capture_model_visible_frames_for_zoom(g_sample_capture.state.recorded_frames,
                                                     g_sample_capture.state.edit_zoom);
    const uint32_t old_start =
        sample_capture_clamp_scroll_for_visible(g_sample_capture.state.edit_scroll_frame,
                                                old_visible);
    uint64_t center = (uint64_t)old_start + ((uint64_t)old_visible / 2ULL);
    if(center > g_sample_capture.state.recorded_frames)
    {
        center = g_sample_capture.state.recorded_frames;
    }

    g_sample_capture.state.edit_zoom = next_zoom;

    const uint32_t new_visible =
        sample_capture_model_visible_frames_for_zoom(g_sample_capture.state.recorded_frames,
                                                     g_sample_capture.state.edit_zoom);
    uint32_t new_start = 0U;
    if(new_visible < g_sample_capture.state.recorded_frames)
    {
        if(center > ((uint64_t)new_visible / 2ULL))
        {
            const uint64_t centered = center - ((uint64_t)new_visible / 2ULL);
            new_start = (centered > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (uint32_t)centered;
        }
        new_start = sample_capture_clamp_scroll_for_visible(new_start, new_visible);
    }
    g_sample_capture.state.edit_scroll_frame = new_start;
}

static uint8_t sample_capture_sign_crosses(int16_t a, int16_t b)
{
    return (uint8_t)(((a <= 0) && (b >= 0)) || ((a >= 0) && (b <= 0)));
}

static uint32_t sample_capture_add_saturate_u32(uint32_t a, uint32_t b)
{
    return (b > (0xFFFFFFFFUL - a)) ? 0xFFFFFFFFUL : (a + b);
}

static uint32_t sample_capture_snap_to_zcross(uint32_t current,
                                              uint32_t candidate,
                                              int16_t delta,
                                              uint32_t min_frame,
                                              uint32_t max_frame)
{
    if(g_sample_capture.state.edit_zcross_enabled == 0U)
    {
        return candidate;
    }

    if(min_frame > max_frame)
    {
        return candidate;
    }

    current = sample_capture_clamp_u32(current, min_frame, max_frame);
    candidate = sample_capture_clamp_u32(candidate, min_frame, max_frame);
    const uint8_t right = (delta > 0) ? 1U : 0U;
    const uint32_t candidate_dist =
        (candidate > current) ? (candidate - current) : (current - candidate);
    uint32_t search_span = SAMPLE_CAPTURE_ZCROSS_SEARCH_FRAMES;
    if(candidate_dist > search_span)
    {
        search_span = candidate_dist;
    }

    uint32_t search_start = min_frame;
    uint32_t search_end = max_frame;
    if(right != 0U)
    {
        search_start = sample_capture_add_saturate_u32(current,
                                                       SAMPLE_CAPTURE_ZCROSS_SAME_GUARD_FRAMES);
        search_end = sample_capture_add_saturate_u32(current, search_span);
    }
    else
    {
        search_start = (current > search_span) ? (current - search_span) : 0U;
        search_end = (current > SAMPLE_CAPTURE_ZCROSS_SAME_GUARD_FRAMES)
            ? (current - SAMPLE_CAPTURE_ZCROSS_SAME_GUARD_FRAMES)
            : 0U;
    }
    search_start = sample_capture_clamp_u32(search_start, min_frame, max_frame);
    search_end = sample_capture_clamp_u32(search_end, min_frame, max_frame);
    if(search_end < search_start)
    {
        return candidate;
    }

    uint32_t cover_start = search_start;
    if(cover_start > min_frame)
    {
        cover_start--;
    }
    if(sample_capture_editor_cache_covers(cover_start, (search_end - cover_start) + 1U) != 0U)
    {
        if(right != 0U)
        {
            int16_t prev = sample_capture_editor_sample_at(cover_start);
            for(uint32_t frame = cover_start + 1U; frame <= search_end; ++frame)
            {
                const int16_t cur = sample_capture_editor_sample_at(frame);
                if((frame >= search_start) && (sample_capture_sign_crosses(prev, cur) != 0U))
                {
                    return sample_capture_clamp_u32(frame, min_frame, max_frame);
                }
                prev = cur;
                if(frame == 0xFFFFFFFFUL)
                {
                    break;
                }
            }
        }
        else
        {
            for(uint32_t frame = search_end; frame > cover_start; --frame)
            {
                if(frame < search_start)
                {
                    continue;
                }
                const int16_t prev = sample_capture_editor_sample_at(frame - 1U);
                const int16_t cur = sample_capture_editor_sample_at(frame);
                if(sample_capture_sign_crosses(prev, cur) != 0U)
                {
                    return sample_capture_clamp_u32(frame, min_frame, max_frame);
                }
            }
        }
    }

    if((g_sample_capture_line_hot.valid != 0U)
            && (g_sample_capture_line_hot.count > 1U)
            && (g_sample_capture_line_hot.frames != 0U)
            && (search_start >= g_sample_capture_line_hot.start_frame)
            && (search_end <= (g_sample_capture_line_hot.start_frame + g_sample_capture_line_hot.frames)))
    {
        uint32_t best_frame = candidate;
        uint32_t best_dist = 0xFFFFFFFFUL;
        uint16_t best_abs = 0xFFFFU;
        for(uint16_t point = 0U; point < g_sample_capture_line_hot.count; ++point)
        {
            const uint32_t frame = g_sample_capture_line_hot.start_frame
                + (uint32_t)(((uint64_t)point * (uint64_t)g_sample_capture_line_hot.frames)
                    / (uint64_t)(g_sample_capture_line_hot.count - 1U));
            if((frame < search_start) || (frame > search_end))
            {
                continue;
            }
            const uint16_t abs_v = sample_capture_abs_i16(g_sample_capture_line_hot.points[point]);
            const uint32_t dist = (frame > current) ? (frame - current) : (current - frame);
            if((abs_v < best_abs) || ((abs_v == best_abs) && (dist < best_dist)))
            {
                best_frame = frame;
                best_dist = dist;
                best_abs = abs_v;
            }
        }
        if(best_dist != 0xFFFFFFFFUL)
        {
            return sample_capture_clamp_u32(best_frame, min_frame, max_frame);
        }
    }

    return candidate;
}

static uint8_t sample_capture_prepare_paths(void)
{
    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_RECORDER) == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_BUSY);
        return 0U;
    }

    uint8_t ok = 0U;
    if(sd_access_fs_mount_if_needed() != 0U)
    {
        (void)f_mkdir("0:/PROJECT");
        (void)f_mkdir(SAMPLE_CAPTURE_TEMP_REC_DIR);
        (void)f_mkdir(SAMPLE_CAPTURE_FINAL_DIR);
        sample_capture_copy_path(g_sample_capture.state.temp_path, SAMPLE_CAPTURE_TEMP_PATH);
        ok = 1U;
    }
    else
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
    return ok;
}

static uint8_t sample_capture_make_next_final_path(char *out_path, uint32_t out_len)
{
    if((out_path == 0) || (out_len == 0U))
    {
        return 0U;
    }

    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_RECORDER) == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_BUSY);
        return 0U;
    }

    uint8_t ok = 0U;
    if(sd_access_fs_mount_if_needed() != 0U)
    {
        (void)f_mkdir(SAMPLE_CAPTURE_FINAL_DIR);
        for(uint32_t attempt = 0U; attempt < SAMPLE_CAPTURE_FINAL_TRIES; ++attempt)
        {
            const uint16_t n = g_sample_capture.final_counter++;
            (void)snprintf(out_path, out_len, SAMPLE_CAPTURE_FINAL_DIR "/REC%04u.WAV", (unsigned)n);
            FILINFO info;
            if(f_stat(out_path, &info) == FR_NO_FILE)
            {
                ok = 1U;
                break;
            }
        }
    }
    else
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
    return ok;
}

static uint8_t sample_capture_prepare_session(void)
{
    if(sd_preview_is_active() != 0U)
    {
        sd_preview_stop();
    }

    if(sample_capture_has_route() == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_NO_ROUTE);
        return 0U;
    }

    if(sample_capture_looper_record_conflict() != 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_LOOPER_ACTIVE);
        return 0U;
    }

    if(sample_capture_prepare_paths() == 0U)
    {
        return 0U;
    }

    const uint32_t frame_limit = sample_capture_len_to_frames(g_sample_capture.state.len_bars);
    if(audio_recorder_prepare_client(AUDIO_RECORDER_CLIENT_AUDIO_REC,
                                     g_sample_capture.state.temp_path,
                                     SAMPLE_CAPTURE_WORKING_WAV_PATH,
                                     frame_limit) == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        return 0U;
    }

    g_sample_capture.state.planned_frames = frame_limit;
    return 1U;
}
static uint8_t sample_capture_start_prepared_at(uint64_t sample_time)
{
    if(sample_capture_publish_audio_projection_at(sample_time) == 0U)
        return 0U;
    if(audio_recorder_start_client_at(AUDIO_RECORDER_CLIENT_AUDIO_REC,
                                      sample_time) == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SAMPLE_ACTIVE);
        return 0U;
    }

    g_sample_capture.state.recording = 1U;
    g_sample_capture.state.armed_pending = 0U;
    g_sample_capture.state.take_valid = 0U;
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_RECORDING;
    g_sample_capture.state.error = SAMPLE_CAPTURE_ERROR_NONE;
    g_sample_capture.last_take_notified = 0U;
    g_sample_capture.rec_edit_enter_deferred_services = 0U;
    sample_capture_waveform_reset();
    sample_capture_detail_reset();
    g_sample_capture.audio_hook_enabled = 1U;
    return 1U;
}

static void sample_capture_on_take_ready(const audio_recorder_status_t *status)
{
    if((status == 0) || (g_sample_capture.last_take_notified != 0U))
    {
        return;
    }

    const char *path = 0;
    uint32_t frames = 0U;
    if(audio_recorder_get_last_take_client(AUDIO_RECORDER_CLIENT_AUDIO_REC,
                                           &path, &frames) == 0U)
    {
        return;
    }
    if(frames == 0U)
    {
        sample_capture_set_audio_hook_enabled(0U);
        g_sample_capture.state.recording = 0U;
        g_sample_capture.state.armed_pending = 0U;
        g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
        g_sample_capture.state.trigger_latched = 0U;
        sample_capture_publish_audio_projection();
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        g_sample_capture.last_take_notified = 1U;
        return;
    }

    g_sample_capture.state.recording = 0U;
    g_sample_capture.state.armed_pending = 0U;
    g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
    g_sample_capture.state.trigger_latched = 0U;
    sample_capture_publish_audio_projection();
#if SAMPLE_CAPTURE_DEBUG_UART
    sample_capture_debug_log("REC_LIVE_DONE frames=%lu\r\n", (unsigned long)frames);
    sample_capture_debug_log("WRITER_FINAL_READY path=%s frames=%lu\r\n",
                             path,
                             (unsigned long)frames);
#endif
#if SAMPLE_CAPTURE_DEBUG_UART
    sample_capture_debug_log("REC_EDIT_ENTER_REQUEST path=%s\r\n", path);
#endif
    g_sample_capture.state.take_valid = 1U;
    g_sample_capture.state.recorded_frames = frames;
    g_sample_capture.state.edit_start_frame = 0U;
    g_sample_capture.state.edit_end_frame = frames;
    g_sample_capture.state.edit_loop_start_frame = 0U;
    g_sample_capture.state.edit_loop_end_frame = frames;
    g_sample_capture.state.edit_zoom = 0U;
    g_sample_capture.state.edit_vzoom = SAMPLE_CAPTURE_EDIT_VZOOM_DEFAULT;
    g_sample_capture.state.edit_zcross_enabled = 0U;
    g_sample_capture.state.edit_scroll_frame = 0U;
    g_sample_capture.state.view = SAMPLE_CAPTURE_VIEW_REC_EDIT;
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_REC_EDIT;
    sample_capture_copy_path(g_sample_capture.state.temp_path, path);
    sample_capture_detail_reset();
    sample_capture_global_overview_request();
    uint8_t cache_queued = 0U;
    if(sample_capture_path_is_temp(path) == 0U)
    {
        if(frames >= WAVEFORM_CACHE_PERSIST_MIN_FRAMES)
        {
            cache_queued = waveform_cache_request_for_wav_known_duration(
                path,
                WAVEFORM_CACHE_REASON_POST_AUDIO_REC,
                frames,
                AUDIO_RECORDER_SAMPLE_RATE_HZ);
            g_sample_capture.wave_cache_retry_countdown = 32U;
        }
        else
        {
            g_sample_capture.wave_cache_retry_countdown = 0U;
        }
    }
    else
    {
        g_sample_capture.wave_cache_retry_countdown = 0U;
    }
    g_sample_capture.rec_edit_enter_deferred_services = 2U;
    g_sample_capture.rec_edit_first_render_pending = 1U;
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    sample_capture_debug_log("WAVEFORM_CACHE_%s path=%s\r\n",
                             (cache_queued != 0U) ? "JOB_QUEUED" : "DEFERRED",
                             path);
#else
    (void)cache_queued;
#endif
#if SAMPLE_CAPTURE_DEBUG_UART
    sample_capture_debug_log("REC_EDIT_ENTER_OK\r\n");
#endif
    g_sample_capture.last_take_notified = 1U;
    (void)status;
}

uint8_t sample_capture_push_audio_block_from_irq(const int32_t *lr_interleaved,
                                                 uint32_t frames)
{
    sample_capture_waveform_push_minmax_from_irq(lr_interleaved, frames);
    return audio_recorder_push_from_irq_client(
        AUDIO_RECORDER_CLIENT_AUDIO_REC, lr_interleaved, frames);
}

static void sample_capture_set_audio_hook_enabled(uint8_t enabled)
{
    g_sample_capture.audio_hook_enabled = (enabled != 0U) ? 1U : 0U;
    sample_capture_publish_audio_projection();
}

