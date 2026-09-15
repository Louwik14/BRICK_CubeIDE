uint32_t sample_capture_model_visible_frames_for_zoom(uint32_t recorded_frames, uint8_t zoom)
{
    if(recorded_frames == 0U)
    {
        return 1U;
    }
    const uint32_t min_frames =
        (recorded_frames < SAMPLE_CAPTURE_EDIT_MIN_VISIBLE_FRAMES)
            ? recorded_frames
            : SAMPLE_CAPTURE_EDIT_MIN_VISIBLE_FRAMES;
    if((zoom == 0U) || (recorded_frames <= min_frames))
    {
        return recorded_frames;
    }
    if(zoom >= SAMPLE_CAPTURE_EDIT_ZOOM_MAX)
    {
        return (min_frames == 0U) ? 1U : min_frames;
    }

    const float total = (float)recorded_frames;
    const float min_view = (float)min_frames;
    const float zoom_norm = (float)zoom / (float)SAMPLE_CAPTURE_EDIT_ZOOM_MAX;
    const float ratio = min_view / total;
    uint32_t frames = (uint32_t)((total * powf(ratio, zoom_norm)) + 0.5f);
    if(frames > recorded_frames) { frames = recorded_frames; }
    if(frames < min_frames) { frames = min_frames; }
    if(frames == 0U)
    {
        frames = 1U;
    }
    return frames;
}

static void sample_capture_set_error(sample_capture_error_t error)
{
    g_sample_capture.state.error = error;
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_ERROR;
}

static void sample_capture_detail_reset(void)
{
    sample_capture_global_overview_reset();
    memset(&g_sample_capture.wave_cache_handle, 0, sizeof(g_sample_capture.wave_cache_handle));
    g_sample_capture.wave_cache_ready = 0U;
    g_sample_capture.wave_cache_retry_countdown = 0U;
}

static int16_t sample_capture_pcm24_to_waveform_i16(int32_t v)
{
    if(v > (int32_t)SAMPLE_CAPTURE_PCM24_PEAK)
    {
        v = (int32_t)SAMPLE_CAPTURE_PCM24_PEAK;
    }
    else if(v < -8388608L)
    {
        v = -8388608L;
    }
    return (int16_t)(v >> 8);
}

static int16_t sample_capture_pcm24le_to_waveform_i16(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
    if((v & 0x00800000L) != 0)
    {
        v |= (int32_t)0xFF000000L;
    }
    return sample_capture_pcm24_to_waveform_i16(v);
}

static uint16_t sample_capture_abs_i16(int16_t v)
{
    if(v >= 0)
    {
        return (uint16_t)v;
    }
    if(v == (int16_t)-32768)
    {
        return 32768U;
    }
    return (uint16_t)(-v);
}

static void sample_capture_global_overview_reset(void)
{
    if(g_sample_capture_global_overview.file_open != 0U)
    {
        if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_EDITOR_CACHE) == 0U)
        {
            g_sample_capture_global_overview.reset_pending = 1U;
            g_sample_capture_global_overview.state = SAMPLE_CAPTURE_GLOBAL_OVERVIEW_ERROR;
            return;
        }
        (void)f_close(&g_sample_capture_global_overview.file);
        sd_access_gate_release(SD_ACCESS_CLIENT_EDITOR_CACHE);
    }
    memset(&g_sample_capture_global_overview, 0, sizeof(g_sample_capture_global_overview));
    g_sample_capture_global_overview.state = SAMPLE_CAPTURE_GLOBAL_OVERVIEW_EMPTY;
}

static uint8_t sample_capture_global_overview_path_matches(void)
{
    return (uint8_t)((g_sample_capture.state.temp_path[0] != '\0')
            && (strncmp(g_sample_capture_global_overview.path,
                        g_sample_capture.state.temp_path,
                        SAMPLE_CAPTURE_PATH_MAX) == 0));
}

