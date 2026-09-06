static void sample_capture_editor_cache_reset(void);
static void sample_capture_global_overview_reset(void);
static void sample_capture_set_capture_enabled(uint8_t enabled);

static uint32_t sample_capture_threshold_to_peak_abs_pcm24(int8_t threshold_dbfs)
{
    const float linear = powf(10.0f, (float)threshold_dbfs * 0.05f);
    const float peak = linear * (float)SAMPLE_CAPTURE_PCM24_PEAK;
    return (peak >= (float)SAMPLE_CAPTURE_PCM24_PEAK)
        ? SAMPLE_CAPTURE_PCM24_PEAK : (uint32_t)(peak + 0.5f);
}

static uint32_t sample_capture_next_trigger_arm_epoch(void)
{
    uint32_t next = g_sample_capture.trigger_arm_epoch + 1U;
    if (next == 0U) next = 1U;
    g_sample_capture.trigger_arm_epoch = next;
    return next;
}

static void sample_capture_publish_trigger_config(void)
{
    audio_recorder_status_t recorder_status;
    const uint8_t recorder_ready =
        (audio_recorder_get_status_client(AUDIO_RECORDER_CLIENT_AUDIO_REC,
                                          &recorder_status) != 0U)
        && (recorder_status.state == AUDIO_RECORDER_STATE_PREPARED);
    const uint8_t enabled = (uint8_t)(
        (g_sample_capture.state.arm == SAMPLE_CAPTURE_ARM_TRIG)
        && (recorder_ready != 0U)
        && (g_sample_capture.state.recording == 0U)
        && (g_sample_capture.state.trigger_latched == 0U));
    audio_rec_level_control_publish_trigger_config(
        enabled,
        g_sample_capture.trigger_arm_epoch,
        g_sample_capture.trigger_threshold_peak_abs_pcm24);
}

static void sample_capture_publish_rec_bus(void)
{
    uint16_t source_entity_mask = 0U;
    for(uint8_t track = 0U; track < SAMPLE_CAPTURE_TRACK_COUNT; ++track)
    {
        if((g_sample_capture.route_mask[track] != 0U)
                && (entity_topology_is_active((brick_entity_id_t)track) != 0U)
                && (track_runtime_is_audio_routable(track) != 0U))
        {
            source_entity_mask |= (uint16_t)(1U << track);
        }
    }

    uint8_t source_flags = 0U;
    if(g_sample_capture.state.line_enabled != 0U)
    {
        uint8_t line_owner = TRACK_INPUT_OWNER_NONE;
        const uint8_t owner_is_routed = (uint8_t)(
            (track_input_ownership_get_external_owner(0U, &line_owner) != 0U)
            && (line_owner < BRICK_ENTITY_CAPACITY)
            && ((source_entity_mask & (uint16_t)(1U << line_owner)) != 0U));
        if(owner_is_routed == 0U)
        {
            source_flags |= AUDIO_REC_BUS_SOURCE_LINE_DIRECT;
        }
    }
    if(g_sample_capture.state.mic_enabled != 0U)
    {
        source_flags |= AUDIO_REC_BUS_SOURCE_MIC_LOGICAL;
    }
    if(g_sample_capture.state.usb_enabled != 0U)
    {
        source_flags |= AUDIO_REC_BUS_SOURCE_USB_DIRECT;
    }
    if(g_sample_capture.capture_enabled != 0U)
    {
        source_flags |= AUDIO_REC_BUS_CAPTURE_ENABLED;
    }

    const uint8_t accepted = (g_sample_capture_control_context != 0U)
        ? control_audio_rec_bus_publish(source_entity_mask,
            (audio_rec_bus_arm_t)g_sample_capture.state.arm, source_flags)
        : control_domain_request_rec_bus(source_entity_mask,
            (uint8_t)g_sample_capture.state.arm, source_flags, 0U, 0U);
    if (accepted == 0U)
        Error_Handler();
}

