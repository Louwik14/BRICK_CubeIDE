static uint16_t sample_page_cache_key_slot(sample_audio_key_t key);
static uint8_t sample_page_cache_page_is_contractual(const sample_page_desc_t *page);
static uint8_t sample_page_cache_can_evict_for_request(sample_audio_key_t request_key,
                                                       sample_audio_key_t victim_key);

static float *sample_page_cache_data_resolve(const sample_page_desc_t *page)
{
    if ((page == 0) || (page->data_offset >= sizeof(g_sample_page_data))
        || ((page->data_offset % SAMPLE_PAGE_BYTES) != 0U))
        return 0;
    return (float *)((uint8_t *)g_sample_page_data + page->data_offset);
}

static uint32_t sample_page_cache_audio_use_count(const sample_page_desc_t *page)
{
    if (page == 0) return 0U;
    const uint32_t slot = (uint32_t)(page - g_sample_page_desc);
    return (slot < SAMPLE_PAGE_MAX_COUNT)
        ? __atomic_load_n(&g_sample_page_audio_use_count[slot], __ATOMIC_ACQUIRE)
        : 0U;
}

static sample_audio_format_t sample_page_cache_format_key(sample_audio_key_t key)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        const sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
        if ((sample->valid != 0U) && (sample_audio_format_is_valid(sample->format) != 0U))
        {
            return sample->format;
        }
    }
    return SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED;
}

