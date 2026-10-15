static uint8_t sample_page_cache_sample_is_stream_loadable_key(sample_audio_key_t key);

static uint32_t sample_page_cache_stream_page_frame_count_key(sample_audio_key_t key,
                                                              uint32_t page_index)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        const sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
        if (((sample->flags & SAMPLE_PAGE_SAMPLE_VALID) != 0U)
            && (sample->stream_safe.total_frames != 0U))
        {
            const sample_audio_format_t format = sample_audio_format_or_stereo(
                (sample_audio_format_t)sample->format);
            const uint32_t frames_per_page = sample_audio_format_frames_per_page(format);
            const uint32_t start_frame = sample_audio_format_page_start_frame(format, page_index);
            if (start_frame >= sample->stream_safe.total_frames)
            {
                return 0U;
            }

            uint32_t frame_count = sample->stream_safe.total_frames - start_frame;
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
    return (((sample->flags & SAMPLE_PAGE_SAMPLE_VALID) != 0U)
        && (sample_audio_key_equal(&sample->stream_safe.key, &key) != 0U)
        && (sample_stream_physical_map_is_current(
            &sample->stream_safe.physical_map) != 0U)) ? 1U : 0U;
}
static void sample_page_cache_alloc_range(sample_page_alloc_type_t alloc_type,
                                          uint32_t *out_start,
                                          uint32_t *out_count,
                                          uint32_t *out_carrier_start,
                                          uint32_t *out_carrier_count)
{
    uint32_t start = SAMPLE_PAGE_SLOT_POOL_START;
    uint32_t count = SAMPLE_PAGE_SLOT_POOL_COUNT;
    uint32_t carrier_start = SAMPLE_PAGE_ORDINARY_CARRIER_START;
    uint32_t carrier_count = SAMPLE_PAGE_ORDINARY_CARRIER_COUNT;

    switch (alloc_type)
    {
        case SAMPLE_PAGE_ALLOC_SLOT_PERMANENT:
            start = SAMPLE_PAGE_SLOT_POOL_START;
            count = SAMPLE_PAGE_SLOT_POOL_COUNT;
            break;

        case SAMPLE_PAGE_ALLOC_SLICE:
            start = SAMPLE_PAGE_SLICE_POOL_START;
            count = SAMPLE_PAGE_SLICE_POOL_COUNT;
            carrier_start = SAMPLE_PAGE_SLICE_CARRIER_START;
            carrier_count = SAMPLE_PAGE_SLICE_CARRIER_COUNT;
            break;

        case SAMPLE_PAGE_ALLOC_VOICE_WINDOW:
            start = SAMPLE_PAGE_VOICE_WINDOW_POOL_START;
            count = SAMPLE_PAGE_VOICE_WINDOW_POOL_COUNT;
            carrier_start = SAMPLE_PAGE_RUNTIME_CARRIER_START;
            carrier_count = SAMPLE_PAGE_RUNTIME_CARRIER_COUNT;
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
    if (out_carrier_start != 0) *out_carrier_start = carrier_start;
    if (out_carrier_count != 0) *out_carrier_count = carrier_count;
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

static uint32_t sample_page_cache_runtime_bitmap_first_from(
    uint64_t bitmap, uint16_t cursor)
{
    if (bitmap == 0U) return UINT32_MAX;
    uint32_t offset = sample_page_cache_range_cursor_offset(
        cursor, SAMPLE_PAGE_VOICE_WINDOW_POOL_START,
        SAMPLE_PAGE_VOICE_WINDOW_POOL_COUNT);
    const uint64_t after = bitmap & (~UINT64_C(0) << offset);
    if (after != 0U) return (uint32_t)__builtin_ctzll(after);
    return (uint32_t)__builtin_ctzll(bitmap);
}

static sample_page_desc_t *sample_page_cache_alloc_empty_slot_key(sample_audio_key_t key,
                                                                   uint32_t page_index,
                                                                   sample_page_alloc_type_t alloc_type,
                                                                   uint32_t insert_index)
{
    sample_page_desc_t *evict_page = 0;
    const uint32_t frame_count = sample_page_cache_stream_page_frame_count_key(key, page_index);
    if (frame_count == 0U)
    {
        return 0;
    }

    uint32_t range_start = 0U;
    uint32_t range_count = SAMPLE_PAGE_MAX_COUNT;
    uint32_t carrier_start = 0U;
    uint32_t carrier_count = 0U;
    sample_page_cache_alloc_range(alloc_type, &range_start, &range_count,
                                  &carrier_start, &carrier_count);

    uint32_t selected_free = UINT32_MAX;
    if ((range_start == SAMPLE_PAGE_VOICE_WINDOW_POOL_START)
        && (range_count == SAMPLE_PAGE_VOICE_WINDOW_POOL_COUNT))
    {
        const uint32_t offset = sample_page_cache_runtime_bitmap_first_from(
            g_sample_page_runtime_free_bitmap, g_sample_page_free_cursor);
        if (offset != UINT32_MAX) selected_free = range_start + offset;
    }
    else for (uint32_t scan = 0U; scan < range_count; ++scan)
    {
        const uint32_t cursor_offset =
            sample_page_cache_range_cursor_offset(g_sample_page_free_cursor,
                                                  range_start,
                                                  range_count);
        const uint32_t i = range_start + ((cursor_offset + scan) % range_count);
        sample_page_desc_t *const page = &g_sample_page_desc[i];
        if (page->state == SAMPLE_PAGE_FREE)
        {
            selected_free = i;
            break;
        }
    }
    if (selected_free != UINT32_MAX)
    {
        sample_page_desc_t *const page = &g_sample_page_desc[selected_free];
        page->key = key;
        page->page_index = page_index;
        sample_page_cache_set_page_geometry(page, key);
        if ((sample_page_cache_assign_payload(page, carrier_start, carrier_count) == 0U)
            && ((page->format != SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED)
                || (((sample_page_cache_compact_mono_at_stop(carrier_start, carrier_count) != 0U)
                     || (sample_page_cache_recycle_carrier_for_stereo(
                             key, carrier_start, carrier_count) != 0U))
                    && (sample_page_cache_assign_payload(page, carrier_start, carrier_count) != 0U))))
        {
            sample_page_cache_clear_desc(page, selected_free);
            return 0;
        }
        page->start_frame = sample_audio_format_page_start_frame(page->format, page_index);
        page->frame_count = (uint16_t)frame_count;
        page->generation = ++g_sample_page_cache_state.generation_counter;
        page->last_touch = ++g_sample_page_cache_state.touch_counter;
        page->static_resident = 0U;
        sample_page_cache_set_state(page, SAMPLE_PAGE_RESERVED);
        const uint16_t key_slot = sample_page_cache_key_slot(key);
        if (key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
        {
            sample_page_cache_index_put_new_key_at(
                key, page_index, (uint16_t)selected_free, insert_index);
            g_sample_page_last_slot[key_slot] = (uint16_t)selected_free;
        }
        g_sample_page_free_cursor = (uint16_t)(range_start
            + (((selected_free - range_start) + 1U) % range_count));
        return page;
    }

    sample_page_lease_active_snapshot_t lease_snapshot;
    if (sample_page_lease_control_snapshot_active(&lease_snapshot) == 0U)
        return 0;

    uint64_t ready_candidates = g_sample_page_runtime_ready_bitmap;
    for (uint32_t scan = 0U; scan < range_count; ++scan)
    {
        uint32_t i;
        if ((range_start == SAMPLE_PAGE_VOICE_WINDOW_POOL_START)
            && (range_count == SAMPLE_PAGE_VOICE_WINDOW_POOL_COUNT))
        {
            const uint32_t offset = sample_page_cache_runtime_bitmap_first_from(
                ready_candidates, g_sample_page_evict_cursor);
            if (offset == UINT32_MAX) break;
            ready_candidates &= ~(UINT64_C(1) << offset);
            i = range_start + offset;
        }
        else
        {
            const uint32_t cursor_offset = sample_page_cache_range_cursor_offset(
                g_sample_page_evict_cursor, range_start, range_count);
            i = range_start + ((cursor_offset + scan) % range_count);
        }
        sample_page_desc_t *const page = &g_sample_page_desc[i];
        if ((page->state != SAMPLE_PAGE_READY)
            || (sample_page_cache_page_is_contractual_snapshot(
                    page, &lease_snapshot) != 0U)
            || (sample_page_cache_key_slot(page->key) >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
            || (sample_page_cache_can_evict_for_request(key, page->key) == 0U))
        {
            continue;
        }

        if ((evict_page == 0) || (page->last_touch < evict_page->last_touch))
            evict_page = page;
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
    evict_page->page_index = page_index;
    sample_page_cache_set_page_geometry(evict_page, key);
    if ((sample_page_cache_assign_payload(evict_page, carrier_start, carrier_count) == 0U)
        && ((evict_page->format != SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED)
            || (((sample_page_cache_compact_mono_at_stop(carrier_start, carrier_count) != 0U)
                 || (sample_page_cache_recycle_carrier_for_stereo(
                         key, carrier_start, carrier_count) != 0U))
                && (sample_page_cache_assign_payload(evict_page, carrier_start, carrier_count) != 0U))))
    {
        sample_page_cache_clear_desc(evict_page, slot_index);
        return 0;
    }
    evict_page->start_frame = sample_audio_format_page_start_frame(evict_page->format, page_index);
    evict_page->frame_count = (uint16_t)frame_count;
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
