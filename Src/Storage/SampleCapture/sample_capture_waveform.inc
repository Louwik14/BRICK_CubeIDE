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

uint32_t sample_capture_model_tile_cache_capacity_frames(void)
{
    return SAMPLE_CAPTURE_EDITOR_CACHE_FRAMES;
}

uint8_t sample_capture_model_view_uses_tile_cache(uint32_t frame_count)
{
    return (uint8_t)((frame_count != 0U)
            && (frame_count <= SAMPLE_CAPTURE_EDITOR_CACHE_FRAMES));
}

static void sample_capture_set_error(sample_capture_error_t error)
{
    g_sample_capture.state.error = error;
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_ERROR;
}

static void sample_capture_detail_reset(void)
{
    g_sample_capture.state.detail_valid = 0U;
    g_sample_capture.state.detail_start_frame = 0U;
    g_sample_capture.state.detail_frames = 0U;
    g_sample_capture.state.detail_count = 0U;
    memset(g_sample_capture.state.detail, 0, sizeof(g_sample_capture.state.detail));
    memset(g_sample_capture.detail_seen, 0, sizeof(g_sample_capture.detail_seen));
    g_sample_capture.detail_requested = 0U;
    g_sample_capture.detail_building = 0U;
    g_sample_capture.detail_request_start_frame = 0U;
    g_sample_capture.detail_request_frames = 0U;
    g_sample_capture.detail_request_columns = 0U;
    g_sample_capture.detail_build_next_frame = 0U;
    g_sample_capture.state.line_valid = 0U;
    g_sample_capture.state.line_start_frame = 0U;
    g_sample_capture.state.line_frames = 0U;
    g_sample_capture.state.line_count = 0U;
    g_sample_capture.state.line_peak = 0U;
    memset(g_sample_capture.state.line, 0, sizeof(g_sample_capture.state.line));
    memset(&g_sample_capture_line_hot, 0, sizeof(g_sample_capture_line_hot));
    sample_capture_editor_cache_reset();
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

static int16_t sample_capture_line_sample_at(const int16_t *frames,
                                             uint32_t frame_count,
                                             uint32_t point,
                                             uint32_t point_count);
static void sample_capture_line_accumulate_frame(uint32_t rel_frame, int16_t sample);
static int16_t sample_capture_line_point_from_accum(uint16_t point);

static int16_t sample_capture_line_point_from_values(uint16_t point,
                                                     uint16_t point_count,
                                                     int16_t min_v,
                                                     int16_t max_v,
                                                     int16_t first_v,
                                                     int16_t last_v)
{
    int16_t point_v = first_v;
    if((min_v < 0) && (max_v > 0))
    {
        point_v = ((point & 1U) == 0U) ? max_v : min_v;
    }
    else
    {
        const uint16_t abs_min = sample_capture_abs_i16(min_v);
        const uint16_t abs_max = sample_capture_abs_i16(max_v);
        point_v = (abs_max >= abs_min) ? max_v : min_v;
    }
    if((point > 0U) && (point < (uint16_t)(point_count - 1U))
            && (sample_capture_abs_i16(point_v) < sample_capture_abs_i16(last_v)))
    {
        point_v = last_v;
    }
    return point_v;
}

static void sample_capture_editor_cache_reset(void)
{
    memset(&g_sample_capture_editor_cache, 0, sizeof(g_sample_capture_editor_cache));
    for(uint8_t i = 0U; i < SAMPLE_CAPTURE_EDITOR_TILE_COUNT; ++i)
    {
        g_sample_capture_editor_cache.tiles[i].state = SAMPLE_CAPTURE_EDITOR_CACHE_EMPTY;
    }
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    g_sample_capture_debug.fill_started_logged = 0U;
#endif
}

static void sample_capture_global_overview_reset(void)
{
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
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    sample_capture_debug_log("GOVERVIEW DONE points=%u frames=%lu peak=%u\r\n",
                             (unsigned)g_sample_capture_global_overview.point_count,
                             (unsigned long)g_sample_capture_global_overview.frame_count,
                             (unsigned)peak);
#endif
}

static void sample_capture_global_overview_service(void)
{
    if(sample_capture_global_overview_can_build() == 0U)
    {
        return;
    }

    uint32_t frames_left = g_sample_capture_global_overview.frame_count
        - g_sample_capture_global_overview.build_next_frame;
    if(frames_left == 0U)
    {
        sample_capture_global_overview_finish();
        return;
    }
    if(frames_left > SAMPLE_CAPTURE_GLOBAL_OVERVIEW_BUILD_CHUNK_FRAMES)
    {
        frames_left = SAMPLE_CAPTURE_GLOBAL_OVERVIEW_BUILD_CHUNK_FRAMES;
    }

    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_EDITOR_CACHE) == 0U)
    {
        return;
    }

    FIL fp;
    uint8_t file_open = 0U;
    uint8_t ok = 0U;
    if(sd_access_fs_mount_if_needed() == 0U)
    {
        goto done;
    }
    if(f_open(&fp, g_sample_capture_global_overview.path, FA_READ) != FR_OK)
    {
        goto done;
    }
    file_open = 1U;

    if(f_lseek(&fp, SAMPLE_CAPTURE_WAV_DATA_OFFSET
            + (g_sample_capture_global_overview.build_next_frame
               * AUDIO_RECORDER_BYTES_PER_FRAME)) != FR_OK)
    {
        goto done;
    }

    const uint32_t bytes_to_read = frames_left * AUDIO_RECORDER_BYTES_PER_FRAME;
    UINT br = 0U;
    if((f_read(&fp, g_sample_capture_detail_buf, bytes_to_read, &br) != FR_OK)
            || (br < AUDIO_RECORDER_BYTES_PER_FRAME))
    {
        goto done;
    }

    const uint32_t frames_read = br / AUDIO_RECORDER_BYTES_PER_FRAME;
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

    g_sample_capture_global_overview.build_next_frame += frames_read;
    ok = 1U;
    if((frames_read == 0U)
            || (g_sample_capture_global_overview.build_next_frame
                >= g_sample_capture_global_overview.frame_count))
    {
        sample_capture_global_overview_finish();
    }

done:
    if(file_open != 0U)
    {
        (void)f_close(&fp);
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_EDITOR_CACHE);
    if(ok == 0U)
    {
        g_sample_capture_global_overview.state = SAMPLE_CAPTURE_GLOBAL_OVERVIEW_ERROR;
    }
}

static uint16_t sample_capture_editor_level_count(uint32_t frames, uint32_t block_frames)
{
    if((frames == 0U) || (block_frames == 0U))
    {
        return 0U;
    }
    return (uint16_t)((frames + block_frames - 1U) / block_frames);
}

static void sample_capture_editor_tile_levels_reset(uint8_t tile_index)
{
    if(tile_index >= SAMPLE_CAPTURE_EDITOR_TILE_COUNT)
    {
        return;
    }
    memset(g_sample_capture_editor_level0[tile_index], 0, sizeof(g_sample_capture_editor_level0[tile_index]));
    memset(g_sample_capture_editor_level1[tile_index], 0, sizeof(g_sample_capture_editor_level1[tile_index]));
    memset(g_sample_capture_editor_level2[tile_index], 0, sizeof(g_sample_capture_editor_level2[tile_index]));
    g_sample_capture_editor_cache.tiles[tile_index].level0_count = 0U;
    g_sample_capture_editor_cache.tiles[tile_index].level1_count = 0U;
    g_sample_capture_editor_cache.tiles[tile_index].level2_count = 0U;
}

