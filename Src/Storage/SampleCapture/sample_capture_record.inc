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

static uint8_t sample_capture_ready_mono_at(const rec_source_snapshot_t *source,
                                            uint32_t frame,
                                            uint32_t *loaded_page,
                                            sample_page_span_t *span,
                                            int16_t *out_sample);

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

    rec_source_snapshot_t source;
    if(rec_source_current_snapshot(&source) == 0U
            || source.key.domain != SAMPLE_AUDIO_DOMAIN_REC
            || source.key.generation != g_sample_capture.visible_rec_generation
            || source.frame_count != g_sample_capture.state.recorded_frames)
    {
        return candidate;
    }
    uint32_t cover_start = search_start;
    if(cover_start > min_frame) { cover_start--; }
    (void)sample_page_cache_reserve_page_key(source.key,
        cover_start / SAMPLE_PAGE_FRAMES);
    (void)sample_page_cache_reserve_page_key(source.key,
        search_end / SAMPLE_PAGE_FRAMES);
    sample_page_span_t span;
    uint32_t loaded_page = UINT32_MAX;
    if(right != 0U)
    {
        int16_t prev;
        if(sample_capture_ready_mono_at(&source, cover_start,
            &loaded_page, &span, &prev) == 0U)
        {
            return candidate;
        }
        for(uint32_t frame = cover_start + 1U; frame <= search_end; ++frame)
        {
            int16_t cur;
            if(sample_capture_ready_mono_at(&source, frame,
                &loaded_page, &span, &cur) == 0U)
            {
                return candidate;
            }
            if(frame >= search_start && sample_capture_sign_crosses(prev, cur) != 0U)
            {
                return sample_capture_clamp_u32(frame, min_frame, max_frame);
            }
            prev = cur;
            if(frame == UINT32_MAX) { break; }
        }
    }
    else
    {
        for(uint32_t frame = search_end; frame > cover_start; --frame)
        {
            int16_t prev;
            int16_t cur;
            if(sample_capture_ready_mono_at(&source, frame - 1U,
                    &loaded_page, &span, &prev) == 0U
                    || sample_capture_ready_mono_at(&source, frame,
                    &loaded_page, &span, &cur) == 0U)
            {
                return candidate;
            }
            if(frame >= search_start && sample_capture_sign_crosses(prev, cur) != 0U)
            {
                return sample_capture_clamp_u32(frame, min_frame, max_frame);
            }
        }
    }
    return candidate;
}

static audio_recorder_lifecycle_result_t sample_capture_prepare_paths(void)
{
    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_RECORDER) == 0U)
    {
        return AUDIO_RECORDER_LIFECYCLE_NOT_NOW;
    }

    uint8_t ok = 0U;
    if(sd_access_fs_mount_if_needed() != 0U)
    {
        const FRESULT made = f_mkdir(SAMPLE_CAPTURE_REC_DIR);
        if((made != FR_OK) && (made != FR_EXIST))
        {
            sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
            sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
            return AUDIO_RECORDER_LIFECYCLE_ERROR;
        }
        const char *temporary_path = rec_source_building_temporary_path();
        const char *final_path = rec_source_building_final_path();
        sample_audio_key_t building_key;
        const uint8_t building_ready = (rec_source_building_active() != 0U)
            ? 1U : rec_source_begin_build(&temporary_path, &final_path,
                                          &building_key);
        if (building_ready != 0U)
        {
            sample_capture_copy_path(g_sample_capture.state.temp_path,
                                     temporary_path);
            ok = 1U;
        }
    }
    else
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
    return (ok != 0U) ? AUDIO_RECORDER_LIFECYCLE_OK
                      : AUDIO_RECORDER_LIFECYCLE_ERROR;
}

static audio_recorder_lifecycle_result_t sample_capture_prepare_session(void)
{
    if(sd_preview_is_active() != 0U)
    {
        sd_preview_stop();
    }

    const audio_recorder_lifecycle_result_t paths =
        sample_capture_prepare_paths();
    if(paths != AUDIO_RECORDER_LIFECYCLE_OK)
    {
        return paths;
    }

    const uint32_t frame_limit = sample_capture_len_to_frames(g_sample_capture.state.len_bars);
    const audio_recorder_lifecycle_result_t prepared =
        audio_recorder_prepare_client_cooperative(
            AUDIO_RECORDER_CLIENT_AUDIO_REC,
            g_sample_capture.state.temp_path,
            rec_source_building_final_path(),
            frame_limit);
    if(prepared != AUDIO_RECORDER_LIFECYCLE_OK)
    {
        return prepared;
    }

    g_sample_capture.state.planned_frames = frame_limit;
    return AUDIO_RECORDER_LIFECYCLE_OK;
}

