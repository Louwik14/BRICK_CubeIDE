static uint16_t wavetable_pool_mipmap_band_count(void);
static uint32_t wavetable_pool_mipmap_samples_per_table_cycle(void);
static float wavetable_pool_finite_float(float value);

static uint32_t wavetable_pool_mipmap_cycle_stride(uint32_t cycle_sample_count)
{
    return (cycle_sample_count == 8U) ? 16U : cycle_sample_count;
}

static uint32_t wavetable_pool_source_cycle_sample_count(
    wavetable_source_geometry_t source_geometry)
{
    return (source_geometry == WAVETABLE_SOURCE_GEOMETRY_2048)
        ? WAVETABLE_SOURCE_2048_SAMPLE_COUNT
        : WAVETABLE_FRAME_SAMPLE_COUNT;
}

static void wavetable_pool_source_2048_store(uint32_t index, float value)
{
    if (index < WAVETABLE_SOURCE_2048_SAMPLE_COUNT)
        g_wavetable_fft_real[index] = value;
}

static float wavetable_pool_spectral_gain(uint32_t bin,
                                          uint32_t nyquist,
                                          float pass_ratio,
                                          float stop_ratio)
{
    const float position = (nyquist != 0U)
        ? ((float)bin / (float)nyquist) : 1.0f;
    if (position <= pass_ratio) return 1.0f;
    if ((position >= stop_ratio) || (stop_ratio <= pass_ratio)) return 0.0f;
    const float phase = (position - pass_ratio) / (stop_ratio - pass_ratio);
    return 0.5f * (1.0f + cosf(3.14159265358979323846f * phase));
}

static void wavetable_pool_set_last(wavetable_result_t result)
{
    g_wavetable_pool.last_result = result;
}

static uint32_t wavetable_pool_next_generation(void)
{
    g_wavetable_pool.generation_counter++;
    if (g_wavetable_pool.generation_counter == 0U)
    {
        g_wavetable_pool.generation_counter = 1U;
    }
    return g_wavetable_pool.generation_counter;
}