static void sample_capture_editor_level_accumulate(sample_capture_editor_level_point_t *level,
                                                   uint16_t level_count,
                                                   uint32_t block_frames,
                                                   uint32_t frame,
                                                   int16_t sample)
{
    if((level == 0) || (block_frames == 0U))
    {
        return;
    }
    const uint32_t idx = frame / block_frames;
    if(idx >= level_count)
    {
        return;
    }
    sample_capture_editor_level_point_t *const point = &level[idx];
    if((frame % block_frames) == 0U)
    {
        point->min = sample;
        point->max = sample;
        point->first = sample;
        point->last = sample;
        return;
    }
    if(sample < point->min)
    {
        point->min = sample;
    }
    if(sample > point->max)
    {
        point->max = sample;
    }
    point->last = sample;
}

static void sample_capture_editor_tile_levels_begin(uint8_t tile_index, uint32_t frame_count)
{
    if(tile_index >= SAMPLE_CAPTURE_EDITOR_TILE_COUNT)
    {
        return;
    }
    sample_capture_editor_tile_levels_reset(tile_index);
    g_sample_capture_editor_cache.tiles[tile_index].level0_count =
        sample_capture_editor_level_count(frame_count,
                                          SAMPLE_CAPTURE_EDITOR_LEVEL0_BLOCK_FRAMES);
    g_sample_capture_editor_cache.tiles[tile_index].level1_count =
        sample_capture_editor_level_count(frame_count,
                                          SAMPLE_CAPTURE_EDITOR_LEVEL1_BLOCK_FRAMES);
    g_sample_capture_editor_cache.tiles[tile_index].level2_count =
        sample_capture_editor_level_count(frame_count,
                                          SAMPLE_CAPTURE_EDITOR_LEVEL2_BLOCK_FRAMES);
}

static void sample_capture_editor_tile_level_accumulate(uint8_t tile_index,
                                                        uint32_t frame,
                                                        int16_t sample)
{
    if(tile_index >= SAMPLE_CAPTURE_EDITOR_TILE_COUNT)
    {
        return;
    }
    sample_capture_editor_audio_tile_t *const tile = &g_sample_capture_editor_cache.tiles[tile_index];
    sample_capture_editor_level_accumulate(g_sample_capture_editor_level0[tile_index],
                                               tile->level0_count,
                                               SAMPLE_CAPTURE_EDITOR_LEVEL0_BLOCK_FRAMES,
                                               frame,
                                               sample);
    sample_capture_editor_level_accumulate(g_sample_capture_editor_level1[tile_index],
                                               tile->level1_count,
                                               SAMPLE_CAPTURE_EDITOR_LEVEL1_BLOCK_FRAMES,
                                               frame,
                                               sample);
    sample_capture_editor_level_accumulate(g_sample_capture_editor_level2[tile_index],
                                               tile->level2_count,
                                               SAMPLE_CAPTURE_EDITOR_LEVEL2_BLOCK_FRAMES,
                                               frame,
                                               sample);
}

static uint8_t sample_capture_editor_cache_path_matches(void)
{
    return (uint8_t)((g_sample_capture.state.temp_path[0] != '\0')
            && (strncmp(g_sample_capture_editor_cache.path,
                        g_sample_capture.state.temp_path,
                        SAMPLE_CAPTURE_PATH_MAX) == 0));
}

static uint32_t sample_capture_editor_tile_end(const sample_capture_editor_audio_tile_t *tile)
{
    if(tile == 0)
    {
        return 0U;
    }
    return tile->start_frame + tile->frame_count;
}

static int8_t sample_capture_editor_find_ready_tile(uint32_t frame)
{
    if(sample_capture_editor_cache_path_matches() == 0U)
    {
        return -1;
    }
    for(uint8_t i = 0U; i < SAMPLE_CAPTURE_EDITOR_TILE_COUNT; ++i)
    {
        const sample_capture_editor_audio_tile_t *const tile = &g_sample_capture_editor_cache.tiles[i];
        if((tile->state == SAMPLE_CAPTURE_EDITOR_CACHE_READY)
                && (frame >= tile->start_frame)
                && (frame < sample_capture_editor_tile_end(tile)))
        {
            return (int8_t)i;
        }
    }
    return -1;
}

static int8_t sample_capture_editor_find_any_tile(uint32_t tile_start)
{
    if(sample_capture_editor_cache_path_matches() == 0U)
    {
        return -1;
    }
    for(uint8_t i = 0U; i < SAMPLE_CAPTURE_EDITOR_TILE_COUNT; ++i)
    {
        const sample_capture_editor_audio_tile_t *const tile = &g_sample_capture_editor_cache.tiles[i];
        if((tile->state != SAMPLE_CAPTURE_EDITOR_CACHE_EMPTY)
                && (tile->state != SAMPLE_CAPTURE_EDITOR_CACHE_STALE)
                && (tile->start_frame == tile_start))
        {
            return (int8_t)i;
        }
    }
    return -1;
}

static uint8_t sample_capture_editor_cache_covers(uint32_t start_frame,
                                                  uint32_t frame_count)
{
    if((sample_capture_editor_cache_path_matches() == 0U) || (frame_count == 0U))
    {
        return 0U;
    }
    uint32_t cursor = start_frame;
    const uint32_t end_frame = start_frame + frame_count;
    if(end_frame < start_frame)
    {
        return 0U;
    }
    while(cursor < end_frame)
    {
        const int8_t idx = sample_capture_editor_find_ready_tile(cursor);
        if(idx < 0)
        {
            return 0U;
        }
        const uint32_t tile_end = sample_capture_editor_tile_end(&g_sample_capture_editor_cache.tiles[(uint8_t)idx]);
        if(tile_end <= cursor)
        {
            return 0U;
        }
        cursor = (tile_end < end_frame) ? tile_end : end_frame;
    }
    return 1U;
}

static uint32_t sample_capture_editor_align_tile_start(uint32_t frame)
{
    return (frame / SAMPLE_CAPTURE_EDITOR_TILE_FRAMES) * SAMPLE_CAPTURE_EDITOR_TILE_FRAMES;
}

static uint8_t sample_capture_editor_tile_start_in_list(uint32_t tile_start,
                                                        const uint32_t *list,
                                                        uint8_t count)
{
    for(uint8_t i = 0U; i < count; ++i)
    {
        if(list[i] == tile_start)
        {
            return 1U;
        }
    }
    return 0U;
}

static void sample_capture_editor_add_tile_request(uint32_t tile_start,
                                                   uint32_t *list,
                                                   uint8_t *count)
{
    if((list == 0) || (count == 0) || (*count >= SAMPLE_CAPTURE_EDITOR_TILE_COUNT)
            || (tile_start >= g_sample_capture.state.recorded_frames)
            || (sample_capture_editor_tile_start_in_list(tile_start, list, *count) != 0U))
    {
        return;
    }
    list[*count] = tile_start;
    (*count)++;
}

static int8_t sample_capture_editor_pick_tile_slot(uint32_t tile_start, uint32_t focus);

