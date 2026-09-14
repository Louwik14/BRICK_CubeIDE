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
            || (g_sample_capture.state.save_pending != 0U)
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

static uint8_t sample_capture_save_admit(sd_scheduler_background_kind_t kind,
                                         uint32_t bytes)
{
    const sd_scheduler_background_request_t request = {
        .byte_count = bytes,
        .media_epoch = sd_access_media_epoch(),
        .kind = kind
    };
    return (sd_scheduler_runtime_background_try_begin(&request)
            == SD_SCHEDULER_BACKGROUND_GO) ? 1U : 0U;
}

static void sample_capture_save_fail(void)
{
    sample_capture_save_job_t *const job = &g_sample_capture.save_job;
    sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
    job->phase = (job->destination_open != 0U)
        ? SAMPLE_CAPTURE_SAVE_ROLLBACK_DESTINATION
        : ((job->source_open != 0U)
            ? SAMPLE_CAPTURE_SAVE_ROLLBACK_SOURCE
            : SAMPLE_CAPTURE_SAVE_ROLLBACK_UNLINK);
}

static void sample_capture_save_finish(void)
{
    sample_capture_save_job_t *const job = &g_sample_capture.save_job;
    g_rec_latency_probe.t_save_done = rec_latency_probe_now();
    sample_capture_copy_path(g_sample_capture.state.final_path, job->final_path);
    (void)audio_recorder_discard_client(AUDIO_RECORDER_CLIENT_AUDIO_REC);
    sample_capture_reset_take_transients(1U);
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_SAVED;
    g_sample_capture.state.save_pending = 0U;
    g_sample_capture.state.error = SAMPLE_CAPTURE_ERROR_NONE;
    (void)sample_capture_model_assign_refresh();
    (void)waveform_cache_request_for_wav_known_duration(
        job->final_path, WAVEFORM_CACHE_REASON_EDITOR_VISIBLE,
        job->saved_frames, AUDIO_RECORDER_SAMPLE_RATE_HZ);
    memset(job, 0, sizeof(*job));
}

