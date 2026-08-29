uint8_t sample_page_cache_pin_page(uint16_t sample_id, uint32_t page_index)
{
    return sample_page_cache_pin_page_key(sample_audio_key_classic(sample_id), page_index);
}

uint8_t sample_page_cache_pin_page_key(sample_audio_key_t key, uint32_t page_index)
{
    return sample_page_cache_pin_page_key_alloc(key,
                                                page_index,
                                                SAMPLE_PAGE_ALLOC_GENERAL);
}

uint8_t sample_page_cache_pin_page_key_alloc(sample_audio_key_t key,
                                             uint32_t page_index,
                                             sample_page_alloc_type_t alloc_type)
{
    sample_page_desc_t *page = sample_page_cache_find_page_mut_key(key, page_index);
    if (page == 0)
    {
        page = sample_page_cache_alloc_empty_slot_key(key, page_index, alloc_type);
        if (page == 0)
        {
            return 0U;
        }
    }

    if (page->pin_count != UINT16_MAX)
    {
        page->pin_count++;
    }
    page->last_touch = ++g_sample_page_cache_state.touch_counter;
    return 1U;
}

void sample_page_cache_unpin_page(uint16_t sample_id, uint32_t page_index)
{
    sample_page_cache_unpin_page_key(sample_audio_key_classic(sample_id), page_index);
}

void sample_page_cache_unpin_page_key(sample_audio_key_t key, uint32_t page_index)
{
    sample_page_desc_t *const page = sample_page_cache_find_page_mut_key(key, page_index);
    if (page == 0)
    {
        return;
    }

    if (page->pin_count != 0U)
    {
        page->pin_count--;
    }
    page->last_touch = ++g_sample_page_cache_state.touch_counter;
}

void sample_page_cache_unpin_page_ref_key(sample_audio_key_t key,
                                          const sample_page_ref_t *ref)
{
    if ((ref == 0) || (sample_audio_key_equal(&key, &ref->key) == 0U)
        || (ref->slot_index >= SAMPLE_PAGE_MAX_COUNT))
    {
        return;
    }
    sample_page_desc_t *const page = &g_sample_page_desc[ref->slot_index];
    if ((sample_audio_key_equal(&page->key, &ref->key) == 0U)
        || (page->page_index != ref->page_index)
        || (page->generation != ref->page_generation)
        || (page->registration_epoch != ref->registration_epoch))
    {
        return;
    }
    if (page->pin_count != 0U)
    {
        page->pin_count--;
    }
    page->last_touch = ++g_sample_page_cache_state.touch_counter;
}

uint8_t sample_page_cache_register_live_pcm24_stereo_sample_key(
    sample_audio_key_t key,
    const char *path,
    uint32_t total_frames,
    uint32_t readable_frames,
    uint32_t data_offset,
    uint32_t file_size,
    const sample_stream_physical_extent_t *extents,
    uint16_t extent_count,
    uint32_t media_epoch)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    const uint64_t data_bytes = (uint64_t)total_frames
                              * AUDIO_RECORDER_BYTES_PER_FRAME;
    if ((key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES) || (path == 0)
            || (total_frames == 0U) || (readable_frames > total_frames)
            || (data_bytes > UINT32_MAX)
            || (((uint64_t)data_offset + data_bytes) > file_size))
    {
        return 0U;
    }

    sample_page_cache_clear_key(key);
    sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    if (sample_page_cache_trim_path_copy(
            sample->path, sizeof(sample->path), path) == 0U)
    {
        return 0U;
    }
    sample->key = key;
    sample->info.sample_rate = AUDIO_RECORDER_SAMPLE_RATE_HZ;
    sample->info.channels = AUDIO_RECORDER_CHANNELS;
    sample->info.bits_per_sample = 24U;
    sample->info.block_align = AUDIO_RECORDER_BYTES_PER_FRAME;
    sample->info.byte_rate = AUDIO_RECORDER_SAMPLE_RATE_HZ
                           * AUDIO_RECORDER_BYTES_PER_FRAME;
    sample->info.data_size = (uint32_t)data_bytes;
    sample->total_frames = total_frames;
    sample->readable_frames = readable_frames;
    sample->data_offset = data_offset;
    sample->format = SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED;
    sample->stride_floats = (uint16_t)sample_audio_format_stride_floats(
        sample->format);
    sample->frames_per_page = sample_audio_format_frames_per_page(sample->format);
    sample->registration_epoch = ++g_sample_page_cache_state.registration_epoch_counter;
    sample_stream_safe_metadata_init_fatfs(key,
                                           &sample->info,
                                           total_frames,
                                           data_offset,
                                           &sample->stream_safe);
    sample->stream_safe.file_size = file_size;
    if (sample_stream_physical_map_import(
            &sample->stream_safe.physical_map,
            extents, extent_count, media_epoch) == 0U)
    {
        memset(sample, 0, sizeof(*sample));
        return 0U;
    }
    sample->valid = 1U;
    sample->fully_loaded = 0U;
    sample->live_committed = 1U;
    sample->physical_only = 1U;
    sample->first_slot = UINT16_MAX;
    return 1U;
}