static uint8_t sample_capture_editor_queue_tile_limited(uint32_t tile_start,
                                                        uint32_t focus,
                                                        uint8_t *queued_count)
{
    if((queued_count != 0) && (*queued_count >= SAMPLE_CAPTURE_EDITOR_TILE_REQUEST_MAX))
    {
        return 0U;
    }
    if(sample_capture_editor_find_any_tile(tile_start) >= 0)
    {
        return 0U;
    }
    const int8_t slot = sample_capture_editor_pick_tile_slot(tile_start, focus);
    if(slot < 0)
    {
        return 0U;
    }
    sample_capture_editor_audio_tile_t *const tile = &g_sample_capture_editor_cache.tiles[(uint8_t)slot];
    tile->state = SAMPLE_CAPTURE_EDITOR_CACHE_LOADING;
    tile->start_frame = tile_start;
    tile->frame_count = g_sample_capture.state.recorded_frames - tile_start;
    if(tile->frame_count > SAMPLE_CAPTURE_EDITOR_TILE_FRAMES)
    {
        tile->frame_count = SAMPLE_CAPTURE_EDITOR_TILE_FRAMES;
    }
    tile->load_next_frame = 0U;
    tile->generation = ++g_sample_capture_editor_cache.generation;
    sample_capture_editor_tile_levels_begin((uint8_t)slot, tile->frame_count);
    if(queued_count != 0)
    {
        (*queued_count)++;
    }
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    g_sample_capture_debug.cache_request_count++;
    sample_capture_debug_log("OLD_AUDIO_TILE_REQ slot=%u start=%lu frames=%lu focus=%lu\r\n",
                             (unsigned)(uint8_t)slot,
                             (unsigned long)tile->start_frame,
                             (unsigned long)tile->frame_count,
                             (unsigned long)focus);
#endif
    return 1U;
}

static uint32_t sample_capture_editor_distance_to_focus(uint32_t tile_start, uint32_t focus)
{
    if(focus < tile_start)
    {
        return tile_start - focus;
    }
    const uint32_t tile_end = tile_start + SAMPLE_CAPTURE_EDITOR_TILE_FRAMES;
    if(focus >= tile_end)
    {
        return focus - tile_end + 1U;
    }
    return 0U;
}

static int8_t sample_capture_editor_pick_tile_slot(uint32_t tile_start, uint32_t focus)
{
    int8_t candidate = -1;
    for(uint8_t i = 0U; i < SAMPLE_CAPTURE_EDITOR_TILE_COUNT; ++i)
    {
        const sample_capture_editor_audio_tile_t *const tile = &g_sample_capture_editor_cache.tiles[i];
        if((tile->state == SAMPLE_CAPTURE_EDITOR_CACHE_EMPTY)
                || (tile->state == SAMPLE_CAPTURE_EDITOR_CACHE_STALE))
        {
            return (int8_t)i;
        }
    }

    uint32_t worst_distance = 0U;
    for(uint8_t i = 0U; i < SAMPLE_CAPTURE_EDITOR_TILE_COUNT; ++i)
    {
        const sample_capture_editor_audio_tile_t *const tile = &g_sample_capture_editor_cache.tiles[i];
        if(tile->state == SAMPLE_CAPTURE_EDITOR_CACHE_LOADING)
        {
            continue;
        }
        const uint32_t distance = sample_capture_editor_distance_to_focus(tile->start_frame, focus);
        if((candidate < 0) || (distance > worst_distance))
        {
            candidate = (int8_t)i;
            worst_distance = distance;
        }
    }
    (void)tile_start;
    return candidate;
}

static void sample_capture_editor_cache_request(uint32_t view_start_frame,
                                                uint32_t view_frames)
{
    if((g_sample_capture.state.take_valid == 0U)
            || (g_sample_capture.state.temp_path[0] == '\0')
            || (g_sample_capture.state.recorded_frames == 0U)
            || (view_frames == 0U))
    {
        return;
    }

    if(sample_capture_editor_cache_path_matches() == 0U)
    {
        sample_capture_editor_cache_reset();
        sample_capture_copy_path(g_sample_capture_editor_cache.path,
                                 g_sample_capture.state.temp_path);
    }

    const uint32_t view_end = view_start_frame + view_frames;
    uint32_t focus = view_start_frame + (view_frames / 2U);
    if((view_end < view_start_frame) || (focus >= g_sample_capture.state.recorded_frames))
    {
        focus = g_sample_capture.state.recorded_frames - 1U;
    }
    g_sample_capture_editor_cache.focus_frame = focus;
    g_sample_capture_editor_cache.last_view_start_frame = view_start_frame;
    g_sample_capture_editor_cache.last_view_frames = view_frames;

    uint32_t required[SAMPLE_CAPTURE_EDITOR_TILE_COUNT];
    uint32_t keep[SAMPLE_CAPTURE_EDITOR_TILE_COUNT];
    uint32_t optional[SAMPLE_CAPTURE_EDITOR_TILE_COUNT];
    uint8_t required_count = 0U;
    uint8_t keep_count = 0U;
    uint8_t optional_count = 0U;
    uint8_t queued_count = 0U;

    uint32_t visible_tiles = (view_frames + SAMPLE_CAPTURE_EDITOR_TILE_FRAMES - 1U)
        / SAMPLE_CAPTURE_EDITOR_TILE_FRAMES;
    if(visible_tiles == 0U)
    {
        visible_tiles = 1U;
    }
    if(visible_tiles > SAMPLE_CAPTURE_EDITOR_TILE_COUNT)
    {
        visible_tiles = SAMPLE_CAPTURE_EDITOR_TILE_COUNT;
    }

    const uint32_t focus_tile = sample_capture_editor_align_tile_start(focus);
    sample_capture_editor_add_tile_request(focus_tile, required, &required_count);
    sample_capture_editor_add_tile_request(focus_tile, keep, &keep_count);

    uint32_t tile_start = sample_capture_editor_align_tile_start(view_start_frame);
    for(uint32_t i = 0U; (i < visible_tiles) && (required_count < SAMPLE_CAPTURE_EDITOR_TILE_COUNT); ++i)
    {
        sample_capture_editor_add_tile_request(tile_start, required, &required_count);
        sample_capture_editor_add_tile_request(tile_start, keep, &keep_count);
        if(tile_start > (0xFFFFFFFFUL - SAMPLE_CAPTURE_EDITOR_TILE_FRAMES))
        {
            break;
        }
        tile_start += SAMPLE_CAPTURE_EDITOR_TILE_FRAMES;
    }

    if(view_start_frame >= SAMPLE_CAPTURE_EDITOR_TILE_FRAMES)
    {
        const uint32_t left_tile = sample_capture_editor_align_tile_start(view_start_frame - 1U);
        sample_capture_editor_add_tile_request(left_tile, keep, &keep_count);
        sample_capture_editor_add_tile_request(left_tile, optional, &optional_count);
    }
    const uint32_t right_tile = sample_capture_editor_align_tile_start(view_end);
    sample_capture_editor_add_tile_request(right_tile, keep, &keep_count);
    sample_capture_editor_add_tile_request(right_tile, optional, &optional_count);
    if(focus_tile >= SAMPLE_CAPTURE_EDITOR_TILE_FRAMES)
    {
        sample_capture_editor_add_tile_request(focus_tile - SAMPLE_CAPTURE_EDITOR_TILE_FRAMES,
                                              keep,
                                              &keep_count);
    }
    sample_capture_editor_add_tile_request(focus_tile + SAMPLE_CAPTURE_EDITOR_TILE_FRAMES,
                                          keep,
                                          &keep_count);

    for(uint8_t radius = 2U; radius <= SAMPLE_CAPTURE_EDITOR_TILE_KEEP_RADIUS; ++radius)
    {
        const uint32_t delta = SAMPLE_CAPTURE_EDITOR_TILE_FRAMES * (uint32_t)radius;
        if(focus_tile >= delta)
        {
            sample_capture_editor_add_tile_request(focus_tile - delta, keep, &keep_count);
        }
        sample_capture_editor_add_tile_request(focus_tile + delta, keep, &keep_count);
    }

    for(uint8_t i = 0U; i < SAMPLE_CAPTURE_EDITOR_TILE_COUNT; ++i)
    {
        sample_capture_editor_audio_tile_t *const tile = &g_sample_capture_editor_cache.tiles[i];
        if((tile->state == SAMPLE_CAPTURE_EDITOR_CACHE_LOADING)
                && (sample_capture_editor_tile_start_in_list(tile->start_frame,
                                                            keep,
                                                            keep_count) == 0U))
        {
            tile->state = SAMPLE_CAPTURE_EDITOR_CACHE_STALE;
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
            sample_capture_debug_log("OLD_AUDIO_TILE_DROP slot=%u start=%lu\r\n",
                                     (unsigned)i,
                                     (unsigned long)tile->start_frame);
#endif
        }
    }

    for(uint8_t i = 0U; i < required_count; ++i)
    {
        (void)sample_capture_editor_queue_tile_limited(required[i], focus, &queued_count);
    }
    for(uint8_t i = 0U; i < optional_count; ++i)
    {
        (void)sample_capture_editor_queue_tile_limited(optional[i], focus, &queued_count);
    }
}

