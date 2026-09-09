static void wavetable_load_job_boot_init(void)
{
    memset(&g_wavetable_load_job, 0, sizeof(g_wavetable_load_job));
    g_wavetable_load_job.state = WAVETABLE_LOAD_IDLE;
    wavetable_pool_candidate_init(&g_wavetable_candidate);
}

void wavetable_pool_load_async_cancel(void)
{
    wavetable_load_job_t *const job = &g_wavetable_load_job;
    wavetable_restore_retained_old();
    if (job->cache_open != 0U)
    {
        (void)f_close(&job->cache);
        job->cache_open = 0U;
    }
    if (job->source_open != 0U)
    {
        (void)f_close(&job->source);
        job->source_open = 0U;
    }
    if (job->temp_path[0] != '\0')
    {
        (void)f_unlink(job->temp_path);
    }
    if (job->global_reserved != 0U)
        sample_global_pool_clear_slot(job->global_slot);
    wavetable_pool_candidate_release(&g_wavetable_candidate);
    memset(job, 0, sizeof(*job));
    job->state = WAVETABLE_LOAD_IDLE;
}

static void wavetable_load_fail(wavetable_result_t result)
{
    wavetable_restore_retained_old();
    if (g_wavetable_load_job.global_reserved != 0U)
    {
        sample_global_pool_clear_slot(g_wavetable_load_job.global_slot);
        g_wavetable_load_job.global_reserved = 0U;
    }
    g_wavetable_load_job.result = result;
    g_wavetable_load_job.state = WAVETABLE_LOAD_CLEANUP;
    wavetable_pool_set_last(result);
}

static void wavetable_load_cache_miss(wavetable_load_job_t *job)
{
    if (job->cache_open != 0U)
    {
        (void)f_close(&job->cache);
        job->cache_open = 0U;
    }
    if (job->cache_path[0] != '\0') (void)f_unlink(job->cache_path);
    job->io_offset = 0U;
    job->band = 0U;
    job->cycle = 0U;
    job->state = WAVETABLE_LOAD_DATA_SEEK;
}

static uint32_t wavetable_load_io_quantum(void)
{
    return (sample_stream_manager_has_pending_sd_work() != 0U)
        ? SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES
        : WAVETABLE_POOL_IO_BYTES;
}

uint8_t wavetable_pool_load_async_begin_with_geometry(
    uint16_t wavetable_slot,
    const char *path,
    wavetable_source_geometry_t source_geometry)
{
    return wavetable_pool_load_async_begin_prepared_with_geometry(
        wavetable_slot, path, source_geometry, NULL, 0U, 0U);
}

uint8_t wavetable_pool_load_async_begin_prepared_with_geometry(
    uint16_t wavetable_slot,
    const char *path,
    wavetable_source_geometry_t source_geometry,
    const wav_info_t *prepared_info,
    uint32_t source_file_size,
    uint32_t source_crc32)
{
    wavetable_load_job_t *const job = &g_wavetable_load_job;
    if ((seq_runtime_is_running() != 0U)
        || (seq_runtime_is_start_pending() != 0U))
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_TRANSPORT_ACTIVE);
        return 0U;
    }
    if (audio_recorder_is_active() != 0U)
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_RECORDER_ACTIVE);
        return 0U;
    }
    if ((sampler_ram_pool_load_async_busy() != 0U)
        || (multi_sample_load_has_pending() != 0U))
        return 0U;
    if ((job->state != WAVETABLE_LOAD_IDLE)
        || (wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS)
        || (path == 0) || (path[0] == '\0')
        || ((prepared_info == NULL)
            && (wavetable_pool_path_ext_is_wav(path) == 0U))
        || ((source_geometry != WAVETABLE_SOURCE_GEOMETRY_1024)
            && (source_geometry != WAVETABLE_SOURCE_GEOMETRY_2048)))
    {
        return 0U;
    }
    if (g_wavetable_pool.slots[wavetable_slot].state
        == WAVETABLE_SLOT_RETIRING) return 0U;
    memset(job, 0, sizeof(*job));
    wavetable_pool_candidate_init(&g_wavetable_candidate);
    if (wavetable_pool_copy_path(job->path, sizeof(job->path), path) == 0U)
    {
        return 0U;
    }
    job->wavetable_slot = wavetable_slot;
    job->global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    job->media_epoch = sd_access_media_epoch();
    job->source_geometry = source_geometry;
    if (prepared_info != NULL)
    {
        job->wav_info = *prepared_info;
        job->source_crc = 0U;
        job->prepared_file_size = source_file_size;
        job->prepared_expected_crc = source_crc32;
        job->prepared = 1U;
    }
    job->result = WAVETABLE_RESULT_OK;
    job->state = WAVETABLE_LOAD_MOUNT;
    return 1U;
}

uint8_t wavetable_pool_load_async_begin(uint16_t wavetable_slot, const char *path)
{
    return wavetable_pool_load_async_begin_with_geometry(
        wavetable_slot, path, WAVETABLE_DEFAULT_SOURCE_GEOMETRY);
}

uint8_t wavetable_pool_load_async_busy(void)
{
    return ((g_wavetable_load_job.state != WAVETABLE_LOAD_IDLE)
            && (g_wavetable_load_job.state != WAVETABLE_LOAD_DONE)) ? 1U : 0U;
}

