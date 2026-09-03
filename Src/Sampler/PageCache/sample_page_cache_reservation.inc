const float *sample_page_cache_get_full_sample_base(uint16_t sample_id, uint32_t *out_frames)
{
    return sample_page_cache_get_full_sample_base_key(sample_audio_key_classic(sample_id), out_frames);
}

const float *sample_page_cache_get_full_sample_base_key(sample_audio_key_t key,
                                                        uint32_t *out_frames)
{
    if (out_frames != 0)
    {
        *out_frames = 0U;
    }

    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
    {
        return 0;
    }

    const sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    if ((sample->valid == 0U) || (sample->fully_loaded == 0U) || (sample->page_count == 0U))
    {
        return 0;
    }

    if (out_frames != 0)
    {
        *out_frames = sample->total_frames;
    }

    return sample_page_cache_data_resolve(
        &g_sample_page_desc[sample->first_slot]);
}

uint8_t sample_page_cache_has_reserved_range(uint16_t first_sample_id,
                                           uint16_t sample_count)
{
    return sample_page_cache_has_reserved_domain_range(SAMPLE_AUDIO_DOMAIN_CLASSIC,
                                                     first_sample_id,
                                                     sample_count);
}

uint8_t sample_page_cache_has_reserved_domain_range(sample_audio_domain_t domain,
                                                  uint16_t first_object_id,
                                                  uint16_t object_count)
{
    if (object_count == 0U)
    {
        return 0U;
    }

    for (uint16_t offset_id = 0U; offset_id < object_count; ++offset_id)
    {
        sample_audio_key_t key;
        key.domain = domain;
        key.object_id = (uint16_t)(first_object_id + offset_id);
        const uint16_t key_slot = sample_page_cache_key_slot(key);
        if (key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
        {
            continue;
        }

        if (g_sample_page_reserved_count[key_slot] != 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

uint8_t sample_page_cache_get_stream_info(uint16_t sample_id,
                                          sample_page_stream_info_t *out_info)
{
    return sample_page_cache_get_stream_info_key(sample_audio_key_classic(sample_id), out_info);
}

uint8_t sample_page_cache_get_stream_info_key(sample_audio_key_t key,
                                              sample_page_stream_info_t *out_info)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if ((key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES) || (out_info == 0))
    {
        return 0U;
    }

    sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    if (sample_page_cache_sample_is_stream_loadable_key(key) == 0U)
    {
        return 0U;
    }

    sample_stream_physical_map_t *const map = &sample->stream_safe.physical_map;
    if ((sample->physical_only != 0U)
        && (sample_stream_physical_map_is_current(map) == 0U))
        return 0U;

    memset(out_info, 0, sizeof(*out_info));
    out_info->key = key;
    memcpy(out_info->path, sample->path, sizeof(out_info->path));
    out_info->info = sample->info;
    out_info->total_frames = sample->total_frames;
    out_info->data_offset = sample->data_offset;
    out_info->format = sample->format;
    out_info->stride_floats = sample->stride_floats;
    out_info->frames_per_page = sample->frames_per_page;
    out_info->registration_epoch = sample->registration_epoch;
    out_info->stream_safe = sample->stream_safe;
    out_info->physical_only = sample->physical_only;
    return 1U;
}

uint8_t sample_page_cache_register_prepared_stream(
    const sample_page_stream_info_t *registration)
{
    if (registration == NULL) return 0U;
    const uint16_t key_slot = sample_page_cache_key_slot(registration->key);
    if ((key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES)
        || (registration->total_frames == 0U)
        || (registration->frames_per_page == 0U)
        || (sample_audio_format_is_valid(registration->format) == 0U)
        || ((registration->physical_only != 0U)
            && (sample_stream_physical_map_is_current(
                    &registration->stream_safe.physical_map) == 0U))) return 0U;
    sample_page_cache_clear_key(registration->key);
    sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    if (sample_page_cache_trim_path_copy(sample->path, sizeof(sample->path),
                                         registration->path) == 0U) return 0U;
    sample->key = registration->key;
    sample->info = registration->info;
    sample->total_frames = registration->total_frames;
    sample->readable_frames = registration->total_frames;
    sample->data_offset = registration->data_offset;
    sample->format = registration->format;
    sample->stride_floats = registration->stride_floats;
    sample->frames_per_page = registration->frames_per_page;
    sample->registration_epoch =
        ++g_sample_page_cache_state.registration_epoch_counter;
    sample->stream_safe = registration->stream_safe;
    sample->valid = 1U;
    sample->fully_loaded = 0U;
    sample->physical_only = registration->physical_only;
    sample->first_slot = UINT16_MAX;
    return 1U;
}

uint8_t sample_page_cache_begin_full_reservation(
    const sample_page_stream_info_t *registration,
    sample_page_alloc_type_t alloc_type,
    uint32_t *out_page_count)
{
    if (out_page_count != NULL) *out_page_count = 0U;
    if ((registration == NULL) || (out_page_count == NULL)
        || (sample_page_cache_register_prepared_stream(registration) == 0U))
        return 0U;
    const uint32_t page_count = sample_audio_format_required_page_count(
        registration->format, registration->total_frames);
    uint32_t range_start = 0U, range_count = 0U;
    sample_page_cache_alloc_range(alloc_type, &range_start, &range_count);
    int32_t first = sample_page_cache_find_contiguous_empty_run_in_range(
        page_count, range_start, range_count);
    if (first < 0)
    {
        sample_page_cache_reclaim_stream_pages_for_full_load_in_range(
            range_start, range_count);
        first = sample_page_cache_find_contiguous_empty_run_in_range(
            page_count, range_start, range_count);
    }
    if (first < 0) { sample_page_cache_clear_key(registration->key); return 0U; }
    sample_page_sample_desc_t *const sample =
        &g_sample_page_sample_desc[sample_page_cache_key_slot(registration->key)];
    for (uint32_t i = 0U; i < page_count; ++i)
    {
        const uint32_t start = sample_audio_format_page_start_frame(
            registration->format, i);
        uint32_t frames = registration->total_frames - start;
        if (frames > registration->frames_per_page)
            frames = registration->frames_per_page;
        if (sample_page_cache_assign_slot_key((uint32_t)first + i,
                                              registration->key, i,
                                              frames) == NULL)
        { sample_page_cache_clear_key(registration->key); return 0U; }
    }
    sample->first_slot = (uint16_t)first;
    sample->page_count = (uint16_t)page_count;
    *out_page_count = page_count;
    return 1U;
}

uint8_t sample_page_cache_finish_full_reservation(sample_audio_key_t key)
{
    const uint16_t key_slot = sample_page_cache_key_slot(key);
    if (key_slot >= SAMPLE_PAGE_CACHE_MAX_SAMPLES) return 0U;
    sample_page_sample_desc_t *const sample = &g_sample_page_sample_desc[key_slot];
    if ((sample->valid == 0U) || (sample->page_count == 0U)
        || (sample->first_slot == UINT16_MAX)) return 0U;
    for (uint32_t i = 0U; i < sample->page_count; ++i)
    {
        const sample_page_desc_t *const page =
            &g_sample_page_desc[(uint32_t)sample->first_slot + i];
        if ((page->state != SAMPLE_PAGE_READY)
            || (sample_audio_key_equal(&page->key, &key) == 0U)
            || (page->page_index != i)) return 0U;
    }
    sample_stream_transport_release_map(&sample->stream_safe.physical_map);
    memset(&sample->stream_safe.physical_map, 0,
           sizeof(sample->stream_safe.physical_map));
    sample->stream_safe.physical_map.first_pool_block =
        SAMPLE_STREAM_PHYSICAL_MAP_INVALID_BLOCK;
    sample->physical_only = 0U;
    sample->fully_loaded = 1U;
    return 1U;
}
uint8_t sample_page_cache_get_load_target(uint16_t sample_id,
                                          uint32_t page_index,
                                          sample_page_load_target_t *out_target)
{
    return sample_page_cache_get_load_target_key(sample_audio_key_classic(sample_id),
                                                 page_index,
                                                 out_target);
}

uint8_t sample_page_cache_get_load_target_key(sample_audio_key_t key,
                                              uint32_t page_index,
                                              sample_page_load_target_t *out_target)
{
    if ((sample_page_cache_key_valid(key) == 0U) || (out_target == 0))
    {
        return 0U;
    }

    sample_page_desc_t *const page = sample_page_cache_find_page_mut_key(key, page_index);
    float *const data = sample_page_cache_data_resolve(page);
    if ((page == 0) || (page->state != SAMPLE_PAGE_RESERVED) || (data == 0))
    {
        return 0U;
    }

    memset(out_target, 0, sizeof(*out_target));
    out_target->key = key;
    out_target->sample_id = sample_page_cache_key_slot(key);
    out_target->slot_index = (uint16_t)(page - g_sample_page_desc);
    out_target->page_index = page->page_index;
    out_target->start_frame = page->start_frame;
    out_target->frame_count = page->frame_count;
    out_target->format = (sample_audio_format_t)page->format;
    out_target->stride_floats = page->stride_floats;
    out_target->frames_per_page = page->frames_per_page;
    out_target->registration_epoch = page->registration_epoch;
    out_target->page_generation = page->generation;
    out_target->frames_interleaved = data;
    return 1U;
}

uint8_t sample_page_cache_begin_loading(const sample_page_load_target_t *target,
                                          sample_page_load_token_t *out_token)
{
    if ((target == 0) || (out_token == 0) || (target->slot_index >= SAMPLE_PAGE_MAX_COUNT))
    {
        return 0U;
    }

    const uint32_t primask = sample_page_cache_lock();
    sample_page_desc_t *const page = &g_sample_page_desc[target->slot_index];
    const uint8_t valid = (uint8_t)((page->state == SAMPLE_PAGE_RESERVED)
        && (sample_page_cache_data_resolve(page) == target->frames_interleaved)
        && (page->page_index == target->page_index)
        && (page->generation == target->page_generation)
        && (page->registration_epoch == target->registration_epoch)
        && (sample_audio_key_equal(&page->key, &target->key) != 0U));
    if (valid != 0U)
    {
        memset(out_token, 0, sizeof(*out_token));
        out_token->key = page->key;
        out_token->page_index = page->page_index;
        out_token->page_generation = page->generation;
        out_token->registration_epoch = page->registration_epoch;
        out_token->slot_index = target->slot_index;
        page->load_cancel_requested = 0U;
        sample_page_cache_set_state(page, SAMPLE_PAGE_LOADING);
    }
    sample_page_cache_unlock(primask);
    return valid;
}

uint8_t sample_page_cache_resolve_loading_target(const sample_page_load_token_t *token,
                                                 sample_page_load_target_t *out_target)
{
    if ((token == 0) || (out_target == 0) || (token->slot_index >= SAMPLE_PAGE_MAX_COUNT))
    {
        return 0U;
    }
    const uint32_t primask = sample_page_cache_lock();
    const sample_page_desc_t *const page = &g_sample_page_desc[token->slot_index];
    float *const data = sample_page_cache_data_resolve(page);
    const uint8_t valid = (uint8_t)((page->state == SAMPLE_PAGE_LOADING)
        && (page->load_cancel_requested == 0U)
        && (page->page_index == token->page_index)
        && (page->generation == token->page_generation)
        && (page->registration_epoch == token->registration_epoch)
        && (sample_audio_key_equal(&page->key, &token->key) != 0U)
        && (data != 0));
    if (valid != 0U)
    {
        memset(out_target, 0, sizeof(*out_target));
        out_target->key = page->key;
        out_target->sample_id = page->sample_id;
        out_target->slot_index = token->slot_index;
        out_target->page_index = page->page_index;
        out_target->start_frame = page->start_frame;
        out_target->frame_count = page->frame_count;
        out_target->format = (sample_audio_format_t)page->format;
        out_target->stride_floats = page->stride_floats;
        out_target->frames_per_page = page->frames_per_page;
        out_target->registration_epoch = page->registration_epoch;
        out_target->page_generation = page->generation;
        out_target->frames_interleaved = data;
    }
    sample_page_cache_unlock(primask);
    return valid;
}

uint8_t sample_page_cache_finish_loading(const sample_page_load_token_t *token,
                                           sample_page_finish_result_t result)
{
    if ((token == 0) || (token->slot_index >= SAMPLE_PAGE_MAX_COUNT)
        || ((result != SAMPLE_PAGE_FINISH_READY) && (result != SAMPLE_PAGE_FINISH_ERROR)))
    {
        return 0U;
    }

    const uint32_t primask = sample_page_cache_lock();
    sample_page_desc_t *const page = &g_sample_page_desc[token->slot_index];
    const uint8_t valid = (uint8_t)((page->state == SAMPLE_PAGE_LOADING)
        && (page->page_index == token->page_index)
        && (page->generation == token->page_generation)
        && (page->registration_epoch == token->registration_epoch)
        && (sample_audio_key_equal(&page->key, &token->key) != 0U));
    uint8_t completed = valid;
    if (valid != 0U)
    {
        const uint8_t cancelled = page->load_cancel_requested;
        const sample_page_state_t final_state =
            ((result == SAMPLE_PAGE_FINISH_READY) && (cancelled == 0U))
                ? SAMPLE_PAGE_READY
                : SAMPLE_PAGE_FAILED;
        page->load_cancel_requested = 0U;
        if ((cancelled != 0U) && (page->static_resident == 0U)
            && (sample_page_cache_page_is_leased(page) == 0U))
        {
            sample_page_cache_clear_desc(page, token->slot_index);
        }
        else
        {
            sample_page_cache_set_state(page, final_state);
        }
        if ((result == SAMPLE_PAGE_FINISH_READY) && (final_state != SAMPLE_PAGE_READY))
        {
            completed = 0U;
        }
    }
    sample_page_cache_unlock(primask);
    return completed;
}

uint8_t sample_page_cache_cancel_loading_key(sample_audio_key_t key,
                                               uint32_t page_index)
{
    const uint32_t primask = sample_page_cache_lock();
    sample_page_desc_t *const page = sample_page_cache_find_page_mut_key(key, page_index);
    const uint8_t found = (uint8_t)((page != 0) && (page->state == SAMPLE_PAGE_LOADING));
    if (found != 0U)
    {
        page->load_cancel_requested = 1U;
    }
    sample_page_cache_unlock(primask);
    return found;
}

uint8_t sample_page_cache_prepare_bulk_page_key_alloc(
    sample_audio_key_t key,
    uint32_t page_index,
    sample_page_alloc_type_t alloc_type)
{
    if (sample_page_cache_reserve_page_key_alloc(key, page_index, alloc_type) == 0U)
    {
        return 0U;
    }

    sample_page_desc_t *const page = sample_page_cache_find_page_mut_key(key, page_index);
    if ((page == 0) || (sample_page_cache_data_resolve(page) == 0)
        || (page->frame_count == 0U))
    {
        return 0U;
    }
    if (page->state != SAMPLE_PAGE_RESERVED)
    {
        return 0U;
    }
    return 1U;
}

uint8_t sample_page_cache_get_bulk_load_target_key(
    sample_audio_key_t key,
    uint32_t page_index,
    sample_page_load_target_t *out_target)
{
    if ((sample_page_cache_key_valid(key) == 0U) || (out_target == 0))
    {
        return 0U;
    }

    sample_page_desc_t *const page = sample_page_cache_find_page_mut_key(key, page_index);
    float *const data = sample_page_cache_data_resolve(page);
    if ((page == 0) || (page->state != SAMPLE_PAGE_RESERVED) || (data == 0))
    {
        return 0U;
    }

    memset(out_target, 0, sizeof(*out_target));
    out_target->key = key;
    out_target->sample_id = sample_page_cache_key_slot(key);
    out_target->slot_index = (uint16_t)(page - g_sample_page_desc);
    out_target->page_index = page->page_index;
    out_target->start_frame = page->start_frame;
    out_target->frame_count = page->frame_count;
        out_target->format = (sample_audio_format_t)page->format;
    out_target->stride_floats = page->stride_floats;
    out_target->frames_per_page = page->frames_per_page;
    out_target->registration_epoch = page->registration_epoch;
    out_target->page_generation = page->generation;
    out_target->frames_interleaved = data;
    return 1U;
}

uint8_t sample_page_cache_set_page_state(uint16_t sample_id,
                                         uint32_t page_index,
                                         sample_page_state_t state)
{
    return sample_page_cache_set_page_state_key(sample_audio_key_classic(sample_id),
                                                page_index,
                                                state);
}

uint8_t sample_page_cache_set_page_state_key(sample_audio_key_t key,
                                             uint32_t page_index,
                                             sample_page_state_t state)
{
    sample_page_desc_t *const page = sample_page_cache_find_page_mut_key(key, page_index);
    if (page == 0)
    {
        return 0U;
    }

    const sample_page_state_t current = (sample_page_state_t)page->state;
    const uint8_t transition_allowed =
        (uint8_t)(((current == SAMPLE_PAGE_RESERVED) && (state == SAMPLE_PAGE_FAILED))
                  || ((current == SAMPLE_PAGE_READY) && (state == SAMPLE_PAGE_READY))
                  || ((current == SAMPLE_PAGE_FAILED) && (state == SAMPLE_PAGE_FAILED)));
    if (transition_allowed == 0U)
    {
        return 0U;
    }
    sample_page_cache_set_state(page, state);
    return 1U;
}
uint8_t sample_page_cache_reserve_page(uint16_t sample_id, uint32_t page_index)
{
    return sample_page_cache_reserve_page_key(sample_audio_key_classic(sample_id), page_index);
}

uint8_t sample_page_cache_reserve_page_key(sample_audio_key_t key, uint32_t page_index)
{
    return sample_page_cache_reserve_page_key_alloc(key,
                                                    page_index,
                                                    SAMPLE_PAGE_ALLOC_GENERAL);
}

uint8_t sample_page_cache_reserve_page_key_alloc(sample_audio_key_t key,
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

    if ((page->state == SAMPLE_PAGE_FREE)
        || ((page->state == SAMPLE_PAGE_FAILED)
            && (page->static_resident == 0U)
            && (sample_page_cache_page_is_leased(page) == 0U)))
    {
        page->load_cancel_requested = 0U;
        sample_page_cache_set_state(page, SAMPLE_PAGE_RESERVED);
    }

    sample_page_cache_set_page_geometry(page, key);
    page->start_frame = sample_audio_format_page_start_frame(
        (sample_audio_format_t)page->format, page_index);
    page->frame_count = sample_page_cache_stream_page_frame_count_key(key, page_index);
    if (page->frame_count == 0U)
    {
        sample_page_cache_set_state(page, SAMPLE_PAGE_FAILED);
        return 0U;
    }

    page->last_touch = ++g_sample_page_cache_state.touch_counter;
    return 1U;
}

uint8_t sample_page_cache_reserve_page_ref(uint16_t sample_id,
                                           uint32_t page_index,
                                           sample_page_ref_t *out_ref)
{
    return sample_page_cache_reserve_page_ref_key(sample_audio_key_classic(sample_id),
                                                  page_index,
                                                  out_ref);
}

uint8_t sample_page_cache_reserve_page_ref_key(sample_audio_key_t key,
                                               uint32_t page_index,
                                               sample_page_ref_t *out_ref)
{
    sample_page_desc_t *page = sample_page_cache_find_page_mut_key(key, page_index);
    if (page == 0)
    {
        page = sample_page_cache_alloc_empty_slot_key(key,
                                                      page_index,
                                                      SAMPLE_PAGE_ALLOC_GENERAL);
        if (page == 0)
        {
            return 0U;
        }
    }

    if (page->state == SAMPLE_PAGE_FREE)
    {
        sample_page_cache_set_state(page, SAMPLE_PAGE_RESERVED);
    }

    sample_page_cache_set_page_geometry(page, key);
    page->start_frame = sample_audio_format_page_start_frame(
        (sample_audio_format_t)page->format, page_index);
    page->frame_count = sample_page_cache_stream_page_frame_count_key(key, page_index);
    if (page->frame_count == 0U)
    {
        sample_page_cache_set_state(page, SAMPLE_PAGE_FAILED);
        return 0U;
    }

    page->last_touch = ++g_sample_page_cache_state.touch_counter;
    if (out_ref != 0)
    {
        (void)sample_page_cache_fill_ref(page, out_ref);
    }
    return 1U;
}

uint8_t sample_page_cache_reserve_start_pages(uint16_t sample_id,
                                              uint32_t start_frame,
                                              uint32_t page_count)
{
    return sample_page_cache_reserve_start_pages_key(sample_audio_key_classic(sample_id),
                                                     start_frame,
                                                     page_count);
}

uint8_t sample_page_cache_reserve_start_pages_key(sample_audio_key_t key,
                                                  uint32_t start_frame,
                                                  uint32_t page_count)
{
    return sample_page_cache_reserve_start_pages_key_alloc(
        key,
        start_frame,
        page_count,
        SAMPLE_PAGE_ALLOC_GENERAL);
}

uint8_t sample_page_cache_reserve_start_pages_key_alloc(sample_audio_key_t key,
                                                        uint32_t start_frame,
                                                        uint32_t page_count,
                                                        sample_page_alloc_type_t alloc_type)
{
    const uint32_t first_page = sample_audio_format_page_index_from_frame(
        sample_page_cache_format_key(key), start_frame);
    uint8_t ok = 1U;

    for (uint32_t i = 0U; i < page_count; ++i)
    {
        if (sample_page_cache_reserve_page_key_alloc(key, first_page + i, alloc_type) == 0U)
        {
            ok = 0U;
            break;
        }
    }

    return ok;
}