static uint8_t sample_capture_publish_rec_bus_at(uint64_t sample_time)
{
    uint16_t source_entity_mask = 0U;
    for(uint8_t track = 0U; track < SAMPLE_CAPTURE_TRACK_COUNT; ++track)
        if((g_sample_capture.route_mask[track] != 0U)
                && (entity_topology_is_active((brick_entity_id_t)track) != 0U)
                && (track_runtime_is_audio_routable(track) != 0U))
            source_entity_mask |= (uint16_t)(1U << track);
    uint8_t source_flags = AUDIO_REC_BUS_CAPTURE_ENABLED;
    if(g_sample_capture.state.line_enabled != 0U)
    {
        uint8_t line_owner = TRACK_INPUT_OWNER_NONE;
        const uint8_t owner_is_routed = (uint8_t)(
            (track_input_ownership_get_external_owner(0U, &line_owner) != 0U)
            && (line_owner < BRICK_ENTITY_CAPACITY)
            && ((source_entity_mask & (uint16_t)(1U << line_owner)) != 0U));
        if(owner_is_routed == 0U)
            source_flags |= AUDIO_REC_BUS_SOURCE_LINE_DIRECT;
    }
    if(g_sample_capture.state.mic_enabled != 0U)
        source_flags |= AUDIO_REC_BUS_SOURCE_MIC_LOGICAL;
    if(g_sample_capture.state.usb_enabled != 0U)
        source_flags |= AUDIO_REC_BUS_SOURCE_USB_DIRECT;
    const uint8_t accepted = (g_sample_capture_control_context != 0U)
        ? control_audio_rec_bus_publish_at(source_entity_mask,
            (audio_rec_bus_arm_t)g_sample_capture.state.arm,
            source_flags, sample_time)
        : control_domain_request_rec_bus(source_entity_mask,
            (uint8_t)g_sample_capture.state.arm, source_flags, 1U,
            sample_time);
    if (accepted == 0U)
        return 0U;
    return 1U;
}

#if SAMPLE_CAPTURE_DEBUG_UART
static void sample_capture_debug_log(const char *fmt, ...)
{
    char line[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if(n <= 0)
    {
        return;
    }
    if((uint32_t)n >= sizeof(line))
    {
        n = (int)sizeof(line) - 1;
    }
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)line, (uint16_t)n, 20U);
}

static void sample_capture_debug_reset(void)
{
    memset(&g_sample_capture_debug, 0, sizeof(g_sample_capture_debug));
}
#else
#define sample_capture_debug_reset() ((void)0)
#endif

#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
static const char *sample_capture_debug_renderer_label(sample_capture_renderer_debug_t renderer)
{
    switch(renderer)
    {
        case SAMPLE_CAPTURE_RENDERER_EMPTY: return "EMPTY";
        case SAMPLE_CAPTURE_RENDERER_GLOBAL_OVERVIEW: return "GLOBAL_OVERVIEW";
        case SAMPLE_CAPTURE_RENDERER_OLD_LINE: return "OLD_LINE";
        case SAMPLE_CAPTURE_RENDERER_OLD_AUDIO_TILE: return "OLD_AUDIO_TILE";
        case SAMPLE_CAPTURE_RENDERER_BRKWAVE_TILE: return "BRKWAVE_TILE";
        case SAMPLE_CAPTURE_RENDERER_SD_LINE_FALLBACK: return "SD_LINE";
        case SAMPLE_CAPTURE_RENDERER_BUILDING: return "BUILDING";
        case SAMPLE_CAPTURE_RENDERER_ERROR: return "ERROR";
        default: return "?";
    }
}
#endif

static void sample_capture_copy_path(char *dst, const char *src)
{
    if(dst == 0)
    {
        return;
    }
    dst[0] = '\0';
    if(src == 0)
    {
        return;
    }

    for(uint32_t i = 0U; i < SAMPLE_CAPTURE_PATH_MAX; ++i)
    {
        dst[i] = src[i];
        if(src[i] == '\0')
        {
            return;
        }
    }
    dst[SAMPLE_CAPTURE_PATH_MAX - 1U] = '\0';
}

static uint8_t sample_capture_path_is_temp(const char *path)
{
    if((path == 0) || (path[0] == '\0'))
    {
        return 1U;
    }
    if(strcmp(path, SAMPLE_CAPTURE_TEMP_PATH) == 0)
    {
        return 1U;
    }
    if((strstr(path, "_TMP") != 0) || (strstr(path, "_tmp") != 0))
    {
        return 1U;
    }
    return 0U;
}

static uint8_t sample_capture_has_route(void)
{
    if((g_sample_capture.state.line_enabled != 0U)
            || (g_sample_capture.state.mic_enabled != 0U)
            || (g_sample_capture.state.usb_enabled != 0U))
    {
        return 1U;
    }
    for (uint8_t track = 0U; track < SAMPLE_CAPTURE_TRACK_COUNT; ++track)
        if ((g_sample_capture.route_mask[track] != 0U)
                && (entity_topology_is_active((brick_entity_id_t)track) != 0U)
                && (track_runtime_is_audio_routable(track) != 0U)) return 1U;
    return 0U;
}

static uint32_t sample_capture_len_to_frames(uint8_t len_bars)
{
    if(len_bars == 0U)
    {
        return 0U;
    }

    const uint32_t samples_per_step_q16 = seq_runtime_get_samples_per_step_q16();
    if(samples_per_step_q16 == 0U)
    {
        return 0U;
    }

    const uint64_t steps = (uint64_t)len_bars;
    uint64_t frames = ((steps * (uint64_t)samples_per_step_q16) + 0xFFFFULL) >> 16;
    if(frames > 0xFFFFFFFFULL)
    {
        frames = 0xFFFFFFFFULL;
    }
    return (uint32_t)frames;
}