static uint8_t sample_capture_editor_cache_can_fill(uint8_t *out_reason)
{
    if(out_reason != 0)
    {
        *out_reason = 0U;
    }
    if((g_sample_capture_editor_cache.path[0] == '\0')
            || (g_sample_capture.state.take_valid == 0U)
            || (g_sample_capture.state.recording != 0U)
            || (g_sample_capture.state.armed_pending != 0U)
            || (sample_capture_editor_cache_path_matches() == 0U))
    {
        return 0U;
    }
    if(audio_recorder_is_active() != 0U)
    {
        if(out_reason != 0) { *out_reason = 1U; }
        return 0U;
    }
    if(sd_preview_is_active() != 0U)
    {
        if(out_reason != 0) { *out_reason = 3U; }
        return 0U;
    }
    if(pattern_load_is_pending() != 0U)
    {
        if(out_reason != 0) { *out_reason = 4U; }
        return 0U;
    }
    if(sample_cache_has_pending_sd_work() != 0U)
    {
        if(out_reason != 0) { *out_reason = 5U; }
        return 0U;
    }
    return 1U;
}

static int8_t sample_capture_editor_next_loading_tile(void)
{
    int8_t best = -1;
    uint32_t best_distance = 0xFFFFFFFFUL;
    for(uint8_t i = 0U; i < SAMPLE_CAPTURE_EDITOR_TILE_COUNT; ++i)
    {
        sample_capture_editor_audio_tile_t *const tile = &g_sample_capture_editor_cache.tiles[i];
        if(tile->state == SAMPLE_CAPTURE_EDITOR_CACHE_STALE)
        {
            tile->state = SAMPLE_CAPTURE_EDITOR_CACHE_EMPTY;
        }
        if(tile->state != SAMPLE_CAPTURE_EDITOR_CACHE_LOADING)
        {
            continue;
        }
        const uint32_t distance =
            sample_capture_editor_distance_to_focus(tile->start_frame,
                                                    g_sample_capture_editor_cache.focus_frame);
        if(distance < best_distance)
        {
            best = (int8_t)i;
            best_distance = distance;
        }
    }
    return best;
}

static void sample_capture_editor_cache_service(void)
{
    const int8_t load_idx = sample_capture_editor_next_loading_tile();
    if(load_idx < 0)
    {
        return;
    }

    uint8_t block_reason = 0U;
    if(sample_capture_editor_cache_can_fill(&block_reason) == 0U)
    {
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
        if(block_reason == 1U) { g_sample_capture_debug.cache_block_writer_count++; }
        else if(block_reason == 2U) { g_sample_capture_debug.cache_block_export_count++; }
        else if(block_reason == 3U) { g_sample_capture_debug.cache_block_preview_count++; }
        else if(block_reason == 4U) { g_sample_capture_debug.cache_block_pattern_count++; }
        else if(block_reason == 5U) { g_sample_capture_debug.cache_block_sample_count++; }
#endif
        return;
    }

    sample_capture_editor_audio_tile_t *const tile =
        &g_sample_capture_editor_cache.tiles[(uint8_t)load_idx];

#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    const uint32_t service_start_ms = HAL_GetTick();
    g_sample_capture_debug.fill_passes++;
    if(g_sample_capture_debug.fill_started_logged == 0U)
    {
        g_sample_capture_debug.fill_started_logged = 1U;
        g_sample_capture_debug.fill_start_ms = service_start_ms;
        sample_capture_debug_log("OLD_AUDIO_TILE_START slot=%u start=%lu frames=%lu focus=%lu\r\n",
                                 (unsigned)(uint8_t)load_idx,
                                 (unsigned long)tile->start_frame,
                                 (unsigned long)tile->frame_count,
                                 (unsigned long)g_sample_capture_editor_cache.focus_frame);
    }
#endif

    uint32_t frames_left = tile->frame_count - tile->load_next_frame;
    if(frames_left == 0U)
    {
        tile->state = SAMPLE_CAPTURE_EDITOR_CACHE_READY;
        return;
    }
    if(frames_left > SAMPLE_CAPTURE_EDITOR_TILE_BUILD_CHUNK_FRAMES)
    {
        frames_left = SAMPLE_CAPTURE_EDITOR_TILE_BUILD_CHUNK_FRAMES;
    }

    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_EDITOR_CACHE) == 0U)
    {
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
        g_sample_capture_debug.cache_gate_busy_count++;
#endif
        return;
    }

    FIL fp;
    uint8_t file_open = 0U;
    uint8_t ok = 0U;
    if(sd_access_fs_mount_if_needed() == 0U)
    {
        goto done;
    }
    if(f_open(&fp, g_sample_capture_editor_cache.path, FA_READ) != FR_OK)
    {
        goto done;
    }
    file_open = 1U;

    const uint32_t absolute_frame = tile->start_frame + tile->load_next_frame;
    if(f_lseek(&fp, SAMPLE_CAPTURE_WAV_DATA_OFFSET
            + (absolute_frame * AUDIO_RECORDER_BYTES_PER_FRAME)) != FR_OK)
    {
        goto done;
    }

    const uint32_t bytes_to_read = frames_left * AUDIO_RECORDER_BYTES_PER_FRAME;
    UINT br = 0U;
    if((f_read(&fp, g_sample_capture_detail_buf, bytes_to_read, &br) != FR_OK)
            || (br < AUDIO_RECORDER_BYTES_PER_FRAME))
    {
        goto done;
    }

    const uint32_t frames_read = br / AUDIO_RECORDER_BYTES_PER_FRAME;
    for(uint32_t i = 0U; i < frames_read; ++i)
    {
        const uint8_t *frame = &g_sample_capture_detail_buf[i * AUDIO_RECORDER_BYTES_PER_FRAME];
        const int16_t l = sample_capture_pcm24le_to_waveform_i16(frame);
        const int16_t r = sample_capture_pcm24le_to_waveform_i16(&frame[3]);
        const uint32_t rel = tile->load_next_frame + i;
        const int16_t mono = (int16_t)(((int32_t)l + (int32_t)r) / 2);
        g_sample_capture_editor_audio[(uint8_t)load_idx][rel] = mono;
        sample_capture_editor_tile_level_accumulate((uint8_t)load_idx, rel, mono);
    }

    tile->load_next_frame += frames_read;
    ok = 1U;
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    g_sample_capture_debug.cache_chunks++;
    g_sample_capture_debug.fill_chunks++;