static uint32_t wavetable_pool_crc32_update(uint32_t crc,
                                            const uint8_t *data,
                                            uint32_t size)
{
    crc = ~crc;
    for (uint32_t i = 0U; i < size; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

static uint32_t wavetable_pool_crc32_memory(const void *data, uint32_t size)
{
    return (data != 0)
        ? wavetable_pool_crc32_update(0U, (const uint8_t *)data, size)
        : 0U;
}

static void wavetable_pool_copy_float_exact(float *dst,
                                            const float *src,
                                            uint32_t sample_count)
{
    volatile float *const dst_float = (volatile float *)dst;
    const volatile float *const src_float = (const volatile float *)src;
    for (uint32_t i = 0U; i < sample_count; ++i)
    {
        dst_float[i] = src_float[i];
    }
}

static uint8_t wavetable_pool_copy_path(char *dst, uint32_t dst_size, const char *src)
{
    if ((dst == 0) || (dst_size == 0U) || (src == 0) || (src[0] == '\0'))
    {
        return 0U;
    }

    uint32_t i = 0U;
    while ((i + 1U) < dst_size)
    {
        dst[i] = src[i];
        if (src[i] == '\0')
        {
            return 1U;
        }
        i++;
    }

    dst[i] = '\0';
    return (src[i] == '\0') ? 1U : 0U;
}

static uint16_t wavetable_pool_read_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t wavetable_pool_read_u32(const uint8_t *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static void wavetable_pool_write_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xFFU);
    p[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void wavetable_pool_write_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xFFU);
    p[1] = (uint8_t)((value >> 8) & 0xFFU);
    p[2] = (uint8_t)((value >> 16) & 0xFFU);
    p[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static void wavetable_pool_write_i32(uint8_t *p, int32_t value)
{
    wavetable_pool_write_u32(p, (uint32_t)value);
}

static uint16_t wavetable_pool_preview_abs_i16(int16_t value)
{
    return (value < 0) ? (uint16_t)(-value) : (uint16_t)value;
}

static int16_t wavetable_pool_preview_float_to_i16(float value)
{
    if (value >= 1.0f) return INT16_MAX;
    if (value <= -1.0f) return INT16_MIN;
    return (int16_t)lrintf(value * 32767.0f);
}

static float wavetable_pool_finite_float(float value)
{
    return isfinite(value) ? value : 0.0f;
}

static void wavetable_pool_preview_clear(wavetable_preview_t *preview)
{
    if (preview == 0)
    {
        return;
    }
    memset(preview, 0, sizeof(*preview));
    preview->state = WAVETABLE_PREVIEW_EMPTY;
}

static void wavetable_pool_preview_build(wavetable_slot_t *slot)
{
    if ((slot == 0)
            || (slot->data == 0)
            || (slot->frame_count == 0U)
            || (slot->frame_sample_count != WAVETABLE_FRAME_SAMPLE_COUNT))
    {
        return;
    }

    wavetable_preview_t *const preview = &slot->preview;
    memset(preview, 0, sizeof(*preview));
    preview->state = WAVETABLE_PREVIEW_READY;
    preview->generation = slot->generation;
    preview->frame_count = slot->frame_count;
    preview->columns = WAVETABLE_PREVIEW_COLUMNS;
    for (uint16_t col = 0U; col < WAVETABLE_PREVIEW_COLUMNS; ++col)
    {
        preview->min[col] = 32767;
        preview->max[col] = -32768;
    }

    for (uint32_t frame = 0U; frame < slot->frame_count; ++frame)
    {
        uint32_t col = (uint32_t)(((uint64_t)frame * WAVETABLE_PREVIEW_COLUMNS) / slot->frame_count);
        if (col >= WAVETABLE_PREVIEW_COLUMNS)
        {
            col = WAVETABLE_PREVIEW_COLUMNS - 1U;
        }

        const float *const src = &slot->data[frame * WAVETABLE_FRAME_SAMPLE_COUNT];
        float frame_min = 1.0f;
        float frame_max = -1.0f;
        for (uint32_t i = 0U; i < WAVETABLE_FRAME_SAMPLE_COUNT; ++i)
        {
            const float s = src[i];
            if (s < frame_min)
            {
                frame_min = s;
            }
            if (s > frame_max)
            {
                frame_max = s;
            }
        }

        const int16_t frame_min_s16 =
            wavetable_pool_preview_float_to_i16(frame_min);
        const int16_t frame_max_s16 =
            wavetable_pool_preview_float_to_i16(frame_max);
        if (frame_min_s16 < preview->min[col])
        {
            preview->min[col] = frame_min_s16;
        }
        if (frame_max_s16 > preview->max[col])
        {
            preview->max[col] = frame_max_s16;
        }
        const uint16_t min_peak = wavetable_pool_preview_abs_i16(frame_min_s16);
        const uint16_t max_peak = wavetable_pool_preview_abs_i16(frame_max_s16);
        if (min_peak > preview->global_peak)
        {
            preview->global_peak = min_peak;
        }
        if (max_peak > preview->global_peak)
        {
            preview->global_peak = max_peak;
        }
    }

    for (uint16_t col = 0U; col < WAVETABLE_PREVIEW_COLUMNS; ++col)
    {
        if (preview->min[col] > preview->max[col])
        {
            preview->min[col] = 0;
            preview->max[col] = 0;
        }
    }
}

static uint32_t wavetable_pool_path_hash(const char *path)
{
    uint32_t hash = 2166136261UL;
    if (path == 0)
    {
        return hash;
    }
    while (*path != '\0')
    {
        hash ^= (uint8_t)*path++;
        hash *= 16777619UL;
    }
    return hash;
}

static uint8_t wavetable_pool_make_cache_path_with_extension(char *out,
                                                             uint32_t out_size,
                                                             const char *source_path,
                                                             const FILINFO *source_info,
                                                             const char *extension)
{
    if ((out == 0) || (out_size == 0U) || (source_path == 0)
        || (source_info == 0) || (extension == 0))
    {
        return 0U;
    }
    const int written = snprintf(out,
                                 out_size,
                                 "%s/H%08lX_%08lX_%04X%04X.%s",
                                 WAVETABLE_POOL_CACHE_DIR,
                                 (unsigned long)wavetable_pool_path_hash(source_path),
                                 (unsigned long)((uint32_t)source_info->fsize),
                                 (unsigned)source_info->fdate,
                                 (unsigned)source_info->ftime,
                                 extension);
    return (uint8_t)((written >= 0) && ((uint32_t)written < out_size));
}

static uint8_t wavetable_pool_make_cache_path(char *out,
                                              uint32_t out_size,
                                              const char *source_path,
                                              const FILINFO *source_info)
{
    return wavetable_pool_make_cache_path_with_extension(out,
                                                         out_size,
                                                         source_path,
                                                         source_info,
                                                         "B6WT");
}

static uint8_t wavetable_pool_path_ext_is_wav(const char *path)
{
    const size_t len = (path != 0) ? strlen(path) : 0U;
    if (len < 4U)
    {
        return 0U;
    }
    return (uint8_t)((path[len - 4U] == '.')
                     && ((path[len - 3U] == 'w') || (path[len - 3U] == 'W'))
                     && ((path[len - 2U] == 'a') || (path[len - 2U] == 'A'))
                     && ((path[len - 1U] == 'v') || (path[len - 1U] == 'V')));
}

static void wavetable_pool_slot_error_at(uint16_t wavetable_slot,
                                         wavetable_result_t result,
                                         uint16_t forced_global_slot)
{
    if (wavetable_slot < WAVETABLE_POOL_MAX_SLOTS)
    {
        uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        g_wavetable_pool.slots[wavetable_slot].generation = wavetable_pool_next_generation();
        if (g_wavetable_pool.slots[wavetable_slot].page_count != 0U)
        {
            sample_page_cache_port_release_shared(
                g_wavetable_pool.slots[wavetable_slot].first_page_slot,
                g_wavetable_pool.slots[wavetable_slot].page_count);
        }
        if (g_wavetable_pool.slots[wavetable_slot].mipmap.page_count != 0U)
        {
            sample_page_cache_port_release_shared(
                g_wavetable_pool.slots[wavetable_slot].mipmap.first_page_slot,
                g_wavetable_pool.slots[wavetable_slot].mipmap.page_count);
        }
        g_wavetable_pool.slots[wavetable_slot].format = WAVETABLE_FORMAT_NONE;
        g_wavetable_pool.slots[wavetable_slot].data = 0;
        g_wavetable_pool.slots[wavetable_slot].data_offset = 0U;
        g_wavetable_pool.slots[wavetable_slot].first_page_slot = UINT16_MAX;
        g_wavetable_pool.slots[wavetable_slot].page_count = 0U;
        g_wavetable_pool.slots[wavetable_slot].data_bytes = 0U;
        g_wavetable_pool.slots[wavetable_slot].cost_bytes_aligned = 0U;
        memset(&g_wavetable_pool.slots[wavetable_slot].mipmap,
               0,
               sizeof(g_wavetable_pool.slots[wavetable_slot].mipmap));
        g_wavetable_pool.slots[wavetable_slot].mipmap.first_page_slot = UINT16_MAX;
        wavetable_pool_preview_clear(&g_wavetable_pool.slots[wavetable_slot].preview);
        g_wavetable_pool.slots[wavetable_slot].state = WAVETABLE_SLOT_ERROR;
        g_wavetable_pool.slots[wavetable_slot].error = result;
        const uint8_t registered =
            (forced_global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
                ? sample_global_pool_register_wavetable_error_at(
                    forced_global_slot,
                    wavetable_slot,
                    g_wavetable_pool.slots[wavetable_slot].path)
                : sample_global_pool_register_wavetable_error(
                    wavetable_slot,
                    g_wavetable_pool.slots[wavetable_slot].path,
                    &global_slot);
        if (registered != 0U)
        {
            g_wavetable_pool.slots[wavetable_slot].global_slot =
                (forced_global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
                    ? forced_global_slot
                    : global_slot;
        }
    }
    wavetable_pool_set_last(result);
}

void wavetable_pool_init(void)
{
    wavetable_load_job_boot_init();
    wavetable_pool_reset();
}

void wavetable_pool_reset(void)
{
    wavetable_pool_load_async_cancel();
    memset(g_wavetable_retire_not_before_sample, 0,
           sizeof(g_wavetable_retire_not_before_sample));
    memset(g_wavetable_retire_stop_committed, 0,
           sizeof(g_wavetable_retire_stop_committed));
    uint32_t generation_seed = g_wavetable_pool.generation_counter;
    for (uint16_t i = 0U; i < WAVETABLE_POOL_MAX_SLOTS; ++i)
    {
        g_wavetable_pool.slots[i].generation = wavetable_pool_next_generation();
        if (g_wavetable_pool.slots[i].page_count != 0U)
        {
            sample_page_cache_port_release_shared(
                g_wavetable_pool.slots[i].first_page_slot,
                g_wavetable_pool.slots[i].page_count);
        }
        if (g_wavetable_pool.slots[i].mipmap.page_count != 0U)
        {
            sample_page_cache_port_release_shared(
                g_wavetable_pool.slots[i].mipmap.first_page_slot,
                g_wavetable_pool.slots[i].mipmap.page_count);
        }
        sample_global_pool_clear_backend(SAMPLE_GLOBAL_KIND_WAVETABLE, i);
    }
    generation_seed = g_wavetable_pool.generation_counter;
    memset(&g_wavetable_pool, 0, sizeof(g_wavetable_pool));
    g_wavetable_pool.generation_counter = (generation_seed == 0U) ? 1U : generation_seed;
    for (uint16_t i = 0U; i < WAVETABLE_POOL_MAX_SLOTS; ++i)
    {
        g_wavetable_pool.slots[i].state = WAVETABLE_SLOT_EMPTY;
        g_wavetable_pool.slots[i].global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        g_wavetable_pool.slots[i].first_page_slot = UINT16_MAX;
        g_wavetable_pool.slots[i].mipmap.first_page_slot = UINT16_MAX;
        g_wavetable_pool.slots[i].error = WAVETABLE_RESULT_OK;
        g_wavetable_pool.slots[i].generation = wavetable_pool_next_generation();
    }
    wavetable_pool_set_last(WAVETABLE_RESULT_OK);
}

uint16_t wavetable_pool_find_free_slot(void)
{
    for (uint16_t i = 0U; i < WAVETABLE_POOL_MAX_SLOTS; ++i)
    {
        if (g_wavetable_pool.slots[i].state == WAVETABLE_SLOT_EMPTY)
        {
            return i;
        }
    }
    return WAVETABLE_POOL_INVALID_SLOT;
}

wavetable_result_t wavetable_pool_load_file_auto(const char *path,
                                                 uint16_t *out_wavetable_slot,
                                                 uint16_t *out_global_slot)
{
    const uint16_t wavetable_slot = wavetable_pool_find_free_slot();
    if (out_wavetable_slot != 0)
    {
        *out_wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
    }
    if (wavetable_slot == WAVETABLE_POOL_INVALID_SLOT)
    {
        wavetable_pool_set_last(WAVETABLE_RESULT_POOL_FULL);
        return WAVETABLE_RESULT_POOL_FULL;
    }

    const wavetable_result_t result =
        wavetable_pool_load_file(wavetable_slot, path, out_global_slot);
    if ((result == WAVETABLE_RESULT_OK) && (out_wavetable_slot != 0))
    {
        *out_wavetable_slot = wavetable_slot;
    }
    return result;
}

wavetable_result_t wavetable_pool_load_file(uint16_t wavetable_slot,
                                            const char *path,
                                            uint16_t *out_global_slot)
{
    return wavetable_pool_load_wav(wavetable_slot, path, out_global_slot);
}