static void sample_capture_save_service(void)
{
    sample_capture_save_job_t *const job = &g_sample_capture.save_job;
    if(job->phase == SAMPLE_CAPTURE_SAVE_IDLE) return;
    g_rec_latency_probe.save_service_count++;
    g_rec_latency_probe.save_phase = (uint32_t)job->phase;
    g_rec_latency_probe.save_quantum_bytes = SAMPLE_CAPTURE_SAVE_CHUNK_BYTES;
    g_rec_latency_probe.save_bytes_remaining = job->bytes_left;
    if((job->phase == SAMPLE_CAPTURE_SAVE_READ_DATA) && (job->chunk_bytes == 0U))
        job->chunk_bytes = (job->bytes_left > SAMPLE_CAPTURE_SAVE_CHUNK_BYTES)
            ? SAMPLE_CAPTURE_SAVE_CHUNK_BYTES : job->bytes_left;
    const uint8_t data_phase = (uint8_t)((job->phase == SAMPLE_CAPTURE_SAVE_READ_HEADER)
        || (job->phase == SAMPLE_CAPTURE_SAVE_WRITE_HEADER)
        || (job->phase == SAMPLE_CAPTURE_SAVE_READ_DATA)
        || (job->phase == SAMPLE_CAPTURE_SAVE_WRITE_DATA));
    const uint32_t quantum = data_phase != 0U
        ? ((job->phase == SAMPLE_CAPTURE_SAVE_READ_HEADER
            || job->phase == SAMPLE_CAPTURE_SAVE_WRITE_HEADER)
            ? SAMPLE_CAPTURE_WAV_DATA_OFFSET : job->chunk_bytes) : 0U;
    if(sample_capture_save_admit(data_phase != 0U
            ? SD_SCHEDULER_BACKGROUND_DATA : SD_SCHEDULER_BACKGROUND_METADATA,
            quantum) == 0U) {
        g_rec_latency_probe.save_admission_denied_count++;
        g_rec_latency_probe.save_not_now_count++;
        return;
    }

    FRESULT fr = FR_OK;
    UINT transferred = 0U;
    switch(job->phase)
    {
        case SAMPLE_CAPTURE_SAVE_SELECT_PATH:
        {
            if(job->path_attempts == 0U)
            {
                if((sd_access_fs_mount_if_needed() == 0U)
                        || ((fr = f_mkdir("0:/REC")) != FR_OK && fr != FR_EXIST))
                {
                    sample_capture_save_fail();
                    break;
                }
            }
            if(job->path_attempts >= 10000U)
            {
                sample_capture_save_fail();
                break;
            }
            const uint16_t candidate = g_sample_capture.final_counter;
            FILINFO info;
            (void)snprintf(job->final_path, sizeof(job->final_path),
                           "0:/REC/REC%04u.WAV", (unsigned int)candidate);
            fr = f_stat(job->final_path, &info);
            g_sample_capture.final_counter = (uint16_t)((candidate + 1U) % 10000U);
            job->path_attempts++;
            if(fr == FR_NO_FILE)
            {
                (void)snprintf(job->temporary_path, sizeof(job->temporary_path),
                               "0:/REC/REC%04u.TMP", (unsigned int)candidate);
                job->phase = SAMPLE_CAPTURE_SAVE_OPEN_SOURCE;
            }
            else if(fr != FR_OK) sample_capture_save_fail();
            break;
        }
        case SAMPLE_CAPTURE_SAVE_OPEN_SOURCE:
            if(g_rec_latency_probe.t_save_open_source_start == 0U)
                g_rec_latency_probe.t_save_open_source_start = rec_latency_probe_now();
            if(sd_access_fs_mount_if_needed() == 0U
                    || f_open(&job->source, job->source_path, FA_READ) != FR_OK)
                sample_capture_save_fail();
            else { g_rec_latency_probe.t_save_open_source_done = rec_latency_probe_now();
                job->source_open = 1U; job->phase = SAMPLE_CAPTURE_SAVE_OPEN_DESTINATION; }
            break;
        case SAMPLE_CAPTURE_SAVE_OPEN_DESTINATION:
            if(g_rec_latency_probe.t_save_open_dest_start == 0U)
                g_rec_latency_probe.t_save_open_dest_start = rec_latency_probe_now();
            (void)f_unlink(job->temporary_path);
            if(f_open(&job->destination, job->temporary_path,
                      FA_CREATE_NEW | FA_WRITE) != FR_OK)
                sample_capture_save_fail();
            else { g_rec_latency_probe.t_save_open_dest_done = rec_latency_probe_now();
                job->destination_open = 1U; job->phase = SAMPLE_CAPTURE_SAVE_READ_HEADER; }
            break;
        case SAMPLE_CAPTURE_SAVE_READ_HEADER:
        {
            const uint32_t io_start = rec_latency_probe_now();
            fr = f_read(&job->source, job->header, sizeof(job->header), &transferred);
            const uint32_t io_ticks = rec_latency_probe_now() - io_start;
            g_rec_latency_probe.save_read_io_count++;
            g_rec_latency_probe.save_read_total_ticks += io_ticks;
            if(io_ticks > g_rec_latency_probe.save_read_max_ticks)
                g_rec_latency_probe.save_read_max_ticks = io_ticks;
            if((fr != FR_OK) || (transferred != sizeof(job->header))) sample_capture_save_fail();
            else
            {
                const uint32_t data_bytes = job->saved_frames * AUDIO_RECORDER_BYTES_PER_FRAME;
                const uint32_t riff_bytes = data_bytes
                    + SAMPLE_CAPTURE_WAV_DATA_OFFSET - 8U;
                job->header[4] = (uint8_t)(riff_bytes & 0xFFU);
                job->header[5] = (uint8_t)((riff_bytes >> 8U) & 0xFFU);
                job->header[6] = (uint8_t)((riff_bytes >> 16U) & 0xFFU);
                job->header[7] = (uint8_t)((riff_bytes >> 24U) & 0xFFU);
                job->header[SAMPLE_CAPTURE_WAV_DATA_OFFSET - 4U] =
                    (uint8_t)(data_bytes & 0xFFU);
                job->header[SAMPLE_CAPTURE_WAV_DATA_OFFSET - 3U] =
                    (uint8_t)((data_bytes >> 8U) & 0xFFU);
                job->header[SAMPLE_CAPTURE_WAV_DATA_OFFSET - 2U] =
                    (uint8_t)((data_bytes >> 16U) & 0xFFU);
                job->header[SAMPLE_CAPTURE_WAV_DATA_OFFSET - 1U] =
                    (uint8_t)((data_bytes >> 24U) & 0xFFU);
                job->phase = SAMPLE_CAPTURE_SAVE_WRITE_HEADER;
            }
            break;
        }
        case SAMPLE_CAPTURE_SAVE_WRITE_HEADER:
        {
            const uint32_t io_start = rec_latency_probe_now();
            fr = f_write(&job->destination, job->header, sizeof(job->header), &transferred);
            const uint32_t io_ticks = rec_latency_probe_now() - io_start;
            g_rec_latency_probe.save_write_io_count++;
            g_rec_latency_probe.save_write_total_ticks += io_ticks;
            if(io_ticks > g_rec_latency_probe.save_write_max_ticks)
                g_rec_latency_probe.save_write_max_ticks = io_ticks;
            if((fr != FR_OK) || (transferred != sizeof(job->header))) sample_capture_save_fail();
            else job->phase = SAMPLE_CAPTURE_SAVE_SEEK_DATA;
            break;
        }
        case SAMPLE_CAPTURE_SAVE_SEEK_DATA:
            fr = f_lseek(&job->source, SAMPLE_CAPTURE_WAV_DATA_OFFSET
                + (job->start_frame * AUDIO_RECORDER_BYTES_PER_FRAME));
            if(fr != FR_OK) sample_capture_save_fail();
            else job->phase = SAMPLE_CAPTURE_SAVE_READ_DATA;
            break;
        case SAMPLE_CAPTURE_SAVE_READ_DATA:
        {
            if(g_rec_latency_probe.t_save_copy_start == 0U)
                g_rec_latency_probe.t_save_copy_start = rec_latency_probe_now();
            const uint32_t io_start = rec_latency_probe_now();
            fr = f_read(&job->source, g_sample_capture_copy_buf,
                        job->chunk_bytes, &transferred);
            const uint32_t io_ticks = rec_latency_probe_now() - io_start;
            g_rec_latency_probe.save_read_io_count++;
            g_rec_latency_probe.save_read_total_ticks += io_ticks;
            if(io_ticks > g_rec_latency_probe.save_read_max_ticks)
                g_rec_latency_probe.save_read_max_ticks = io_ticks;
            if((fr != FR_OK) || (transferred != job->chunk_bytes)) sample_capture_save_fail();
            else {
                g_rec_latency_probe.save_read_bytes += transferred;
                g_rec_latency_probe.save_last_chunk_bytes = transferred;
                if(transferred > g_rec_latency_probe.save_max_bytes_per_service)
                    g_rec_latency_probe.save_max_bytes_per_service = transferred;
                rec_latency_probe_progress();
                job->phase = SAMPLE_CAPTURE_SAVE_WRITE_DATA;
            }
            break;
        }
        case SAMPLE_CAPTURE_SAVE_WRITE_DATA:
        {
            const uint32_t io_start = rec_latency_probe_now();
            fr = f_write(&job->destination, g_sample_capture_copy_buf,
                         job->chunk_bytes, &transferred);
            const uint32_t io_ticks = rec_latency_probe_now() - io_start;
            g_rec_latency_probe.save_write_io_count++;
            g_rec_latency_probe.save_write_total_ticks += io_ticks;
            if(io_ticks > g_rec_latency_probe.save_write_max_ticks)
                g_rec_latency_probe.save_write_max_ticks = io_ticks;
            if((fr != FR_OK) || (transferred != job->chunk_bytes)) sample_capture_save_fail();
            else { g_rec_latency_probe.save_write_bytes += transferred;
                g_rec_latency_probe.save_last_chunk_bytes = transferred;
                if(transferred > g_rec_latency_probe.save_max_bytes_per_service)
                    g_rec_latency_probe.save_max_bytes_per_service = transferred;
                rec_latency_probe_progress();
                job->bytes_left -= job->chunk_bytes; job->chunk_bytes = 0U; job->phase =
                (job->bytes_left != 0U) ? SAMPLE_CAPTURE_SAVE_READ_DATA
                                        : SAMPLE_CAPTURE_SAVE_SYNC;
                if(job->bytes_left == 0U) g_rec_latency_probe.t_save_copy_done = rec_latency_probe_now(); }
            break;
        }
        case SAMPLE_CAPTURE_SAVE_SYNC:
            if(g_rec_latency_probe.t_save_sync_start == 0U)
                g_rec_latency_probe.t_save_sync_start = rec_latency_probe_now();
            if(f_sync(&job->destination) != FR_OK) sample_capture_save_fail();
            else { g_rec_latency_probe.t_save_sync_done = rec_latency_probe_now();
                job->phase = SAMPLE_CAPTURE_SAVE_CLOSE_DESTINATION; }
            break;
        case SAMPLE_CAPTURE_SAVE_CLOSE_DESTINATION:
            if(g_rec_latency_probe.t_save_close_start == 0U)
                g_rec_latency_probe.t_save_close_start = rec_latency_probe_now();
            if(f_close(&job->destination) != FR_OK) sample_capture_save_fail();
            else { job->destination_open = 0U; job->phase = SAMPLE_CAPTURE_SAVE_CLOSE_SOURCE; }
            break;
        case SAMPLE_CAPTURE_SAVE_CLOSE_SOURCE:
            if(f_close(&job->source) != FR_OK) sample_capture_save_fail();
            else { g_rec_latency_probe.t_save_close_done = rec_latency_probe_now();
                job->source_open = 0U; job->phase = SAMPLE_CAPTURE_SAVE_RENAME; }
            break;
        case SAMPLE_CAPTURE_SAVE_RENAME:
            if(g_rec_latency_probe.t_save_rename_start == 0U)
                g_rec_latency_probe.t_save_rename_start = rec_latency_probe_now();
            if(f_rename(job->temporary_path, job->final_path) != FR_OK)
                sample_capture_save_fail();
            else { g_rec_latency_probe.t_save_rename_done = rec_latency_probe_now();
                sample_capture_save_finish(); }
            break;
        case SAMPLE_CAPTURE_SAVE_ROLLBACK_DESTINATION:
            (void)f_close(&job->destination); job->destination_open = 0U;
            job->phase = (job->source_open != 0U)
                ? SAMPLE_CAPTURE_SAVE_ROLLBACK_SOURCE : SAMPLE_CAPTURE_SAVE_ROLLBACK_UNLINK;
            break;
        case SAMPLE_CAPTURE_SAVE_ROLLBACK_SOURCE:
            (void)f_close(&job->source); job->source_open = 0U;
            job->phase = SAMPLE_CAPTURE_SAVE_ROLLBACK_UNLINK;
            break;
        case SAMPLE_CAPTURE_SAVE_ROLLBACK_UNLINK:
            (void)f_unlink(job->temporary_path);
            memset(job, 0, sizeof(*job));
            g_sample_capture.state.save_pending = 0U;
            break;
        default:
            sample_capture_save_fail();
            break;
    }
    sd_scheduler_runtime_background_end();
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

    if(g_sample_capture.save_job.phase != SAMPLE_CAPTURE_SAVE_IDLE)
        return 1U;
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

    sample_capture_save_job_t *const job = &g_sample_capture.save_job;
    memset(job, 0, sizeof(*job));
    sample_capture_copy_path(job->source_path, current_path);
    job->start_frame = save_start;
    job->saved_frames = save_end - save_start;
    job->bytes_left = job->saved_frames * AUDIO_RECORDER_BYTES_PER_FRAME;
    g_rec_latency_probe.save_bytes_total = job->bytes_left;
    g_rec_latency_probe.save_bytes_remaining = job->bytes_left;
    job->phase = SAMPLE_CAPTURE_SAVE_SELECT_PATH;
    g_rec_latency_probe.t_save_start = rec_latency_probe_now();
    g_sample_capture.state.save_pending = 1U;
    g_sample_capture.state.error = SAMPLE_CAPTURE_ERROR_NONE;
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