static void wavetable_load_process_band(wavetable_load_job_t *job)
{
    wavetable_slot_t *const candidate = &g_wavetable_candidate;
    wavetable_mipmap_view_t *const view = &candidate->mipmap;
    wavetable_mipmap_band_t *const band = &view->bands[job->band];
    const uint32_t count = band->cycle_sample_count;
    const uint32_t cycle_stride = wavetable_pool_mipmap_cycle_stride(count);
    float *const dst = &band->data[job->cycle * cycle_stride];
    wavetable_pool_generate_band_from_fft(dst, count, job->band);
    if (cycle_stride > count)
    {
        memset(&dst[count], 0,
               (cycle_stride - count) * sizeof(float));
    }
    job->band++;
    if (job->band >= view->band_count)
    {
        job->band = 0U;
        wavetable_pool_copy_float_exact(
            &candidate->data[job->cycle * WAVETABLE_FRAME_SAMPLE_COUNT],
            &view->bands[0].data[job->cycle * WAVETABLE_FRAME_SAMPLE_COUNT],
            WAVETABLE_FRAME_SAMPLE_COUNT);
        job->cycle++;
        job->state = (job->cycle >= candidate->frame_count)
            ? WAVETABLE_LOAD_BASE_CRC : WAVETABLE_LOAD_MIP_FORWARD_PREP;
        job->io_offset = 0U;
    }
}

