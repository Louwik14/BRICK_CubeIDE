static void sample_capture_reset_take_transients(uint8_t preserve_final_path)
{
    sample_capture_detail_reset();
    g_sample_capture.state.take_valid = 0U;
    g_sample_capture.state.recorded_frames = 0U;
    g_sample_capture.state.edit_start_frame = 0U;
    g_sample_capture.state.edit_end_frame = 0U;
    g_sample_capture.state.edit_loop_start_frame = 0U;
    g_sample_capture.state.edit_loop_end_frame = 0U;
    g_sample_capture.state.edit_zoom = 0U;
    g_sample_capture.state.edit_scroll_frame = 0U;
    g_sample_capture.state.temp_path[0] = '\0';
    if (preserve_final_path == 0U)
        g_sample_capture.state.final_path[0] = '\0';
    g_sample_capture.last_take_notified = 0U;
    g_sample_capture.rec_edit_enter_deferred_services = 0U;
    g_sample_capture.rec_edit_first_render_pending = 0U;
}

uint8_t sample_capture_model_return_to_audio_rec(void)
{
    if(sd_preview_is_active() != 0U)
    {
        sd_preview_stop();
    }

    if ((g_sample_capture.state.recording != 0U)
            || (g_sample_capture.state.armed_pending != 0U))
        return 0U;

    g_sample_capture.state.view = SAMPLE_CAPTURE_VIEW_AUDIO_REC;
    g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
    g_sample_capture.state.trigger_latched = 0U;
    sample_capture_set_capture_enabled(0U);
    sample_capture_reset_take_transients(0U);
    sample_capture_publish_trigger_config();
    sample_capture_publish_rec_bus();

    const audio_recorder_lifecycle_result_t discarded =
        audio_recorder_discard_client(AUDIO_RECORDER_CLIENT_AUDIO_REC);
    if (discarded == AUDIO_RECORDER_LIFECYCLE_NOT_NOW)
    {
        g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_STOPPING;
        return 1U;
    }
    if (discarded == AUDIO_RECORDER_LIFECYCLE_ERROR)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        return 0U;
    }
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_IDLE;
    g_sample_capture.state.error = SAMPLE_CAPTURE_ERROR_NONE;
    return 1U;
}

uint8_t sample_capture_model_audition_trimmed(void)
{
    if((g_sample_capture.state.take_valid == 0U)
            || (g_sample_capture.state.recording != 0U)
            || (g_sample_capture.state.armed_pending != 0U)
            || ((g_sample_capture.state.phase != SAMPLE_CAPTURE_PHASE_REC_EDIT)
                && (g_sample_capture.state.phase != SAMPLE_CAPTURE_PHASE_SAVED)
                && (g_sample_capture.state.phase != SAMPLE_CAPTURE_PHASE_ERROR))
            || (g_sample_capture.state.edit_end_frame <= g_sample_capture.state.edit_start_frame)
            || (g_sample_capture.state.edit_end_frame > g_sample_capture.state.recorded_frames))
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_INVALID_ARG);
        return 0U;
    }

    if((sd_preview_is_active() != 0U)
            && (strcmp(sd_preview_get_path(), g_sample_capture.state.temp_path) == 0))
    {
        sd_preview_stop();
        g_sample_capture.state.error = SAMPLE_CAPTURE_ERROR_NONE;
        if(g_sample_capture.state.phase == SAMPLE_CAPTURE_PHASE_ERROR)
        {
            g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_REC_EDIT;
        }
        return 1U;
    }

    if(sd_preview_begin_range(g_sample_capture.state.temp_path,
                              g_sample_capture.state.edit_start_frame,
                              g_sample_capture.state.edit_end_frame) == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_PREVIEW_FAIL);
        return 0U;
    }

    g_sample_capture.state.error = SAMPLE_CAPTURE_ERROR_NONE;
    if(g_sample_capture.state.phase == SAMPLE_CAPTURE_PHASE_ERROR)
    {
        g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_REC_EDIT;
    }
    return 1U;
}