#endif
    if((frames_read == 0U)
            || (tile->load_next_frame >= tile->frame_count))
    {
        tile->frame_count = tile->load_next_frame;
        tile->state = SAMPLE_CAPTURE_EDITOR_CACHE_READY;
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
        const uint32_t elapsed = HAL_GetTick() - g_sample_capture_debug.fill_start_ms;
        sample_capture_debug_log("OLD_AUDIO_TILE_DONE slot=%u start=%lu frames=%lu chunks=%lu passes=%lu gate_busy=%lu ms=%lu\r\n",
                                 (unsigned)(uint8_t)load_idx,
                                 (unsigned long)tile->start_frame,
                                 (unsigned long)tile->frame_count,
                                 (unsigned long)g_sample_capture_debug.fill_chunks,
                                 (unsigned long)g_sample_capture_debug.fill_passes,
                                 (unsigned long)g_sample_capture_debug.cache_gate_busy_count,
                                 (unsigned long)elapsed);
        g_sample_capture_debug.fill_chunks = 0U;
        g_sample_capture_debug.fill_passes = 0U;
        g_sample_capture_debug.cache_gate_busy_count = 0U;
        g_sample_capture_debug.fill_started_logged = 0U;
#endif
    }

done:
    if(file_open != 0U)
    {
        (void)f_close(&fp);
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_EDITOR_CACHE);
    if(ok == 0U)
    {
        tile->state = SAMPLE_CAPTURE_EDITOR_CACHE_EMPTY;
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
        sample_capture_debug_log("OLD_AUDIO_TILE_ERR slot=%u start=%lu loaded=%lu req=%lu\r\n",
                                 (unsigned)(uint8_t)load_idx,
                                 (unsigned long)tile->start_frame,
                                 (unsigned long)tile->load_next_frame,
                                 (unsigned long)tile->frame_count);
#endif
    }
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    const uint32_t service_ms = HAL_GetTick() - service_start_ms;
    g_sample_capture_debug.fill_last_ms = service_ms;
    if(service_ms > g_sample_capture_debug.fill_max_ms)
    {
        g_sample_capture_debug.fill_max_ms = service_ms;
    }
#endif
}

static int16_t sample_capture_editor_sample_at(uint32_t frame)
{
    const int8_t idx = sample_capture_editor_find_ready_tile(frame);
    if(idx < 0)
    {
        return 0;
    }
    const sample_capture_editor_audio_tile_t *const tile =
        &g_sample_capture_editor_cache.tiles[(uint8_t)idx];
    return g_sample_capture_editor_audio[(uint8_t)idx][frame - tile->start_frame];
}

static void sample_capture_editor_range_from_level(uint8_t tile_index,
                                                   uint32_t rel_start,
                                                   uint32_t rel_end,
                                                   uint32_t block_frames,
                                                   const sample_capture_editor_level_point_t *level,
                                                   uint16_t level_count,
                                                   uint8_t *seen,
                                                   int16_t *min_v,
                                                   int16_t *max_v,
                                                   int16_t *first_v,
                                                   int16_t *last_v)
{
    if((level == 0) || (seen == 0) || (min_v == 0) || (max_v == 0)
            || (first_v == 0) || (last_v == 0) || (rel_end <= rel_start)
            || (block_frames == 0U) || (level_count == 0U)
            || (tile_index >= SAMPLE_CAPTURE_EDITOR_TILE_COUNT))
    {
        return;
    }
    uint32_t idx0 = rel_start / block_frames;
    uint32_t idx1 = (rel_end + block_frames - 1U) / block_frames;
    if(idx0 >= level_count)
    {
        return;
    }
    if(idx1 > level_count)
    {
        idx1 = level_count;
    }
    for(uint32_t idx = idx0; idx < idx1; ++idx)
    {
        const sample_capture_editor_level_point_t *const p = &level[idx];
        if(*seen == 0U)
        {
            *min_v = p->min;
            *max_v = p->max;
            *first_v = p->first;
            *last_v = p->last;
            *seen = 1U;
        }
        else
        {
            if(p->min < *min_v) { *min_v = p->min; }
            if(p->max > *max_v) { *max_v = p->max; }
            *last_v = p->last;
        }
    }
}

static uint8_t sample_capture_editor_accumulate_range(uint32_t start_frame,
                                                      uint32_t end_frame,
                                                      uint32_t samples_per_point,
                                                      int16_t *min_v,
                                                      int16_t *max_v,
                                                      int16_t *first_v,
                                                      int16_t *last_v)
{
    uint8_t seen = 0U;
    uint32_t cursor = start_frame;
    while(cursor < end_frame)
    {
        const int8_t idx = sample_capture_editor_find_ready_tile(cursor);
        if(idx < 0)
        {
            return 0U;
        }
        const sample_capture_editor_audio_tile_t *const tile =
            &g_sample_capture_editor_cache.tiles[(uint8_t)idx];
        uint32_t span_end = sample_capture_editor_tile_end(tile);
        if(span_end > end_frame)
        {
            span_end = end_frame;
        }
        const uint32_t rel_start = cursor - tile->start_frame;
        const uint32_t rel_end = span_end - tile->start_frame;
        if(samples_per_point >= SAMPLE_CAPTURE_EDITOR_LEVEL2_BLOCK_FRAMES)
        {
            sample_capture_editor_range_from_level((uint8_t)idx, rel_start, rel_end,
                                                   SAMPLE_CAPTURE_EDITOR_LEVEL2_BLOCK_FRAMES,
                                                   g_sample_capture_editor_level2[(uint8_t)idx],
                                                   tile->level2_count,
                                                   &seen, min_v, max_v, first_v, last_v);
        }
        else if(samples_per_point >= SAMPLE_CAPTURE_EDITOR_LEVEL1_BLOCK_FRAMES)
        {
            sample_capture_editor_range_from_level((uint8_t)idx, rel_start, rel_end,
                                                   SAMPLE_CAPTURE_EDITOR_LEVEL1_BLOCK_FRAMES,
                                                   g_sample_capture_editor_level1[(uint8_t)idx],
                                                   tile->level1_count,
                                                   &seen, min_v, max_v, first_v, last_v);
        }
        else if(samples_per_point >= SAMPLE_CAPTURE_EDITOR_LEVEL0_BLOCK_FRAMES)
        {
            sample_capture_editor_range_from_level((uint8_t)idx, rel_start, rel_end,
                                                   SAMPLE_CAPTURE_EDITOR_LEVEL0_BLOCK_FRAMES,
                                                   g_sample_capture_editor_level0[(uint8_t)idx],
                                                   tile->level0_count,
                                                   &seen, min_v, max_v, first_v, last_v);
        }
        else
        {
            for(uint32_t frame = cursor; frame < span_end; ++frame)
            {
                const int16_t sample = sample_capture_editor_sample_at(frame);
                if(seen == 0U)
                {
                    *min_v = sample;
                    *max_v = sample;
                    *first_v = sample;
                    *last_v = sample;
                    seen = 1U;
                }
                else
                {
                    if(sample < *min_v) { *min_v = sample; }
                    if(sample > *max_v) { *max_v = sample; }
                    *last_v = sample;
                }
            }
        }
        cursor = span_end;
    }
    return seen;
}