static void sample_capture_global_overview_request(void)
{
    if(g_sample_capture_global_overview.reset_pending != 0U)
        return;
    if((g_sample_capture.state.take_valid == 0U)
            || (g_sample_capture.state.recorded_frames == 0U)
            || (g_sample_capture.state.temp_path[0] == '\0'))
    {
        return;
    }
    if((g_sample_capture_global_overview.state == SAMPLE_CAPTURE_GLOBAL_OVERVIEW_READY)
            && (sample_capture_global_overview_path_matches() != 0U)
            && (g_sample_capture_global_overview.frame_count == g_sample_capture.state.recorded_frames))
    {
        return;
    }
    if((g_sample_capture_global_overview.state == SAMPLE_CAPTURE_GLOBAL_OVERVIEW_BUILDING)
            && (sample_capture_global_overview_path_matches() != 0U)
            && (g_sample_capture_global_overview.frame_count == g_sample_capture.state.recorded_frames))
    {
        return;
    }

    rec_source_snapshot_t current_snapshot;
    const char *current_path = 0;
    const rec_source_waveform_summary_t *const captured =
        rec_source_current_waveform();
    if ((captured != 0) && (captured->ready != 0U)
            && (captured->frame_count == g_sample_capture.state.recorded_frames)
            && (rec_source_current_path(&current_path, &current_snapshot) != 0U)
            && (captured->generation == current_snapshot.key.generation)
            && (strncmp(current_path, g_sample_capture.state.temp_path,
                        SAMPLE_CAPTURE_PATH_MAX) == 0))
    {
        sample_capture_copy_path(g_sample_capture_global_overview.path, current_path);
        g_sample_capture_global_overview.frame_count = captured->frame_count;
        g_sample_capture_global_overview.point_count = captured->bin_count;
        g_sample_capture_global_overview.build_next_frame = captured->frame_count;
        uint16_t peak = 0U;
        for (uint16_t i = 0U; i < captured->bin_count; ++i)
        {
            g_sample_capture_global_overview_points[i].min = captured->min[i];
            g_sample_capture_global_overview_points[i].max = captured->max[i];
            const uint16_t amin = sample_capture_abs_i16(captured->min[i]);
            const uint16_t amax = sample_capture_abs_i16(captured->max[i]);
            if (amin > peak) peak = amin;
            if (amax > peak) peak = amax;
        }
        g_sample_capture_global_overview.peak = peak;
        g_sample_capture_global_overview.state = SAMPLE_CAPTURE_GLOBAL_OVERVIEW_READY;
        g_sample_capture_global_overview.generation++;
        return;
    }

    sample_capture_copy_path(g_sample_capture_global_overview.path,
                             g_sample_capture.state.temp_path);
    g_sample_capture_global_overview.frame_count = g_sample_capture.state.recorded_frames;
    g_sample_capture_global_overview.point_count =
        (g_sample_capture.state.recorded_frames < SAMPLE_CAPTURE_GLOBAL_OVERVIEW_POINTS)
            ? (uint16_t)g_sample_capture.state.recorded_frames
            : (uint16_t)SAMPLE_CAPTURE_GLOBAL_OVERVIEW_POINTS;
    if(g_sample_capture_global_overview.point_count == 0U)
    {
        g_sample_capture_global_overview.point_count = 1U;
    }
    g_sample_capture_global_overview.build_next_frame = 0U;
    g_sample_capture_global_overview.peak = 0U;
    g_sample_capture_global_overview.state = SAMPLE_CAPTURE_GLOBAL_OVERVIEW_BUILDING;
    g_rec_latency_probe.t_waveform_start = rec_latency_probe_now();
    g_rec_latency_probe.t_waveform_first_progress = 0U;
    g_rec_latency_probe.t_waveform_done = 0U;
    g_rec_latency_probe.waveform_bytes_total =
        g_sample_capture_global_overview.frame_count * AUDIO_RECORDER_BYTES_PER_FRAME;
    g_rec_latency_probe.waveform_bytes_processed = 0U;
    g_rec_latency_probe.waveform_service_count = 0U;
    g_rec_latency_probe.waveform_progress_count = 0U;
    g_rec_latency_probe.waveform_read_count = 0U;
    g_rec_latency_probe.waveform_read_total_ticks = 0U;
    g_rec_latency_probe.waveform_read_max_ticks = 0U;
    g_rec_latency_probe.waveform_chunk_bytes =
        SAMPLE_CAPTURE_GLOBAL_OVERVIEW_BUILD_CHUNK_FRAMES * AUDIO_RECORDER_BYTES_PER_FRAME;
    g_rec_latency_probe.waveform_max_bytes_per_service = 0U;
    g_rec_latency_probe.waveform_not_now_count = 0U;
    g_rec_latency_probe.waveform_admission_denied_count = 0U;
    g_rec_latency_probe.waveform_last_progress_t = 0U;
    g_rec_latency_probe.waveform_max_progress_gap = 0U;
    g_rec_latency_probe.waveform_service_total_ticks = 0U;
    g_rec_latency_probe.waveform_service_max_ticks = 0U;
    g_rec_latency_probe.waveform_compute_total_ticks = 0U;
    g_rec_latency_probe.waveform_compute_max_ticks = 0U;
    g_rec_latency_probe.waveform_interservice_total_ticks = 0U;
    g_rec_latency_probe.waveform_interservice_max_ticks = 0U;
    g_rec_latency_probe.waveform_last_service_t = 0U;
    g_rec_latency_probe.waveform_mount_total_ticks = 0U;
    g_rec_latency_probe.waveform_mount_max_ticks = 0U;
    g_rec_latency_probe.waveform_open_total_ticks = 0U;
    g_rec_latency_probe.waveform_open_max_ticks = 0U;
    g_rec_latency_probe.waveform_seek_total_ticks = 0U;
    g_rec_latency_probe.waveform_seek_max_ticks = 0U;
    g_rec_latency_probe.waveform_close_total_ticks = 0U;
    g_rec_latency_probe.waveform_close_max_ticks = 0U;
    g_rec_latency_probe.waveform_publish_total_ticks = 0U;
    g_rec_latency_probe.waveform_publish_max_ticks = 0U;
    g_rec_latency_probe.waveform_other_total_ticks = 0U;
    g_rec_latency_probe.waveform_other_max_ticks = 0U;
    g_sample_capture_global_overview.generation++;
    for(uint16_t i = 0U; i < SAMPLE_CAPTURE_GLOBAL_OVERVIEW_POINTS; ++i)
    {
        g_sample_capture_global_overview_points[i].min = 32767;
        g_sample_capture_global_overview_points[i].max = (int16_t)-32768;
    }
}