static uint8_t sample_capture_copy_trimmed_take(const char *src_path,
                                                const char *dst_path,
                                                uint32_t start_frame,
                                                uint32_t end_frame)
{
    if((src_path == 0) || (dst_path == 0) || (end_frame <= start_frame))
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_INVALID_ARG);
        return 0U;
    }

    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_RECORDER) == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_BUSY);
        return 0U;
    }

    FIL src;
    FIL dst;
    uint8_t src_open = 0U;
    uint8_t dst_open = 0U;
    uint8_t ok = 0U;
    if(sd_access_fs_mount_if_needed() == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        goto done;
    }

    FRESULT fr = f_open(&src, src_path, FA_READ);
    if(fr != FR_OK)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        goto done;
    }
    src_open = 1U;

    fr = f_open(&dst, dst_path, FA_CREATE_NEW | FA_WRITE);
    if(fr != FR_OK)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        goto done;
    }
    dst_open = 1U;

    uint8_t header[SAMPLE_CAPTURE_WAV_DATA_OFFSET];
    UINT br = 0U;
    fr = f_lseek(&src, 0U);
    if(fr != FR_OK)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        goto done;
    }
    fr = f_read(&src, header, sizeof(header), &br);
    if((fr != FR_OK) || (br != sizeof(header)))
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        goto done;
    }

    const uint32_t frames = end_frame - start_frame;
    const uint32_t data_bytes = frames * AUDIO_RECORDER_BYTES_PER_FRAME;
    header[4] = (uint8_t)(((SAMPLE_CAPTURE_WAV_DATA_OFFSET - 8U) + data_bytes) & 0xFFU);
    header[5] = (uint8_t)((((SAMPLE_CAPTURE_WAV_DATA_OFFSET - 8U) + data_bytes) >> 8) & 0xFFU);
    header[6] = (uint8_t)((((SAMPLE_CAPTURE_WAV_DATA_OFFSET - 8U) + data_bytes) >> 16) & 0xFFU);
    header[7] = (uint8_t)((((SAMPLE_CAPTURE_WAV_DATA_OFFSET - 8U) + data_bytes) >> 24) & 0xFFU);
    header[SAMPLE_CAPTURE_WAV_DATA_OFFSET - 4U] = (uint8_t)(data_bytes & 0xFFU);
    header[SAMPLE_CAPTURE_WAV_DATA_OFFSET - 3U] = (uint8_t)((data_bytes >> 8) & 0xFFU);
    header[SAMPLE_CAPTURE_WAV_DATA_OFFSET - 2U] = (uint8_t)((data_bytes >> 16) & 0xFFU);
    header[SAMPLE_CAPTURE_WAV_DATA_OFFSET - 1U] = (uint8_t)((data_bytes >> 24) & 0xFFU);

    UINT bw = 0U;
    fr = f_write(&dst, header, sizeof(header), &bw);
    if((fr != FR_OK) || (bw != sizeof(header)))
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        goto done;
    }

    fr = f_lseek(&src, SAMPLE_CAPTURE_WAV_DATA_OFFSET +
        (start_frame * AUDIO_RECORDER_BYTES_PER_FRAME));
    if(fr != FR_OK)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        goto done;
    }

    uint32_t bytes_left = data_bytes;
    while(bytes_left != 0U)
    {
        uint32_t chunk = sizeof(g_sample_capture_copy_buf);
        if(chunk > bytes_left)
        {
            chunk = bytes_left;
        }
        br = 0U;
        fr = f_read(&src, g_sample_capture_copy_buf, chunk, &br);
        if((fr != FR_OK) || (br != chunk))
        {
            sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
            goto done;
        }
        bw = 0U;
        fr = f_write(&dst, g_sample_capture_copy_buf, chunk, &bw);
        if((fr != FR_OK) || (bw != chunk))
        {
            sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
            goto done;
        }
        bytes_left -= chunk;
    }

    if(f_sync(&dst) != FR_OK)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        goto done;
    }

    ok = 1U;
    g_sample_capture.state.error = SAMPLE_CAPTURE_ERROR_NONE;

done:
    if(dst_open != 0U)
    {
        (void)f_close(&dst);
    }
    if(src_open != 0U)
    {
        (void)f_close(&src);
    }
    if((ok == 0U) && (dst_open != 0U))
        (void)f_unlink(dst_path);
    sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
    return ok;
}

