void sample_page_cache_init(void)
{
    sample_page_cache_reset();
    g_sample_page_cache_state.initialized = 1U;
}

void sample_page_cache_reset(void)
{
    sample_stream_transport_reset_storage_maps();
    memset(&g_sample_page_cache_state, 0, sizeof(g_sample_page_cache_state));
    memset(g_sample_page_sample_desc, 0, sizeof(g_sample_page_sample_desc));
    memset(g_sample_page_index, 0, sizeof(g_sample_page_index));
    memset((void *)g_sample_page_audio_use_count, 0,
           sizeof(g_sample_page_audio_use_count));
    memset(g_sample_page_reserved_count, 0, sizeof(g_sample_page_reserved_count));
    g_sample_page_free_cursor = 0U;
    g_sample_page_evict_cursor = 0U;
    for (uint32_t i = 0U; i < SAMPLE_PAGE_CACHE_MAX_SAMPLES; ++i)
    {
        g_sample_page_last_slot[i] = UINT16_MAX;
    }
    for (uint32_t i = 0U; i < SAMPLE_PAGE_MAX_COUNT; ++i)
    {
        sample_page_cache_clear_desc(&g_sample_page_desc[i], i);
    }
}

void sample_page_cache_clear_sample(uint16_t sample_id)
{
    sample_page_cache_clear_key(sample_audio_key_classic(sample_id));
}

void sample_page_cache_clear_key(sample_audio_key_t key)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        return;
    }

    for (uint32_t i = 0U; i < SAMPLE_PAGE_MAX_COUNT; ++i)
    {
        if (sample_audio_key_equal(&g_sample_page_desc[i].key, &key) != 0U)
        {
            if (g_sample_page_desc[i].state == SAMPLE_PAGE_LOADING)
            {
                g_sample_page_desc[i].load_cancel_requested = 1U;
                continue;
            }
            if (sample_page_cache_page_is_contractual(&g_sample_page_desc[i]) != 0U)
            {
                /* Withdraw immediately from new admissions, but preserve the
                 * storage until all AUDIO refs/pins/contracts are gone. */
                g_sample_page_desc[i].lifecycle_reserved[0] = 1U;
                sample_page_cache_set_state(&g_sample_page_desc[i], SAMPLE_PAGE_FAILED);
                continue;
            }
            sample_page_cache_clear_desc(&g_sample_page_desc[i], i);
        }
    }

    sample_stream_transport_release_map(
        &g_sample_page_sample_desc[key_slot].stream_safe.physical_map);
    memset(&g_sample_page_sample_desc[key_slot], 0, sizeof(g_sample_page_sample_desc[key_slot]));
    g_sample_page_sample_desc[key_slot].key = key;
    g_sample_page_sample_desc[key_slot].first_slot = UINT16_MAX;
    g_sample_page_last_slot[key_slot] = UINT16_MAX;
}

uint8_t sample_page_cache_cancel_reserved_page_key(sample_audio_key_t key,
                                                 uint32_t page_index,
                                                 uint8_t reason)
{
    (void)reason;
    sample_page_desc_t *const page = sample_page_cache_find_page_mut_key(key, page_index);
    if ((page == 0) || (page->state != SAMPLE_PAGE_RESERVED))
    {
        return 0U;
    }
    if ((page->pin_count != 0U)
        || (sample_page_cache_audio_use_count(page) != 0U))
    {
        return 0U;
    }

    sample_page_cache_clear_desc(page, (uint32_t)(page - g_sample_page_desc));
    return 1U;
}

uint32_t sample_page_cache_cancel_reserved_key(sample_audio_key_t key, uint8_t reason)
{
    (void)reason;
    uint32_t count = 0U;
    if (sample_page_cache_key_valid(key) == 0U)
    {
        return 0U;
    }

    for (uint32_t i = 0U; i < SAMPLE_PAGE_MAX_COUNT; ++i)
    {
        sample_page_desc_t *const page = &g_sample_page_desc[i];
        if ((page->state == SAMPLE_PAGE_RESERVED)
            && (sample_audio_key_equal(&page->key, &key) != 0U)
            && (page->pin_count == 0U)
            && (sample_page_cache_audio_use_count(page) == 0U))
        {
            sample_page_cache_clear_desc(page, i);
            count++;
        }
    }
    return count;
}