static void wavetable_pool_load_async_step(void)
{
    wavetable_load_job_t *const job = &g_wavetable_load_job;
    wavetable_slot_t *const candidate = &g_wavetable_candidate;
    if ((job->state == WAVETABLE_LOAD_IDLE) || (job->state == WAVETABLE_LOAD_DONE)) return;
    if ((seq_runtime_is_running() != 0U)
        || (seq_runtime_is_start_pending() != 0U))
        wavetable_load_fail(WAVETABLE_RESULT_TRANSPORT_ACTIVE);
    else if (audio_recorder_is_active() != 0U)
        wavetable_load_fail(WAVETABLE_RESULT_RECORDER_ACTIVE);

    switch (job->state)
    {
        case WAVETABLE_LOAD_ALLOCATE:
        {
            const uint32_t samples = job->wav_info.data_size / job->wav_info.block_align;
            job->source_cycle_sample_count =
                wavetable_pool_source_cycle_sample_count(job->source_geometry);
            if ((samples % job->source_cycle_sample_count) != 0U)
            {
                wavetable_load_fail(WAVETABLE_RESULT_UNSUPPORTED);
                return;
            }
            const wavetable_result_t result = wavetable_pool_candidate_allocate(
                job->wavetable_slot, job->path,
                samples / job->source_cycle_sample_count, candidate);
            if (result != WAVETABLE_RESULT_OK) wavetable_load_fail(result);
            else job->state = WAVETABLE_LOAD_CACHE_LOOKUP;
            return;
        }
        case WAVETABLE_LOAD_DECODE:
        {
            uint32_t count = job->buffered_samples - job->converted_samples;
            if (count > WAVETABLE_DECODE_QUANTUM_SAMPLES)
                count = WAVETABLE_DECODE_QUANTUM_SAMPLES;
            const uint8_t *src = &g_wavetable_pool_io[
                job->converted_samples * job->wav_info.block_align];
            for (uint32_t i = 0U; i < count; ++i)
            {
                float left = 0.0f, right = 0.0f;
                wav_audio_codec_decode_stereo_frame(src, job->wav_info.channels,
                                                    job->wav_info.bits_per_sample,
                                                    &left, &right);
                const float mono = (job->wav_info.channels == 1U)
                    ? left : ((left + right) * 0.5f);
                if (job->source_cycle_sample_count == WAVETABLE_FRAME_SAMPLE_COUNT)
                {
                    candidate->data[job->io_offset
                        + job->converted_samples + i] =
                        wavetable_pool_finite_float(mono);
                }
                else
                {
                    wavetable_pool_source_2048_store(
                        job->source_cycle_offset++, mono);
                    if (job->source_cycle_offset
                            == WAVETABLE_SOURCE_2048_SAMPLE_COUNT)
                    {
                        wavetable_pool_resample_2048_cycle_frequency(
                            &candidate->data[job->cycle
                                * WAVETABLE_FRAME_SAMPLE_COUNT]);
                        job->cycle++;
                        job->source_cycle_offset = 0U;
                    }
                }
                src += job->wav_info.block_align;
            }
            job->converted_samples += count;
            if (job->converted_samples >= job->buffered_samples)
            {
                job->io_offset += job->buffered_samples;
                job->buffered_samples = 0U;
                job->converted_samples = 0U;
                job->state = (job->io_offset >= candidate->frame_count
                        * job->source_cycle_sample_count)
                    ? WAVETABLE_LOAD_MIP_INIT : WAVETABLE_LOAD_DATA_READ;
            }
            return;
        }
        case WAVETABLE_LOAD_MIP_INIT:
            wavetable_pool_candidate_mipmap_init(candidate);
            job->cycle = 0U;
            job->band = 0U;
            job->state = WAVETABLE_LOAD_MIP_FORWARD_PREP;
            return;
        case WAVETABLE_LOAD_MIP_FORWARD_PREP:
        {
            const float *src = &candidate->data[job->cycle * WAVETABLE_FRAME_SAMPLE_COUNT];
            for (uint32_t i = 0U; i < WAVETABLE_FRAME_SAMPLE_COUNT; ++i)
            {
                g_wavetable_fft_real[i] = src[i];
                g_wavetable_fft_imag[i] = 0.0f;
            }
            job->state = WAVETABLE_LOAD_MIP_FORWARD_FFT;
            return;
        }
        case WAVETABLE_LOAD_MIP_FORWARD_FFT:
            wavetable_pool_fft(g_wavetable_fft_real, g_wavetable_fft_imag,
                               WAVETABLE_FRAME_SAMPLE_COUNT, 0U);
            job->state = WAVETABLE_LOAD_MIP_BAND;
            return;
        case WAVETABLE_LOAD_MIP_BAND:
            wavetable_load_process_band(job);
            return;
        case WAVETABLE_LOAD_BASE_CRC:
        case WAVETABLE_LOAD_PAYLOAD_CRC:
        {
            const uint8_t *data = (job->state == WAVETABLE_LOAD_BASE_CRC)
                ? (const uint8_t *)candidate->data : (const uint8_t *)candidate->mipmap.data;
            const uint32_t total = (job->state == WAVETABLE_LOAD_BASE_CRC)
                ? candidate->data_bytes : candidate->mipmap.data_bytes;
            uint32_t chunk = total - job->io_offset;
            const uint32_t quantum = wavetable_load_io_quantum();
            if (chunk > quantum) chunk = quantum;
            if (job->state == WAVETABLE_LOAD_BASE_CRC)
                job->base_crc = wavetable_pool_crc32_update(job->base_crc, &data[job->io_offset], chunk);
            else
                job->payload_crc = wavetable_pool_crc32_update(job->payload_crc, &data[job->io_offset], chunk);
            job->io_offset += chunk;
            if (job->io_offset >= total)
            {
                job->io_offset = 0U;
                job->state = (job->state == WAVETABLE_LOAD_BASE_CRC)
                    ? WAVETABLE_LOAD_PAYLOAD_CRC : WAVETABLE_LOAD_CACHE_DIR;
            }
            return;
        }
        case WAVETABLE_LOAD_PREVIEW_INIT:
            memset(&candidate->preview, 0, sizeof(candidate->preview));
            candidate->preview.state = WAVETABLE_PREVIEW_READY;
            candidate->preview.generation = candidate->generation;
            candidate->preview.frame_count = candidate->frame_count;
            candidate->preview.columns = WAVETABLE_PREVIEW_COLUMNS;
            for (uint16_t col = 0U; col < WAVETABLE_PREVIEW_COLUMNS; ++col)
            {
                candidate->preview.min[col] = 32767;
                candidate->preview.max[col] = -32768;
            }
            job->cycle = 0U;
            job->state = WAVETABLE_LOAD_PREVIEW_FRAME;
            return;
        case WAVETABLE_LOAD_PREVIEW_FRAME:
        {
            uint32_t col = (uint32_t)(((uint64_t)job->cycle * WAVETABLE_PREVIEW_COLUMNS)
                                      / candidate->frame_count);
            if (col >= WAVETABLE_PREVIEW_COLUMNS) col = WAVETABLE_PREVIEW_COLUMNS - 1U;
            const float *src = &candidate->data[job->cycle * WAVETABLE_FRAME_SAMPLE_COUNT];
            float min = 1.0f, max = -1.0f;
            for (uint32_t i = 0U; i < WAVETABLE_FRAME_SAMPLE_COUNT; ++i)
            {
                if (src[i] < min) min = src[i];
                if (src[i] > max) max = src[i];
            }
            const int16_t min_s16 = wavetable_pool_preview_float_to_i16(min);
            const int16_t max_s16 = wavetable_pool_preview_float_to_i16(max);
            if (min_s16 < candidate->preview.min[col]) candidate->preview.min[col] = min_s16;
            if (max_s16 > candidate->preview.max[col]) candidate->preview.max[col] = max_s16;
            const uint16_t min_peak = wavetable_pool_preview_abs_i16(min_s16);
            const uint16_t max_peak = wavetable_pool_preview_abs_i16(max_s16);
            if (min_peak > candidate->preview.global_peak) candidate->preview.global_peak = min_peak;
            if (max_peak > candidate->preview.global_peak) candidate->preview.global_peak = max_peak;
            job->cycle++;
            if (job->cycle >= candidate->frame_count)
            {
                for (uint16_t i = 0U; i < WAVETABLE_PREVIEW_COLUMNS; ++i)
                    if (candidate->preview.min[i] > candidate->preview.max[i])
                        candidate->preview.min[i] = candidate->preview.max[i] = 0;
                job->state = WAVETABLE_LOAD_PUBLISH;
            }
            return;
        }
        case WAVETABLE_LOAD_CACHE_HIT_BASE:
        {
            const wavetable_mipmap_band_t *const base = &candidate->mipmap.bands[0];
            const uint32_t stride =
                wavetable_pool_mipmap_cycle_stride(base->cycle_sample_count);
            wavetable_pool_copy_float_exact(
                &candidate->data[job->cycle * WAVETABLE_FRAME_SAMPLE_COUNT],
                &base->data[job->cycle * stride],
                WAVETABLE_FRAME_SAMPLE_COUNT);
            job->cycle++;
            if (job->cycle >= candidate->frame_count)
            {
                job->io_offset = 0U;
                job->base_crc = 0U;
                job->state = WAVETABLE_LOAD_CACHE_HIT_BASE_CRC;
            }
            return;
        }
        case WAVETABLE_LOAD_CACHE_HIT_BASE_CRC:
        {
            uint32_t chunk = candidate->data_bytes - job->io_offset;
            const uint32_t quantum = wavetable_load_io_quantum();
            if (chunk > quantum) chunk = quantum;
            job->base_crc = wavetable_pool_crc32_update(
                job->base_crc,
                &((const uint8_t *)candidate->data)[job->io_offset], chunk);
            job->io_offset += chunk;
            if (job->io_offset >= candidate->data_bytes)
            {
                if (job->base_crc != job->cache_header.base_crc32)
                    wavetable_load_cache_miss(job);
                else
                    job->state = WAVETABLE_LOAD_CACHE_HIT_CLOSE;
            }
            return;
        }
        case WAVETABLE_LOAD_PUBLISH:
        {
            wavetable_slot_t *const destination =
                &g_wavetable_pool.slots[job->wavetable_slot];
            const wavetable_slot_state_t state = destination->state;
            if (candidate->generation == 0U)
            {
                uint16_t global_slot = destination->global_slot;
                if (global_slot >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
                {
                    if (sample_global_pool_reserve_wavetable(
                            job->wavetable_slot, candidate->path,
                            candidate->cost_bytes_aligned, &global_slot) == 0U)
                    {
                        wavetable_load_fail(WAVETABLE_RESULT_REGISTER_FAIL);
                        return;
                    }
                    job->global_reserved = 1U;
                }
                job->global_slot = global_slot;
                candidate->global_slot = global_slot;
                candidate->generation = wavetable_pool_next_generation();
                candidate->state = WAVETABLE_SLOT_READY;
                candidate->error = WAVETABLE_RESULT_OK;
                candidate->preview.generation = candidate->generation;
                if (audio_wave_table_projection_build_descriptor(
                        job->wavetable_slot, candidate,
                        &job->prepared_descriptor) == 0U)
                {
                    wavetable_load_fail(WAVETABLE_RESULT_REGISTER_FAIL);
                    return;
                }
            }
            if ((state == WAVETABLE_SLOT_READY)
                && (job->quiesce_committed == 0U))
            {
                job->old_snapshot = *destination;
                job->retain_old_for_commit = 1U;
                if (job->ingress_closed == 0U)
                {
                    resource_mutation_ingress_close();
                    job->ingress_closed = 1U;
                }
                uint64_t now_sample = 0U;
                uint64_t stop_sample = 0U;
                if ((control_rt_publication_horizon_active() != 0U)
                    || (control_rt_now_sample(&now_sample) == 0U)
                    || (control_rt_resolve_asap_sample(now_sample,
                                                       &stop_sample) == 0U)
                    || (control_rt_publish_param_now(
                            (uint8_t)job->wavetable_slot, 0xFFF7U,
                            destination->generation, 0U) == 0U))
                    return;
                job->retire_not_before_sample = stop_sample
                    + CONTROL_AUDIO_RESOURCE_RETIRE_GRACE_FRAMES;
                job->quiesce_committed = 1U;
                job->state = WAVETABLE_LOAD_WAIT_SAFE;
                return;
            }
            if (state == WAVETABLE_SLOT_RETIRING)
            {
                wavetable_load_fail(WAVETABLE_RESULT_REGISTER_FAIL);
                return;
            }
            if (sample_global_pool_register_wavetable_at(
                    job->global_slot, job->wavetable_slot, candidate->path,
                    candidate->cost_bytes_aligned) == 0U)
                brick_fatal_raise(BRICK_FATAL_WAVETABLE_COMMIT,
                                  job->wavetable_slot, job->global_slot,
                                  candidate->cost_bytes_aligned,
                                  SAMPLE_GLOBAL_POOL_BUDGET_BYTES);
            job->global_reserved = 0U;
            const uint32_t primask = __get_PRIMASK();
            __disable_irq();
            *destination = *candidate;
            __DMB();
            if (primask == 0U) __enable_irq();
            candidate->page_count = 0U;
            candidate->mipmap.page_count = 0U;
            audio_wave_table_projection_install_prepared(
                &job->prepared_descriptor);
            wavetable_pool_release_slot_allocations(&job->old_snapshot);
            job->retain_old_for_commit = 0U;
            job->quiesce_committed = 0U;
            if (job->ingress_closed != 0U)
            {
                resource_mutation_ingress_open();
                job->ingress_closed = 0U;
            }
            {
                job->result = WAVETABLE_RESULT_OK;
                job->state = WAVETABLE_LOAD_DONE;
                wavetable_pool_set_last(WAVETABLE_RESULT_OK);
            }
            return;
        }
        case WAVETABLE_LOAD_WAIT_SAFE:
        {
            uint64_t now_sample = 0U;
            if ((control_rt_now_sample(&now_sample) != 0U)
                && (now_sample >= job->retire_not_before_sample))
                job->state = WAVETABLE_LOAD_PUBLISH;
            return;
        }
        default:
            break;
    }

    sd_scheduler_background_kind_t kind = SD_SCHEDULER_BACKGROUND_METADATA;
    uint32_t bytes = 0U;
    if ((job->state == WAVETABLE_LOAD_CRC_READ)
        || (job->state == WAVETABLE_LOAD_PARSE)
        || (job->state == WAVETABLE_LOAD_DATA_READ)
        || (job->state == WAVETABLE_LOAD_CACHE_HIT_HEADER)
        || (job->state == WAVETABLE_LOAD_CACHE_HIT_BAND)
        || (job->state == WAVETABLE_LOAD_CACHE_HIT_PAYLOAD)
        || (job->state == WAVETABLE_LOAD_CACHE_HEADER)
        || (job->state == WAVETABLE_LOAD_CACHE_BAND)
        || (job->state == WAVETABLE_LOAD_CACHE_PAYLOAD))
    {
        kind = SD_SCHEDULER_BACKGROUND_DATA;
        if (job->state == WAVETABLE_LOAD_PARSE)
            bytes = SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES;
        else if ((job->state == WAVETABLE_LOAD_CACHE_HEADER)
                 || (job->state == WAVETABLE_LOAD_CACHE_HIT_HEADER))
            bytes = WAVETABLE_PREPARED_HEADER_SIZE;
        else if ((job->state == WAVETABLE_LOAD_CACHE_BAND)
                 || (job->state == WAVETABLE_LOAD_CACHE_HIT_BAND))
            bytes = WAVETABLE_PREPARED_BAND_ENTRY_SIZE;
        else
        {
            uint32_t remaining = 0U;
            if (job->state == WAVETABLE_LOAD_CRC_READ)
                remaining = (uint32_t)job->source_info.fsize - job->io_offset;
            else if (job->state == WAVETABLE_LOAD_DATA_READ)
                remaining = (candidate->frame_count * job->source_cycle_sample_count - job->io_offset)
                            * job->wav_info.block_align;
            else remaining = candidate->mipmap.data_bytes - job->io_offset;
            const uint32_t quantum = wavetable_load_io_quantum();
            bytes = (remaining > quantum) ? quantum : remaining;
        }
    }
    const sd_scheduler_background_admission_t admission = wavetable_load_sd_begin(kind, bytes);
    if (admission == SD_SCHEDULER_BACKGROUND_NOT_NOW) return;
    if (admission != SD_SCHEDULER_BACKGROUND_GO)
    {
        job->source_open = 0U;
        job->cache_open = 0U;
        wavetable_load_fail(WAVETABLE_RESULT_SD_MOUNT_FAIL);
        wavetable_pool_candidate_release(candidate);
        job->state = WAVETABLE_LOAD_DONE;
        return;
    }

    switch (job->state)
    {
        case WAVETABLE_LOAD_MOUNT:
            if (sd_access_fs_mount_if_needed() == 0U) wavetable_load_fail(WAVETABLE_RESULT_SD_MOUNT_FAIL);
            else job->state = WAVETABLE_LOAD_STAT;
            break;
        case WAVETABLE_LOAD_STAT:
            if ((f_stat(job->path, &job->source_info) != FR_OK)
                || ((job->prepared != 0U)
                    && ((uint32_t)job->source_info.fsize != job->prepared_file_size)))
                wavetable_load_fail(WAVETABLE_RESULT_OPEN_FAIL);
            else job->state = WAVETABLE_LOAD_OPEN;
            break;
        case WAVETABLE_LOAD_OPEN:
            if (f_open(&job->source, job->path, FA_READ) != FR_OK) wavetable_load_fail(WAVETABLE_RESULT_OPEN_FAIL);
            else
            {
                job->source_open = 1U;
                job->state = WAVETABLE_LOAD_CRC_SEEK;
            }
            break;
        case WAVETABLE_LOAD_CRC_SEEK:
            if (f_lseek(&job->source, 0U) != FR_OK) wavetable_load_fail(WAVETABLE_RESULT_READ_FAIL);
            else { job->io_offset = 0U; job->source_crc = 0U; job->state = WAVETABLE_LOAD_CRC_READ; }
            break;
        case WAVETABLE_LOAD_CRC_READ:
        {
            UINT read = 0U;
            if ((f_read(&job->source, g_wavetable_pool_io, bytes, &read) != FR_OK) || (read != bytes))
                wavetable_load_fail(WAVETABLE_RESULT_READ_FAIL);
            else
            {
                job->source_crc = wavetable_pool_crc32_update(job->source_crc, g_wavetable_pool_io, read);
                job->io_offset += read;
                if (job->io_offset >= (uint32_t)job->source_info.fsize)
                {
                    if ((job->prepared != 0U)
                        && (job->source_crc != job->prepared_expected_crc))
                        wavetable_load_fail(WAVETABLE_RESULT_READ_FAIL);
                    else
                        job->state = (job->prepared != 0U)
                            ? WAVETABLE_LOAD_ALLOCATE : WAVETABLE_LOAD_PARSE;
                }
            }
            break;
        }
        case WAVETABLE_LOAD_PARSE:
            if ((wav_parser_parse_info(&job->source, &job->wav_info) == 0)
                || (wavetable_pool_wav_info_valid(&job->wav_info) == 0U))
                wavetable_load_fail(WAVETABLE_RESULT_UNSUPPORTED);
            else job->state = WAVETABLE_LOAD_ALLOCATE;
            break;
        case WAVETABLE_LOAD_CACHE_LOOKUP:
            if (wavetable_pool_make_cache_path(job->cache_path,
                                               sizeof(job->cache_path),
                                               job->path,
                                               &job->source_info) == 0U)
                wavetable_load_fail(WAVETABLE_RESULT_PATH_TOO_LONG);
            else
                job->state = WAVETABLE_LOAD_CACHE_HIT_OPEN;
            break;
        case WAVETABLE_LOAD_CACHE_HIT_OPEN:
            if (f_open(&job->cache, job->cache_path, FA_READ) != FR_OK)
                job->state = WAVETABLE_LOAD_DATA_SEEK;
            else
            {
                job->cache_open = 1U;
                job->state = WAVETABLE_LOAD_CACHE_HIT_HEADER;
            }
            break;
        case WAVETABLE_LOAD_CACHE_HIT_HEADER:
        {
            UINT read = 0U;
            if ((f_read(&job->cache, g_wavetable_pool_io, bytes, &read) != FR_OK)
                || (read != bytes)
                || (wavetable_pool_prepared_header_validate(
                        g_wavetable_pool_io, (uint32_t)f_size(&job->cache),
                        job->path, &job->source_info, job->source_crc,
                        candidate, &job->cache_header) == 0U))
            {
                wavetable_load_cache_miss(job);
            }
            else
            {
                job->band = 0U;
                job->io_offset = 0U;
                job->state = WAVETABLE_LOAD_CACHE_HIT_BAND;
            }
            break;
        }
        case WAVETABLE_LOAD_CACHE_HIT_BAND:
        {
            UINT read = 0U;
            uint32_t next_offset = 0U;
            if ((f_read(&job->cache, g_wavetable_pool_io, bytes, &read) != FR_OK)
                || (read != bytes)
                || (wavetable_pool_prepared_band_decode_validate(
                        g_wavetable_pool_io, job->band, job->io_offset,
                        candidate, &next_offset) == 0U))
            {
                wavetable_load_cache_miss(job);
            }
            else
            {
                job->io_offset = next_offset;
                job->band++;
                if (job->band >= candidate->mipmap.band_count)
                {
                    if ((job->io_offset != candidate->mipmap.data_bytes)
                        || (f_lseek(&job->cache,
                                    job->cache_header.data_offset) != FR_OK))
                        wavetable_load_cache_miss(job);
                    else
                    {
                        job->io_offset = 0U;
                        job->payload_crc = 0U;
                        job->state = WAVETABLE_LOAD_CACHE_HIT_PAYLOAD;
                    }
                }
            }
            break;
        }
        case WAVETABLE_LOAD_CACHE_HIT_PAYLOAD:
        {
            UINT read = 0U;
            if ((f_read(&job->cache, g_wavetable_pool_io, bytes, &read) != FR_OK)
                || (read != bytes))
            {
                wavetable_load_cache_miss(job);
            }
            else
            {
                memcpy(&((uint8_t *)candidate->mipmap.data)[job->io_offset],
                       g_wavetable_pool_io, read);
                job->payload_crc = wavetable_pool_crc32_update(
                    job->payload_crc, g_wavetable_pool_io, read);
                job->io_offset += read;
                if (job->io_offset >= candidate->mipmap.data_bytes)
                {
                    if (job->payload_crc != job->cache_header.payload_crc32)
                        wavetable_load_cache_miss(job);
                    else
                    {
                        job->cycle = 0U;
                        job->state = WAVETABLE_LOAD_CACHE_HIT_BASE;
                    }
                }
            }
            break;
        }
        case WAVETABLE_LOAD_CACHE_HIT_CLOSE:
            if (f_close(&job->cache) != FR_OK)
                wavetable_load_fail(WAVETABLE_RESULT_READ_FAIL);
            else
            {
                job->cache_open = 0U;
                job->state = WAVETABLE_LOAD_SOURCE_CLOSE;
            }
            break;
        case WAVETABLE_LOAD_DATA_SEEK:
            if (f_lseek(&job->source, job->wav_info.data_offset) != FR_OK) wavetable_load_fail(WAVETABLE_RESULT_READ_FAIL);
            else { job->io_offset = 0U; job->state = WAVETABLE_LOAD_DATA_READ; }
            break;
        case WAVETABLE_LOAD_DATA_READ:
        {
            uint32_t samples = bytes / job->wav_info.block_align;
            UINT wanted = (UINT)(samples * job->wav_info.block_align), read = 0U;
            if ((f_read(&job->source, g_wavetable_pool_io, wanted, &read) != FR_OK) || (read != wanted))
                wavetable_load_fail(WAVETABLE_RESULT_READ_FAIL);
            else { job->buffered_samples = samples; job->converted_samples = 0U; job->state = WAVETABLE_LOAD_DECODE; }
            break;
        }
        case WAVETABLE_LOAD_CACHE_DIR:
            (void)f_mkdir("0:/WAVETABLES");
            job->state = WAVETABLE_LOAD_CACHE_SUBDIR;
            break;
        case WAVETABLE_LOAD_CACHE_SUBDIR:
            (void)f_mkdir(WAVETABLE_POOL_CACHE_DIR);
            if ((wavetable_pool_make_cache_path(job->cache_path, sizeof(job->cache_path),
                                                job->path, &job->source_info) == 0U)
                || (wavetable_pool_make_temp_path(job->temp_path, sizeof(job->temp_path),
                                                  job->cache_path) == 0U))
                wavetable_load_fail(WAVETABLE_RESULT_PATH_TOO_LONG);
            else job->state = WAVETABLE_LOAD_CACHE_CLEAN;
            break;
        case WAVETABLE_LOAD_CACHE_CLEAN:
            (void)f_unlink(job->temp_path);
            job->state = WAVETABLE_LOAD_CACHE_OPEN;
            break;
        case WAVETABLE_LOAD_CACHE_OPEN:
            if (f_open(&job->cache, job->temp_path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
                wavetable_load_fail(WAVETABLE_RESULT_WRITE_FAIL);
            else { job->cache_open = 1U; job->state = WAVETABLE_LOAD_CACHE_HEADER; }
            break;
        case WAVETABLE_LOAD_CACHE_HEADER:
        {
            wavetable_pool_encode_prepared_header(g_wavetable_pool_io, candidate,
                                                  &job->source_info, job->source_crc,
                                                  job->base_crc, job->payload_crc);
            UINT written = 0U;
            if ((f_write(&job->cache, g_wavetable_pool_io, bytes, &written) != FR_OK) || (written != bytes))
                wavetable_load_fail(WAVETABLE_RESULT_WRITE_FAIL);
            else { job->band = 0U; job->io_offset = 0U; job->state = WAVETABLE_LOAD_CACHE_BAND; }
            break;
        }
        case WAVETABLE_LOAD_CACHE_BAND:
        {
            const wavetable_mipmap_band_t *band = &candidate->mipmap.bands[job->band];
            wavetable_pool_encode_prepared_band(g_wavetable_pool_io, band, job->io_offset);
            UINT written = 0U;
            if ((f_write(&job->cache, g_wavetable_pool_io, bytes, &written) != FR_OK) || (written != bytes))
                wavetable_load_fail(WAVETABLE_RESULT_WRITE_FAIL);
            else
            {
                job->io_offset += band->sample_count * sizeof(float);
                job->band++;
                if (job->band >= candidate->mipmap.band_count) { job->io_offset = 0U; job->state = WAVETABLE_LOAD_CACHE_PAYLOAD; }
            }
            break;
        }
        case WAVETABLE_LOAD_CACHE_PAYLOAD:
        {
            UINT written = 0U;
            const uint8_t *src = (const uint8_t *)candidate->mipmap.data;
            if ((f_write(&job->cache, &src[job->io_offset], bytes, &written) != FR_OK) || (written != bytes))
                wavetable_load_fail(WAVETABLE_RESULT_WRITE_FAIL);
            else { job->io_offset += bytes; if (job->io_offset >= candidate->mipmap.data_bytes) job->state = WAVETABLE_LOAD_CACHE_SYNC; }
            break;
        }
        case WAVETABLE_LOAD_CACHE_SYNC:
            if (f_sync(&job->cache) != FR_OK) wavetable_load_fail(WAVETABLE_RESULT_WRITE_FAIL);
            else job->state = WAVETABLE_LOAD_CACHE_CLOSE;
            break;
        case WAVETABLE_LOAD_CACHE_CLOSE:
            if (f_close(&job->cache) != FR_OK) wavetable_load_fail(WAVETABLE_RESULT_WRITE_FAIL);
            else { job->cache_open = 0U; job->state = WAVETABLE_LOAD_CACHE_FINAL_CLEAN; }
            break;
        case WAVETABLE_LOAD_CACHE_FINAL_CLEAN:
            (void)f_unlink(job->cache_path);
            job->state = WAVETABLE_LOAD_CACHE_RENAME;
            break;
        case WAVETABLE_LOAD_CACHE_RENAME:
            if (f_rename(job->temp_path, job->cache_path) != FR_OK) wavetable_load_fail(WAVETABLE_RESULT_WRITE_FAIL);
            else job->state = WAVETABLE_LOAD_SOURCE_CLOSE;
            break;
        case WAVETABLE_LOAD_SOURCE_CLOSE:
            if (f_close(&job->source) != FR_OK) wavetable_load_fail(WAVETABLE_RESULT_READ_FAIL);
            else { job->source_open = 0U; job->state = WAVETABLE_LOAD_PREVIEW_INIT; }
            break;
        case WAVETABLE_LOAD_CLEANUP:
            if ((job->cleanup_phase == 0U) && (job->cache_open != 0U))
            { (void)f_close(&job->cache); job->cache_open = 0U; job->cleanup_phase++; }
            else if ((job->cleanup_phase <= 1U) && (job->source_open != 0U))
            { (void)f_close(&job->source); job->source_open = 0U; job->cleanup_phase = 2U; }
            else if ((job->cleanup_phase <= 2U) && (job->temp_path[0] != '\0'))
            { (void)f_unlink(job->temp_path); job->cleanup_phase = 3U; }
            else
            { wavetable_pool_candidate_release(candidate); job->state = WAVETABLE_LOAD_DONE; }
            break;
        default:
            break;
    }
    sd_scheduler_runtime_background_end();
}

void wavetable_pool_load_async_service(void)
{
    const uint32_t started_at = HAL_GetTick();
    for (uint32_t step = 0U; step < WAVETABLE_LOAD_SERVICE_MAX_STEPS; ++step)
    {
        wavetable_load_job_t *const job = &g_wavetable_load_job;
        const wavetable_load_state_t old_state = job->state;
        const uint32_t old_io_offset = job->io_offset;
        const uint32_t old_converted = job->converted_samples;
        const uint32_t old_cycle = job->cycle;
        const uint16_t old_band = job->band;
        wavetable_pool_load_async_step();
        if ((job->state == WAVETABLE_LOAD_IDLE)
            || (job->state == WAVETABLE_LOAD_DONE))
            break;
        if ((job->state == old_state)
            && (job->io_offset == old_io_offset)
            && (job->converted_samples == old_converted)
            && (job->cycle == old_cycle)
            && (job->band == old_band))
            break;
        if ((uint32_t)(HAL_GetTick() - started_at)
            >= WAVETABLE_LOAD_SERVICE_BUDGET_MS)
            break;
    }
}

uint8_t wavetable_pool_load_async_take_result(wavetable_result_t *out_result,
                                              uint16_t *out_wavetable_slot,
                                              uint16_t *out_global_slot,
                                              const char **out_path)
{
    wavetable_load_job_t *const job = &g_wavetable_load_job;
    if (job->state != WAVETABLE_LOAD_DONE) return 0U;
    if (out_result != 0) *out_result = job->result;
    if (out_wavetable_slot != 0) *out_wavetable_slot = job->wavetable_slot;
    if (out_global_slot != 0) *out_global_slot = job->global_slot;
    if (out_path != 0) *out_path = job->path;
    job->state = WAVETABLE_LOAD_IDLE;
    return 1U;
}

static wavetable_result_t wavetable_pool_load_wav_transactional(
    uint16_t wavetable_slot,
    uint16_t forced_global_slot,
    const char *path,
    wavetable_source_geometry_t source_geometry,
    uint16_t *out_global_slot)
{
    if ((seq_runtime_is_running() != 0U)
        || (seq_runtime_is_start_pending() != 0U))
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_TRANSPORT_ACTIVE);
        return WAVETABLE_RESULT_TRANSPORT_ACTIVE;
    }
    if (audio_recorder_is_active() != 0U)
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_RECORDER_ACTIVE);
        return WAVETABLE_RESULT_RECORDER_ACTIVE;
    }
    FIL *const source_fp = &g_wavetable_transaction_files[0];
    FILINFO source_info;
    wav_info_t wav_info;
    wavetable_slot_t *const candidate = &g_wavetable_candidate;
    wavetable_pool_candidate_init(candidate);
    if (out_global_slot != 0)
    {
        *out_global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }
    if ((wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS)
        || ((forced_global_slot != SAMPLE_GLOBAL_POOL_INVALID_INDEX)
            && (forced_global_slot >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS))
        || (path == 0) || (path[0] == '\0')
        || ((source_geometry != WAVETABLE_SOURCE_GEOMETRY_1024)
            && (source_geometry != WAVETABLE_SOURCE_GEOMETRY_2048)))
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_INVALID_ARG);
        return WAVETABLE_RESULT_INVALID_ARG;
    }
    if (sample_global_pool_validate_entries(SAMPLE_GLOBAL_KIND_WAVETABLE,
                                            wavetable_slot,
                                            1U) == 0U)
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_GLOBAL_SLOT_FULL);
        return WAVETABLE_RESULT_GLOBAL_SLOT_FULL;
    }

    char *const cache_path = g_wavetable_transaction_paths[0];
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_CACHE) == 0U)
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_SD_BUSY);
        return WAVETABLE_RESULT_SD_BUSY;
    }
    wavetable_result_t result = WAVETABLE_RESULT_OK;
    uint8_t source_open = 0U;
    if (sd_access_fs_mount_if_needed() == 0U)
    {
        result = WAVETABLE_RESULT_SD_MOUNT_FAIL;
        goto done;
    }
    if ((f_stat(path, &source_info) != FR_OK)
        || (f_open(source_fp, path, FA_READ) != FR_OK))
    {
        result = WAVETABLE_RESULT_OPEN_FAIL;
        goto done;
    }
    source_open = 1U;

    uint32_t source_crc32 = 0U;
    if (wavetable_pool_source_crc32(source_fp, &source_crc32) == 0U)
    {
        result = WAVETABLE_RESULT_READ_FAIL;
        goto done;
    }
    if ((wav_parser_parse_info(source_fp, &wav_info) == 0)
        || (wavetable_pool_wav_info_valid(&wav_info) == 0U))
    {
        result = WAVETABLE_RESULT_UNSUPPORTED;
        goto done;
    }
    const uint32_t source_samples = wav_info.data_size / wav_info.block_align;
    const uint32_t source_cycle_sample_count =
        wavetable_pool_source_cycle_sample_count(source_geometry);
    if ((source_samples % source_cycle_sample_count) != 0U)
    {
        result = WAVETABLE_RESULT_UNSUPPORTED;
        goto done;
    }
    result = wavetable_pool_candidate_allocate(
        wavetable_slot,
        path,
        source_samples / source_cycle_sample_count,
        candidate);
    if (result != WAVETABLE_RESULT_OK)
    {
        goto done;
    }
    if (wavetable_pool_make_cache_path(cache_path,
                                       WAVETABLE_POOL_PATH_MAX,
                                       path,
                                       &source_info) == 0U)
    {
        result = WAVETABLE_RESULT_PATH_TOO_LONG;
        goto done;
    }
    char *const temp_path = g_wavetable_transaction_paths[1];
    if (wavetable_pool_make_temp_path(temp_path,
                                      WAVETABLE_POOL_PATH_MAX,
                                      cache_path) == 0U)
    {
        result = WAVETABLE_RESULT_PATH_TOO_LONG;
        goto done;
    }
    (void)f_unlink(temp_path);

    if (wavetable_pool_load_prepared_cache(cache_path,
                                           path,
                                           &source_info,
                                           source_crc32,
                                           candidate) == 0U)
    {
        (void)f_unlink(cache_path);
        result = wavetable_pool_candidate_decode_wav(source_fp,
                                                     &wav_info,
                                                     source_geometry,
                                                     candidate);
        if (result != WAVETABLE_RESULT_OK)
        {
            goto done;
        }
        const uint32_t base_crc32 =
            wavetable_pool_crc32_memory(candidate->data, candidate->data_bytes);
        const uint32_t payload_crc32 =
            wavetable_pool_crc32_memory(candidate->mipmap.data,
                                        candidate->mipmap.data_bytes);
        if (wavetable_pool_write_cache_transactional(cache_path,
                                                     candidate,
                                                     &source_info,
                                                     source_crc32,
                                                     base_crc32,
                                                     payload_crc32) == 0U)
        {
            result = WAVETABLE_RESULT_WRITE_FAIL;
            goto done;
        }
    }