uint8_t sample_capture_model_save_trimmed(void)
{
    if(sd_preview_is_active() != 0U)
    {
        sd_preview_stop();
    }

    if(g_sample_capture.state.final_path[0] != '\0')
    {
        return 1U;
    }
    rec_source_snapshot_t current;
    const char *current_path = 0;
    if(rec_source_current_path(&current_path, &current) == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_NO_TAKE);
        return 0U;
    }

    uint32_t save_start = 0U;
    uint32_t save_end = current.frame_count;
    if((g_sample_capture.state.take_valid != 0U)
            && (strcmp(g_sample_capture.state.temp_path, current_path) == 0)
            && (g_sample_capture.state.edit_end_frame
                > g_sample_capture.state.edit_start_frame)
            && (g_sample_capture.state.edit_end_frame <= current.frame_count))
    {
        save_start = g_sample_capture.state.edit_start_frame;
        save_end = g_sample_capture.state.edit_end_frame;
    }

    char final_path[SAMPLE_CAPTURE_PATH_MAX];
    if(sample_capture_make_next_final_path(final_path, sizeof(final_path)) == 0U)
    {
        return 0U;
    }

    if(sample_capture_copy_trimmed_take(current_path,
                                        final_path,
                                        save_start,
                                        save_end) == 0U)
    {
        return 0U;
    }

    const uint32_t saved_frames = save_end - save_start;
    sample_capture_copy_path(g_sample_capture.state.final_path, final_path);
    const audio_recorder_lifecycle_result_t discarded =
        audio_recorder_discard_client(AUDIO_RECORDER_CLIENT_AUDIO_REC);
    sample_capture_reset_take_transients(1U);
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_SAVED;
    (void)sample_capture_model_assign_refresh();
    (void)waveform_cache_request_for_wav_known_duration(
        final_path,
        WAVEFORM_CACHE_REASON_EDITOR_VISIBLE,
        saved_frames,
        AUDIO_RECORDER_SAMPLE_RATE_HZ);
    if (discarded == AUDIO_RECORDER_LIFECYCLE_ERROR)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        return 0U;
    }
    return 1U;
}

uint8_t sample_capture_model_assign_refresh(void)
{
    g_sample_capture.assign_count = 0U;
    g_sample_capture.assign_index = 0U;
    g_sample_capture.assign_loading = 0U;
    for(uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        entity_topology_descriptor_t topology;
        if((entity_topology_get(entity, &topology) == 0U)
                || (topology.active == 0U)
                || (topology.role == ENTITY_ROLE_GROUP_MASTER))
            continue;
        const track_type_t type = track_state_get_type(entity);
        if((type == TRACK_TYPE_RAM) || (type == TRACK_TYPE_STREAM))
            g_sample_capture.assign_tracks[g_sample_capture.assign_count++] = entity;
    }
    return g_sample_capture.assign_count;
}

uint8_t sample_capture_model_assign_count(void)
{
    return g_sample_capture.assign_count;
}

uint8_t sample_capture_model_assign_selected(uint8_t *out_track)
{
    if((out_track == 0) || (g_sample_capture.assign_count == 0U)) return 0U;
    *out_track = g_sample_capture.assign_tracks[g_sample_capture.assign_index];
    return 1U;
}

uint8_t sample_capture_model_assign_step(int16_t delta)
{
    if((g_sample_capture.assign_count == 0U) || (g_sample_capture.assign_loading != 0U))
        return 0U;
    int16_t next = (int16_t)g_sample_capture.assign_index + ((delta > 0) ? 1 : -1);
    if(next < 0) next = (int16_t)g_sample_capture.assign_count - 1;
    if(next >= g_sample_capture.assign_count) next = 0;
    g_sample_capture.assign_index = (uint8_t)next;
    return 1U;
}

static uint8_t sample_capture_assign_commit(uint8_t track, uint32_t kind,
                                            uint16_t logical)
{
    uint16_t runtime;
    if(project_control_resolve_sample_runtime_kind(kind, logical, &runtime) == 0U)
        return 0U;
    return project_control_track_asset_select_logical(
        track, PROJECT_CONTROL_ASSET_SAMPLER, logical);
}