static void sample_capture_line_publish_from_editor_cache(uint32_t start_frame,
                                                          uint32_t frame_count,
                                                          uint16_t points)
{
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    const uint32_t eline_start_ms = HAL_GetTick();
    const char *eline_src = "OLD_AUDIO_TILE";
#endif
    if((points == 0U) || (points > SAMPLE_CAPTURE_LINE_POINTS)
            || (sample_capture_editor_cache_covers(start_frame, frame_count) == 0U))
    {
        return;
    }

    uint16_t line_peak = 0U;
    const uint32_t samples_per_point = (frame_count + (uint32_t)points - 1U) / (uint32_t)points;

    g_sample_capture_line_hot.valid = 1U;
    g_sample_capture_line_hot.start_frame = start_frame;
    g_sample_capture_line_hot.frames = frame_count;
    g_sample_capture_line_hot.count = points;
    g_sample_capture_line_hot.requested = 0U;
    g_sample_capture_line_hot.building = 0U;
    g_sample_capture_line_hot.build_next_frame = 0U;

    for(uint16_t point = 0U; point < points; ++point)
    {
        uint32_t frame0 = (uint32_t)(((uint64_t)point * (uint64_t)frame_count)
            / (uint64_t)points);
        uint32_t frame1 = (uint32_t)(((uint64_t)(point + 1U) * (uint64_t)frame_count)
            / (uint64_t)points);
        if(frame1 <= frame0)
        {
            frame1 = frame0 + 1U;
        }
        frame0 += start_frame;
        frame1 += start_frame;
        int16_t min_v = 0;
        int16_t max_v = 0;
        int16_t first_v = 0;
        int16_t last_v = 0;
        if(sample_capture_editor_accumulate_range(frame0, frame1, samples_per_point,
                                                  &min_v, &max_v, &first_v, &last_v) == 0U)
        {
            g_sample_capture_line_hot.valid = 0U;
            return;
        }
        const int16_t v = sample_capture_line_point_from_values(point,
                                                                points,
                                                                min_v,
                                                                max_v,
                                                                first_v,
                                                                last_v);
        g_sample_capture_line_hot.points[point] = v;
        const uint16_t abs_v = sample_capture_abs_i16(v);
        if(abs_v > line_peak)
        {
            line_peak = abs_v;
        }
    }
    g_sample_capture_line_hot.peak = line_peak;
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    const uint32_t eline_ms = HAL_GetTick() - eline_start_ms;
    g_sample_capture_debug.eline_count++;
    g_sample_capture_debug.eline_last_ms = eline_ms;
    if(eline_ms > g_sample_capture_debug.eline_max_ms)
    {
        g_sample_capture_debug.eline_max_ms = eline_ms;
    }
    if(eline_ms != 0U)
    {
        sample_capture_debug_log("ELINE src=%s points=%u vf=%lu spp=%lu ms=%lu peak=%u\r\n",
                                 eline_src,
                                 (unsigned)points,
                                 (unsigned long)frame_count,
                                 (unsigned long)samples_per_point,
                                 (unsigned long)eline_ms,
                                 (unsigned)line_peak);
    }
#endif
}