static uint8_t sample_capture_global_overview_can_build(void)
{
    if((g_sample_capture_global_overview.state != SAMPLE_CAPTURE_GLOBAL_OVERVIEW_BUILDING)
            || (g_sample_capture_global_overview.path[0] == '\0')
            || (g_sample_capture_global_overview.frame_count == 0U)
            || (g_sample_capture_global_overview.point_count == 0U)
            || (g_sample_capture.state.take_valid == 0U)
            || (g_sample_capture.state.recording != 0U)
            || (g_sample_capture.state.armed_pending != 0U)
            || (sample_capture_global_overview_path_matches() == 0U))
    {
        return 0U;
    }
    if((audio_recorder_is_active() != 0U)
            || (sd_preview_is_active() != 0U)
            || (pattern_load_is_pending() != 0U)
            || (sample_cache_has_pending_sd_work() != 0U))
    {
        return 0U;
    }
    return 1U;
}

static void sample_capture_global_overview_finish(void)
{
    uint16_t peak = 0U;
    for(uint16_t i = 0U; i < g_sample_capture_global_overview.point_count; ++i)
    {
        if(g_sample_capture_global_overview_points[i].min == 32767
                && g_sample_capture_global_overview_points[i].max == (int16_t)-32768)
        {
            g_sample_capture_global_overview_points[i].min = 0;
            g_sample_capture_global_overview_points[i].max = 0;
        }
        const uint16_t abs_min =
            sample_capture_abs_i16(g_sample_capture_global_overview_points[i].min);
        const uint16_t abs_max =
            sample_capture_abs_i16(g_sample_capture_global_overview_points[i].max);
        if(abs_min > peak)
        {
            peak = abs_min;
        }
        if(abs_max > peak)
        {
            peak = abs_max;
        }
    }
    g_sample_capture_global_overview.peak = peak;
    g_sample_capture_global_overview.state = SAMPLE_CAPTURE_GLOBAL_OVERVIEW_READY;
    g_rec_latency_probe.t_waveform_done = rec_latency_probe_now();
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    sample_capture_debug_log("GOVERVIEW DONE points=%u frames=%lu peak=%u\r\n",
                             (unsigned)g_sample_capture_global_overview.point_count,
                             (unsigned long)g_sample_capture_global_overview.frame_count,
                             (unsigned)peak);
#endif
}