static uint8_t sample_capture_ready_mono_at(const rec_source_snapshot_t *source,
                                            uint32_t frame,
                                            uint32_t *loaded_page,
                                            sample_page_span_t *span,
                                            int16_t *out_sample)
{
    const uint32_t page = frame / SAMPLE_PAGE_FRAMES;
    if(page != *loaded_page)
    {
        if(sample_page_cache_control_resolve_page_key(source->key,
            source->registration_epoch, page, span) == 0U)
        {
            return 0U;
        }
        *loaded_page = page;
    }
    if(frame < span->start_frame
            || frame - span->start_frame >= span->frame_count
            || span->stride_floats != 2U)
    {
        return 0U;
    }
    const uint32_t offset = (frame - span->start_frame) * 2U;
    float mono = (span->frames_interleaved[offset]
        + span->frames_interleaved[offset + 1U]) * 0.5f;
    if(mono >= 1.0f) { mono = 0.999969f; }
    if(mono < -1.0f) { mono = -1.0f; }
    *out_sample = (int16_t)(mono * 32768.0f);
    return 1U;
}
static uint8_t sample_capture_start_prepared_at(uint64_t sample_time)
{
    audio_recorder_status_t status;
    if((audio_recorder_get_status_client(AUDIO_RECORDER_CLIENT_AUDIO_REC,
                                         &status) == 0U)
            || (status.state != AUDIO_RECORDER_STATE_PREPARED))
        return 0U;
    if(sample_capture_publish_rec_bus_at(sample_time) == 0U)
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
    sample_capture_publish_trigger_config();
    g_sample_capture.last_take_notified = 0U;
    g_sample_capture.rec_edit_enter_deferred_services = 0U;
    sample_capture_detail_reset();
    g_sample_capture.capture_enabled = 1U;
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
        sample_capture_set_capture_enabled(0U);
        g_sample_capture.state.recording = 0U;
        g_sample_capture.state.armed_pending = 0U;
        g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
        g_sample_capture.state.trigger_latched = 0U;
        sample_capture_publish_trigger_config();
        sample_capture_publish_rec_bus();
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        g_sample_capture.last_take_notified = 1U;
        return;
    }

    g_sample_capture.state.recording = 0U;
    g_sample_capture.state.armed_pending = 0U;
    g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
    g_sample_capture.state.trigger_latched = 0U;
    sample_capture_publish_trigger_config();
        sample_capture_publish_rec_bus();
#if SAMPLE_CAPTURE_DEBUG_UART
    sample_capture_debug_log("REC_LIVE_DONE frames=%lu\r\n", (unsigned long)frames);
    sample_capture_debug_log("WRITER_FINAL_READY path=%s frames=%lu\r\n",
                             path,
                             (unsigned long)frames);
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
    g_sample_capture.state.view = SAMPLE_CAPTURE_VIEW_AUDIO_REC;
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_TAKE_READY;
    sample_capture_copy_path(g_sample_capture.state.temp_path, path);
    rec_source_snapshot_t current;
    if (rec_source_current_snapshot(&current) != 0U)
        g_sample_capture.visible_rec_generation = current.key.generation;
    sample_capture_detail_reset();
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
    g_sample_capture.rec_edit_enter_deferred_services = 0U;
    g_sample_capture.rec_edit_first_render_pending = 0U;
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    sample_capture_debug_log("WAVEFORM_CACHE_%s path=%s\r\n",
                             (cache_queued != 0U) ? "JOB_QUEUED" : "DEFERRED",
                             path);
#else
    (void)cache_queued;
#endif
    g_sample_capture.last_take_notified = 1U;
    (void)status;
}

static void sample_capture_set_capture_enabled(uint8_t enabled)
{
    g_sample_capture.capture_enabled = (enabled != 0U) ? 1U : 0U;
    sample_capture_publish_rec_bus();
}