static void sample_page_cache_set_page_geometry(sample_page_desc_t *page,
                                                sample_audio_key_t key)
{
    if (page == 0)
    {
        return;
    }

    page->format = sample_page_cache_format_key(key);
    page->stride_floats = (uint16_t)sample_audio_format_stride_floats(page->format);
    page->frames_per_page = sample_audio_format_frames_per_page(page->format);
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    page->registration_epoch = (key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
                                   ? g_sample_page_sample_desc[key_slot].registration_epoch
                                   : 0U;
}

static uint32_t sample_page_cache_lock(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void sample_page_cache_unlock(uint32_t primask)
{
    if (primask == 0U)
    {
        __enable_irq();
    }
}

sample_audio_key_t sample_audio_key_classic(uint16_t sample_id)
{
    sample_audio_key_t key;
    key.domain = SAMPLE_AUDIO_DOMAIN_CLASSIC;
    key.object_id = sample_id;
    return key;
}

sample_audio_key_t sample_audio_key_looper(uint16_t looper_id)
{
    sample_audio_key_t key;
    key.domain = SAMPLE_AUDIO_DOMAIN_LOOPER;
    key.object_id = looper_id;
    return key;
}

sample_audio_key_t sample_audio_key_multi(uint16_t multi_sample_id)
{
    sample_audio_key_t key;
    key.domain = SAMPLE_AUDIO_DOMAIN_MULTI;
    key.object_id = multi_sample_id;
    return key;
}

uint8_t sample_audio_key_equal(const sample_audio_key_t *a, const sample_audio_key_t *b)
{
    if ((a == 0) || (b == 0))
    {
        return 0U;
    }

    return ((a->domain == b->domain) && (a->object_id == b->object_id)) ? 1U : 0U;
}

static uint16_t sample_page_cache_key_slot(sample_audio_key_t key)
{
    switch (key.domain)
    {
        case SAMPLE_AUDIO_DOMAIN_CLASSIC:
            return (key.object_id < SAMPLE_CLASSIC_CAPACITY) ? key.object_id : UINT16_MAX;

        case SAMPLE_AUDIO_DOMAIN_LOOPER:
        {
            const uint32_t slot = (uint32_t)SAMPLE_PAGE_CACHE_LOOPER_ID_BASE
                                + (uint32_t)key.object_id;
            return (slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES) ? (uint16_t)slot : UINT16_MAX;
        }

        case SAMPLE_AUDIO_DOMAIN_MULTI:
        {
            const uint32_t slot = (uint32_t)SAMPLE_PAGE_CACHE_MULTI_ID_BASE
                                + (uint32_t)key.object_id;
            return (key.object_id < SAMPLE_PAGE_CACHE_MULTI_ID_CAPACITY)
                       ? (uint16_t)slot
                       : UINT16_MAX;
        }

        default:
            break;
    }

    return UINT16_MAX;
}

static uint8_t sample_page_cache_key_valid(sample_audio_key_t key)
{
    return (sample_page_cache_key_slot(key) < SAMPLE_PAGE_CACHE_MAX_SAMPLES) ? 1U : 0U;
}

static uint8_t sample_page_cache_can_evict_for_request(sample_audio_key_t request_key,
                                                       sample_audio_key_t victim_key)
{
    if ((request_key.domain == SAMPLE_AUDIO_DOMAIN_MULTI)
        && (victim_key.domain != SAMPLE_AUDIO_DOMAIN_MULTI))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t sample_page_cache_page_is_full_contract(const sample_page_desc_t *page)
{
    if (page == 0)
    {
        return 0U;
    }

    const uint16_t key_slot = sample_page_cache_key_slot(page->key);
    if (key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        return 0U;
    }

    const sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    return ((sample->valid != 0U) && (sample->fully_loaded != 0U)) ? 1U : 0U;
}

static uint8_t sample_page_cache_page_is_contractual(const sample_page_desc_t *page)
{
    if (page == 0)
    {
        return 0U;
    }

    return ((sample_page_cache_audio_use_count(page) != 0U)
            || (page->pin_count != 0U)
            || (sample_stream_needs_registry_contains_any(page->key,
                                                          page->page_index,
                                                          page->registration_epoch) != 0U)
            || (sample_page_cache_page_is_full_contract(page) != 0U)) ? 1U : 0U;
}

static uint32_t sample_page_cache_hash_key(sample_audio_key_t key, uint32_t page_index)
{
    return ((((uint32_t)key.object_id * 2654435761UL)
             ^ ((uint32_t)key.domain * 40503UL)
             ^ (page_index * 2246822519UL))
           % SAMPLE_PAGE_INDEX_SIZE);
}

static uint8_t sample_page_cache_index_find_slot_key(sample_audio_key_t key,
                                                 uint32_t page_index,
                                                 uint16_t *out_slot)
{
    const uint32_t start = sample_page_cache_hash_key(key, page_index);

    for (uint32_t probe = 0U; probe < SAMPLE_PAGE_INDEX_SIZE; ++probe)
    {
        const uint32_t index = (start + probe) % SAMPLE_PAGE_INDEX_SIZE;
        const sample_page_index_entry_t *const entry = &g_sample_page_index[index];
        if (entry->used == 0U)
        {
            return 0U;
        }

        if ((entry->used == 1U) && (sample_audio_key_equal(&entry->key, &key) != 0U)
            && (entry->page_index == page_index))
        {
            if (out_slot != 0)
            {
                *out_slot = entry->slot_index;
            }
            return 1U;
        }
    }
    return 0U;
}

static void sample_page_cache_index_put_key(sample_audio_key_t key, uint32_t page_index, uint16_t slot_index)
{
    const uint32_t start = sample_page_cache_hash_key(key, page_index);
    uint32_t first_deleted = UINT32_MAX;

    for (uint32_t probe = 0U; probe < SAMPLE_PAGE_INDEX_SIZE; ++probe)
    {
        const uint32_t index = (start + probe) % SAMPLE_PAGE_INDEX_SIZE;
        sample_page_index_entry_t *const entry = &g_sample_page_index[index];
        if ((entry->used == 1U) && (sample_audio_key_equal(&entry->key, &key) != 0U)
            && (entry->page_index == page_index))
        {
            entry->slot_index = slot_index;
            intercore_cache_publish(entry, sizeof(*entry));
            return;
        }
        if ((entry->used == 2U) && (first_deleted == UINT32_MAX))
        {
            first_deleted = index;
        }
        if (entry->used == 0U)
        {
            sample_page_index_entry_t *const dst =
                (first_deleted != UINT32_MAX) ? &g_sample_page_index[first_deleted] : entry;
            dst->used = 1U;
            dst->key.domain = key.domain;
            dst->key.object_id = key.object_id;
    dst->page_index = page_index;
    dst->slot_index = slot_index;
    intercore_cache_publish(dst, sizeof(*dst));
            return;
        }
    }

    if (first_deleted != UINT32_MAX)
    {
        sample_page_index_entry_t *const dst = &g_sample_page_index[first_deleted];
        dst->used = 1U;
        dst->key.domain = key.domain;
        dst->key.object_id = key.object_id;
        dst->page_index = page_index;
        dst->slot_index = slot_index;
        intercore_cache_publish(dst, sizeof(*dst));
    }
}

static void sample_page_cache_index_remove_key(sample_audio_key_t key, uint32_t page_index)
{
    const uint32_t start = sample_page_cache_hash_key(key, page_index);
    for (uint32_t probe = 0U; probe < SAMPLE_PAGE_INDEX_SIZE; ++probe)
    {
        const uint32_t index = (start + probe) % SAMPLE_PAGE_INDEX_SIZE;
        sample_page_index_entry_t *const entry = &g_sample_page_index[index];
        if (entry->used == 0U)
        {
            return;
        }
        if ((entry->used == 1U) && (sample_audio_key_equal(&entry->key, &key) != 0U)
            && (entry->page_index == page_index))
        {
            entry->used = 2U;
            intercore_cache_publish(entry, sizeof(*entry));
            return;
        }
    }
}

static void sample_page_cache_set_state(sample_page_desc_t *page, sample_page_state_t state)
{
    if (page == 0)
    {
        return;
    }

    const uint16_t key_slot = sample_page_cache_key_slot(page->key);
    if ((key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES) && (page->state != state))
    {
        if ((page->state == SAMPLE_PAGE_RESERVED) && (g_sample_page_reserved_count[key_slot] != 0U))
        {
            g_sample_page_reserved_count[key_slot]--;
        }
        if (state == SAMPLE_PAGE_RESERVED)
        {
            g_sample_page_reserved_count[key_slot]++;
        }
    }

    if (state == SAMPLE_PAGE_READY)
    {
        const float *const payload = sample_page_cache_data_resolve(page);
        const size_t payload_bytes = (size_t)page->frame_count
            * page->stride_floats * sizeof(float);
        intercore_cache_publish(payload, payload_bytes);
    }
    page->state = state;
    intercore_cache_publish(page, sizeof(*page));
}

static uint8_t sample_page_cache_claim_for_recycle(sample_page_desc_t *page)
{
    const uint32_t primask = sample_page_cache_lock();
    const uint8_t claimed = (uint8_t)((page != 0)
        && (page->state == SAMPLE_PAGE_READY)
        && (sample_page_cache_page_is_contractual(page) == 0U));
    if (claimed != 0U)
    {
        sample_page_cache_set_state(page, SAMPLE_PAGE_FREE);
    }
    sample_page_cache_unlock(primask);
    return claimed;
}

static void sample_page_cache_clear_desc(sample_page_desc_t *page, uint32_t slot_index)
{
    if (page == 0)
    {
        return;
    }

    const uint16_t key_slot = sample_page_cache_key_slot(page->key);
    if ((key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
        && (g_sample_page_last_slot[key_slot] == slot_index))
    {
        g_sample_page_last_slot[key_slot] = UINT16_MAX;
    }
    if ((key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES) && (page->page_index != UINT32_MAX))
    {
        sample_page_cache_index_remove_key(page->key, page->page_index);
    }
    sample_page_cache_set_state(page, SAMPLE_PAGE_FREE);

    memset(page, 0, sizeof(*page));
    page->sample_id = UINT16_MAX;
    page->key = sample_audio_key_classic(UINT16_MAX);
    page->page_index = UINT32_MAX;
    page->start_frame = UINT32_MAX;
    page->data_offset = slot_index * SAMPLE_PAGE_BYTES;
    page->state = SAMPLE_PAGE_FREE;
}

static sample_page_desc_t *sample_page_cache_find_page_mut_key(sample_audio_key_t key, uint32_t page_index)
{
    uint16_t slot = UINT16_MAX;
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        const uint16_t last_slot = g_sample_page_last_slot[key_slot];
        if (last_slot < SAMPLE_PAGE_MAX_COUNT)
        {
            sample_page_desc_t *const last_page = &g_sample_page_desc[last_slot];
            if ((sample_audio_key_equal(&last_page->key, &key) != 0U)
                && (last_page->page_index == page_index))
            {
                return last_page;
            }
        }

        if (sample_page_cache_index_find_slot_key(key, page_index, &slot) != 0U)
        {
            sample_page_desc_t *const page = &g_sample_page_desc[slot];
            if ((sample_audio_key_equal(&page->key, &key) != 0U)
                && (page->page_index == page_index)
                && (page->state != SAMPLE_PAGE_FREE))
            {
                g_sample_page_last_slot[key_slot] = slot;
                return page;
            }
        }
    }

    return 0;
}

static const sample_page_desc_t *sample_page_cache_find_page_key(sample_audio_key_t key, uint32_t page_index)
{
    return sample_page_cache_find_page_mut_key(key, page_index);
}

/* AUDIO-side lookup. CONTROL owns the cacheable index/descriptor bytes; AUDIO
 * only consumes published snapshots and owns the non-cacheable use credit. */
static sample_page_desc_t *sample_page_cache_audio_find_page_key(
    sample_audio_key_t key, uint32_t page_index)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES) return 0;
    const uint16_t last_slot = g_sample_page_last_slot[key_slot];
    __DMB();
    if (last_slot < SAMPLE_PAGE_MAX_COUNT)
    {
        sample_page_desc_t *const page = &g_sample_page_desc[last_slot];
        intercore_cache_consume(page, sizeof(*page));
        if ((sample_audio_key_equal(&page->key, &key) != 0U)
            && (page->page_index == page_index)
            && (page->state != SAMPLE_PAGE_FREE))
            return page;
    }

    const uint32_t start = sample_page_cache_hash_key(key, page_index);
    for (uint32_t probe = 0U; probe < SAMPLE_PAGE_INDEX_SIZE; ++probe)
    {
        sample_page_index_entry_t *const entry =
            &g_sample_page_index[(start + probe) % SAMPLE_PAGE_INDEX_SIZE];
        intercore_cache_consume(entry, sizeof(*entry));
        if (entry->used == 0U) return 0;
        if ((entry->used == 1U)
            && (sample_audio_key_equal(&entry->key, &key) != 0U)
            && (entry->page_index == page_index)
            && (entry->slot_index < SAMPLE_PAGE_MAX_COUNT))
        {
            sample_page_desc_t *const page =
                &g_sample_page_desc[entry->slot_index];
            intercore_cache_consume(page, sizeof(*page));
            if ((sample_audio_key_equal(&page->key, &key) != 0U)
                && (page->page_index == page_index)
                && (page->state != SAMPLE_PAGE_FREE))
                return page;
            return 0;
        }
    }
    return 0;
}

static void sample_page_cache_audio_release_slot(uint32_t slot)
{
    if (slot >= SAMPLE_PAGE_MAX_COUNT) return;
    uint32_t count = __atomic_load_n(&g_sample_page_audio_use_count[slot],
                                     __ATOMIC_ACQUIRE);
    while ((count != 0U)
        && (__atomic_compare_exchange_n(&g_sample_page_audio_use_count[slot],
                &count, count - 1U, 0, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE) == 0))
    {
    }
}

static uint8_t sample_page_cache_fill_ref(const sample_page_desc_t *page,
                                          sample_page_ref_t *out_ref)
{
    if ((page == 0) || (out_ref == 0))
    {
        return 0U;
    }

    out_ref->key = page->key;
    out_ref->page_index = page->page_index;
    out_ref->page_generation = page->generation;
    out_ref->format = page->format;
    out_ref->stride_floats = page->stride_floats;
    out_ref->frames_per_page = page->frames_per_page;
    out_ref->registration_epoch = page->registration_epoch;
    out_ref->slot_index = (uint32_t)(page - g_sample_page_desc);
    return 1U;
}
