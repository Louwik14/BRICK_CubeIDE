static uint8_t wavetable_pool_wav_info_valid(const wav_info_t *info)
{
    if ((info == 0)
        || ((info->channels != 1U) && (info->channels != 2U))
        || ((info->bits_per_sample != 16U)
            && (info->bits_per_sample != 24U)
            && (info->bits_per_sample != 32U))
        || (info->block_align == 0U)
        || (info->data_size < info->block_align))
    {
        return 0U;
    }

    const uint16_t bytes_per_sample = (uint16_t)(info->bits_per_sample / 8U);
    if ((bytes_per_sample == 0U)
        || (info->block_align != (uint16_t)(info->channels * bytes_per_sample))
        || ((info->data_size % info->block_align) != 0U))
    {
        return 0U;
    }

    const uint32_t source_frames = info->data_size / info->block_align;
    return (uint8_t)((source_frames != 0U)
                     && ((source_frames % WAVETABLE_FRAME_SAMPLE_COUNT) == 0U));
}

static uint8_t wavetable_pool_mipmap_transition_magnitude(uint32_t cycle_count)
{
    if (cycle_count <= 1U)
    {
        return 0U;
    }

    uint32_t transitions = cycle_count - 1U;
    uint8_t magnitude = 0U;
    while (transitions != 0U)
    {
        magnitude++;
        transitions >>= 1U;
    }
    return magnitude;
}

static void wavetable_pool_encode_prepared_header(uint8_t *dst,
                                                const wavetable_slot_t *slot,
                                                const FILINFO *source_info,
                                                uint32_t source_crc32,
                                                uint32_t base_crc32,
                                                uint32_t payload_crc32)
{
    const wavetable_mipmap_view_t *const view = &slot->mipmap;

    memset(dst, 0, WAVETABLE_PREPARED_HEADER_SIZE);
    wavetable_pool_write_u32(&dst[0], WAVETABLE_PREPARED_FILE_MAGIC);
    wavetable_pool_write_u16(&dst[4], WAVETABLE_PREPARED_FILE_VERSION);
    wavetable_pool_write_u16(&dst[6], WAVETABLE_PREPARED_HEADER_SIZE);
    wavetable_pool_write_u32(&dst[8], WAVETABLE_MIPMAP_FLAG_MULTIBAND);
    wavetable_pool_write_u16(&dst[12], (uint16_t)WAVETABLE_FILE_SAMPLE_FLOAT32);
    wavetable_pool_write_u16(&dst[14], 0U);
    wavetable_pool_write_u32(&dst[16], WAVETABLE_FRAME_SAMPLE_COUNT);
    wavetable_pool_write_u32(&dst[20], view->cycle_count);
    wavetable_pool_write_u16(&dst[24], view->band_count);
    wavetable_pool_write_u16(&dst[26], WAVETABLE_PREPARED_BAND_ENTRY_SIZE);
    wavetable_pool_write_u32(&dst[28], WAVETABLE_PREPARED_HEADER_SIZE);
    wavetable_pool_write_u32(&dst[32],
                             WAVETABLE_PREPARED_HEADER_SIZE
                                 + ((uint32_t)view->band_count
                                    * WAVETABLE_PREPARED_BAND_ENTRY_SIZE));
    wavetable_pool_write_u32(&dst[36], view->data_bytes);
    dst[40] = view->cycle_transition_magnitude;
    wavetable_pool_write_i32(&dst[44], view->wave_index_multiplier);
    wavetable_pool_write_u32(&dst[48], wavetable_pool_path_hash(slot->path));
    wavetable_pool_write_u32(&dst[52], (uint32_t)source_info->fsize);
    wavetable_pool_write_u16(&dst[56], source_info->fdate);
    wavetable_pool_write_u16(&dst[58], source_info->ftime);
    wavetable_pool_write_u32(&dst[60], WAVETABLE_MIPMAP_PREP_REVISION);
    memcpy(&dst[64],
           g_wavetable_mipmap_upstream_commit_sha1,
           sizeof(g_wavetable_mipmap_upstream_commit_sha1));
    wavetable_pool_write_u32(&dst[WAVETABLE_PREPARED_SOURCE_CRC_OFFSET],
                             source_crc32);
    wavetable_pool_write_u32(&dst[WAVETABLE_PREPARED_BASE_CRC_OFFSET],
                             base_crc32);
    wavetable_pool_write_u32(&dst[WAVETABLE_PREPARED_PAYLOAD_CRC_OFFSET],
                             payload_crc32);
    wavetable_pool_write_u32(
        &dst[WAVETABLE_PREPARED_TOTAL_SIZE_OFFSET],
        WAVETABLE_PREPARED_HEADER_SIZE
            + ((uint32_t)view->band_count * WAVETABLE_PREPARED_BAND_ENTRY_SIZE)
            + view->data_bytes);
}

static void wavetable_pool_encode_prepared_band(uint8_t *dst,
                                              const wavetable_mipmap_band_t *band,
                                              uint32_t data_offset_bytes)
{
    memset(dst, 0, WAVETABLE_PREPARED_BAND_ENTRY_SIZE);
    wavetable_pool_write_u32(&dst[0], band->max_phase_increment);
    wavetable_pool_write_u32(&dst[4], band->from_cycle);
    wavetable_pool_write_u32(&dst[8], band->to_cycle);
    wavetable_pool_write_u32(&dst[12], band->cycle_sample_count);
    wavetable_pool_write_u16(&dst[16], band->cycle_magnitude);
    wavetable_pool_write_u16(&dst[18], band->flags);
    wavetable_pool_write_u32(&dst[20], data_offset_bytes);
    wavetable_pool_write_u32(&dst[24], band->sample_count);
    wavetable_pool_write_u32(
        &dst[28], wavetable_pool_mipmap_cycle_stride(band->cycle_sample_count));
}