static uint32_t sample_capture_waveform_probe_attributed_ticks(void)
{
    return g_rec_latency_probe.waveform_read_total_ticks
        + g_rec_latency_probe.waveform_compute_total_ticks
        + g_rec_latency_probe.waveform_mount_total_ticks
        + g_rec_latency_probe.waveform_open_total_ticks
        + g_rec_latency_probe.waveform_seek_total_ticks
        + g_rec_latency_probe.waveform_close_total_ticks
        + g_rec_latency_probe.waveform_publish_total_ticks;
}

static void sample_capture_waveform_probe_service_end(uint32_t start,
                                                       uint32_t attributed_start)
{
    const uint32_t now = rec_latency_probe_now();
    const uint32_t elapsed = now - start;
    g_rec_latency_probe.waveform_service_total_ticks += elapsed;
    if(elapsed > g_rec_latency_probe.waveform_service_max_ticks)
        g_rec_latency_probe.waveform_service_max_ticks = elapsed;
    const uint32_t attributed = sample_capture_waveform_probe_attributed_ticks()
        - attributed_start;
    const uint32_t other = (elapsed > attributed) ? (elapsed - attributed) : 0U;
    g_rec_latency_probe.waveform_other_total_ticks += other;
    if(other > g_rec_latency_probe.waveform_other_max_ticks)
        g_rec_latency_probe.waveform_other_max_ticks = other;
    g_rec_latency_probe.waveform_last_service_t = now;
}