uint32_t sample_page_cache_cancel_reserved_domain(sample_audio_domain_t domain, uint8_t reason)
{
    (void)reason;
    uint32_t count = 0U;
    for (uint32_t i = 0U; i < SAMPLE_PAGE_MAX_COUNT; ++i)
    {
        sample_page_desc_t *const page = &g_sample_page_desc[i];
        if ((page->state == SAMPLE_PAGE_RESERVED)
            && (page->key.domain == domain)
            && (page->pin_count == 0U)
            && (sample_page_cache_audio_use_count(page) == 0U))
        {
            sample_page_cache_clear_desc(page, i);
            count++;
        }
    }
    return count;
}

sample_page_state_t sample_page_cache_get_page_state(uint16_t sample_id, uint32_t page_index)
{
    return sample_page_cache_get_page_state_key(sample_audio_key_classic(sample_id), page_index);
}

sample_page_state_t sample_page_cache_get_page_state_key(sample_audio_key_t key,
                                                         uint32_t page_index)
{
    const sample_page_desc_t *const page = sample_page_cache_find_page_key(key, page_index);
    const sample_page_state_t state = (page != 0) ? page->state : SAMPLE_PAGE_FREE;
    if (state == SAMPLE_PAGE_READY)
    {
        __DMB();
    }
    return state;
}

sample_page_state_t sample_page_cache_audio_get_page_state_key(
    sample_audio_key_t key, uint32_t page_index)
{
    const sample_page_desc_t *const page =
        sample_page_cache_audio_find_page_key(key, page_index);
    return (page != 0) ? page->state : SAMPLE_PAGE_FREE;
}

uint8_t sample_page_cache_page_exists_key(sample_audio_key_t key,
                                          uint32_t page_index)
{
    return (sample_page_cache_find_page_key(key, page_index) != 0) ? 1U : 0U;
}

uint8_t sample_page_cache_try_acquire_page(uint16_t sample_id,
                                           uint32_t page_index,
                                           sample_page_span_t *out_span)
{
    return sample_page_cache_try_acquire_page_key(sample_audio_key_classic(sample_id),
                                                  page_index,
                                                  out_span);
}

uint8_t sample_page_cache_try_acquire_page_key(sample_audio_key_t key,
                                               uint32_t page_index,
                                               sample_page_span_t *out_span)
{
    if (out_span == 0)
    {
        return 0U;
    }

    memset(out_span, 0, sizeof(*out_span));

    sample_page_desc_t *const page =
        sample_page_cache_audio_find_page_key(key, page_index);
    if ((page == 0) || (page->state != SAMPLE_PAGE_READY)) return 0U;
    const uint32_t slot = (uint32_t)(page - g_sample_page_desc);
    const sample_page_desc_t snapshot = *page;
    float *const payload = sample_page_cache_data_resolve(&snapshot);
    if (payload == 0) return 0U;
    (void)__atomic_fetch_add(&g_sample_page_audio_use_count[slot], 1U,
                             __ATOMIC_ACQ_REL);
    intercore_cache_consume(page, sizeof(*page));
    if ((page->state != SAMPLE_PAGE_READY)
        || (page->generation != snapshot.generation)
        || (page->page_index != snapshot.page_index))
    {
        sample_page_cache_audio_release_slot(slot);
        return 0U;
    }
    intercore_cache_consume(payload, (size_t)snapshot.frame_count
        * snapshot.stride_floats * sizeof(float));
    out_span->frames_interleaved = payload;
    out_span->frame_count = snapshot.frame_count;
    out_span->start_frame = snapshot.start_frame;
    out_span->page_index = snapshot.page_index;
    out_span->page_generation = snapshot.generation;
    out_span->key = snapshot.key;
    out_span->format = snapshot.format;
    out_span->stride_floats = snapshot.stride_floats;
    out_span->frames_per_page = snapshot.frames_per_page;
    out_span->registration_epoch = snapshot.registration_epoch;
    out_span->slot_index = slot;
    return 1U;
}

uint8_t sample_page_cache_try_acquire_page_ref(uint16_t sample_id,
                                               const sample_page_ref_t *ref,
                                               sample_page_span_t *out_span)
{
    return sample_page_cache_try_acquire_page_ref_key(sample_audio_key_classic(sample_id),
                                                      ref,
                                                      out_span);
}