static uint8_t wavetable_pool_write_prepared_cache_file(const char *cache_path,
                                                      const wavetable_slot_t *slot,
                                                      const FILINFO *source_info,
                                                      uint32_t source_crc32,
                                                      uint32_t base_crc32,
                                                      uint32_t payload_crc32)
{
    FIL *const fp = &g_wavetable_transaction_files[1];
    UINT bw = 0U;
    if ((cache_path == 0) || (slot == 0) || (source_info == 0)
        || (slot->mipmap.band_count != wavetable_pool_mipmap_band_count())
        || (slot->mipmap.data == 0))
    {
        return 0U;
    }

    (void)f_mkdir("0:/WAVETABLES");
    (void)f_mkdir(WAVETABLE_POOL_CACHE_DIR);
    if (f_open(fp, cache_path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
    {
        return 0U;
    }

    wavetable_pool_encode_prepared_header(g_wavetable_pool_io,
                                        slot,
                                        source_info,
                                        source_crc32,
                                        base_crc32,
                                        payload_crc32);
    if ((f_write(fp, g_wavetable_pool_io, WAVETABLE_PREPARED_HEADER_SIZE, &bw) != FR_OK)
        || (bw != WAVETABLE_PREPARED_HEADER_SIZE))
    {
        (void)f_close(fp);
        return 0U;
    }

    uint32_t band_data_offset = 0U;
    for (uint16_t band_index = 0U;
         band_index < slot->mipmap.band_count;
         ++band_index)
    {
        const wavetable_mipmap_band_t *const band =
            &slot->mipmap.bands[band_index];
        wavetable_pool_encode_prepared_band(g_wavetable_pool_io,
                                          band,
                                          band_data_offset);
        if ((f_write(fp,
                     g_wavetable_pool_io,
                     WAVETABLE_PREPARED_BAND_ENTRY_SIZE,
                     &bw) != FR_OK)
            || (bw != WAVETABLE_PREPARED_BAND_ENTRY_SIZE))
        {
            (void)f_close(fp);
            return 0U;
        }
        band_data_offset += band->sample_count * sizeof(float);
    }

    const uint8_t *const src = (const uint8_t *)slot->mipmap.data;
    uint32_t done = 0U;
    while (done < slot->mipmap.data_bytes)
    {
        uint32_t chunk = slot->mipmap.data_bytes - done;
        if (chunk > WAVETABLE_POOL_IO_BYTES)
        {
            chunk = WAVETABLE_POOL_IO_BYTES;
        }
        if ((f_write(fp, &src[done], chunk, &bw) != FR_OK) || (bw != chunk))
        {
            (void)f_close(fp);
            return 0U;
        }
        done += chunk;
    }

    const FRESULT sync_result = f_sync(fp);
    const FRESULT close_result = f_close(fp);
    return (uint8_t)((sync_result == FR_OK) && (close_result == FR_OK));
}

typedef struct
{
    uint32_t flags;
    uint16_t sample_format;
    uint16_t duplicate_sample_count;
    uint32_t cycle_sample_count;
    uint32_t cycle_count;
    uint16_t band_count;
    uint16_t band_entry_size;
    uint32_t directory_offset;
    uint32_t data_offset;
    uint32_t data_bytes;
    uint8_t transition_magnitude;
    int32_t wave_index_multiplier;
    uint32_t path_hash;
    uint32_t source_size;
    uint16_t source_date;
    uint16_t source_time;
    uint32_t prep_revision;
    uint32_t source_crc32;
    uint32_t base_crc32;
    uint32_t payload_crc32;
    uint32_t total_file_size;
} wavetable_prepared_cache_header_t;

static void wavetable_pool_decode_prepared_header(
    const uint8_t *src,
    wavetable_prepared_cache_header_t *header)
{
    memset(header, 0, sizeof(*header));
    header->flags = wavetable_pool_read_u32(&src[8]);
    header->sample_format = wavetable_pool_read_u16(&src[12]);
    header->duplicate_sample_count = wavetable_pool_read_u16(&src[14]);
    header->cycle_sample_count = wavetable_pool_read_u32(&src[16]);
    header->cycle_count = wavetable_pool_read_u32(&src[20]);
    header->band_count = wavetable_pool_read_u16(&src[24]);
    header->band_entry_size = wavetable_pool_read_u16(&src[26]);
    header->directory_offset = wavetable_pool_read_u32(&src[28]);
    header->data_offset = wavetable_pool_read_u32(&src[32]);
    header->data_bytes = wavetable_pool_read_u32(&src[36]);
    header->transition_magnitude = src[40];
    header->wave_index_multiplier =
        (int32_t)wavetable_pool_read_u32(&src[44]);
    header->path_hash = wavetable_pool_read_u32(&src[48]);
    header->source_size = wavetable_pool_read_u32(&src[52]);
    header->source_date = wavetable_pool_read_u16(&src[56]);
    header->source_time = wavetable_pool_read_u16(&src[58]);
    header->prep_revision = wavetable_pool_read_u32(&src[60]);
    header->source_crc32 =
        wavetable_pool_read_u32(&src[WAVETABLE_PREPARED_SOURCE_CRC_OFFSET]);
    header->base_crc32 =
        wavetable_pool_read_u32(&src[WAVETABLE_PREPARED_BASE_CRC_OFFSET]);
    header->payload_crc32 =
        wavetable_pool_read_u32(&src[WAVETABLE_PREPARED_PAYLOAD_CRC_OFFSET]);
    header->total_file_size =
        wavetable_pool_read_u32(&src[WAVETABLE_PREPARED_TOTAL_SIZE_OFFSET]);
}

static uint8_t wavetable_pool_source_crc32(FIL *fp, uint32_t *out_crc32)
{
    if ((fp == 0) || (out_crc32 == 0) || (f_lseek(fp, 0U) != FR_OK))
    {
        return 0U;
    }

    uint32_t crc = 0U;
    for (;;)
    {
        UINT br = 0U;
        if (f_read(fp, g_wavetable_pool_io, WAVETABLE_POOL_IO_BYTES, &br) != FR_OK)
        {
            return 0U;
        }
        if (br == 0U)
        {
            break;
        }
        crc = wavetable_pool_crc32_update(crc, g_wavetable_pool_io, br);
    }
    *out_crc32 = crc;
    return (f_lseek(fp, 0U) == FR_OK) ? 1U : 0U;
}

static void wavetable_pool_candidate_init(wavetable_slot_t *candidate)
{
    memset(candidate, 0, sizeof(*candidate));
    candidate->state = WAVETABLE_SLOT_LOADING;
    candidate->global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    candidate->first_page_slot = UINT16_MAX;
    candidate->mipmap.first_page_slot = UINT16_MAX;
    candidate->error = WAVETABLE_RESULT_OK;
    wavetable_pool_preview_clear(&candidate->preview);
}

static void wavetable_pool_candidate_release(wavetable_slot_t *candidate)
{
    if (candidate == 0)
    {
        return;
    }
    if (candidate->page_count != 0U)
    {
        sample_page_cache_port_release_shared(candidate->first_page_slot,
                                                       candidate->page_count);
    }
    if (candidate->mipmap.page_count != 0U)
    {
        sample_page_cache_port_release_shared(
            candidate->mipmap.first_page_slot,
            candidate->mipmap.page_count);
    }
    wavetable_pool_candidate_init(candidate);
}

static uint16_t wavetable_pool_mipmap_band_count(void)
{
    return (uint16_t)(WAVETABLE_MIPMAP_INITIAL_CYCLE_MAGNITUDE
                      - WAVETABLE_MIPMAP_MIN_CYCLE_MAGNITUDE + 1U);
}

static uint32_t wavetable_pool_mipmap_samples_per_table_cycle(void)
{
    uint32_t samples = 0U;
    for (uint32_t magnitude = WAVETABLE_MIPMAP_INITIAL_CYCLE_MAGNITUDE;
         magnitude >= WAVETABLE_MIPMAP_MIN_CYCLE_MAGNITUDE;
         --magnitude)
    {
        samples += wavetable_pool_mipmap_cycle_stride(1UL << magnitude);
    }
    return samples;
}

static void wavetable_pool_fft(float *real, float *imag, uint32_t count, uint8_t inverse)
{
    for (uint32_t i = 1U, j = 0U; i < count; ++i)
    {
        uint32_t bit = count >> 1U;
        while ((j & bit) != 0U)
        {
            j ^= bit;
            bit >>= 1U;
        }
        j ^= bit;
        if (i < j)
        {
            const float real_tmp = real[i];
            const float imag_tmp = imag[i];
            real[i] = real[j];
            imag[i] = imag[j];
            real[j] = real_tmp;
            imag[j] = imag_tmp;
        }
    }

    for (uint32_t length = 2U; length <= count; length <<= 1U)
    {
        const float angle = ((inverse != 0U) ? 2.0f : -2.0f)
            * 3.14159265358979323846f / (float)length;
        const float step_real = cosf(angle);
        const float step_imag = sinf(angle);
        for (uint32_t base = 0U; base < count; base += length)
        {
            float twiddle_real = 1.0f;
            float twiddle_imag = 0.0f;
            const uint32_t half = length >> 1U;
            for (uint32_t offset = 0U; offset < half; ++offset)
            {
                const uint32_t even = base + offset;
                const uint32_t odd = even + half;
                const float odd_real = real[odd] * twiddle_real
                    - imag[odd] * twiddle_imag;
                const float odd_imag = real[odd] * twiddle_imag
                    + imag[odd] * twiddle_real;
                const float even_real = real[even];
                const float even_imag = imag[even];
                real[even] = even_real + odd_real;
                imag[even] = even_imag + odd_imag;
                real[odd] = even_real - odd_real;
                imag[odd] = even_imag - odd_imag;
                const float next_real = twiddle_real * step_real
                    - twiddle_imag * step_imag;
                twiddle_imag = twiddle_real * step_imag
                    + twiddle_imag * step_real;
                twiddle_real = next_real;
            }
        }
    }

    if (inverse != 0U)
    {
        const float scale = 1.0f / (float)count;
        for (uint32_t i = 0U; i < count; ++i)
        {
            real[i] *= scale;
            imag[i] *= scale;
        }
    }
}

static void wavetable_pool_resample_2048_cycle_frequency(float *dst)
{
    for (uint32_t i = 0U; i < WAVETABLE_SOURCE_2048_SAMPLE_COUNT; ++i)
    {
        g_wavetable_fft_imag[i] = 0.0f;
    }
    wavetable_pool_fft(g_wavetable_fft_real,
                       g_wavetable_fft_imag,
                       WAVETABLE_SOURCE_2048_SAMPLE_COUNT,
                       0U);

    memset(g_wavetable_fft_work_real, 0, sizeof(g_wavetable_fft_work_real));
    memset(g_wavetable_fft_work_imag, 0, sizeof(g_wavetable_fft_work_imag));
    const float scale = (float)WAVETABLE_FRAME_SAMPLE_COUNT
        / (float)WAVETABLE_SOURCE_2048_SAMPLE_COUNT;
    g_wavetable_fft_work_real[0] = g_wavetable_fft_real[0] * scale;
    const uint32_t nyquist = WAVETABLE_FRAME_SAMPLE_COUNT >> 1U;
    for (uint32_t bin = 1U; bin < nyquist; ++bin)
    {
        const float gain = wavetable_pool_spectral_gain(
            bin,
            nyquist,
            WAVETABLE_PREP_RESAMPLE_PASS_RATIO,
            WAVETABLE_PREP_RESAMPLE_STOP_RATIO);
        const float real = g_wavetable_fft_real[bin] * scale * gain;
        const float imag = g_wavetable_fft_imag[bin] * scale * gain;
        g_wavetable_fft_work_real[bin] = real;
        g_wavetable_fft_work_imag[bin] = imag;
        g_wavetable_fft_work_real[WAVETABLE_FRAME_SAMPLE_COUNT - bin] = real;
        g_wavetable_fft_work_imag[WAVETABLE_FRAME_SAMPLE_COUNT - bin] = -imag;
    }
    /* Nyquist has no phase-preserving representation after decimation. */
    g_wavetable_fft_work_real[nyquist] = 0.0f;
    g_wavetable_fft_work_imag[nyquist] = 0.0f;
    wavetable_pool_fft(g_wavetable_fft_work_real,
                       g_wavetable_fft_work_imag,
                       WAVETABLE_FRAME_SAMPLE_COUNT,
                       1U);
    for (uint32_t i = 0U; i < WAVETABLE_FRAME_SAMPLE_COUNT; ++i)
    {
        dst[i] = wavetable_pool_finite_float(g_wavetable_fft_work_real[i]);
    }
}

static void wavetable_pool_generate_band_from_fft(float *dst,
                                                   uint32_t count,
                                                   uint16_t band_index)
{
    memset(g_wavetable_fft_work_real, 0, count * sizeof(float));
    memset(g_wavetable_fft_work_imag, 0, count * sizeof(float));
    const float spectrum_scale =
        (float)count / (float)WAVETABLE_FRAME_SAMPLE_COUNT;
    const float pass_ratio = (band_index == 0U)
        ? WAVETABLE_PREP_BASE_PASS_RATIO : WAVETABLE_PREP_MIP_PASS_RATIO;
    const float stop_ratio = (band_index == 0U)
        ? WAVETABLE_PREP_BASE_STOP_RATIO : WAVETABLE_PREP_MIP_STOP_RATIO;
    g_wavetable_fft_work_real[0] = g_wavetable_fft_real[0] * spectrum_scale;
    const uint32_t nyquist = count >> 1U;
    for (uint32_t bin = 1U; bin < nyquist; ++bin)
    {
        const float gain = wavetable_pool_spectral_gain(
            bin, nyquist, pass_ratio, stop_ratio);
        const float real = g_wavetable_fft_real[bin] * spectrum_scale * gain;
        const float imag = g_wavetable_fft_imag[bin] * spectrum_scale * gain;
        g_wavetable_fft_work_real[bin] = real;
        g_wavetable_fft_work_imag[bin] = imag;
        g_wavetable_fft_work_real[count - bin] = real;
        g_wavetable_fft_work_imag[count - bin] = -imag;
    }
    g_wavetable_fft_work_real[nyquist] = 0.0f;
    g_wavetable_fft_work_imag[nyquist] = 0.0f;
    wavetable_pool_fft(g_wavetable_fft_work_real,
                       g_wavetable_fft_work_imag,
                       count,
                       1U);
    for (uint32_t i = 0U; i < count; ++i)
    {
        dst[i] = wavetable_pool_finite_float(g_wavetable_fft_work_real[i]);
    }
}

static wavetable_result_t wavetable_pool_candidate_allocate(
    uint16_t wavetable_slot,
    const char *path,
    uint32_t frame_count,
    wavetable_slot_t *candidate)
{
    if ((path == 0) || (candidate == 0) || (frame_count == 0U)
        || (frame_count > (UINT32_MAX / WAVETABLE_FRAME_SAMPLE_COUNT)))
    {
        return WAVETABLE_RESULT_INVALID_ARG;
    }

    const uint32_t brick_samples =
        frame_count * WAVETABLE_FRAME_SAMPLE_COUNT;
    const uint32_t mipmap_samples_per_cycle =
        wavetable_pool_mipmap_samples_per_table_cycle();
    if (frame_count > (UINT32_MAX / mipmap_samples_per_cycle)
        || brick_samples > (UINT32_MAX / sizeof(float)))
    {
        return WAVETABLE_RESULT_TOO_LARGE;
    }
    const uint32_t mipmap_samples = frame_count * mipmap_samples_per_cycle;
    if (mipmap_samples > (UINT32_MAX / sizeof(float)))
    {
        return WAVETABLE_RESULT_TOO_LARGE;
    }

    const uint32_t brick_bytes = brick_samples * sizeof(float);
    const uint32_t mipmap_bytes = mipmap_samples * sizeof(float);
    const uint32_t brick_pages =
        (brick_bytes + SAMPLE_PAGE_BYTES - 1U) / SAMPLE_PAGE_BYTES;
    const uint32_t mipmap_pages =
        (mipmap_bytes + SAMPLE_PAGE_BYTES - 1U) / SAMPLE_PAGE_BYTES;
    if ((brick_pages == 0U) || (mipmap_pages == 0U)
        || (brick_pages > (UINT32_MAX / SAMPLE_PAGE_BYTES))
        || (mipmap_pages > (UINT32_MAX / SAMPLE_PAGE_BYTES)))
    {
        return WAVETABLE_RESULT_TOO_LARGE;
    }
    const uint32_t brick_cost = brick_pages * SAMPLE_PAGE_BYTES;
    const uint32_t mipmap_cost = mipmap_pages * SAMPLE_PAGE_BYTES;
    if ((brick_cost > sample_page_cache_port_shared_total_bytes())
        || (mipmap_cost > sample_page_cache_port_shared_total_bytes())
        || (brick_cost > (UINT32_MAX - mipmap_cost)))
    {
        return WAVETABLE_RESULT_TOO_LARGE;
    }
    if (sample_global_pool_validate_budget(SAMPLE_GLOBAL_KIND_WAVETABLE,
                                           wavetable_slot,
                                           brick_cost + mipmap_cost) == 0U)
    {
        return WAVETABLE_RESULT_GLOBAL_BUDGET_FULL;
    }

    sample_page_loader_allocation_t brick_allocation;
    sample_page_loader_allocation_t mipmap_allocation;
    memset(&brick_allocation, 0, sizeof(brick_allocation));
    memset(&mipmap_allocation, 0, sizeof(mipmap_allocation));
    if (sample_page_cache_port_alloc_shared(brick_bytes,
                                                &brick_allocation) == 0U)
    {
        return WAVETABLE_RESULT_RAM_POOL_FULL;
    }
    if (sample_page_cache_port_alloc_shared(mipmap_bytes,
                                                &mipmap_allocation) == 0U)
    {
        sample_page_cache_port_release_shared(
            brick_allocation.first_slot,
            brick_allocation.page_count);
        return WAVETABLE_RESULT_RAM_POOL_FULL;
    }

    wavetable_pool_candidate_init(candidate);
    if (wavetable_pool_copy_path(candidate->path,
                                 sizeof(candidate->path),
                                 path) == 0U)
    {
        sample_page_cache_port_release_shared(
            brick_allocation.first_slot,
            brick_allocation.page_count);
        sample_page_cache_port_release_shared(
            mipmap_allocation.first_slot,
            mipmap_allocation.page_count);
        return WAVETABLE_RESULT_PATH_TOO_LONG;
    }
    candidate->format = WAVETABLE_FORMAT_FLOAT32_MONO;
    candidate->frame_sample_count = WAVETABLE_FRAME_SAMPLE_COUNT;
    candidate->frame_count = frame_count;
    candidate->data_offset =
        (uint32_t)brick_allocation.first_slot * SAMPLE_PAGE_BYTES;
    candidate->first_page_slot = brick_allocation.first_slot;
    candidate->page_count = brick_allocation.page_count;
    candidate->data_bytes = brick_bytes;
    candidate->data = (float *)sample_page_cache_port_resolve_shared(
        &brick_allocation);
    candidate->cost_bytes_aligned =
        brick_allocation.capacity_bytes + mipmap_allocation.capacity_bytes;
    candidate->mipmap.data = (float *)sample_page_cache_port_resolve_shared(
        &mipmap_allocation);
    candidate->mipmap.data_bytes = mipmap_bytes;
    candidate->mipmap.first_page_slot = mipmap_allocation.first_slot;
    candidate->mipmap.page_count = mipmap_allocation.page_count;
    candidate->mipmap.cost_bytes_aligned = mipmap_allocation.capacity_bytes;
    return WAVETABLE_RESULT_OK;
}

static void wavetable_pool_candidate_mipmap_init(wavetable_slot_t *candidate)
{
    wavetable_mipmap_view_t *const view = &candidate->mipmap;
    float *const data = view->data;
    const uint32_t data_bytes = view->data_bytes;
    const uint16_t first_page_slot = view->first_page_slot;
    const uint16_t page_count = view->page_count;
    const uint32_t cost_bytes_aligned = view->cost_bytes_aligned;

    memset(view, 0, sizeof(*view));
    view->band_count = wavetable_pool_mipmap_band_count();
    view->cycle_count = candidate->frame_count;
    view->cycle_transition_magnitude =
        wavetable_pool_mipmap_transition_magnitude(candidate->frame_count);
    if (candidate->frame_count > 1U)
    {
        const uint32_t transitions = candidate->frame_count - 1U;
        view->wave_index_multiplier =
            (int32_t)(transitions << (31U - view->cycle_transition_magnitude));
    }
    view->data = data;
    view->data_bytes = data_bytes;
    view->first_page_slot = first_page_slot;
    view->page_count = page_count;
    view->cost_bytes_aligned = cost_bytes_aligned;

    uint32_t data_offset_samples = 0U;
    for (uint16_t band_index = 0U;
         band_index < view->band_count;
         ++band_index)
    {
        wavetable_mipmap_band_t *const band = &view->bands[band_index];
        const uint32_t magnitude =
            WAVETABLE_MIPMAP_INITIAL_CYCLE_MAGNITUDE - band_index;
        const uint32_t cycle_samples = 1UL << magnitude;
        band->max_phase_increment =
            (uint32_t)((double)(UINT32_MAX >> magnitude)
                * WAVETABLE_PREP_PHASE_LIMIT_RATIO);
        band->from_cycle = 0U;
        band->to_cycle = candidate->frame_count;
        band->cycle_sample_count = cycle_samples;
        band->cycle_magnitude = (uint16_t)magnitude;
        band->flags = (uint16_t)WAVETABLE_MIPMAP_FLAG_MULTIBAND;
        band->data = &view->data[data_offset_samples];
        band->sample_count = candidate->frame_count
            * wavetable_pool_mipmap_cycle_stride(cycle_samples);
        data_offset_samples += band->sample_count;
    }

}

static void wavetable_pool_candidate_prepare_mipmap(wavetable_slot_t *candidate)
{
    wavetable_pool_candidate_mipmap_init(candidate);
    wavetable_mipmap_view_t *const view = &candidate->mipmap;
    for (uint32_t cycle = 0U; cycle < candidate->frame_count; ++cycle)
    {
        const float *const src =
            &candidate->data[cycle * WAVETABLE_FRAME_SAMPLE_COUNT];
        for (uint32_t i = 0U; i < WAVETABLE_FRAME_SAMPLE_COUNT; ++i)
        {
            g_wavetable_fft_real[i] = src[i];
            g_wavetable_fft_imag[i] = 0.0f;
        }
        wavetable_pool_fft(g_wavetable_fft_real,
                           g_wavetable_fft_imag,
                           WAVETABLE_FRAME_SAMPLE_COUNT,
                           0U);

        for (uint16_t band_index = 0U;
             band_index < view->band_count;
             ++band_index)
        {
            wavetable_mipmap_band_t *const band = &view->bands[band_index];
            const uint32_t count = band->cycle_sample_count;
            const uint32_t cycle_stride =
                wavetable_pool_mipmap_cycle_stride(count);
            float *const dst = &band->data[cycle * cycle_stride];
            wavetable_pool_generate_band_from_fft(dst, count, band_index);
            if (cycle_stride > count)
            {
                memset(&dst[count], 0,
                       (cycle_stride - count) * sizeof(float));
            }
        }
        wavetable_pool_copy_float_exact(
            &candidate->data[cycle * WAVETABLE_FRAME_SAMPLE_COUNT],
            &view->bands[0].data[cycle * WAVETABLE_FRAME_SAMPLE_COUNT],
            WAVETABLE_FRAME_SAMPLE_COUNT);
    }
}

static wavetable_result_t wavetable_pool_candidate_decode_wav(
    FIL *fp,
    const wav_info_t *wav_info,
    wavetable_source_geometry_t source_geometry,
    wavetable_slot_t *candidate)
{
    if (f_lseek(fp, wav_info->data_offset) != FR_OK)
    {
        return WAVETABLE_RESULT_READ_FAIL;
    }

    const uint32_t source_cycle_sample_count =
        wavetable_pool_source_cycle_sample_count(source_geometry);
    const uint32_t sample_count = candidate->frame_count
        * source_cycle_sample_count;
    uint32_t frames_done = 0U;
    uint32_t cycle = 0U;
    uint32_t cycle_offset = 0U;
    while (frames_done < sample_count)
    {
        uint32_t frames_chunk = WAVETABLE_POOL_IO_BYTES / wav_info->block_align;
        if (frames_chunk > (sample_count - frames_done))
        {
            frames_chunk = sample_count - frames_done;
        }
        if (frames_chunk == 0U)
        {
            return WAVETABLE_RESULT_READ_FAIL;
        }

        const UINT bytes_to_read = (UINT)(frames_chunk * wav_info->block_align);
        UINT br = 0U;
        if ((f_read(fp, g_wavetable_pool_io, bytes_to_read, &br) != FR_OK)
            || (br != bytes_to_read))
        {
            return WAVETABLE_RESULT_READ_FAIL;
        }

        const uint8_t *src = g_wavetable_pool_io;
        for (uint32_t i = 0U; i < frames_chunk; ++i)
        {
            float left = 0.0f;
            float right = 0.0f;
            wav_audio_codec_decode_stereo_frame(src,
                                                wav_info->channels,
                                                wav_info->bits_per_sample,
                                                &left,
                                                &right);
            const float mono = (wav_info->channels == 1U)
                ? left : ((left + right) * 0.5f);
            if (source_cycle_sample_count == WAVETABLE_FRAME_SAMPLE_COUNT)
            {
                candidate->data[frames_done + i] =
                    wavetable_pool_finite_float(mono);
            }
            else
            {
                wavetable_pool_source_2048_store(cycle_offset++, mono);
                if (cycle_offset == WAVETABLE_SOURCE_2048_SAMPLE_COUNT)
                {
                    wavetable_pool_resample_2048_cycle_frequency(
                        &candidate->data[cycle * WAVETABLE_FRAME_SAMPLE_COUNT]);
                    cycle++;
                    cycle_offset = 0U;
                }
            }
            src += wav_info->block_align;
        }
        frames_done += frames_chunk;
    }
    wavetable_pool_candidate_prepare_mipmap(candidate);
    return WAVETABLE_RESULT_OK;
}

static uint8_t wavetable_pool_make_temp_path(char *out,
                                             uint32_t out_size,
                                             const char *final_path)
{
    const int written = snprintf(out, out_size, "%s.TMP", final_path);
    return (uint8_t)((written >= 0) && ((uint32_t)written < out_size));
}

static uint8_t wavetable_pool_write_cache_transactional(
    const char *cache_path,
    const wavetable_slot_t *candidate,
    const FILINFO *source_info,
    uint32_t source_crc32,
    uint32_t base_crc32,
    uint32_t payload_crc32)
{
    char *const temp_path = g_wavetable_transaction_paths[1];
    if (wavetable_pool_make_temp_path(temp_path,
                                      WAVETABLE_POOL_PATH_MAX,
                                      cache_path) == 0U)
    {
        return 0U;
    }

    (void)f_unlink(temp_path);
    if (wavetable_pool_write_prepared_cache_file(temp_path,
                                                 candidate,
                                                 source_info,
                                                 source_crc32,
                                                 base_crc32,
                                                 payload_crc32) == 0U)
    {
        (void)f_unlink(temp_path);
        return 0U;
    }

    (void)f_unlink(cache_path);
    if (f_rename(temp_path, cache_path) != FR_OK)
    {
        (void)f_unlink(temp_path);
        return 0U;
    }
    return 1U;
}

static uint8_t wavetable_pool_read_exact(FIL *fp,
                                         uint8_t *dst,
                                         uint32_t bytes,
                                         uint32_t *out_crc32)
{
    uint32_t done = 0U;
    uint32_t crc = 0U;
    while (done < bytes)
    {
        uint32_t chunk = bytes - done;
        if (chunk > WAVETABLE_POOL_IO_BYTES)
        {
            chunk = WAVETABLE_POOL_IO_BYTES;
        }
        UINT br = 0U;
        if ((f_read(fp, g_wavetable_pool_io, chunk, &br) != FR_OK)
            || (br != chunk))
        {
            return 0U;
        }
        memcpy(&dst[done], g_wavetable_pool_io, chunk);
        crc = wavetable_pool_crc32_update(crc, g_wavetable_pool_io, chunk);
        done += chunk;
    }
    if (out_crc32 != 0)
    {
        *out_crc32 = crc;
    }
    return 1U;
}

static uint8_t wavetable_pool_load_prepared_cache(
    const char *cache_path,
    const char *source_path,
    const FILINFO *source_info,
    uint32_t source_crc32,
    wavetable_slot_t *candidate)
{
    FIL *const fp = &g_wavetable_transaction_files[1];
    UINT br = 0U;
    wavetable_prepared_cache_header_t header;
    if ((f_open(fp, cache_path, FA_READ) != FR_OK)
        || (f_read(fp, g_wavetable_pool_io,
                   WAVETABLE_PREPARED_HEADER_SIZE, &br) != FR_OK)
        || (br != WAVETABLE_PREPARED_HEADER_SIZE)
        || (wavetable_pool_read_u32(&g_wavetable_pool_io[0])
            != WAVETABLE_PREPARED_FILE_MAGIC)
        || (wavetable_pool_read_u16(&g_wavetable_pool_io[4])
            != WAVETABLE_PREPARED_FILE_VERSION)
        || (wavetable_pool_read_u16(&g_wavetable_pool_io[6])
            != WAVETABLE_PREPARED_HEADER_SIZE))
    {
        (void)f_close(fp);
        return 0U;
    }

    wavetable_pool_decode_prepared_header(g_wavetable_pool_io, &header);
    const uint16_t band_count = wavetable_pool_mipmap_band_count();
    const uint32_t directory_bytes =
        (uint32_t)band_count * WAVETABLE_PREPARED_BAND_ENTRY_SIZE;
    const uint32_t expected_data_offset =
        WAVETABLE_PREPARED_HEADER_SIZE + directory_bytes;
    const uint32_t expected_total_size =
        expected_data_offset + candidate->mipmap.data_bytes;
    const uint8_t transition =
        wavetable_pool_mipmap_transition_magnitude(candidate->frame_count);
    int32_t wave_index_multiplier = 0;
    if (candidate->frame_count > 1U)
    {
        wave_index_multiplier = (int32_t)(
            (candidate->frame_count - 1U) << (31U - transition));
    }

    if ((header.flags != WAVETABLE_MIPMAP_FLAG_MULTIBAND)
        || (header.sample_format != (uint16_t)WAVETABLE_FILE_SAMPLE_FLOAT32)
        || (header.duplicate_sample_count != 0U)
        || (header.cycle_sample_count != WAVETABLE_FRAME_SAMPLE_COUNT)
        || (header.cycle_count != candidate->frame_count)
        || (header.band_count != band_count)
        || (header.band_entry_size != WAVETABLE_PREPARED_BAND_ENTRY_SIZE)
        || (header.directory_offset != WAVETABLE_PREPARED_HEADER_SIZE)
        || (header.data_offset != expected_data_offset)
        || (header.data_bytes != candidate->mipmap.data_bytes)
        || (header.transition_magnitude != transition)
        || (header.wave_index_multiplier != wave_index_multiplier)
        || (header.path_hash != wavetable_pool_path_hash(source_path))
        || (header.source_size != (uint32_t)source_info->fsize)
        || (header.source_date != source_info->fdate)
        || (header.source_time != source_info->ftime)
        || (header.prep_revision != WAVETABLE_MIPMAP_PREP_REVISION)
        || (memcmp(&g_wavetable_pool_io[64],
                   g_wavetable_mipmap_upstream_commit_sha1,
                   sizeof(g_wavetable_mipmap_upstream_commit_sha1)) != 0)
        || (header.source_crc32 != source_crc32)
        || (header.total_file_size != expected_total_size)
        || (f_size(fp) != (FSIZE_t)expected_total_size)
        || (f_lseek(fp, header.directory_offset) != FR_OK))
    {
        (void)f_close(fp);
        return 0U;
    }

    wavetable_mipmap_view_t *const view = &candidate->mipmap;
    view->band_count = band_count;
    view->cycle_count = candidate->frame_count;
    view->cycle_transition_magnitude = transition;
    view->wave_index_multiplier = wave_index_multiplier;
    uint32_t payload_offset = 0U;
    for (uint16_t i = 0U; i < band_count; ++i)
    {
        if ((f_read(fp, g_wavetable_pool_io,
                    WAVETABLE_PREPARED_BAND_ENTRY_SIZE, &br) != FR_OK)
            || (br != WAVETABLE_PREPARED_BAND_ENTRY_SIZE))
        {
            (void)f_close(fp);
            return 0U;
        }
        wavetable_mipmap_band_t *const band = &view->bands[i];
        band->max_phase_increment = wavetable_pool_read_u32(&g_wavetable_pool_io[0]);
        band->from_cycle = wavetable_pool_read_u32(&g_wavetable_pool_io[4]);
        band->to_cycle = wavetable_pool_read_u32(&g_wavetable_pool_io[8]);
        band->cycle_sample_count = wavetable_pool_read_u32(&g_wavetable_pool_io[12]);
        band->cycle_magnitude = wavetable_pool_read_u16(&g_wavetable_pool_io[16]);
        band->flags = wavetable_pool_read_u16(&g_wavetable_pool_io[18]);
        const uint32_t file_offset = wavetable_pool_read_u32(&g_wavetable_pool_io[20]);
        band->sample_count = wavetable_pool_read_u32(&g_wavetable_pool_io[24]);
        const uint32_t file_cycle_stride =
            wavetable_pool_read_u32(&g_wavetable_pool_io[28]);
        const uint32_t magnitude = WAVETABLE_MIPMAP_INITIAL_CYCLE_MAGNITUDE - i;
        const uint32_t cycle_samples = 1UL << magnitude;
        const uint32_t cycle_stride =
            wavetable_pool_mipmap_cycle_stride(cycle_samples);
        const uint32_t sample_count = candidate->frame_count * cycle_stride;
        const uint32_t max_inc =
            (uint32_t)((double)(UINT32_MAX >> magnitude)
                * WAVETABLE_PREP_PHASE_LIMIT_RATIO);
        const uint32_t bytes = sample_count * sizeof(float);
        if ((band->from_cycle != 0U)
            || (band->to_cycle != candidate->frame_count)
            || (band->cycle_sample_count != cycle_samples)
            || (file_cycle_stride != cycle_stride)
            || (band->cycle_magnitude != magnitude)
            || (band->max_phase_increment != max_inc)
            || (band->flags != WAVETABLE_MIPMAP_FLAG_MULTIBAND)
            || (band->sample_count != sample_count)
            || (file_offset != payload_offset)
            || (payload_offset > view->data_bytes)
            || (bytes > (view->data_bytes - payload_offset)))
        {
            (void)f_close(fp);
            return 0U;
        }
        band->data = &view->data[payload_offset / sizeof(float)];
        payload_offset += bytes;
    }
    if ((payload_offset != view->data_bytes)
        || (f_lseek(fp, header.data_offset) != FR_OK))
    {
        (void)f_close(fp);
        return 0U;
    }

    uint32_t payload_crc = 0U;
    const uint8_t payload_ok = wavetable_pool_read_exact(
        fp, (uint8_t *)view->data, view->data_bytes, &payload_crc);
    (void)f_close(fp);
    if ((payload_ok == 0U) || (payload_crc != header.payload_crc32))
    {
        return 0U;
    }

    const wavetable_mipmap_band_t *const base = &view->bands[0];
    const uint32_t stride =
        wavetable_pool_mipmap_cycle_stride(base->cycle_sample_count);
    for (uint32_t frame = 0U; frame < candidate->frame_count; ++frame)
    {
        wavetable_pool_copy_float_exact(
            &candidate->data[frame * WAVETABLE_FRAME_SAMPLE_COUNT],
            &base->data[frame * stride],
            WAVETABLE_FRAME_SAMPLE_COUNT);
    }
    return (wavetable_pool_crc32_memory(candidate->data, candidate->data_bytes)
            == header.base_crc32) ? 1U : 0U;
}


static void wavetable_pool_release_slot_allocations(
    const wavetable_slot_t *slot)
{
    if (slot->page_count != 0U)
    {
        sample_page_cache_port_release_shared(slot->first_page_slot,
                                                       slot->page_count);
    }
    if (slot->mipmap.page_count != 0U)
    {
        sample_page_cache_port_release_shared(
            slot->mipmap.first_page_slot,
            slot->mipmap.page_count);
    }
}

static uint8_t wavetable_pool_publish_audio_descriptor(
    uint16_t wavetable_slot, const wavetable_slot_t *slot)
{
    audio_wavetable_descriptor_t descriptor;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.generation = slot->generation;
    descriptor.frame_count = slot->frame_count;
    descriptor.frame_sample_count = slot->frame_sample_count;
    descriptor.wavetable_slot = wavetable_slot;
    descriptor.global_slot = slot->global_slot;
    descriptor.band_count = slot->mipmap.band_count;
    if (shared_memory_ref_make_page_pool(slot->first_page_slot, 0U,
                                     slot->data_bytes, &descriptor.base_data) == 0U)
        return 0U;
    dcache_clean_by_addr_aligned(slot->data, slot->data_bytes);
    uint32_t mip_offset = 0U;
    for (uint16_t i = 0U; i < descriptor.band_count; ++i)
    {
        const wavetable_mipmap_band_t *const src = &slot->mipmap.bands[i];
        audio_wavetable_band_t *const dst = &descriptor.bands[i];
        const uint32_t bytes = src->sample_count * sizeof(float);
        dst->max_phase_increment = src->max_phase_increment;
        dst->cycle_sample_count = src->cycle_sample_count;
        dst->sample_count = src->sample_count;
        dst->cycle_magnitude = src->cycle_magnitude;
        dst->flags = src->flags;
        if (shared_memory_ref_make_page_pool(slot->mipmap.first_page_slot,
                                         mip_offset, bytes, &dst->data) == 0U)
            return 0U;
        mip_offset += bytes;
    }
    dcache_clean_by_addr_aligned(slot->mipmap.data, slot->mipmap.data_bytes);
    return audio_wave_table_projection_install_descriptor(&descriptor);
}

static wavetable_result_t wavetable_pool_commit_candidate(
    uint16_t wavetable_slot,
    uint16_t forced_global_slot,
    wavetable_slot_t *candidate,
    uint16_t *out_global_slot)
{
    g_wavetable_old_commit_snapshot = g_wavetable_pool.slots[wavetable_slot];
    const wavetable_slot_t *const old = &g_wavetable_old_commit_snapshot;
    if (old->state == WAVETABLE_SLOT_READY)
    {
        wavetable_pool_clear(wavetable_slot);
        return WAVETABLE_RESULT_SD_BUSY;
    }
    if (old->state == WAVETABLE_SLOT_RETIRING)
        return WAVETABLE_RESULT_SD_BUSY;
    uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (forced_global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
    {
        global_slot = forced_global_slot;
    }
    else if (old->global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
    {
        global_slot = old->global_slot;
    }

    const uint8_t registered =
        (global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
            ? sample_global_pool_register_wavetable_at(global_slot,
                                                       wavetable_slot,
                                                       candidate->path,
                                                       candidate->cost_bytes_aligned)
            : sample_global_pool_register_wavetable(wavetable_slot,
                                                    candidate->path,
                                                    candidate->cost_bytes_aligned,
                                                    &global_slot);
    if (registered == 0U)
    {
        return WAVETABLE_RESULT_REGISTER_FAIL;
    }

    candidate->global_slot = global_slot;
    candidate->generation = wavetable_pool_next_generation();
    candidate->state = WAVETABLE_SLOT_READY;
    candidate->error = WAVETABLE_RESULT_OK;
    if (candidate->preview.state != WAVETABLE_PREVIEW_READY)
    {
        wavetable_pool_preview_build(candidate);
    }
    else
    {
        candidate->preview.generation = candidate->generation;
    }

    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    g_wavetable_pool.slots[wavetable_slot] = *candidate;
    __DMB();
    if (primask == 0U)
    {
        __enable_irq();
    }

    candidate->page_count = 0U;
    candidate->mipmap.page_count = 0U;
    if (wavetable_pool_publish_audio_descriptor(
            wavetable_slot, &g_wavetable_pool.slots[wavetable_slot]) == 0U)
        return WAVETABLE_RESULT_REGISTER_FAIL;
    wavetable_pool_release_slot_allocations(old);
    if (out_global_slot != 0)
    {
        *out_global_slot = global_slot;
    }
    return WAVETABLE_RESULT_OK;
}

static sd_scheduler_background_admission_t wavetable_load_sd_begin(
    sd_scheduler_background_kind_t kind, uint32_t bytes)
{
    const sd_scheduler_background_request_t request = {
        .byte_count = bytes,
        .media_epoch = g_wavetable_load_job.media_epoch,
        .kind = kind
    };
    return sd_scheduler_runtime_background_try_begin(&request);
}
