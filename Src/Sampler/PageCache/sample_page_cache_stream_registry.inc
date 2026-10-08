static uint8_t sample_page_cache_sample_is_stream_loadable_key(sample_audio_key_t key);

static uint32_t sample_page_cache_stream_page_frame_count_key(sample_audio_key_t key,
                                                              uint32_t page_index)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        const sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
        if ((sample->valid != 0U) && (sample->total_frames != 0U))
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
    return ((sample->valid != 0U) && (sample->path[0] != '\0')) ? 1U : 0U;
}

static void sample_page_cache_alloc_range(sample_page_alloc_type_t alloc_type,
                                          uint32_t *out_start,
                                          uint32_t *out_count)
{
    uint32_t start = SAMPLE_PAGE_SLOT_POOL_START;
    uint32_t count = SAMPLE_PAGE_SLOT_POOL_COUNT;

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

static void sample_page_cache_protection_tracker_clear(
    sample_page_protection_tracker_t *tracker)
{
    if (tracker == 0) return;
    for (uint8_t role = 0U; role < SAMPLE_PAGE_LEASE_PAGE_COUNT; ++role)
        sample_page_cache_protection_ref_clear(&tracker->role[role]);
    memset(tracker, 0, sizeof(*tracker));
    for (uint8_t role = 0U; role < SAMPLE_PAGE_LEASE_PAGE_COUNT; ++role)
        tracker->role[role].slot_index = UINT16_MAX;
}

static sample_page_protection_tracker_t *
sample_page_cache_protection_tracker_for_slot(uint8_t lease_slot)
{
    sample_page_protection_tracker_t *free_tracker = 0;
    for (uint8_t i = 0U; i < SAMPLE_STREAM_ACTIVE_READER_CAPACITY; ++i)
    {
        sample_page_protection_tracker_t *const tracker =
            &g_sample_page_protection_tracker[i];
        if ((tracker->active != 0U) && (tracker->lease_slot == lease_slot))
            return tracker;
        if ((tracker->active == 0U) && (free_tracker == 0)) free_tracker = tracker;
    }
    return free_tracker;
}

static uint8_t sample_page_cache_sync_runtime_protections(void)
{
    __DMB();
    const uint32_t change_before = g_sample_page_lease_change_counter;
    if ((change_before == g_sample_page_protection_synced_change)
        && (g_sample_page_protection_has_unresolved == 0U)) return 1U;

    sample_page_lease_active_snapshot_t snapshot;
    if (sample_page_lease_control_snapshot_active(&snapshot) == 0U) return 0U;
    PERF_COUNT(PERF_N_PROTECTION_UPDATE_STORAGE);

    for (uint8_t tracker_index = 0U;
         tracker_index < SAMPLE_STREAM_ACTIVE_READER_CAPACITY;
         ++tracker_index)
    {
        sample_page_protection_tracker_t *const tracker =
            &g_sample_page_protection_tracker[tracker_index];
        if (tracker->active == 0U) continue;
        uint8_t still_active = 0U;
        for (uint8_t lease_index = 0U; lease_index < snapshot.count; ++lease_index)
        {
            if (snapshot.slot[lease_index] == tracker->lease_slot)
            {
                still_active = 1U;
                break;
            }
        }
        if (still_active == 0U)
            sample_page_cache_protection_tracker_clear(tracker);
    }

    uint8_t unresolved = 0U;
    for (uint8_t lease_index = 0U; lease_index < snapshot.count; ++lease_index)
    {
        const sample_page_lease_t *const lease = &snapshot.lease[lease_index];
        sample_page_protection_tracker_t *const tracker =
            sample_page_cache_protection_tracker_for_slot(snapshot.slot[lease_index]);
        if (tracker == 0) return 0U;
        if ((tracker->active == 0U) || (tracker->lease_seq != lease->seq))
        {
            sample_page_cache_protection_tracker_clear(tracker);
            tracker->active = 1U;
            tracker->lease_slot = snapshot.slot[lease_index];
            tracker->lease_seq = lease->seq;
        }
        for (uint8_t role = 0U; role < SAMPLE_PAGE_LEASE_PAGE_COUNT; ++role)
        {
            sample_page_protection_ref_t *const ref = &tracker->role[role];
            if ((lease->valid_mask & SAMPLE_PAGE_LEASE_VALID(role)) == 0U)
            {
                sample_page_cache_protection_ref_clear(ref);
                continue;
            }
            if (ref->slot_index < SAMPLE_PAGE_MAX_COUNT) continue;
            if (sample_page_cache_protection_ref_set(
                    ref, lease->key, lease->pages[role]) == 0U)
                unresolved = 1U;
        }
    }
    g_sample_page_protection_has_unresolved = unresolved;
    __DMB();
    const uint32_t change_after = g_sample_page_lease_change_counter;
    if (change_after == change_before)
        g_sample_page_protection_synced_change = change_before;
    return 1U;
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
    sample_page_cache_alloc_range(alloc_type, &range_start, &range_count);

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
        page->sample_id = sample_page_cache_key_slot(key);
        page->page_index = page_index;
        sample_page_cache_set_page_geometry(page, key);
        page->start_frame = sample_audio_format_page_start_frame(page->format, page_index);
        page->frame_count = frame_count;
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

    const uint8_t runtime_range = (uint8_t)(
        (range_start == SAMPLE_PAGE_VOICE_WINDOW_POOL_START)
        && (range_count == SAMPLE_PAGE_VOICE_WINDOW_POOL_COUNT));
    sample_page_lease_active_snapshot_t lease_snapshot;
    if (runtime_range != 0U)
    {
        if (sample_page_cache_sync_runtime_protections() == 0U)
        {
            PERF_COUNT(PERF_N_RESERVE_NO_CANDIDATE);
            return 0;
        }
        uint64_t candidates = g_sample_page_runtime_recyclable_bitmap;
        PERF_COUNT(PERF_N_RESERVE_SEARCHES);
        PERF_ACCUM(PERF_N_RECYCLABLE_CANDIDATES,
                   (uint32_t)__builtin_popcountll(candidates));
        uint32_t candidates_tested = 0U;
        while (candidates != 0U)
        {
            sample_page_desc_t *candidate_page = 0;
            uint32_t candidate_offset = UINT32_MAX;
            uint64_t scan_bitmap = candidates;
            while (scan_bitmap != 0U)
            {
                const uint32_t offset = (uint32_t)__builtin_ctzll(scan_bitmap);
                scan_bitmap &= scan_bitmap - 1U;
                sample_page_desc_t *const page =
                    &g_sample_page_desc[range_start + offset];
                if ((page->state != SAMPLE_PAGE_READY)
                    || (page->static_resident != 0U)
                    || (sample_page_cache_key_slot(page->key)
                        >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
                    || (sample_page_cache_can_evict_for_request(
                            key, page->key) == 0U)) continue;
                if ((candidate_page == 0)
                    || (page->last_touch < candidate_page->last_touch))
                {
                    candidate_page = page;
                    candidate_offset = offset;
                }
            }
            if (candidate_page == 0) break;
            candidates &= ~(UINT64_C(1) << candidate_offset);
            ++candidates_tested;
            uint8_t revalidation_failed = 0U;
            if (sample_page_cache_claim_for_recycle(
                    candidate_page, &revalidation_failed) != 0U)
            {
                evict_page = candidate_page;
                break;
            }
            if (revalidation_failed != 0U)
                PERF_COUNT(PERF_N_RESERVE_REVALIDATION_FAIL);
        }
        PERF_ACCUM(PERF_N_RESERVE_CANDIDATES_TESTED, candidates_tested);
        PERF_MAX(PERF_N_RESERVE_CANDIDATES_TESTED_MAX, candidates_tested);
    }
    else
    {
        if (sample_page_lease_control_snapshot_active(&lease_snapshot) == 0U)
            return 0;
        for (uint32_t scan = 0U; scan < range_count; ++scan)
        {
            const uint32_t cursor_offset = sample_page_cache_range_cursor_offset(
                g_sample_page_evict_cursor, range_start, range_count);
            const uint32_t i = range_start + ((cursor_offset + scan) % range_count);
            sample_page_desc_t *const page = &g_sample_page_desc[i];
            if ((page->state != SAMPLE_PAGE_READY)
                || (sample_page_cache_page_is_contractual_snapshot(
                        page, &lease_snapshot) != 0U)
                || (sample_page_cache_key_slot(page->key)
                    >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
                || (sample_page_cache_can_evict_for_request(
                        key, page->key) == 0U)) continue;
            if ((evict_page == 0) || (page->last_touch < evict_page->last_touch))
                evict_page = page;
        }
        if ((evict_page != 0)
            && (sample_page_cache_claim_for_recycle(evict_page, 0) == 0U))
            evict_page = 0;
    }
    if (evict_page == 0)
    {
        if (runtime_range != 0U) PERF_COUNT(PERF_N_RESERVE_NO_CANDIDATE);
        return 0;
    }

    const uint32_t slot_index = (uint32_t)(evict_page - g_sample_page_desc);
    sample_page_cache_clear_desc(evict_page, slot_index);
    evict_page->key = key;
    evict_page->sample_id = sample_page_cache_key_slot(key);
    evict_page->page_index = page_index;
    sample_page_cache_set_page_geometry(evict_page, key);
    evict_page->start_frame = sample_audio_format_page_start_frame(evict_page->format, page_index);
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

static void sample_page_cache_reclaim_stream_pages_for_slot_allocation(uint32_t range_start,
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
        if (sample->valid != 0U)
        {
            if (sample_page_cache_claim_for_recycle(page, 0) != 0U)
            {
                sample_page_cache_clear_desc(page, i);
            }
        }
    }
}
