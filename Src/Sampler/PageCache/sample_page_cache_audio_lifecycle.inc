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
    if (page == 0) return 0U;
    const sample_page_state_t state = page->state;
    __DMB();
    if (state != SAMPLE_PAGE_READY) return 0U;
    const sample_page_desc_t snapshot = *page;
    float *const payload = sample_page_cache_data_resolve(&snapshot);
    if (payload == 0) return 0U;
    if ((snapshot.generation == 0U)
        || (snapshot.page_index != page_index)
        || (sample_audio_key_equal(&snapshot.key, &key) == 0U)) return 0U;
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
    const sample_page_state_t state = page->state;
    __DMB();
    const sample_page_desc_t snapshot = *page;
    if ((state != SAMPLE_PAGE_READY)
        || (sample_audio_key_equal(&snapshot.key, &key) == 0U)
        || (snapshot.page_index != ref->page_index)
        || (snapshot.generation != ref->page_generation)
        || (sample_audio_key_equal(&ref->key, &key) == 0U)
        || (sample_audio_format_is_valid(ref->format) == 0U)
        || (snapshot.format != ref->format)
        || ((ref->registration_epoch != 0U)
            && (snapshot.registration_epoch != ref->registration_epoch))
        || (ref->stride_floats != snapshot.stride_floats)
        || (ref->frames_per_page != snapshot.frames_per_page))
    {
        return 0U;
    }
    float *const payload = sample_page_cache_data_resolve(&snapshot);
    if (payload == 0) return 0U;
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
