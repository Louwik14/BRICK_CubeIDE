void sample_capture_model_init(void)
{
    memset(&g_sample_capture, 0, sizeof(g_sample_capture));
    sample_capture_debug_reset();
    g_sample_capture.state.view = SAMPLE_CAPTURE_VIEW_AUDIO_REC;
    g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
    g_sample_capture.state.trig = SAMPLE_CAPTURE_TRIG_NOW;
    g_sample_capture.state.len_bars = 1U;
    g_sample_capture.state.quant = SAMPLE_CAPTURE_QUANT_NOW;
    g_sample_capture.state.threshold_dbfs = SAMPLE_CAPTURE_THRESHOLD_DBFS_DEFAULT;
    g_sample_capture.trigger_threshold_peak_abs_pcm24 =
        sample_capture_threshold_to_peak_abs_pcm24(
            SAMPLE_CAPTURE_THRESHOLD_DBFS_DEFAULT);
    audio_rec_level_control_publish_trigger_config(0U, 0U,
        g_sample_capture.trigger_threshold_peak_abs_pcm24);
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_IDLE;
    g_sample_capture.state.edit_vzoom = SAMPLE_CAPTURE_EDIT_VZOOM_DEFAULT;
    sample_capture_detail_reset();
    control_audio_rec_bus_init();
    sample_capture_publish_rec_bus();
}

void sample_capture_model_get_state(sample_capture_state_t *out_state)
{
    if(out_state == 0)
    {
        return;
    }
    *out_state = g_sample_capture.state;
    for(uint8_t track = 0U; track < SAMPLE_CAPTURE_TRACK_COUNT; ++track)
    {
        out_state->route_enabled[track] = g_sample_capture.route_mask[track];
    }
}

sample_capture_rec_led_state_t sample_capture_model_rec_led_state(void)
{
    if (g_sample_capture.state.armed_pending != 0U)
        return SAMPLE_CAPTURE_REC_LED_WAITING;
    if (g_sample_capture.state.recording != 0U)
        return SAMPLE_CAPTURE_REC_LED_ACTIVE;
    return SAMPLE_CAPTURE_REC_LED_OFF;
}

void sample_capture_model_set_view(sample_capture_view_t view)
{
    g_sample_capture.state.view = view;
    if(view != SAMPLE_CAPTURE_VIEW_REC_EDIT)
        waveform_service_local_suspend();
}

uint8_t sample_capture_model_open_crop(void)
{
    if ((g_sample_capture.state.take_valid == 0U)
            || (g_sample_capture.state.recording != 0U)
            || (g_sample_capture.state.armed_pending != 0U)
            || (g_sample_capture.state.save_pending != 0U))
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_NO_TAKE);
        return 0U;
    }

    g_sample_capture.state.view = SAMPLE_CAPTURE_VIEW_REC_EDIT;
    g_rec_latency_probe.t_crop_enter = rec_latency_probe_now();
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_REC_EDIT;
    g_sample_capture.rec_edit_enter_deferred_services = 2U;
    g_sample_capture.rec_edit_first_render_pending = 1U;
    return 1U;
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
        (void)sample_capture_next_trigger_arm_epoch();
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
        sample_capture_publish_trigger_config();
        sample_capture_publish_rec_bus();
        return 1U;
    }
    if((arm != SAMPLE_CAPTURE_ARM_OFF)
            && (g_sample_capture.state.save_pending != 0U)) return 0U;

    if (audio_recorder_client_is_active(AUDIO_RECORDER_CLIENT_AUDIO_REC) != 0U)
    {
        const audio_recorder_lifecycle_result_t discarded =
            audio_recorder_discard_client(AUDIO_RECORDER_CLIENT_AUDIO_REC);
        if (discarded == AUDIO_RECORDER_LIFECYCLE_NOT_NOW)
            return 0U;
        if (discarded == AUDIO_RECORDER_LIFECYCLE_ERROR)
        {
            if (g_sample_capture.state.error == SAMPLE_CAPTURE_ERROR_NONE)
                sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
            return 0U;
        }
    }

    g_sample_capture.state.arm = arm;
    (void)sample_capture_next_trigger_arm_epoch();
    g_sample_capture.state.armed_pending =
        ((arm == SAMPLE_CAPTURE_ARM_REC)
            && (seq_runtime_rec_is_armed() != 0U)) ? 1U : 0U;
    g_sample_capture.state.trigger_latched = 0U;
    g_sample_capture.state.error = SAMPLE_CAPTURE_ERROR_NONE;
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_IDLE;
    sample_capture_reset_take_transients(0U);

    const audio_recorder_lifecycle_result_t prepared =
        sample_capture_prepare_session();
    if (prepared == AUDIO_RECORDER_LIFECYCLE_ERROR)
    {
        g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
        g_sample_capture.state.armed_pending = 0U;
        if (g_sample_capture.state.error == SAMPLE_CAPTURE_ERROR_NONE)
            sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        sample_capture_publish_trigger_config();
        sample_capture_publish_rec_bus();
        return 0U;
    }
    if (prepared == AUDIO_RECORDER_LIFECYCLE_OK)
        g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_ARMED;
    sample_capture_publish_trigger_config();
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

