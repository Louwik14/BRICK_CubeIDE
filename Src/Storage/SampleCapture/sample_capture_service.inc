static void sample_capture_waveform_cache_service_handle(void)
{
    if((g_sample_capture.state.take_valid == 0U)
            || (g_sample_capture.state.recording != 0U)
            || (g_sample_capture.state.armed_pending != 0U)
            || (g_sample_capture.state.temp_path[0] == '\0')
            || (sample_capture_path_is_temp(g_sample_capture.state.temp_path) != 0U)
            || (g_sample_capture.state.recorded_frames < WAVEFORM_CACHE_PERSIST_MIN_FRAMES)
            || (g_sample_capture.wave_cache_ready != 0U))
    {
        return;
    }
    if(g_sample_capture.wave_cache_retry_countdown != 0U)
    {
        g_sample_capture.wave_cache_retry_countdown--;
        return;
    }
    if(waveform_cache_open_for_wav(g_sample_capture.state.temp_path,
                                   &g_sample_capture.wave_cache_handle) != 0U)
    {
        g_sample_capture.wave_cache_ready = 1U;
        return;
    }
    g_sample_capture.wave_cache_retry_countdown = 32U;
}

void sample_capture_model_service(void)
{
    audio_recorder_status_t status;
    if(audio_recorder_get_status_client(AUDIO_RECORDER_CLIENT_AUDIO_REC,
                                        &status) == 0U)
    {
        return;
    }

    audio_rec_level_snapshot_t level;
    if(audio_rec_level_reader_read(&level) != 0U)
    {
        g_sample_capture.state.live_peak_abs_pcm24 = level.peak_abs_pcm24;
        g_sample_capture.state.live_peak_generation = level.generation;
        if((g_sample_capture.state.arm == SAMPLE_CAPTURE_ARM_TRIG)
                && (g_sample_capture.state.recording == 0U)
                && (g_sample_capture.state.trigger_latched == 0U)
                && (level.generation != g_sample_capture.trigger_last_level_generation))
        {
            g_sample_capture.trigger_last_level_generation = level.generation;
            if(level.peak_abs_pcm24 >= g_sample_capture.trigger_threshold_peak_abs_pcm24)
            {
                g_sample_capture.state.trigger_latched = 1U;
                g_sample_capture.state.armed_pending = 1U;
                g_sample_capture.state.phase = (seq_runtime_is_running() != 0U)
                    ? SAMPLE_CAPTURE_PHASE_WAIT_QUANT
                    : SAMPLE_CAPTURE_PHASE_ARMED;
                if ((seq_runtime_is_running() != 0U)
                        && (g_sample_capture.state.quant == SAMPLE_CAPTURE_QUANT_NOW))
                {
                    uint64_t now_sample = 0U;
                    if (control_rt_now_sample(&now_sample) == 0U)
                    {
                        Error_Handler();
                        return;
                    }
                    const uint64_t sample_time =
                        control_music_output_first_unpublished_sample(now_sample);
                    (void)control_domain_request_audio_rec(
                        &(control_audio_rec_intent_t){
                            .operation = CONTROL_AUDIO_REC_START_AT,
                            .has_sample_time = 1U,
                            .sample_time = sample_time
                        });
                }
            }
        }
    }

    if((status.state == AUDIO_RECORDER_STATE_FAILED)
            && ((g_sample_capture.state.recording != 0U) || (g_sample_capture.state.armed_pending != 0U)))
    {
        sample_capture_set_capture_enabled(0U);
        g_sample_capture.state.recording = 0U;
        g_sample_capture.state.armed_pending = 0U;
        g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
        g_sample_capture.state.trigger_latched = 0U;
        sample_capture_publish_rec_bus();
        sample_capture_set_error((status.error == AUDIO_RECORDER_ERROR_RING_OVERFLOW)
            ? SAMPLE_CAPTURE_ERROR_SAMPLE_ACTIVE
            : SAMPLE_CAPTURE_ERROR_SD_IO);
        return;
    }

    if((g_sample_capture.state.recording != 0U)
            && (g_sample_capture.state.planned_frames != 0U)
            && (status.frames_received >= g_sample_capture.state.planned_frames))
    {
        sample_capture_set_capture_enabled(0U);
        (void)control_domain_request_audio_rec(
            &(control_audio_rec_intent_t){
                .operation = CONTROL_AUDIO_REC_STOP_CLIENT,
                .value0 = AUDIO_RECORDER_CLIENT_AUDIO_REC
            });
        g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_STOPPING;
    }

    if(status.state == AUDIO_RECORDER_STATE_TAKE_READY)
    {
        sample_capture_on_take_ready(&status);
        if(g_sample_capture.rec_edit_enter_deferred_services != 0U)
        {
            g_sample_capture.rec_edit_enter_deferred_services--;
            return;
        }
    }

    sample_capture_global_overview_service();
    sample_capture_waveform_cache_service_handle();
    sample_capture_editor_cache_service();
    sample_capture_detail_service();
    sample_capture_line_service();
}
void sample_capture_control_on_global_rec_arm(uint8_t armed)
{
    sample_capture_model_set_control_context(1U);
    if (g_sample_capture.state.arm != SAMPLE_CAPTURE_ARM_REC)
    {
        sample_capture_model_set_control_context(0U);
        return;
    }
    g_sample_capture.state.armed_pending = (armed != 0U) ? 1U : 0U;
    if (armed == 0U)
    {
        if (g_sample_capture.state.recording != 0U)
        {
        sample_capture_set_capture_enabled(0U);
            uint64_t now_sample = 0U;
            if (control_rt_now_sample(&now_sample) == 0U)
            {
                Error_Handler();
                sample_capture_model_set_control_context(0U);
                return;
            }
            (void)audio_recorder_request_stop_client_at(
                AUDIO_RECORDER_CLIENT_AUDIO_REC,
                control_music_output_first_unpublished_sample(now_sample));
            g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_STOPPING;
        }
        else
            g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_ARMED;
    }
    sample_capture_model_set_control_context(0U);
}