uint8_t sample_page_cache_try_acquire_page_ref_key(sample_audio_key_t key,
                                                   const sample_page_ref_t *ref,
                                                   sample_page_span_t *out_span)
{
    if ((ref == 0) || (out_span == 0) || (ref->slot_index >= SAMPLE_PAGE_MAX_COUNT))
    {
        return 0U;
    }

    memset(out_span, 0, sizeof(*out_span));

    sample_page_desc_t *const page = &g_sample_page_desc[ref->slot_index];
    intercore_cache_consume(page, sizeof(*page));
    if ((sample_audio_key_equal(&page->key, &key) == 0U)
        || (page->page_index != ref->page_index)
        || (page->generation != ref->page_generation) || (page->state != SAMPLE_PAGE_READY)
        || (sample_audio_key_equal(&ref->key, &key) == 0U)
        || (sample_audio_format_is_valid(ref->format) == 0U)
        || (page->format != ref->format)
        || ((ref->registration_epoch != 0U)
            && (page->registration_epoch != ref->registration_epoch))
        || (ref->stride_floats != page->stride_floats)
        || (ref->frames_per_page != page->frames_per_page))
    {
        return 0U;
    }
    const sample_page_desc_t snapshot = *page;
    float *const payload = sample_page_cache_data_resolve(&snapshot);
    if (payload == 0) return 0U;
    (void)__atomic_fetch_add(&g_sample_page_audio_use_count[ref->slot_index],
                             1U, __ATOMIC_ACQ_REL);
    intercore_cache_consume(page, sizeof(*page));
    if ((page->state != SAMPLE_PAGE_READY)
        || (page->generation != snapshot.generation))
    {
        sample_page_cache_audio_release_slot(ref->slot_index);
        return 0U;
    }
    intercore_cache_consume(payload, (size_t)snapshot.frame_count
        * snapshot.stride_floats * sizeof(float));
    out_span->frames_interleaved = payload;
    out_span->frame_count = snapshot.frame_count;
    out_span->start_frame = snapshot.start_frame;
    out_span->page_index = snapshot.page_index;
    out_span->page_generation = snapshot.generation;
    out_span->key = snapshot.key;
    out_span->format = snapshot.format;
    out_span->stride_floats = snapshot.stride_floats;
    out_span->frames_per_page = snapshot.frames_per_page;
    out_span->registration_epoch = snapshot.registration_epoch;
    out_span->slot_index = ref->slot_index;
    return 1U;
}

void sample_page_cache_release_page(uint16_t sample_id, uint32_t page_index)
{
    sample_page_cache_release_page_key(sample_audio_key_classic(sample_id), page_index);
}

void sample_page_cache_release_page_key(sample_audio_key_t key, uint32_t page_index)
{
    sample_page_desc_t *const page =
        sample_page_cache_audio_find_page_key(key, page_index);
    if (page != 0)
        sample_page_cache_audio_release_slot(
            (uint32_t)(page - g_sample_page_desc));
}

void sample_page_cache_release_page_ref(uint16_t sample_id, const sample_page_ref_t *ref)
{
    sample_page_cache_release_page_ref_key(sample_audio_key_classic(sample_id), ref);
}

void sample_page_cache_release_page_ref_key(sample_audio_key_t key, const sample_page_ref_t *ref)
{
    if ((ref == 0) || (ref->slot_index >= SAMPLE_PAGE_MAX_COUNT))
    {
        return;
    }

    sample_page_desc_t *const page = &g_sample_page_desc[ref->slot_index];
    intercore_cache_consume(page, sizeof(*page));
    if ((sample_audio_key_equal(&page->key, &key) == 0U)
        || (page->page_index != ref->page_index)
        || (page->generation != ref->page_generation)
        || (sample_audio_key_equal(&ref->key, &key) == 0U)
        || (sample_audio_format_is_valid(ref->format) == 0U)
        || (page->format != ref->format)
        || ((ref->registration_epoch != 0U)
            && (page->registration_epoch != ref->registration_epoch))
        || (ref->stride_floats != page->stride_floats)
        || (ref->frames_per_page != page->frames_per_page))
    {
        return;
    }
    sample_page_cache_audio_release_slot(ref->slot_index);
}