uint8_t sample_capture_model_step_trig(int16_t delta)
{
    if((delta == 0) || (g_sample_capture.state.arm != SAMPLE_CAPTURE_ARM_OFF)
            || (g_sample_capture.state.recording != 0U)) return 0U;
    int16_t next = (int16_t)g_sample_capture.state.trig + ((delta > 0) ? 1 : -1);
    if(next < 0) next = 0;
    if(next >= (int16_t)SAMPLE_CAPTURE_TRIG_COUNT)
        next = (int16_t)SAMPLE_CAPTURE_TRIG_COUNT - 1;
    if(next == (int16_t)g_sample_capture.state.trig) return 0U;
    g_sample_capture.state.trig = (sample_capture_trig_t)next;
    return 1U;
}

uint8_t sample_capture_model_toggle_record(void)
{
    if(g_sample_capture.state.save_pending != 0U) return 0U;
    if((g_sample_capture.state.arm != SAMPLE_CAPTURE_ARM_OFF)
            || (g_sample_capture.state.recording != 0U))
        return sample_capture_model_set_arm(SAMPLE_CAPTURE_ARM_OFF);
    const sample_capture_arm_t arm =
        ((g_sample_capture.state.trig == SAMPLE_CAPTURE_TRIG_THRESHOLD)
         || (g_sample_capture.state.trig == SAMPLE_CAPTURE_TRIG_THRESHOLD_PLAY))
            ? SAMPLE_CAPTURE_ARM_TRIG : SAMPLE_CAPTURE_ARM_REC;
    if(sample_capture_model_set_arm(arm) == 0U) return 0U;
    if(g_sample_capture.state.trig == SAMPLE_CAPTURE_TRIG_NOW)
    {
        g_sample_capture.state.armed_pending = 1U;
        if(g_sample_capture.state.phase == SAMPLE_CAPTURE_PHASE_ARMED)
        {
            uint64_t now_sample = 0U;
            if(control_rt_now_sample(&now_sample) == 0U) return 0U;
            return sample_capture_start_prepared_at(
                control_music_output_first_unpublished_sample(now_sample));
        }
    }
    else if(g_sample_capture.state.trig == SAMPLE_CAPTURE_TRIG_PATTERN)
    {
        g_sample_capture.state.armed_pending = 1U;
        if (g_sample_capture.state.phase == SAMPLE_CAPTURE_PHASE_ARMED)
            g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_WAIT_QUANT;
    }
    return 1U;
}

uint8_t sample_capture_model_cancel_for_note_rec(void)
{
    if((g_sample_capture.state.arm == SAMPLE_CAPTURE_ARM_OFF)
            && (g_sample_capture.state.recording == 0U)) return 1U;
    return sample_capture_model_set_arm(SAMPLE_CAPTURE_ARM_OFF);
}