static uint8_t sample_capture_detail_can_build(void)
{
    if((g_sample_capture.state.take_valid == 0U)
            || (g_sample_capture.state.recording != 0U)
            || (g_sample_capture.state.armed_pending != 0U)
            || (g_sample_capture.state.temp_path[0] == '\0')
            || (g_sample_capture.detail_request_frames == 0U)
            || (g_sample_capture.detail_request_columns == 0U)
            || (g_sample_capture.detail_request_columns > SAMPLE_CAPTURE_DETAIL_POINTS))
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

static uint8_t sample_capture_line_can_build(void)
{
    if((g_sample_capture.state.take_valid == 0U)
            || (g_sample_capture.state.recording != 0U)
            || (g_sample_capture.state.armed_pending != 0U)
            || (g_sample_capture.state.temp_path[0] == '\0')
            || (g_sample_capture_line_hot.request_frames == 0U)
            || (g_sample_capture_line_hot.request_points == 0U)
            || (g_sample_capture_line_hot.request_points > SAMPLE_CAPTURE_LINE_POINTS))
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

static void sample_capture_detail_begin_build(void)
{
    g_sample_capture.state.detail_valid = 0U;
    g_sample_capture.state.detail_start_frame = g_sample_capture.detail_request_start_frame;
    g_sample_capture.state.detail_frames = g_sample_capture.detail_request_frames;
    g_sample_capture.state.detail_count = g_sample_capture.detail_request_columns;
    memset(g_sample_capture.state.detail, 0, sizeof(g_sample_capture.state.detail));
    memset(g_sample_capture.detail_seen, 0, sizeof(g_sample_capture.detail_seen));
    g_sample_capture.detail_build_next_frame = 0U;
    g_sample_capture.detail_building = 1U;
}

static void sample_capture_detail_finish_build(void)
{
    for(uint16_t col = 0U; col < g_sample_capture.state.detail_count; ++col)
    {
        if(g_sample_capture.detail_seen[col] == 0U)
        {
            g_sample_capture.state.detail[col].min = 0;
            g_sample_capture.state.detail[col].max = 0;
            g_sample_capture.state.detail[col].first = 0;
            g_sample_capture.state.detail[col].last = 0;
        }
    }
    g_sample_capture.state.detail_valid = 1U;
    g_sample_capture.detail_building = 0U;
    g_sample_capture.detail_requested = 0U;
}

static void sample_capture_detail_service(void)
{
    if(g_sample_capture.detail_requested == 0U)
    {
        return;
    }
    if(sample_capture_detail_can_build() == 0U)
    {
        return;
    }
    if(g_sample_capture.detail_building == 0U)
    {
        sample_capture_detail_begin_build();
    }

    uint32_t frames_left = g_sample_capture.detail_request_frames - g_sample_capture.detail_build_next_frame;
    if(frames_left == 0U)
    {
        sample_capture_detail_finish_build();
        return;
    }
    if(frames_left > SAMPLE_CAPTURE_DETAIL_BUILD_CHUNK_FRAMES)
    {
        frames_left = SAMPLE_CAPTURE_DETAIL_BUILD_CHUNK_FRAMES;
    }

    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PREVIEW) == 0U)
    {
        return;
    }

    FIL fp;
    uint8_t file_open = 0U;
    uint8_t ok = 0U;
    if(sd_access_fs_mount_if_needed() == 0U)
    {
        goto done;
    }
    if(f_open(&fp, g_sample_capture.state.temp_path, FA_READ) != FR_OK)
    {
        goto done;
    }
    file_open = 1U;

    const uint32_t absolute_frame = g_sample_capture.detail_request_start_frame
        + g_sample_capture.detail_build_next_frame;
    if(f_lseek(&fp, SAMPLE_CAPTURE_WAV_DATA_OFFSET
            + (absolute_frame * AUDIO_RECORDER_BYTES_PER_FRAME)) != FR_OK)
    {
        goto done;
    }

    const uint32_t bytes_to_read = frames_left * AUDIO_RECORDER_BYTES_PER_FRAME;
    UINT br = 0U;
    if((f_read(&fp, g_sample_capture_detail_buf, bytes_to_read, &br) != FR_OK)
            || (br < AUDIO_RECORDER_BYTES_PER_FRAME))
    {
        goto done;
    }

    const uint32_t frames_read = br / AUDIO_RECORDER_BYTES_PER_FRAME;
    for(uint32_t i = 0U; i < frames_read; ++i)
    {
        const uint32_t rel_frame = g_sample_capture.detail_build_next_frame + i;
        uint32_t col = (uint32_t)(((uint64_t)rel_frame
                * (uint64_t)g_sample_capture.detail_request_columns)
            / (uint64_t)g_sample_capture.detail_request_frames);
        if(col >= g_sample_capture.detail_request_columns)
        {
            col = g_sample_capture.detail_request_columns - 1U;
        }

        const uint8_t *frame = &g_sample_capture_detail_buf[i * AUDIO_RECORDER_BYTES_PER_FRAME];
        const int16_t l = sample_capture_pcm24le_to_waveform_i16(frame);
        const int16_t r = sample_capture_pcm24le_to_waveform_i16(&frame[3]);
        const int16_t sample_min = (l < r) ? l : r;
        const int16_t sample_max = (l > r) ? l : r;
        const int16_t sample_mid = (int16_t)(((int32_t)l + (int32_t)r) / 2);
        if(g_sample_capture.detail_seen[col] == 0U)
        {
            g_sample_capture.state.detail[col].min = sample_min;
            g_sample_capture.state.detail[col].max = sample_max;
            g_sample_capture.state.detail[col].first = sample_mid;
            g_sample_capture.state.detail[col].last = sample_mid;
            g_sample_capture.detail_seen[col] = 1U;
        }
        else
        {
            if(sample_min < g_sample_capture.state.detail[col].min)
            {
                g_sample_capture.state.detail[col].min = sample_min;
            }
            if(sample_max > g_sample_capture.state.detail[col].max)
            {
                g_sample_capture.state.detail[col].max = sample_max;
            }
            g_sample_capture.state.detail[col].last = sample_mid;
        }
    }

    g_sample_capture.detail_build_next_frame += frames_read;
    ok = 1U;
    if((frames_read == 0U)
            || (g_sample_capture.detail_build_next_frame >= g_sample_capture.detail_request_frames))
    {
        sample_capture_detail_finish_build();
    }

done:
    if(file_open != 0U)
    {
        (void)f_close(&fp);
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
    if(ok == 0U)
    {
        g_sample_capture.detail_building = 0U;
    }
}

static int16_t sample_capture_line_sample_at(const int16_t *frames,
                                             uint32_t frame_count,
                                             uint32_t point,
                                             uint32_t point_count)
{
    if((frames == 0) || (frame_count == 0U) || (point_count == 0U))
    {
        return 0;
    }
    if(frame_count == 1U || point_count == 1U)
    {
        return frames[0];
    }

    const uint64_t pos_q16 = ((uint64_t)point * (uint64_t)(frame_count - 1U) * 65536ULL)
        / (uint64_t)(point_count - 1U);
    uint32_t idx = (uint32_t)(pos_q16 >> 16);
    uint32_t frac = (uint32_t)(pos_q16 & 0xFFFFULL);
    if(idx >= (frame_count - 1U))
    {
        return frames[frame_count - 1U];
    }

    const int32_t a = frames[idx];
    const int32_t b = frames[idx + 1U];
    return (int16_t)(a + (((b - a) * (int32_t)frac) >> 16));
}

static void sample_capture_line_begin_build(void)
{
    g_sample_capture_line_hot.building = 1U;
    g_sample_capture_line_hot.requested = 0U;
    g_sample_capture_line_hot.build_start_frame = g_sample_capture_line_hot.request_start_frame;
    g_sample_capture_line_hot.build_frames = g_sample_capture_line_hot.request_frames;
    g_sample_capture_line_hot.build_points = g_sample_capture_line_hot.request_points;
    g_sample_capture_line_hot.build_next_frame = 0U;
    memset(g_sample_capture_line_hot.seen, 0, sizeof(g_sample_capture_line_hot.seen));
    memset(g_sample_capture_line_hot.min, 0, sizeof(g_sample_capture_line_hot.min));
    memset(g_sample_capture_line_hot.max, 0, sizeof(g_sample_capture_line_hot.max));
    memset(g_sample_capture_line_hot.first, 0, sizeof(g_sample_capture_line_hot.first));
    memset(g_sample_capture_line_hot.last, 0, sizeof(g_sample_capture_line_hot.last));
}

static void sample_capture_line_accumulate_frame(uint32_t rel_frame, int16_t sample)
{
    if((g_sample_capture_line_hot.build_frames == 0U)
            || (g_sample_capture_line_hot.build_points == 0U))
    {
        return;
    }

    uint32_t point = (uint32_t)(((uint64_t)rel_frame
            * (uint64_t)g_sample_capture_line_hot.build_points)
        / (uint64_t)g_sample_capture_line_hot.build_frames);
    if(point >= g_sample_capture_line_hot.build_points)
    {
        point = g_sample_capture_line_hot.build_points - 1U;
    }

    if(g_sample_capture_line_hot.seen[point] == 0U)
    {
        g_sample_capture_line_hot.min[point] = sample;
        g_sample_capture_line_hot.max[point] = sample;
        g_sample_capture_line_hot.first[point] = sample;
        g_sample_capture_line_hot.seen[point] = 1U;
    }
    else
    {
        if(sample < g_sample_capture_line_hot.min[point])
        {
            g_sample_capture_line_hot.min[point] = sample;
        }
        if(sample > g_sample_capture_line_hot.max[point])
        {
            g_sample_capture_line_hot.max[point] = sample;
        }
    }
    g_sample_capture_line_hot.last[point] = sample;
}

static int16_t sample_capture_line_point_from_accum(uint16_t point)
{
    if((point >= g_sample_capture_line_hot.build_points)
            || (g_sample_capture_line_hot.seen[point] == 0U))
    {
        return 0;
    }

    const int16_t min_v = g_sample_capture_line_hot.min[point];
    const int16_t max_v = g_sample_capture_line_hot.max[point];
    const int16_t last_v = g_sample_capture_line_hot.last[point];
    return sample_capture_line_point_from_values(point,
                                                 g_sample_capture_line_hot.build_points,
                                                 min_v,
                                                 max_v,
                                                 g_sample_capture_line_hot.first[point],
                                                 last_v);
}

static void sample_capture_line_finish_build(void)
{
    uint16_t line_peak = 0U;
    for(uint16_t point = 0U; point < g_sample_capture_line_hot.build_points; ++point)
    {
        const int16_t point_v = sample_capture_line_point_from_accum(point);
        g_sample_capture_line_hot.points[point] = point_v;
        const uint16_t abs_v = sample_capture_abs_i16(point_v);
        if(abs_v > line_peak)
        {
            line_peak = abs_v;
        }
    }
    g_sample_capture_line_hot.valid = 1U;
    g_sample_capture_line_hot.start_frame = g_sample_capture_line_hot.build_start_frame;
    g_sample_capture_line_hot.frames = g_sample_capture_line_hot.build_frames;
    g_sample_capture_line_hot.count = g_sample_capture_line_hot.build_points;
    g_sample_capture_line_hot.peak = line_peak;
    g_sample_capture_line_hot.building = 0U;
    g_sample_capture_line_hot.build_next_frame = 0U;
}

static void sample_capture_line_service(void)
{
    if((g_sample_capture_line_hot.requested == 0U)
            && (g_sample_capture_line_hot.building == 0U))
    {
        return;
    }
    if(sample_capture_line_can_build() == 0U)
    {
        return;
    }

    if(g_sample_capture_line_hot.building == 0U)
    {
        if(g_sample_capture_line_hot.request_settle_ticks != 0U)
        {
            g_sample_capture_line_hot.request_settle_ticks--;
            return;
        }
        sample_capture_line_begin_build();
    }

    if(g_sample_capture_line_hot.build_frames <= SAMPLE_CAPTURE_LINE_MAX_SOURCE_FRAMES)
    {
        if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PREVIEW) == 0U)
        {
            return;
        }

        FIL fp;
        uint8_t file_open = 0U;
        uint8_t ok = 0U;
        if(sd_access_fs_mount_if_needed() == 0U)
        {
            goto short_done;
        }
        if(f_open(&fp, g_sample_capture.state.temp_path, FA_READ) != FR_OK)
        {
            goto short_done;
        }
        file_open = 1U;

        if(f_lseek(&fp, SAMPLE_CAPTURE_WAV_DATA_OFFSET
                + (g_sample_capture_line_hot.build_start_frame * AUDIO_RECORDER_BYTES_PER_FRAME)) != FR_OK)
        {
            goto short_done;
        }

        const uint32_t bytes_to_read =
            g_sample_capture_line_hot.build_frames * AUDIO_RECORDER_BYTES_PER_FRAME;
        UINT br = 0U;
        if((f_read(&fp, g_sample_capture_detail_buf, bytes_to_read, &br) != FR_OK)
                || (br < AUDIO_RECORDER_BYTES_PER_FRAME))
        {
            goto short_done;
        }

        const uint32_t frames_read = br / AUDIO_RECORDER_BYTES_PER_FRAME;
        if(frames_read == 0U)
        {
            goto short_done;
        }

        for(uint32_t i = 0U; i < frames_read; ++i)
        {
            const uint8_t *frame = &g_sample_capture_detail_buf[i * AUDIO_RECORDER_BYTES_PER_FRAME];
            const int16_t l = sample_capture_pcm24le_to_waveform_i16(frame);
            const int16_t r = sample_capture_pcm24le_to_waveform_i16(&frame[3]);
            g_sample_capture_line_source[i] = (int16_t)(((int32_t)l + (int32_t)r) / 2);
        }

        uint16_t line_peak = 0U;
        for(uint16_t point = 0U; point < g_sample_capture_line_hot.build_points; ++point)
        {
            g_sample_capture_line_hot.points[point] =
                sample_capture_line_sample_at(g_sample_capture_line_source,
                                              frames_read,
                                              point,
                                              g_sample_capture_line_hot.build_points);
            const uint16_t abs_v = sample_capture_abs_i16(g_sample_capture_line_hot.points[point]);
            if(abs_v > line_peak)
            {
                line_peak = abs_v;
            }
        }
        g_sample_capture_line_hot.valid = 1U;
        g_sample_capture_line_hot.start_frame = g_sample_capture_line_hot.build_start_frame;
        g_sample_capture_line_hot.frames = frames_read;
        g_sample_capture_line_hot.count = g_sample_capture_line_hot.build_points;
        g_sample_capture_line_hot.peak = line_peak;
        g_sample_capture_line_hot.building = 0U;
        g_sample_capture_line_hot.build_next_frame = 0U;
        ok = 1U;

short_done:
        if(file_open != 0U)
        {
            (void)f_close(&fp);
        }
        sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
        if(ok == 0U)
        {
            g_sample_capture_line_hot.building = 0U;
        }
        return;
    }

    uint32_t frames_left = g_sample_capture_line_hot.build_frames
        - g_sample_capture_line_hot.build_next_frame;
    if(frames_left == 0U)
    {
        sample_capture_line_finish_build();
        return;
    }
    if(frames_left > SAMPLE_CAPTURE_LINE_BUILD_CHUNK_FRAMES)
    {
        frames_left = SAMPLE_CAPTURE_LINE_BUILD_CHUNK_FRAMES;
    }

    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PREVIEW) == 0U)
    {
        return;
    }

    FIL fp;
    uint8_t file_open = 0U;
    uint8_t ok = 0U;
    if(sd_access_fs_mount_if_needed() == 0U)
    {
        goto done;
    }
    if(f_open(&fp, g_sample_capture.state.temp_path, FA_READ) != FR_OK)
    {
        goto done;
    }
    file_open = 1U;

    const uint32_t absolute_frame = g_sample_capture_line_hot.build_start_frame
        + g_sample_capture_line_hot.build_next_frame;
    if(f_lseek(&fp, SAMPLE_CAPTURE_WAV_DATA_OFFSET
            + (absolute_frame * AUDIO_RECORDER_BYTES_PER_FRAME)) != FR_OK)
    {
        goto done;
    }

    const uint32_t bytes_to_read = frames_left * AUDIO_RECORDER_BYTES_PER_FRAME;
    UINT br = 0U;
    if((f_read(&fp, g_sample_capture_detail_buf, bytes_to_read, &br) != FR_OK)
            || (br < AUDIO_RECORDER_BYTES_PER_FRAME))
    {
        goto done;
    }

    const uint32_t frames_read = br / AUDIO_RECORDER_BYTES_PER_FRAME;
    for(uint32_t i = 0U; i < frames_read; ++i)
    {
        const uint8_t *frame = &g_sample_capture_detail_buf[i * AUDIO_RECORDER_BYTES_PER_FRAME];
        const int16_t l = sample_capture_pcm24le_to_waveform_i16(frame);
        const int16_t r = sample_capture_pcm24le_to_waveform_i16(&frame[3]);
        const int16_t sample = (int16_t)(((int32_t)l + (int32_t)r) / 2);
        sample_capture_line_accumulate_frame(g_sample_capture_line_hot.build_next_frame + i,
                                             sample);
    }
    g_sample_capture_line_hot.build_next_frame += frames_read;
    ok = 1U;
    if((frames_read == 0U)
            || (g_sample_capture_line_hot.build_next_frame >= g_sample_capture_line_hot.build_frames))
    {
        sample_capture_line_finish_build();
    }

done:
    if(file_open != 0U)
    {
        (void)f_close(&fp);
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
    if(ok == 0U)
    {
        g_sample_capture_line_hot.building = 0U;
    }
}

