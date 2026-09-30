/* AUDIO-side lookup of STORAGE-owned local metadata. AUDIO extends its lease
 * before entering this function. */
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
        const sample_page_state_t state = page->state;
        __DMB();
        if ((sample_audio_key_equal(&page->key, &key) != 0U)
            && (page->page_index == page_index)
            && (state != SAMPLE_PAGE_FREE))
            return page;
    }

    const uint32_t start = sample_page_cache_hash_key(key, page_index);
    for (uint32_t probe = 0U; probe < SAMPLE_PAGE_INDEX_SIZE; ++probe)
    {
        sample_page_index_entry_t *const entry =
            &g_sample_page_index[(start + probe) % SAMPLE_PAGE_INDEX_SIZE];
        const uint8_t used = entry->used;
        __DMB();
        if (used == 0U) return 0;
        if ((used == 1U)
            && (sample_audio_key_equal(&entry->key, &key) != 0U)
            && (entry->page_index == page_index)
            && (entry->slot_index < SAMPLE_PAGE_MAX_COUNT))
        {
            sample_page_desc_t *const page =
                &g_sample_page_desc[entry->slot_index];
            const sample_page_state_t state = page->state;
            __DMB();
            if ((sample_audio_key_equal(&page->key, &key) != 0U)
                && (page->page_index == page_index)
                && (state != SAMPLE_PAGE_FREE))
                return page;
            return 0;
        }
    }
    return 0;
}