static void sample_capture_global_overview_service(void)
{
    if(g_sample_capture_global_overview.reset_pending != 0U)
    {
        if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_EDITOR_CACHE) != 0U)
        {
            if(g_sample_capture_global_overview.file_open != 0U)
                (void)f_close(&g_sample_capture_global_overview.file);
            sd_access_gate_release(SD_ACCESS_CLIENT_EDITOR_CACHE);
            memset(&g_sample_capture_global_overview, 0,
                   sizeof(g_sample_capture_global_overview));
            g_sample_capture_global_overview.state = SAMPLE_CAPTURE_GLOBAL_OVERVIEW_EMPTY;
        }
        return;
    }
    const uint8_t probe_active = (uint8_t)(
        g_sample_capture_global_overview.state == SAMPLE_CAPTURE_GLOBAL_OVERVIEW_BUILDING);
    const uint32_t service_start = rec_latency_probe_now();
    const uint32_t attributed_start = sample_capture_waveform_probe_attributed_ticks();
    if(probe_active != 0U) {
        if(g_rec_latency_probe.waveform_last_service_t != 0U) {
            const uint32_t gap = service_start - g_rec_latency_probe.waveform_last_service_t;
            g_rec_latency_probe.waveform_interservice_total_ticks += gap;
            if(gap > g_rec_latency_probe.waveform_interservice_max_ticks)
                g_rec_latency_probe.waveform_interservice_max_ticks = gap;
        }
        g_rec_latency_probe.waveform_service_count++;
    }
    if(sample_capture_global_overview_can_build() == 0U)
    {
        if(g_sample_capture_global_overview.state == SAMPLE_CAPTURE_GLOBAL_OVERVIEW_BUILDING)
            g_rec_latency_probe.waveform_not_now_count++;
        if(probe_active != 0U) sample_capture_waveform_probe_service_end(service_start, attributed_start);
        return;
    }

    uint32_t frames_left = g_sample_capture_global_overview.frame_count
        - g_sample_capture_global_overview.build_next_frame;
    if(frames_left == 0U)
    {
        const uint32_t publish_start = rec_latency_probe_now();
        sample_capture_global_overview_finish();
        const uint32_t publish_ticks = rec_latency_probe_now() - publish_start;
        g_rec_latency_probe.waveform_publish_total_ticks += publish_ticks;
        if(publish_ticks > g_rec_latency_probe.waveform_publish_max_ticks)
            g_rec_latency_probe.waveform_publish_max_ticks = publish_ticks;
        sample_capture_waveform_probe_service_end(service_start, attributed_start);
        return;
    }
    if(frames_left > SAMPLE_CAPTURE_GLOBAL_OVERVIEW_BUILD_CHUNK_FRAMES)
    {
        frames_left = SAMPLE_CAPTURE_GLOBAL_OVERVIEW_BUILD_CHUNK_FRAMES;
    }

    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_EDITOR_CACHE) == 0U)
    {
        g_rec_latency_probe.waveform_admission_denied_count++;
        g_rec_latency_probe.waveform_not_now_count++;
        sample_capture_waveform_probe_service_end(service_start, attributed_start);
        return;
    }

    uint8_t ok = 0U;
    uint32_t op_start;
    uint32_t op_ticks;
    if(g_sample_capture_global_overview.file_open == 0U)
    {
        op_start = rec_latency_probe_now();
        const uint8_t mount_ok = sd_access_fs_mount_if_needed();
        op_ticks = rec_latency_probe_now() - op_start;
        g_rec_latency_probe.waveform_mount_total_ticks += op_ticks;
        if(op_ticks > g_rec_latency_probe.waveform_mount_max_ticks)
            g_rec_latency_probe.waveform_mount_max_ticks = op_ticks;
        if(mount_ok == 0U) goto done;

        op_start = rec_latency_probe_now();
        const FRESULT open_result = f_open(&g_sample_capture_global_overview.file,
            g_sample_capture_global_overview.path, FA_READ);
        op_ticks = rec_latency_probe_now() - op_start;
        g_rec_latency_probe.waveform_open_total_ticks += op_ticks;
        if(op_ticks > g_rec_latency_probe.waveform_open_max_ticks)
            g_rec_latency_probe.waveform_open_max_ticks = op_ticks;
        if(open_result != FR_OK) goto done;
        g_sample_capture_global_overview.file_open = 1U;

        op_start = rec_latency_probe_now();
        const FRESULT seek_result = f_lseek(&g_sample_capture_global_overview.file,
            SAMPLE_CAPTURE_WAV_DATA_OFFSET);
        op_ticks = rec_latency_probe_now() - op_start;
        g_rec_latency_probe.waveform_seek_total_ticks += op_ticks;
        if(op_ticks > g_rec_latency_probe.waveform_seek_max_ticks)
            g_rec_latency_probe.waveform_seek_max_ticks = op_ticks;
        if(seek_result != FR_OK) goto done;
    }

    const uint32_t bytes_to_read = frames_left * AUDIO_RECORDER_BYTES_PER_FRAME;
    UINT br = 0U;
    const uint32_t read_start = rec_latency_probe_now();
    const FRESULT read_result = f_read(&g_sample_capture_global_overview.file,
        g_sample_capture_detail_buf, bytes_to_read, &br);
    const uint32_t read_ticks = rec_latency_probe_now() - read_start;
    g_rec_latency_probe.waveform_read_count++;
    g_rec_latency_probe.waveform_read_total_ticks += read_ticks;
    if(read_ticks > g_rec_latency_probe.waveform_read_max_ticks)
        g_rec_latency_probe.waveform_read_max_ticks = read_ticks;
    if((read_result != FR_OK) || (br < AUDIO_RECORDER_BYTES_PER_FRAME))
        goto done;
    if(br > g_rec_latency_probe.waveform_max_bytes_per_service)
        g_rec_latency_probe.waveform_max_bytes_per_service = br;
    {
        const uint32_t now = rec_latency_probe_now();
        if(g_rec_latency_probe.t_waveform_first_progress == 0U)
            g_rec_latency_probe.t_waveform_first_progress = now;
        if(g_rec_latency_probe.waveform_last_progress_t != 0U) {
            const uint32_t gap = now - g_rec_latency_probe.waveform_last_progress_t;
            if(gap > g_rec_latency_probe.waveform_max_progress_gap)
                g_rec_latency_probe.waveform_max_progress_gap = gap;
        }
        g_rec_latency_probe.waveform_last_progress_t = now;
        g_rec_latency_probe.waveform_progress_count++;
        g_rec_latency_probe.waveform_bytes_processed += br;
    }

    const uint32_t frames_read = br / AUDIO_RECORDER_BYTES_PER_FRAME;
    const uint32_t compute_start = rec_latency_probe_now();
    for(uint32_t i = 0U; i < frames_read; ++i)
    {
        const uint32_t frame_index = g_sample_capture_global_overview.build_next_frame + i;
        uint32_t point = (uint32_t)(((uint64_t)frame_index
                * (uint64_t)g_sample_capture_global_overview.point_count)
            / (uint64_t)g_sample_capture_global_overview.frame_count);
        if(point >= g_sample_capture_global_overview.point_count)
        {
            point = g_sample_capture_global_overview.point_count - 1U;
        }

        const uint8_t *frame = &g_sample_capture_detail_buf[i * AUDIO_RECORDER_BYTES_PER_FRAME];
        const int16_t l = sample_capture_pcm24le_to_waveform_i16(frame);
        const int16_t r = sample_capture_pcm24le_to_waveform_i16(&frame[3]);
        const int16_t sample_min = (l < r) ? l : r;
        const int16_t sample_max = (l > r) ? l : r;
        if(sample_min < g_sample_capture_global_overview_points[point].min)
        {
            g_sample_capture_global_overview_points[point].min = sample_min;
        }
        if(sample_max > g_sample_capture_global_overview_points[point].max)
        {
            g_sample_capture_global_overview_points[point].max = sample_max;
        }
    }
    {
        const uint32_t compute_ticks = rec_latency_probe_now() - compute_start;
        g_rec_latency_probe.waveform_compute_total_ticks += compute_ticks;
        if(compute_ticks > g_rec_latency_probe.waveform_compute_max_ticks)
            g_rec_latency_probe.waveform_compute_max_ticks = compute_ticks;
    }

    g_sample_capture_global_overview.build_next_frame += frames_read;
    ok = 1U;
    if((frames_read == 0U)
            || (g_sample_capture_global_overview.build_next_frame
                >= g_sample_capture_global_overview.frame_count))
    {
        const uint32_t publish_start = rec_latency_probe_now();
        sample_capture_global_overview_finish();
        const uint32_t publish_ticks = rec_latency_probe_now() - publish_start;
        g_rec_latency_probe.waveform_publish_total_ticks += publish_ticks;
        if(publish_ticks > g_rec_latency_probe.waveform_publish_max_ticks)
            g_rec_latency_probe.waveform_publish_max_ticks = publish_ticks;
    }

done:
    if((g_sample_capture_global_overview.file_open != 0U)
            && ((ok == 0U)
                || (g_sample_capture_global_overview.state != SAMPLE_CAPTURE_GLOBAL_OVERVIEW_BUILDING)))
    {
        op_start = rec_latency_probe_now();
        (void)f_close(&g_sample_capture_global_overview.file);
        op_ticks = rec_latency_probe_now() - op_start;
        g_rec_latency_probe.waveform_close_total_ticks += op_ticks;
        if(op_ticks > g_rec_latency_probe.waveform_close_max_ticks)
            g_rec_latency_probe.waveform_close_max_ticks = op_ticks;
        g_sample_capture_global_overview.file_open = 0U;
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_EDITOR_CACHE);
    if(ok == 0U)
    {
        g_sample_capture_global_overview.state = SAMPLE_CAPTURE_GLOBAL_OVERVIEW_ERROR;
    }
    sample_capture_waveform_probe_service_end(service_start, attributed_start);
}