done:
    if (source_open != 0U)
    {
        (void)f_close(source_fp);
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
    if (result == WAVETABLE_RESULT_OK)
    {
        result = wavetable_pool_commit_candidate(wavetable_slot,
                                                 forced_global_slot,
                                                 candidate,
                                                 out_global_slot);
    }
    if (result != WAVETABLE_RESULT_OK)
    {
        wavetable_pool_candidate_release(candidate);
    }
    wavetable_pool_set_last(result);
    return result;
}

wavetable_result_t wavetable_pool_load_wav(uint16_t wavetable_slot,
                                           const char *path,
                                           uint16_t *out_global_slot)
{
    return wavetable_pool_load_wav_with_geometry(
        wavetable_slot,
        path,
        WAVETABLE_DEFAULT_SOURCE_GEOMETRY,
        out_global_slot);
}

wavetable_result_t wavetable_pool_load_wav_with_geometry(
    uint16_t wavetable_slot,
    const char *path,
    wavetable_source_geometry_t source_geometry,
    uint16_t *out_global_slot)
{
    return wavetable_pool_load_wav_transactional(
        wavetable_slot,
        SAMPLE_GLOBAL_POOL_INVALID_INDEX,
        path,
        source_geometry,
        out_global_slot);
}

wavetable_result_t wavetable_pool_load_file_at(uint16_t wavetable_slot,
                                               uint16_t global_slot,
                                               const char *path)
{
    if (global_slot >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_INVALID_ARG);
        return WAVETABLE_RESULT_INVALID_ARG;
    }
    if (wavetable_pool_path_ext_is_wav(path) != 0U)
    {
        uint16_t loaded_global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        const wavetable_result_t result =
            wavetable_pool_load_wav_transactional(wavetable_slot,
                                                  global_slot,
                                                  path,
                                                  WAVETABLE_DEFAULT_SOURCE_GEOMETRY,
                                                  &loaded_global);
        if ((result != WAVETABLE_RESULT_OK)
            && (wavetable_slot < WAVETABLE_POOL_MAX_SLOTS)
            && (g_wavetable_pool.slots[wavetable_slot].state
                != WAVETABLE_SLOT_READY)
            && (path != 0)
            && (path[0] != '\0'))
        {
            (void)wavetable_pool_copy_path(g_wavetable_pool.slots[wavetable_slot].path,
                                           sizeof(g_wavetable_pool.slots[wavetable_slot].path),
                                           path);
            wavetable_pool_slot_error_at(wavetable_slot, result, global_slot);
        }
        return result;
    }

    const wavetable_result_t result = WAVETABLE_RESULT_UNSUPPORTED;
    if ((result != WAVETABLE_RESULT_OK)
        && (wavetable_slot < WAVETABLE_POOL_MAX_SLOTS)
        && (path != 0)
        && (path[0] != '\0'))
    {
        (void)wavetable_pool_copy_path(g_wavetable_pool.slots[wavetable_slot].path,
                                       sizeof(g_wavetable_pool.slots[wavetable_slot].path),
                                       path);
        wavetable_pool_slot_error_at(wavetable_slot, result, global_slot);
    }
    return result;
}
