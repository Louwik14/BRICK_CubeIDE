static uint8_t sample_page_cache_sample_is_stream_loadable_key(sample_audio_key_t key);

static uint32_t sample_page_cache_stream_page_frame_count_key(sample_audio_key_t key,
                                                              uint32_t page_index)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        const sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
        if ((sample->valid != 0U) && (sample->fully_loaded == 0U) && (sample->total_frames != 0U))
        {
            const sample_audio_format_t format = sample_audio_format_or_stereo(sample->format);
            const uint32_t frames_per_page = sample_audio_format_frames_per_page(format);
            const uint32_t start_frame = sample_audio_format_page_start_frame(format, page_index);
            if (start_frame >= sample->total_frames)
            {
                return 0U;
            }

            uint32_t frame_count = sample->total_frames - start_frame;
            if (frame_count > frames_per_page)
            {
                frame_count = frames_per_page;
            }
            if (((uint64_t)start_frame + frame_count) > sample->readable_frames)
            {
                return 0U;
            }
            return frame_count;
        }
    }

    return sample_audio_format_frames_per_page(SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED);
}

static uint8_t sample_page_cache_sample_is_stream_loadable_key(sample_audio_key_t key)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        return 0U;
    }

    const sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    return ((sample->valid != 0U) && (sample->fully_loaded == 0U)
            && (sample->path[0] != '\0')) ? 1U : 0U;
}

static void sample_page_cache_alloc_range(sample_page_alloc_type_t alloc_type,
                                          uint32_t *out_start,
                                          uint32_t *out_count)
{
    uint32_t start = 0U;
    uint32_t count = SAMPLE_PAGE_MAX_COUNT;

    switch (alloc_type)
    {
        case SAMPLE_PAGE_ALLOC_SLOT_PERMANENT:
            start = SAMPLE_PAGE_SLOT_POOL_START;
            count = SAMPLE_PAGE_SLOT_POOL_COUNT;
            break;

        case SAMPLE_PAGE_ALLOC_VOICE_WINDOW:
            start = SAMPLE_PAGE_VOICE_WINDOW_POOL_START;
            count = SAMPLE_PAGE_VOICE_WINDOW_POOL_COUNT;
            break;

        case SAMPLE_PAGE_ALLOC_MARGIN:
            start = SAMPLE_PAGE_MARGIN_POOL_START;
            count = SAMPLE_PAGE_MARGIN_POOL_COUNT;
            break;

        case SAMPLE_PAGE_ALLOC_GENERAL:
        default:
            break;
    }

    if (out_start != 0)
    {
        *out_start = start;
    }
    if (out_count != 0)
    {
        *out_count = count;
    }
}

static uint32_t sample_page_cache_range_cursor_offset(uint16_t cursor,
                                                      uint32_t range_start,
                                                      uint32_t range_count)
{
    if (((uint32_t)cursor >= range_start)
        && ((uint32_t)cursor < (range_start + range_count)))
    {
        return (uint32_t)cursor - range_start;
    }
    return 0U;
}

