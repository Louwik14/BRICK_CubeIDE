static uint8_t sample_page_cache_page_is_contractual(const sample_page_desc_t *page);
static uint8_t sample_page_cache_can_evict_for_request(sample_audio_key_t request_key,
                                                       sample_audio_key_t victim_key);
static void sample_page_cache_clear_desc(sample_page_desc_t *page,
                                         uint32_t slot_index);

static float *sample_page_cache_data_resolve(const sample_page_desc_t *page)
{
    if ((page == 0) || (page->data_offset >= sizeof(g_sample_page_data))
        || ((page->data_offset % SAMPLE_PAGE_BLOCK_BYTES) != 0U))
        return 0;
    return (float *)((uint8_t *)g_sample_page_data + page->data_offset);
}

static uint8_t sample_page_cache_page_is_leased(const sample_page_desc_t *page)
{
    return ((page != 0)
            && (sample_page_lease_control_protects(page->key,
                                                   page->registration_epoch,
                                                   page->page_index) != 0U)) ? 1U : 0U;
}

static sample_audio_format_t sample_page_cache_format_key(sample_audio_key_t key)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot < SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        const sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
        if (((sample->flags & SAMPLE_PAGE_SAMPLE_VALID) != 0U)
            && (sample_audio_format_is_valid(
                (sample_audio_format_t)sample->format) != 0U))
        {
            return (sample_audio_format_t)sample->format;
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

static uint8_t sample_page_cache_page_is_contractual(const sample_page_desc_t *page)
{
    if (page == 0)
    {
        return 0U;
    }

    return ((page->static_resident != 0U)
            || (sample_page_lease_control_protects(page->key,
                                                   page->registration_epoch,
                                                   page->page_index) != 0U)) ? 1U : 0U;
}

static uint8_t sample_page_cache_index_find_slot_key(sample_audio_key_t key,
                                                 uint32_t page_index,
                                                 uint16_t *out_slot,
                                                 uint32_t *out_insert_index)
{
    const uint32_t start = sample_page_cache_hash_key(key, page_index);
    uint32_t first_deleted = UINT32_MAX;

    for (uint32_t probe = 0U; probe < SAMPLE_PAGE_INDEX_SIZE; ++probe)
    {
        const uint32_t index = (start + probe) % SAMPLE_PAGE_INDEX_SIZE;
        const sample_page_index_entry_t *const entry = &g_sample_page_index[index];
        if (entry->used == 0U)
        {
            if (out_insert_index != 0)
                *out_insert_index = (first_deleted != UINT32_MAX)
                    ? first_deleted : index;
            return 0U;
        }

        if ((entry->used == 2U) && (first_deleted == UINT32_MAX))
            first_deleted = index;

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
    if (out_insert_index != 0) *out_insert_index = first_deleted;
    return 0U;
}

static void sample_page_cache_index_put_key(sample_audio_key_t key,
    uint32_t page_index, uint16_t slot_index);

static void sample_page_cache_index_put_new_key_at(
    sample_audio_key_t key, uint32_t page_index, uint16_t slot_index,
    uint32_t insert_index)
{
    if ((insert_index >= SAMPLE_PAGE_INDEX_SIZE)
        || (g_sample_page_index[insert_index].used == 1U))
    {
        sample_page_cache_index_put_key(key, page_index, slot_index);
        return;
    }
    sample_page_index_entry_t *const dst = &g_sample_page_index[insert_index];
    dst->key = key;
    dst->page_index = page_index;
    dst->slot_index = slot_index;
    __DMB();
    dst->used = 1U;
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
            __DMB();
            entry->slot_index = slot_index;
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
            dst->key = key;
            dst->page_index = page_index;
            dst->slot_index = slot_index;
            __DMB();
            dst->used = 1U;
            return;
        }
    }

    if (first_deleted != UINT32_MAX)
    {
        sample_page_index_entry_t *const dst = &g_sample_page_index[first_deleted];
        dst->key = key;
        dst->page_index = page_index;
        dst->slot_index = slot_index;
        __DMB();
        dst->used = 1U;
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
            uint32_t hole = index;
            entry->used = 2U;
            __DMB();
            for (uint32_t scan_probe = 1U;
                 scan_probe < SAMPLE_PAGE_INDEX_SIZE; ++scan_probe)
            {
                const uint32_t scan = (index + scan_probe)
                                      % SAMPLE_PAGE_INDEX_SIZE;
                sample_page_index_entry_t *const candidate =
                    &g_sample_page_index[scan];
                if (candidate->used == 0U)
                {
                    g_sample_page_index[hole].used = 0U;
                    __DMB();
                    return;
                }
                if (candidate->used != 1U) continue;
                const uint32_t home = sample_page_cache_hash_key(
                    candidate->key, candidate->page_index);
                const uint32_t candidate_distance =
                    (scan + SAMPLE_PAGE_INDEX_SIZE - home)
                    % SAMPLE_PAGE_INDEX_SIZE;
                const uint32_t hole_distance =
                    (hole + SAMPLE_PAGE_INDEX_SIZE - home)
                    % SAMPLE_PAGE_INDEX_SIZE;
                if (candidate_distance <= hole_distance) continue;

                sample_page_index_entry_t *const dst =
                    &g_sample_page_index[hole];
                dst->key = candidate->key;
                dst->page_index = candidate->page_index;
                dst->slot_index = candidate->slot_index;
                __DMB();
                dst->used = 1U;
                __DMB();
                candidate->used = 2U;
                __DMB();
                hole = scan;
            }
            g_sample_page_index[hole].used = 0U;
            __DMB();
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

    const uint32_t slot_index = (uint32_t)(page - g_sample_page_desc);
    if ((slot_index >= SAMPLE_PAGE_VOICE_WINDOW_POOL_START)
        && (slot_index < (SAMPLE_PAGE_VOICE_WINDOW_POOL_START
                          + SAMPLE_PAGE_VOICE_WINDOW_POOL_COUNT)))
    {
        const uint64_t bit = UINT64_C(1)
            << (slot_index - SAMPLE_PAGE_VOICE_WINDOW_POOL_START);
        g_sample_page_runtime_free_bitmap &= ~bit;
        g_sample_page_runtime_ready_bitmap &= ~bit;
        if (state == SAMPLE_PAGE_FREE)
            g_sample_page_runtime_free_bitmap |= bit;
        else if (state == SAMPLE_PAGE_READY)
            g_sample_page_runtime_ready_bitmap |= bit;
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

    if ((state == SAMPLE_PAGE_FAILED) && (page->state != SAMPLE_PAGE_FAILED))
        PERF_COUNT(PERF_N_PAGE_FAILED);
    /* Publish descriptor and CPU/DMA-visible payload before the state edge.
     * DMA cache maintenance remains owned by sd_block_device. */
    __DMB();
    page->state = state;
}

static uint8_t sample_page_cache_page_is_contractual_snapshot(
    const sample_page_desc_t *page,
    const sample_page_lease_active_snapshot_t *lease_snapshot)
{
    if (page == 0) return 0U;
    return ((page->static_resident != 0U)
            || (sample_page_lease_control_snapshot_protects(
                    lease_snapshot,
                    page->key,
                    page->registration_epoch,
                    page->page_index) != 0U)) ? 1U : 0U;
}

static uint8_t sample_page_cache_claim_for_recycle(sample_page_desc_t *page)
{
    PERF_START(recycle_start);
    if (page == 0) return 0U;
    uint32_t primask = sample_page_cache_lock();
    if ((page->state != SAMPLE_PAGE_READY)
        || (page->static_resident != 0U))
    {
        sample_page_cache_unlock(primask);
        return 0U;
    }
    const sample_audio_key_t key = page->key;
    const uint32_t page_index = page->page_index;
    const uint32_t epoch = page->registration_epoch;
    const uint32_t generation = page->generation;
    sample_page_cache_set_state(page, SAMPLE_PAGE_EVICTING);
    sample_page_cache_unlock(primask);

    const uint8_t leased = sample_page_lease_control_protects(
        key, epoch, page_index);
    primask = sample_page_cache_lock();
    if ((page->state != SAMPLE_PAGE_EVICTING)
        || (page->generation != generation)
        || (sample_audio_key_equal(&page->key, &key) == 0U)
        || (page->page_index != page_index))
    {
        sample_page_cache_unlock(primask);
        return 0U;
    }
    sample_page_cache_set_state(page, (leased != 0U) ? SAMPLE_PAGE_READY
                                                     : SAMPLE_PAGE_FREE);
    sample_page_cache_unlock(primask);
    PERF_END(PERF_CPU_CACHE_RECYCLE, recycle_start);
    if (leased != 0U) PERF_COUNT(PERF_N_CACHE_PROTECTED);
    else PERF_COUNT(PERF_N_CACHE_RECYCLE);
    return (leased == 0U) ? 1U : 0U;
}

static uint32_t sample_page_cache_payload_bytes(const sample_page_desc_t *page)
{
    if (page == 0) return 0U;
    if ((page->static_resident != 0U)
        && (sample_audio_format_is_valid(page->format) == 0U)) return SAMPLE_PAGE_BYTES;
    return sample_audio_format_page_bytes(page->format);
}

static uint8_t sample_page_cache_payload_block_used(uint32_t block,
                                                     const sample_page_desc_t *ignore)
{
    if (block >= SAMPLE_PAGE_BLOCK_COUNT) return 1U;
    const uint16_t owner = g_sample_page_payload_owner[block];
    if (owner >= SAMPLE_PAGE_MAX_COUNT) return 0U;
    return ((ignore != 0) && (&g_sample_page_desc[owner] == ignore)) ? 0U : 1U;
}

static sample_page_desc_t *sample_page_cache_payload_block_owner(uint32_t block)
{
    if (block >= SAMPLE_PAGE_BLOCK_COUNT) return 0;
    const uint16_t owner = g_sample_page_payload_owner[block];
    return (owner < SAMPLE_PAGE_MAX_COUNT) ? &g_sample_page_desc[owner] : 0;
}

static void sample_page_cache_claim_payload_blocks(sample_page_desc_t *page)
{
    const uint32_t bytes = sample_page_cache_payload_bytes(page);
    if ((page == 0) || (bytes == 0U)
        || (page->data_offset >= sizeof(g_sample_page_data))) return;
    const uint32_t first = page->data_offset / SAMPLE_PAGE_BLOCK_BYTES;
    const uint32_t count = bytes / SAMPLE_PAGE_BLOCK_BYTES;
    const uint16_t owner = (uint16_t)(page - g_sample_page_desc);
    for (uint32_t i = 0U; i < count; ++i)
        g_sample_page_payload_owner[first + i] = owner;
}

static void sample_page_cache_release_payload_blocks(sample_page_desc_t *page)
{
    const uint32_t bytes = sample_page_cache_payload_bytes(page);
    if ((page == 0) || (bytes == 0U)
        || (page->data_offset >= sizeof(g_sample_page_data))) return;
    const uint32_t first = page->data_offset / SAMPLE_PAGE_BLOCK_BYTES;
    const uint32_t count = bytes / SAMPLE_PAGE_BLOCK_BYTES;
    const uint16_t owner = (uint16_t)(page - g_sample_page_desc);
    for (uint32_t i = 0U; i < count; ++i)
        if (g_sample_page_payload_owner[first + i] == owner)
            g_sample_page_payload_owner[first + i] = UINT16_MAX;
}

static uint8_t sample_page_cache_assign_payload(sample_page_desc_t *page,
                                                 uint32_t range_start,
                                                 uint32_t range_count)
{
    if ((page == 0) || ((range_start + range_count) > SAMPLE_PAGE_MAX_COUNT)) return 0U;
    const uint8_t mono = (page->format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO) ? 1U : 0U;
    if (mono != 0U)
    {
        /* Pack a half-full carrier first; this leaves whole carriers available
         * to the stereo fast path. */
        for (uint32_t carrier = range_start; carrier < (range_start + range_count); ++carrier)
        {
            const uint32_t block = carrier * SAMPLE_PAGE_BLOCKS_PER_CARRIER;
            const uint8_t lo = sample_page_cache_payload_block_used(block, page);
            const uint8_t hi = sample_page_cache_payload_block_used(block + 1U, page);
            if ((lo ^ hi) != 0U)
            {
                page->data_offset = (block + ((lo != 0U) ? 1U : 0U))
                    * SAMPLE_PAGE_BLOCK_BYTES;
                sample_page_cache_claim_payload_blocks(page);
                return 1U;
            }
        }
    }
    for (uint32_t carrier = range_start; carrier < (range_start + range_count); ++carrier)
    {
        const uint32_t block = carrier * SAMPLE_PAGE_BLOCKS_PER_CARRIER;
        if ((sample_page_cache_payload_block_used(block, page) == 0U)
            && ((mono != 0U)
                || (sample_page_cache_payload_block_used(block + 1U, page) == 0U)))
        {
            page->data_offset = block * SAMPLE_PAGE_BLOCK_BYTES;
            sample_page_cache_claim_payload_blocks(page);
            return 1U;
        }
    }
    return 0U;
}

static uint8_t sample_page_cache_compact_mono_at_stop(uint32_t range_start,
                                                       uint32_t range_count)
{
    if ((seq_runtime_is_running() != 0U)
        || (sample_page_lease_control_all_released() == 0U)) return 0U;

    sample_page_desc_t *target_owner = 0;
    uint32_t target_free_block = UINT32_MAX;
    sample_page_desc_t *source_owner = 0;
    for (uint32_t carrier = range_start; carrier < (range_start + range_count); ++carrier)
    {
        const uint32_t block = carrier * SAMPLE_PAGE_BLOCKS_PER_CARRIER;
        sample_page_desc_t *const lo = sample_page_cache_payload_block_owner(block);
        sample_page_desc_t *const hi = sample_page_cache_payload_block_owner(block + 1U);
        if ((lo != 0) == (hi != 0)) continue;
        sample_page_desc_t *const owner = (lo != 0) ? lo : hi;
        if ((owner->format != SAMPLE_AUDIO_FORMAT_FLOAT32_MONO)
            || (owner->state != SAMPLE_PAGE_READY)
            || (owner->static_resident != 0U) || (owner->load_cancel_requested != 0U)) continue;
        if (target_owner == 0)
        {
            target_owner = owner;
            target_free_block = block + ((lo != 0) ? 1U : 0U);
        }
        else
        {
            source_owner = owner;
            break;
        }
    }
    if ((target_owner == 0) || (source_owner == 0)
        || (target_owner == source_owner)) return 0U;

    float *const src = sample_page_cache_data_resolve(source_owner);
    float *const dst = (float *)((uint8_t *)g_sample_page_data
                                 + target_free_block * SAMPLE_PAGE_BLOCK_BYTES);
    if ((src == 0) || (dst == 0)) return 0U;
    memmove(dst, src, SAMPLE_PAGE_BLOCK_BYTES);
    dcache_clean_by_addr_aligned(dst, SAMPLE_PAGE_BLOCK_BYTES);
    sample_page_cache_release_payload_blocks(source_owner);
    source_owner->data_offset = target_free_block * SAMPLE_PAGE_BLOCK_BYTES;
    sample_page_cache_claim_payload_blocks(source_owner);
    source_owner->generation = ++g_sample_page_cache_state.generation_counter;
    __DMB();
    return 1U;
}

static uint8_t sample_page_cache_recycle_carrier_for_stereo(
    sample_audio_key_t request_key, uint32_t range_start, uint32_t range_count)
{
    uint32_t best_carrier = UINT32_MAX;
    uint32_t best_touch = UINT32_MAX;
    for (uint32_t carrier = range_start; carrier < (range_start + range_count); ++carrier)
    {
        const uint32_t block = carrier * SAMPLE_PAGE_BLOCKS_PER_CARRIER;
        sample_page_desc_t *const lo = sample_page_cache_payload_block_owner(block);
        sample_page_desc_t *const hi = sample_page_cache_payload_block_owner(block + 1U);
        if ((lo == 0) && (hi == 0)) return 1U;
        if (((lo != 0) && ((lo->state != SAMPLE_PAGE_READY) || (lo->static_resident != 0U)
                           || (sample_page_cache_page_is_leased(lo) != 0U)
                           || (sample_page_cache_can_evict_for_request(request_key, lo->key) == 0U)))
            || ((hi != 0) && (hi != lo)
                && ((hi->state != SAMPLE_PAGE_READY) || (hi->static_resident != 0U)
                    || (sample_page_cache_page_is_leased(hi) != 0U)
                    || (sample_page_cache_can_evict_for_request(request_key, hi->key) == 0U))))
            continue;
        uint32_t touch = (lo != 0) ? lo->last_touch : 0U;
        if ((hi != 0) && (hi != lo) && (hi->last_touch > touch)) touch = hi->last_touch;
        if ((best_carrier == UINT32_MAX) || (touch < best_touch))
        {
            best_carrier = carrier;
            best_touch = touch;
        }
    }
    if (best_carrier == UINT32_MAX) return 0U;
    const uint32_t block = best_carrier * SAMPLE_PAGE_BLOCKS_PER_CARRIER;
    sample_page_desc_t *const lo = sample_page_cache_payload_block_owner(block);
    sample_page_desc_t *const hi = sample_page_cache_payload_block_owner(block + 1U);
    if ((lo != 0) && (sample_page_cache_claim_for_recycle(lo) != 0U))
        sample_page_cache_clear_desc(lo, (uint32_t)(lo - g_sample_page_desc));
    if ((hi != 0) && (hi != lo) && (sample_page_cache_claim_for_recycle(hi) != 0U))
        sample_page_cache_clear_desc(hi, (uint32_t)(hi - g_sample_page_desc));
    return ((sample_page_cache_payload_block_used(block, 0) == 0U)
            && (sample_page_cache_payload_block_used(block + 1U, 0) == 0U)) ? 1U : 0U;
}

static void sample_page_cache_clear_desc(sample_page_desc_t *page, uint32_t slot_index)
{
    if (page == 0)
    {
        return;
    }

    if ((g_sample_page_payload_clean_contract_active != 0U)
        && (page->payload_cpu_clean == 0U))
    {
        float *const payload = sample_page_cache_data_resolve(page);
        const uint32_t bytes = sample_page_cache_payload_bytes(page);
        if ((payload != 0) && (bytes != 0U))
            dcache_clean_by_addr_aligned(payload, bytes);
    }
    sample_page_cache_release_payload_blocks(page);
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
    page->data_offset = UINT32_MAX;
    page->payload_cpu_clean = 1U;
    page->state = SAMPLE_PAGE_FREE;
}

static sample_page_desc_t *sample_page_cache_find_page_mut_key_hint(
    sample_audio_key_t key, uint32_t page_index, uint32_t *out_insert_index)
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

        if (sample_page_cache_index_find_slot_key(
                key, page_index, &slot, out_insert_index) != 0U)
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

static sample_page_desc_t *sample_page_cache_find_page_mut_key(
    sample_audio_key_t key, uint32_t page_index)
{
    return sample_page_cache_find_page_mut_key_hint(key, page_index, 0);
}

static const sample_page_desc_t *sample_page_cache_find_page_key(sample_audio_key_t key, uint32_t page_index)
{
    return sample_page_cache_find_page_mut_key(key, page_index);
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