uint8_t sample_page_cache_update_readable_frames_key(sample_audio_key_t key,
                                                      uint32_t readable_frames)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        return 0U;
    }
    sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    if ((sample->valid == 0U) || (readable_frames > sample->total_frames))
    {
        return 0U;
    }
    const uint32_t primask = sample_page_cache_lock();
    if (readable_frames > sample->readable_frames)
    {
        sample->readable_frames = readable_frames;
    }
    sample_page_cache_unlock(primask);
    return 1U;
}

uint8_t sample_page_cache_update_live_map_key(
    sample_audio_key_t key,
    uint32_t file_size,
    const sample_stream_physical_extent_t *extents,
    uint16_t extent_count,
    uint32_t media_epoch)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if ((key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES) || (extents == 0)
            || (extent_count == 0U))
    {
        return 0U;
    }
    sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    if ((sample->valid == 0U) || (sample->live_committed == 0U))
    {
        return 0U;
    }

    sample_stream_physical_map_t map;
    memset(&map, 0, sizeof(map));
    map.first_pool_block = SAMPLE_STREAM_PHYSICAL_MAP_INVALID_BLOCK;
    if (sample_stream_physical_map_import(
            &map, extents, extent_count, media_epoch) == 0U)
    {
        return 0U;
    }
    const uint32_t primask = sample_page_cache_lock();
    const sample_stream_physical_map_t previous = sample->stream_safe.physical_map;
    sample->stream_safe.physical_map = map;
    sample->stream_safe.file_size = file_size;
    sample_page_cache_unlock(primask);
    sample_stream_physical_map_t released = previous;
    sample_stream_physical_map_release(&released);
    return 1U;
}

uint8_t sample_page_cache_update_stream_path_key(sample_audio_key_t key,
                                                  const char *path)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if ((key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES) || (path == 0)
            || (g_sample_page_sample_desc[key_slot].valid == 0U))
    {
        return 0U;
    }
    return (sample_page_cache_trim_path_copy(
        g_sample_page_sample_desc[key_slot].path,
        sizeof(g_sample_page_sample_desc[key_slot].path), path) != 0U) ? 1U : 0U;
}

uint8_t sample_page_cache_get_reserved_load_target_domain_range(
    sample_audio_domain_t domain,
    uint16_t first_object_id,
    uint16_t object_count,
    sample_page_load_target_t *out_target)
{
    if ((out_target == 0) || (object_count == 0U))
    {
        return 0U;
    }
    for (uint16_t offset_id = 0U; offset_id < object_count; ++offset_id)
    {
        const sample_audio_key_t key = {
            .domain = domain,
            .object_id = (uint16_t)(first_object_id + offset_id),
        };
        const uint16_t key_slot = sample_page_cache_key_slot(key);
        if (key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
        {
            continue;
        }

        sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
        if ((sample->valid == 0U) || (sample->fully_loaded != 0U))
        {
            continue;
        }

        for (uint32_t i = 0U; i < SAMPLE_PAGE_MAX_COUNT; ++i)
        {
            sample_page_desc_t *const page = &g_sample_page_desc[i];
            if ((sample_audio_key_equal(&page->key, &key) == 0U)
                || (page->state != SAMPLE_PAGE_RESERVED))
            {
                continue;
            }

            return sample_page_cache_get_load_target_key(
                key, page->page_index, out_target);
        }
    }
    return 0U;
}
