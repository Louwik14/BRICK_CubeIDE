uint32_t sample_capture_model_visible_frames_for_zoom(uint32_t recorded_frames, uint8_t zoom)
{
    static uint32_t cached_recorded_frames;
    static uint32_t cached_visible_frames;
    static uint8_t cached_zoom;
    if((recorded_frames == cached_recorded_frames) && (zoom == cached_zoom)
            && (cached_visible_frames != 0U))
    {
        return cached_visible_frames;
    }
    cached_recorded_frames = recorded_frames;
    cached_zoom = zoom;
    if(recorded_frames == 0U)
    {
        cached_visible_frames = 1U;
        return cached_visible_frames;
    }
    const uint32_t min_frames =
        (recorded_frames < SAMPLE_CAPTURE_EDIT_MIN_VISIBLE_FRAMES)
            ? recorded_frames
            : SAMPLE_CAPTURE_EDIT_MIN_VISIBLE_FRAMES;
    if((zoom == 0U) || (recorded_frames <= min_frames))
    {
        cached_visible_frames = recorded_frames;
        return cached_visible_frames;
    }
    if(zoom >= SAMPLE_CAPTURE_EDIT_ZOOM_MAX)
    {
        cached_visible_frames = (min_frames == 0U) ? 1U : min_frames;
        return cached_visible_frames;
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
    cached_visible_frames = frames;
    return cached_visible_frames;
}

static void sample_capture_set_error(sample_capture_error_t error)
{
    g_sample_capture.state.error = error;
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_ERROR;
}

static void sample_capture_detail_reset(void)
{
    memset(&g_sample_capture.wave_cache_handle, 0, sizeof(g_sample_capture.wave_cache_handle));
    g_sample_capture.wave_cache_ready = 0U;
    g_sample_capture.wave_cache_retry_countdown = 0U;
}