static sample_page_desc_t *sample_page_cache_alloc_empty_slot_key(sample_audio_key_t key,
                                                                  uint32_t page_index,
                                                                  sample_page_alloc_type_t alloc_type)
{
    sample_page_desc_t *evict_page = 0;
    const uint32_t frame_count = sample_page_cache_stream_page_frame_count_key(key, page_index);
    if (frame_count == 0U)
    {
        return 0;
    }

    uint32_t range_start = 0U;
    uint32_t range_count = SAMPLE_PAGE_MAX_COUNT;
    sample_page_cache_alloc_range(alloc_type, &range_start, &range_count);

    for (uint32_t scan = 0U; scan < range_count; ++scan)
    {
        const uint32_t cursor_offset =
            sample_page_cache_range_cursor_offset(g_sample_page_free_cursor,
                                                  range_start,
                                                  range_count);
        const uint32_t i = range_start + ((cursor_offset + scan) % range_count);
        sample_page_desc_t *const page = &g_sample_page_desc[i];
        if (page->state == SAMPLE_PAGE_FREE)
        {
            page->key = key;
            page->sample_id = sample_page_cache_key_slot(key);
            page->page_index = page_index;
            sample_page_cache_set_page_geometry(page, key);
            page->start_frame = sample_audio_format_page_start_frame(
                (sample_audio_format_t)page->format, page_index);
            page->frame_count = frame_count;
            page->generation = ++g_sample_page_cache_state.generation_counter;
            page->last_touch = ++g_sample_page_cache_state.touch_counter;
            page->static_resident = 0U;
            sample_page_cache_set_state(page, SAMPLE_PAGE_RESERVED);
            const uint16_t key_slot = sample_page_cache_key_slot(key);
            if (key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
            {
                sample_page_cache_index_put_key(key, page_index, (uint16_t)i);
                g_sample_page_last_slot[key_slot] = (uint16_t)i;
            }
            g_sample_page_free_cursor =
                (uint16_t)(range_start + (((i - range_start) + 1U) % range_count));
            return page;
        }
    }

    for (uint32_t scan = 0U; scan < range_count; ++scan)
    {
        const uint32_t cursor_offset =
            sample_page_cache_range_cursor_offset(g_sample_page_evict_cursor,
                                                  range_start,
                                                  range_count);
        const uint32_t i = range_start + ((cursor_offset + scan) % range_count);
        sample_page_desc_t *const page = &g_sample_page_desc[i];
        if ((page->state != SAMPLE_PAGE_READY)
            || (sample_page_cache_page_is_contractual(page) != 0U)
            || (sample_page_cache_key_slot(page->key) >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
            || (sample_page_cache_can_evict_for_request(key, page->key) == 0U))
        {
            continue;
        }

        if ((evict_page == 0) || (page->last_touch < evict_page->last_touch))
        {
            evict_page = page;
        }
    }

    if (evict_page == 0)
    {
        return 0;
    }

    if (sample_page_cache_claim_for_recycle(evict_page) == 0U)
    {
        return 0;
    }

    const uint32_t slot_index = (uint32_t)(evict_page - g_sample_page_desc);
    sample_page_cache_clear_desc(evict_page, slot_index);
    evict_page->key = key;
    evict_page->sample_id = sample_page_cache_key_slot(key);
    evict_page->page_index = page_index;
    sample_page_cache_set_page_geometry(evict_page, key);
        evict_page->start_frame = sample_audio_format_page_start_frame(
            (sample_audio_format_t)evict_page->format, page_index);
    evict_page->frame_count = frame_count;
    evict_page->generation = ++g_sample_page_cache_state.generation_counter;
    evict_page->last_touch = ++g_sample_page_cache_state.touch_counter;
    evict_page->static_resident = 0U;
    sample_page_cache_set_state(evict_page, SAMPLE_PAGE_RESERVED);
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        sample_page_cache_index_put_key(key, page_index, (uint16_t)slot_index);
        g_sample_page_last_slot[key_slot] = (uint16_t)slot_index;
    }
    g_sample_page_evict_cursor =
        (uint16_t)(range_start + (((slot_index - range_start) + 1U) % range_count));
    return evict_page;
}

static uint32_t sample_page_cache_trim_path_copy(char *dst, uint32_t dst_size, const char *src)
{
    uint32_t start = 0U;
    uint32_t end;

    if ((dst == 0) || (dst_size == 0U) || (src == 0))
    {
        return 0U;
    }

    end = (uint32_t)strlen(src);
    while ((start < end) && (((unsigned char)src[start]) <= ' '))
    {
        start++;
    }
    while ((end > start) && (((unsigned char)src[end - 1U]) <= ' '))
    {
        end--;
    }

    const uint32_t len = end - start;
    if ((len == 0U) || (len >= dst_size))
    {
        return 0U;
    }

    memcpy(dst, &src[start], len);
    dst[len] = '\0';
    return len;
}

static int32_t sample_page_cache_find_contiguous_empty_run_in_range(uint32_t page_count,
                                                                    uint32_t range_start,
                                                                    uint32_t range_count)
{
    if ((page_count == 0U) || (page_count > range_count)
        || ((range_start + range_count) > SAMPLE_PAGE_MAX_COUNT))
    {
        return -1;
    }

    for (uint32_t start = range_start; (start + page_count) <= (range_start + range_count); ++start)
    {
        uint32_t i = 0U;
        for (; i < page_count; ++i)
        {
            if (g_sample_page_desc[start + i].state != SAMPLE_PAGE_FREE)
            {
                break;
            }
        }

        if (i == page_count)
        {
            return (int32_t)start;
        }

        start += i;
    }

    return -1;
}

static void sample_page_cache_reclaim_stream_pages_for_full_load_in_range(uint32_t range_start,
                                                                          uint32_t range_count)
{
    if ((range_start + range_count) > SAMPLE_PAGE_MAX_COUNT)
    {
        return;
    }

    for (uint32_t i = range_start; i < (range_start + range_count); ++i)
    {
        sample_page_desc_t *const page = &g_sample_page_desc[i];
        if ((page->state != SAMPLE_PAGE_READY)
            || (sample_page_cache_page_is_contractual(page) != 0U)
            || (sample_page_cache_key_slot(page->key) >= SAMPLE_PAGE_CACHE_MAX_SAMPLES))
        {
            continue;
        }

        const sample_page_sample_desc_t *const sample =
            &g_sample_page_sample_desc[sample_page_cache_key_slot(page->key)];
        if ((sample->valid != 0U) && (sample->fully_loaded == 0U))
        {
            if (sample_page_cache_claim_for_recycle(page) != 0U)
            {
                sample_page_cache_clear_desc(page, i);
            }
        }
    }
}

static sample_page_desc_t *sample_page_cache_assign_slot_key(uint32_t slot_index,
                                                             sample_audio_key_t key,
                                                             uint32_t page_index,
                                                             uint32_t frame_count);

static sample_page_desc_t *sample_page_cache_assign_slot_key(uint32_t slot_index,
                                                             sample_audio_key_t key,
                                                             uint32_t page_index,
                                                             uint32_t frame_count)
{
    if (slot_index >= SAMPLE_PAGE_MAX_COUNT)
    {
        return 0;
    }

    sample_page_desc_t *const page = &g_sample_page_desc[slot_index];
    sample_page_cache_clear_desc(page, slot_index);
    page->key = key;
    page->sample_id = sample_page_cache_key_slot(key);
    page->page_index = page_index;
    sample_page_cache_set_page_geometry(page, key);
    page->start_frame = sample_audio_format_page_start_frame(
        (sample_audio_format_t)page->format, page_index);
    page->frame_count = frame_count;
    page->generation = ++g_sample_page_cache_state.generation_counter;
    page->last_touch = ++g_sample_page_cache_state.touch_counter;
    sample_page_cache_set_state(page, SAMPLE_PAGE_RESERVED);
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        sample_page_cache_index_put_key(key, page_index, (uint16_t)slot_index);
        g_sample_page_last_slot[key_slot] = (uint16_t)slot_index;
    }
    return page;
}
