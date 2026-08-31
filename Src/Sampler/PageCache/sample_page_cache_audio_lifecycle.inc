sample_page_state_t sample_page_cache_audio_get_page_state_key(
    sample_audio_key_t key, uint32_t page_index)
{
    const sample_page_desc_t *const page =
        sample_page_cache_audio_find_page_key(key, page_index);
    return (page != 0) ? page->state : SAMPLE_PAGE_FREE;
}

uint8_t sample_page_cache_audio_resolve_page(uint16_t sample_id,
                                           uint32_t page_index,
                                           sample_page_span_t *out_span)
{
    return sample_page_cache_audio_resolve_page_key(sample_audio_key_classic(sample_id),
                                                  page_index,
                                                  out_span);
}

uint8_t sample_page_cache_audio_resolve_page_key(sample_audio_key_t key,
                                               uint32_t page_index,
                                               sample_page_span_t *out_span)
{
    if (out_span == 0)
    {
        return 0U;
    }

    memset(out_span, 0, sizeof(*out_span));

    const sample_page_desc_t *const page =
        sample_page_cache_audio_find_page_key(key, page_index);
    if ((page == 0) || (page->state != SAMPLE_PAGE_READY)) return 0U;
    const sample_page_desc_t snapshot = *page;
    float *const payload = sample_page_cache_data_resolve(&snapshot);
    if (payload == 0) return 0U;
    intercore_cache_consume(page, sizeof(*page));
    if ((page->state != SAMPLE_PAGE_READY)
        || (page->generation != snapshot.generation)
        || (page->page_index != snapshot.page_index)
        || (page->registration_epoch != snapshot.registration_epoch)
        || (sample_audio_key_equal(&page->key, &snapshot.key) == 0U))
    {
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
    out_span->slot_index = (uint32_t)(page - g_sample_page_desc);
    return 1U;
}

uint8_t sample_page_cache_audio_resolve_page_ref(uint16_t sample_id,
                                               const sample_page_ref_t *ref,
                                               sample_page_span_t *out_span)
{
    return sample_page_cache_audio_resolve_page_ref_key(sample_audio_key_classic(sample_id),
                                                      ref,
                                                      out_span);
}

uint8_t sample_page_cache_audio_resolve_page_ref_key(sample_audio_key_t key,
                                                   const sample_page_ref_t *ref,
                                                   sample_page_span_t *out_span)
{
    if ((ref == 0) || (out_span == 0) || (ref->slot_index >= SAMPLE_PAGE_MAX_COUNT))
    {
        return 0U;
    }

    memset(out_span, 0, sizeof(*out_span));

    const sample_page_desc_t *const page = &g_sample_page_desc[ref->slot_index];
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
    intercore_cache_consume(page, sizeof(*page));
    if ((page->state != SAMPLE_PAGE_READY)
        || (page->generation != snapshot.generation)
        || (page->registration_epoch != snapshot.registration_epoch)
        || (sample_audio_key_equal(&page->key, &snapshot.key) == 0U))
    {
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