uint8_t sample_page_cache_alloc_slot_pool_bytes(uint32_t bytes,
                                                sample_page_raw_allocation_t *out_allocation)
{
    if (out_allocation != 0)
    {
        memset(out_allocation, 0, sizeof(*out_allocation));
    }
    if ((bytes == 0U) || (out_allocation == 0))
    {
        return 0U;
    }

    const uint32_t page_count = (bytes + SAMPLE_PAGE_BYTES - 1U) / SAMPLE_PAGE_BYTES;
    if ((page_count == 0U) || (page_count > SAMPLE_PAGE_SLOT_POOL_COUNT)
        || (page_count > UINT16_MAX))
    {
        return 0U;
    }

    int32_t start_slot =
        sample_page_cache_find_contiguous_empty_run_in_range(page_count,
                                                             SAMPLE_PAGE_SLOT_POOL_START,
                                                             SAMPLE_PAGE_SLOT_POOL_COUNT);
    if (start_slot < 0)
    {
        sample_page_cache_reclaim_stream_pages_for_full_load_in_range(SAMPLE_PAGE_SLOT_POOL_START,
                                                                      SAMPLE_PAGE_SLOT_POOL_COUNT);
        start_slot = sample_page_cache_find_contiguous_empty_run_in_range(page_count,
                                                                         SAMPLE_PAGE_SLOT_POOL_START,
                                                                         SAMPLE_PAGE_SLOT_POOL_COUNT);
    }
    if (start_slot < 0)
    {
        return 0U;
    }

    for (uint32_t i = 0U; i < page_count; ++i)
    {
        const uint32_t slot_index = (uint32_t)start_slot + i;
        sample_page_desc_t *const page = &g_sample_page_desc[slot_index];
        sample_page_cache_clear_desc(page, slot_index);
        page->key = sample_audio_key_classic(UINT16_MAX);
        page->sample_id = UINT16_MAX;
        page->page_index = i;
        page->start_frame = 0U;
        page->frame_count = 0U;
        page->generation = ++g_sample_page_cache_state.generation_counter;
        page->last_touch = ++g_sample_page_cache_state.touch_counter;
        page->pin_count = 1U;
        sample_page_cache_set_state(page, SAMPLE_PAGE_READY);
    }

    out_allocation->first_slot = (uint16_t)start_slot;
    out_allocation->page_count = (uint16_t)page_count;
    out_allocation->capacity_bytes = page_count * SAMPLE_PAGE_BYTES;
    out_allocation->data = (void *)&g_sample_page_data[start_slot][0];
    return 1U;
}

void sample_page_cache_release_slot_pool_allocation(uint16_t first_slot,
                                                    uint16_t page_count)
{
    if ((page_count == 0U)
        || (first_slot < SAMPLE_PAGE_SLOT_POOL_START)
        || ((uint32_t)first_slot >= (SAMPLE_PAGE_SLOT_POOL_START + SAMPLE_PAGE_SLOT_POOL_COUNT))
        || (((uint32_t)first_slot + page_count)
            > (SAMPLE_PAGE_SLOT_POOL_START + SAMPLE_PAGE_SLOT_POOL_COUNT)))
    {
        return;
    }

    for (uint32_t i = 0U; i < page_count; ++i)
    {
        const uint32_t slot_index = (uint32_t)first_slot + i;
        sample_page_desc_t *const page = &g_sample_page_desc[slot_index];
        /* pin_count==1 is the allocation owner's lifetime pin. Releasing the
         * allocation removes that contract; foreign users still defer reuse. */
        if (page->pin_count != 0U) page->pin_count--;
        if ((sample_page_cache_audio_use_count(page) == 0U)
            && (page->pin_count == 0U))
            sample_page_cache_clear_desc(page, slot_index);
        else
        {
            page->lifecycle_reserved[0] = 1U;
            sample_page_cache_set_state(page, SAMPLE_PAGE_FAILED);
        }
    }
}

uint32_t sample_page_cache_slot_pool_total_bytes(void)
{
    return SAMPLE_PAGE_SLOT_POOL_COUNT * SAMPLE_PAGE_BYTES;
}

uint32_t sample_page_cache_slot_pool_free_bytes(void)
{
    uint32_t free_pages = 0U;
    for (uint32_t i = SAMPLE_PAGE_SLOT_POOL_START;
         i < (SAMPLE_PAGE_SLOT_POOL_START + SAMPLE_PAGE_SLOT_POOL_COUNT);
         ++i)
    {
        if (g_sample_page_desc[i].state == SAMPLE_PAGE_FREE)
        {
            free_pages++;
        }
    }
    return free_pages * SAMPLE_PAGE_BYTES;
}

uint8_t sample_page_cache_slot_pool_offset(uint16_t first_slot,
                                           uint32_t byte_offset,
                                           uint32_t length,
                                           uint32_t *out_offset)
{
    if (out_offset != NULL) *out_offset = 0U;
    if ((out_offset == NULL) || (first_slot >= SAMPLE_PAGE_MAX_COUNT)) return 0U;
    const uint64_t offset = ((uint64_t)first_slot * SAMPLE_PAGE_BYTES) + byte_offset;
    const uint64_t end = offset + length;
    if ((end < offset) || (end > sizeof(g_sample_page_data))) return 0U;
    *out_offset = (uint32_t)offset;
    return 1U;
}

const void *sample_page_cache_slot_pool_resolve(uint32_t offset,
                                                uint32_t length)
{
    if ((offset > sizeof(g_sample_page_data))
        || (length > (sizeof(g_sample_page_data) - offset))) return NULL;
    return &((const uint8_t *)g_sample_page_data)[offset];
}