uint8_t sample_capture_model_toggle_overdub(void)
{
    if((g_sample_capture.state.arm != SAMPLE_CAPTURE_ARM_OFF)
            || (g_sample_capture.state.recording != 0U)) return 0U;
    g_sample_capture.state.overdub_enabled =
        (g_sample_capture.state.overdub_enabled == 0U) ? 1U : 0U;
    sample_capture_publish_rec_bus();
    return 1U;
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

    static const uint8_t lengths[] = {0U, 1U, 2U, 4U, 8U, 16U, 32U, 64U};
    uint8_t index = 0U;
    while((index + 1U < sizeof(lengths))
            && (lengths[index] < g_sample_capture.state.len_bars)) index++;
    if((delta > 0) && (index + 1U < sizeof(lengths))) index++;
    else if((delta < 0) && (index > 0U)) index--;
    g_sample_capture.state.len_bars = lengths[index];
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
    if(g_sample_capture.state.arm == SAMPLE_CAPTURE_ARM_TRIG)
    {
        (void)sample_capture_next_trigger_arm_epoch();
        sample_capture_publish_trigger_config();
    }
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
    if(g_sample_capture.state.arm == SAMPLE_CAPTURE_ARM_TRIG)
    {
        (void)sample_capture_next_trigger_arm_epoch();
        sample_capture_publish_trigger_config();
    }
    return 1U;
}

uint8_t sample_capture_model_set_line_enabled(uint8_t enabled)
{
    const uint8_t next = (enabled != 0U) ? 1U : 0U;
    if(next == g_sample_capture.state.line_enabled)
    {
        return 1U;
    }
    if((next != 0U) && (g_sample_capture.state.mic_enabled != 0U))
    {
        if((g_sample_capture.state.recording != 0U)
                || (g_sample_capture.state.arm != SAMPLE_CAPTURE_ARM_OFF))
        {
            return 0U;
        }
        if(board_audio_set_analog_input_mode(BOARD_AUDIO_ANALOG_INPUT_LINE) == 0U)
        {
            return 0U;
        }
        g_sample_capture.state.mic_enabled = 0U;
    }
    g_sample_capture.state.line_enabled = next;
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
    const uint8_t next = (enabled != 0U) ? 1U : 0U;
    if(next == g_sample_capture.state.mic_enabled)
    {
        return 1U;
    }
    if((g_sample_capture.state.recording != 0U)
            || (g_sample_capture.state.arm != SAMPLE_CAPTURE_ARM_OFF))
    {
        return 0U;
    }
    if(board_audio_set_analog_input_mode((next != 0U)
            ? BOARD_AUDIO_ANALOG_INPUT_MIC : BOARD_AUDIO_ANALOG_INPUT_LINE) == 0U)
    {
        return 0U;
    }
    g_sample_capture.state.mic_enabled = next;
    if(next != 0U)
    {
        g_sample_capture.state.line_enabled = 0U;
    }
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

uint8_t sample_capture_model_rec_waveform_source(waveform_source_t *out_source)
{
    if((out_source == 0) || (g_sample_capture.state.take_valid == 0U)
            || (g_sample_capture.visible_rec_generation == 0U)
            || (waveform_rec_current_source(out_source) == 0U)
            || (out_source->key.generation != g_sample_capture.visible_rec_generation)
            || (out_source->frame_count != g_sample_capture.state.recorded_frames))
    {
        return 0U;
    }
    return 1U;
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
    g_sample_capture_debug.draw_count++;
    if((uint8_t)renderer < 8U)
    {
        g_sample_capture_debug.renderer_count[(uint8_t)renderer]++;
    }
    const uint32_t now = HAL_GetTick();
    if(now - g_sample_capture_debug.last_summary_ms >= 1000U
            || g_sample_capture_debug.last_renderer != renderer)
    {
        sample_capture_debug_log("RECEDIT R=%s z=%u vs=%lu vf=%lu w=%u spp=%lu fb=%u\r\n",
            sample_capture_debug_renderer_label(renderer), (unsigned)zoom,
            (unsigned long)view_start_frame, (unsigned long)view_frames,
            (unsigned)inner_w, (unsigned long)samples_per_pixel,
            (unsigned)fallback_reason);
        g_sample_capture_debug.last_summary_ms = now;
        g_sample_capture_debug.last_renderer = renderer;
    }
#else
    (void)renderer;
    (void)zoom;
    (void)view_start_frame;
    (void)view_frames;
    (void)inner_w;
    (void)samples_per_pixel;
    (void)fallback_reason;
#endif
    (void)wavecache_frames_per_column;
    (void)line_valid;
    (void)line_points;
    (void)draw_line_segments;
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