void sample_capture_control_on_transport_start(uint64_t sample_time)
{
    sample_capture_model_set_control_context(1U);
    if ((g_sample_capture.state.recording == 0U)
            && (g_sample_capture.state.armed_pending != 0U)
            && (((g_sample_capture.state.arm == SAMPLE_CAPTURE_ARM_REC)
                    && (seq_runtime_rec_is_armed() != 0U))
                || ((g_sample_capture.state.arm == SAMPLE_CAPTURE_ARM_TRIG)
                    && (g_sample_capture.state.trigger_latched != 0U))))
        (void)sample_capture_start_prepared_at(sample_time);
    sample_capture_model_set_control_context(0U);
}

void sample_capture_control_on_musical_boundary(uint8_t track,
                                                uint64_t sample_time)
{
    sample_capture_model_set_control_context(1U);
    if ((track != 0U) || (g_sample_capture.state.recording != 0U)
            || (g_sample_capture.state.armed_pending == 0U)
            || (g_sample_capture.state.quant == SAMPLE_CAPTURE_QUANT_NOW))
    {
        sample_capture_model_set_control_context(0U);
        return;
    }
    (void)sample_capture_start_prepared_at(sample_time);
    sample_capture_model_set_control_context(0U);
}

void sample_capture_control_on_transport_stop(uint64_t sample_time)
{
    sample_capture_model_set_control_context(1U);
    if ((g_sample_capture.state.recording != 0U)
            && (g_sample_capture.state.len_bars == 0U))
    {
        sample_capture_set_capture_enabled(0U);
        (void)audio_recorder_request_stop_client_at(
            AUDIO_RECORDER_CLIENT_AUDIO_REC, sample_time);
        g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_STOPPING;
    }
    sample_capture_model_set_control_context(0U);
}