uint8_t sample_capture_model_assign_selected_take(void)
{
    uint8_t track;
    uint16_t logical;
    uint16_t runtime;
    if((g_sample_capture.state.final_path[0] == '\0')
            || (sample_capture_model_assign_selected(&track) == 0U)
            || (g_sample_capture.assign_loading != 0U)) return 0U;
    const track_type_t type = track_state_get_type(track);
    const uint32_t kind = (type == TRACK_TYPE_RAM)
        ? PERSIST_ASSET_SAMPLE_RAM : PERSIST_ASSET_SAMPLE_STREAM;
    if(project_control_find_asset(kind, g_sample_capture.state.final_path,
                                  &logical) != 0U
            && project_control_resolve_sample_runtime_kind(kind, logical,
                                                           &runtime) != 0U)
        return sample_capture_assign_commit(track, kind, logical);
    if(type == TRACK_TYPE_STREAM)
    {
        const uint16_t slot = sample_global_pool_find_free_slot();
        if((slot >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
                || (sample_global_pool_load_classic(
                    slot, g_sample_capture.state.final_path) == 0U)
                || (project_control_register_sample_runtime(
                    kind, g_sample_capture.state.final_path, slot, &logical) == 0U)
                || (sample_capture_assign_commit(track, kind, logical) == 0U))
        {
            sample_capture_set_error(SAMPLE_CAPTURE_ERROR_LOAD_FAIL);
            return 0U;
        }
        return 1U;
    }
    if(type == TRACK_TYPE_RAM)
    {
        const uint16_t slot = sampler_ram_pool_find_free_slot();
        if((slot >= SAMPLER_RAM_POOL_MAX_SLOTS)
                || (project_control_ram_load_begin(
                    slot, g_sample_capture.state.final_path) == 0U))
        {
            sample_capture_set_error(SAMPLE_CAPTURE_ERROR_LOAD_FAIL);
            return 0U;
        }
        g_sample_capture.assign_loading = 1U;
        g_sample_capture.assign_target = track;
        return 1U;
    }
    return 0U;
}

void sample_capture_model_assign_service(void)
{
    if(g_sample_capture.assign_loading == 0U) return;
    project_control_ram_load_result_t result;
    if(project_control_ram_load_take_result(&result) == 0U) return;
    g_sample_capture.assign_loading = 0U;
    if((result.success == 0U)
            || (sample_capture_assign_commit(g_sample_capture.assign_target,
                PERSIST_ASSET_SAMPLE_RAM, result.logical_slot) == 0U))
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_LOAD_FAIL);
    else
        g_sample_capture.state.error = SAMPLE_CAPTURE_ERROR_NONE;
}

void sample_capture_model_assign_cancel(void)
{
    if(g_sample_capture.assign_loading == 0U)
    {
        g_sample_capture.assign_count = 0U;
        g_sample_capture.assign_index = 0U;
    }
}

uint8_t sample_capture_model_assign_trimmed(void)
{
    if(sd_preview_is_active() != 0U)
    {
        sd_preview_stop();
    }

    if(g_sample_capture.state.take_valid == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_NO_TAKE);
        return 0U;
    }

    const uint16_t slot = sample_global_pool_find_free_slot();

    if(slot >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_NO_SLOT);
        return 0U;
    }

    if(sample_capture_model_save_trimmed() == 0U)
    {
        return 0U;
    }

    if(sample_global_pool_load_classic(slot, g_sample_capture.state.final_path) == false)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_LOAD_FAIL);
        return 0U;
    }

    return 1U;
}

uint8_t sample_capture_model_assign_saved_take_to_pool(void)
{
    if(g_sample_capture.state.final_path[0] == '\0')
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_NO_TAKE);
        return 0U;
    }

    const uint16_t slot = sample_global_pool_find_free_slot();

    if(slot >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_NO_SLOT);
        return 0U;
    }

    if(sample_global_pool_load_classic(slot, g_sample_capture.state.final_path) == false)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_LOAD_FAIL);
        return 0U;
    }

    g_sample_capture.state.error = SAMPLE_CAPTURE_ERROR_NONE;
    if(g_sample_capture.state.phase == SAMPLE_CAPTURE_PHASE_ERROR)
    {
        g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_SAVED;
    }
    return 1U;
}

uint8_t sample_capture_model_toggle_zcross(void)
{
    g_sample_capture.state.edit_zcross_enabled =
        (g_sample_capture.state.edit_zcross_enabled == 0U) ? 1U : 0U;
    return 1U;
}
