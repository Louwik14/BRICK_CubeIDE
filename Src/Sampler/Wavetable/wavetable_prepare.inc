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

uint8_t wavetable_pool_source_crc32_file(FIL *file, uint32_t *out_crc32)
{
    return wav_parser_crc32_file(file, out_crc32);
}

static void wavetable_pool_candidate_init(wavetable_slot_t *candidate)
{
    memset(candidate, 0, sizeof(*candidate));
    candidate->state = WAVETABLE_SLOT_LOADING;
    candidate->global_slot = WAVETABLE_POOL_INVALID_SLOT;
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

static uint8_t wavetable_pool_frame_geometry(
    uint32_t frame_count,
    uint32_t *out_brick_bytes,
    uint32_t *out_mipmap_bytes,
    uint32_t *out_brick_pages,
    uint32_t *out_mipmap_pages)
{
    const uint32_t mipmap_samples_per_cycle =
        wavetable_pool_mipmap_samples_per_table_cycle();
    if ((frame_count == 0U)
        || (frame_count > WAVETABLE_FLASH_MAX_FRAMES)
        || (frame_count > (UINT32_MAX / WAVETABLE_FRAME_SAMPLE_COUNT))
        || (frame_count > (UINT32_MAX / mipmap_samples_per_cycle)))
        return 0U;
    const uint64_t brick_bytes64 = (uint64_t)frame_count
        * WAVETABLE_FRAME_SAMPLE_COUNT * sizeof(float);
    const uint64_t mipmap_bytes64 = (uint64_t)frame_count
        * mipmap_samples_per_cycle * sizeof(float);
    if ((brick_bytes64 > UINT32_MAX) || (mipmap_bytes64 > UINT32_MAX))
        return 0U;
    const uint32_t brick_bytes = (uint32_t)brick_bytes64;
    const uint32_t mipmap_bytes = (uint32_t)mipmap_bytes64;
    const uint32_t brick_pages = (brick_bytes + SAMPLE_PAGE_BYTES - 1U)
        / SAMPLE_PAGE_BYTES;
    const uint32_t mipmap_pages = (mipmap_bytes + SAMPLE_PAGE_BYTES - 1U)
        / SAMPLE_PAGE_BYTES;
    if ((brick_pages == 0U) || (mipmap_pages == 0U)
        || (brick_pages > (UINT32_MAX / SAMPLE_PAGE_BYTES))
        || (mipmap_pages > (UINT32_MAX / SAMPLE_PAGE_BYTES)))
        return 0U;
    if (out_brick_bytes != NULL) *out_brick_bytes = brick_bytes;
    if (out_mipmap_bytes != NULL) *out_mipmap_bytes = mipmap_bytes;
    if (out_brick_pages != NULL) *out_brick_pages = brick_pages;
    if (out_mipmap_pages != NULL) *out_mipmap_pages = mipmap_pages;
    return 1U;
}

static wavetable_result_t wavetable_pool_candidate_allocate(
    uint16_t wavetable_slot,
    const char *path,
    uint32_t frame_count,
    wavetable_slot_t *candidate)
{
    if ((path == 0) || (candidate == 0))
    {
        return WAVETABLE_RESULT_INVALID_ARG;
    }

    uint32_t brick_bytes = 0U;
    uint32_t mipmap_bytes = 0U;
    uint32_t brick_pages = 0U;
    uint32_t mipmap_pages = 0U;
    if (wavetable_pool_frame_geometry(frame_count, &brick_bytes,
                                      &mipmap_bytes, &brick_pages,
                                      &mipmap_pages) == 0U)
        return WAVETABLE_RESULT_TOO_LARGE;
    (void)wavetable_slot;

    wavetable_pool_candidate_init(candidate);
    if (wavetable_pool_copy_path(candidate->path,
                                 sizeof(candidate->path),
                                 path) == 0U)
    {
        return WAVETABLE_RESULT_PATH_TOO_LONG;
    }
    candidate->format = WAVETABLE_FORMAT_FLOAT32_MONO;
    candidate->frame_sample_count = WAVETABLE_FRAME_SAMPLE_COUNT;
    candidate->frame_count = frame_count;
    candidate->data_offset = 0U;
    candidate->first_page_slot = UINT16_MAX;
    candidate->page_count = 0U;
    candidate->data_bytes = brick_bytes;
    candidate->data = g_wavetable_prepare_base;
    candidate->cost_bytes_aligned = brick_bytes + mipmap_bytes;
    candidate->mipmap.data = g_wavetable_prepare_mipmap;
    candidate->mipmap.data_bytes = mipmap_bytes;
    candidate->mipmap.first_page_slot = UINT16_MAX;
    candidate->mipmap.page_count = 0U;
    candidate->mipmap.cost_bytes_aligned = mipmap_bytes;
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

static uint8_t wavetable_pool_make_temp_path(char *out,
                                             uint32_t out_size,
                                             const char *final_path)
{
    const int written = snprintf(out, out_size, "%s.TMP", final_path);
    return (uint8_t)((written >= 0) && ((uint32_t)written < out_size));
}

static uint8_t wavetable_pool_prepared_header_validate(
    const uint8_t *encoded,
    uint32_t file_size,
    const char *source_path,
    const FILINFO *source_info,
    uint32_t source_crc32,
    wavetable_slot_t *candidate,
    wavetable_prepared_cache_header_t *out_header)
{
    if ((encoded == NULL) || (source_path == NULL) || (source_info == NULL)
        || (candidate == NULL) || (out_header == NULL)
        || (wavetable_pool_read_u32(&encoded[0]) != WAVETABLE_PREPARED_FILE_MAGIC)
        || (wavetable_pool_read_u16(&encoded[4]) != WAVETABLE_PREPARED_FILE_VERSION)
        || (wavetable_pool_read_u16(&encoded[6]) != WAVETABLE_PREPARED_HEADER_SIZE))
        return 0U;
    wavetable_pool_decode_prepared_header(encoded, out_header);
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
        wave_index_multiplier = (int32_t)(
            (candidate->frame_count - 1U) << (31U - transition));
    if ((out_header->flags != WAVETABLE_MIPMAP_FLAG_MULTIBAND)
        || (out_header->sample_format != (uint16_t)WAVETABLE_FILE_SAMPLE_FLOAT32)
        || (out_header->duplicate_sample_count != 0U)
        || (out_header->cycle_sample_count != WAVETABLE_FRAME_SAMPLE_COUNT)
        || (out_header->cycle_count != candidate->frame_count)
        || (out_header->band_count != band_count)
        || (out_header->band_entry_size != WAVETABLE_PREPARED_BAND_ENTRY_SIZE)
        || (out_header->directory_offset != WAVETABLE_PREPARED_HEADER_SIZE)
        || (out_header->data_offset != expected_data_offset)
        || (out_header->data_bytes != candidate->mipmap.data_bytes)
        || (out_header->transition_magnitude != transition)
        || (out_header->wave_index_multiplier != wave_index_multiplier)
        || (out_header->path_hash != wavetable_pool_path_hash(source_path))
        || (out_header->source_size != (uint32_t)source_info->fsize)
        || (out_header->source_date != source_info->fdate)
        || (out_header->source_time != source_info->ftime)
        || (out_header->prep_revision != WAVETABLE_MIPMAP_PREP_REVISION)
        || (memcmp(&encoded[64], g_wavetable_mipmap_upstream_commit_sha1,
                   sizeof(g_wavetable_mipmap_upstream_commit_sha1)) != 0)
        || (out_header->source_crc32 != source_crc32)
        || (out_header->total_file_size != expected_total_size)
        || (file_size != expected_total_size))
        return 0U;
    wavetable_mipmap_view_t *const view = &candidate->mipmap;
    view->band_count = band_count;
    view->cycle_count = candidate->frame_count;
    view->cycle_transition_magnitude = transition;
    view->wave_index_multiplier = wave_index_multiplier;
    return 1U;
}

static uint8_t wavetable_pool_prepared_band_decode_validate(
    const uint8_t *encoded,
    uint16_t index,
    uint32_t payload_offset,
    wavetable_slot_t *candidate,
    uint32_t *out_next_offset)
{
    wavetable_mipmap_band_t *const band = &candidate->mipmap.bands[index];
    band->max_phase_increment = wavetable_pool_read_u32(&encoded[0]);
    band->from_cycle = wavetable_pool_read_u32(&encoded[4]);
    band->to_cycle = wavetable_pool_read_u32(&encoded[8]);
    band->cycle_sample_count = wavetable_pool_read_u32(&encoded[12]);
    band->cycle_magnitude = wavetable_pool_read_u16(&encoded[16]);
    band->flags = wavetable_pool_read_u16(&encoded[18]);
    const uint32_t file_offset = wavetable_pool_read_u32(&encoded[20]);
    band->sample_count = wavetable_pool_read_u32(&encoded[24]);
    const uint32_t file_cycle_stride = wavetable_pool_read_u32(&encoded[28]);
    const uint32_t magnitude = WAVETABLE_MIPMAP_INITIAL_CYCLE_MAGNITUDE - index;
    const uint32_t cycle_samples = 1UL << magnitude;
    const uint32_t cycle_stride = wavetable_pool_mipmap_cycle_stride(cycle_samples);
    const uint32_t sample_count = candidate->frame_count * cycle_stride;
    const uint32_t max_inc = (uint32_t)((double)(UINT32_MAX >> magnitude)
        * WAVETABLE_PREP_PHASE_LIMIT_RATIO);
    const uint32_t bytes = sample_count * sizeof(float);
    wavetable_mipmap_view_t *const view = &candidate->mipmap;
    if ((band->from_cycle != 0U) || (band->to_cycle != candidate->frame_count)
        || (band->cycle_sample_count != cycle_samples)
        || (file_cycle_stride != cycle_stride)
        || (band->cycle_magnitude != magnitude)
        || (band->max_phase_increment != max_inc)
        || (band->flags != WAVETABLE_MIPMAP_FLAG_MULTIBAND)
        || (band->sample_count != sample_count)
        || (file_offset != payload_offset)
        || (payload_offset > view->data_bytes)
        || (bytes > (view->data_bytes - payload_offset)))
        return 0U;
    band->data = &view->data[payload_offset / sizeof(float)];
    *out_next_offset = payload_offset + bytes;
    return 1U;
}

static void wavetable_pool_release_slot_allocations(
    const wavetable_slot_t *slot)
{
    (void)slot;
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
