static void wavetable_pool_finalize_clear(uint16_t wavetable_slot)
{
    if (wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS)
    {
        return;
    }

    wavetable_slot_t *const slot = &g_wavetable_pool.slots[wavetable_slot];
    const wavetable_slot_t old = *slot;
    const uint32_t generation = wavetable_pool_next_generation();
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    slot->state = WAVETABLE_SLOT_EMPTY;
    slot->generation = generation;
    __DMB();
    if (primask == 0U)
    {
        __enable_irq();
    }
    if (old.page_count != 0U)
    {
        sample_page_cache_port_release_shared(old.first_page_slot,
                                                       old.page_count);
    }
    if (old.mipmap.page_count != 0U)
    {
        sample_page_cache_port_release_shared(old.mipmap.first_page_slot,
                                                       old.mipmap.page_count);
    }
    sample_global_pool_clear_backend(SAMPLE_GLOBAL_KIND_WAVETABLE, wavetable_slot);
    memset(&g_wavetable_pool.slots[wavetable_slot], 0, sizeof(g_wavetable_pool.slots[wavetable_slot]));
    g_wavetable_pool.slots[wavetable_slot].state = WAVETABLE_SLOT_EMPTY;
    g_wavetable_pool.slots[wavetable_slot].global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    g_wavetable_pool.slots[wavetable_slot].first_page_slot = UINT16_MAX;
    g_wavetable_pool.slots[wavetable_slot].mipmap.first_page_slot = UINT16_MAX;
    g_wavetable_pool.slots[wavetable_slot].generation = generation;
    wavetable_pool_preview_clear(&g_wavetable_pool.slots[wavetable_slot].preview);
}

void wavetable_pool_clear(uint16_t wavetable_slot)
{
    if (wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS) return;
    wavetable_slot_t *const slot = &g_wavetable_pool.slots[wavetable_slot];
    if (slot->state == WAVETABLE_SLOT_RETIRING) return;
    if (slot->state != WAVETABLE_SLOT_READY)
    {
        wavetable_pool_finalize_clear(wavetable_slot);
        return;
    }
    uint64_t due_sample = 0U;
    if (!live_clock_read_audio_sample(&due_sample)) return;
    const uint32_t generation = slot->generation;
    if (control_audio_publish_param_fenced((uint8_t)wavetable_slot, 0xFFF7U,
                                           generation, 0U, due_sample,
                                           &g_wavetable_retire_fence[wavetable_slot]) == 0U)
        return;
    audio_wave_table_projection_withdraw_slot(wavetable_slot, generation);
    slot->state = WAVETABLE_SLOT_RETIRING;
    __DMB();
}

void wavetable_pool_service_retire(void)
{
    for (uint16_t i = 0U; i < WAVETABLE_POOL_MAX_SLOTS; ++i)
    {
        wavetable_slot_t *const slot = &g_wavetable_pool.slots[i];
        if ((slot->state == WAVETABLE_SLOT_RETIRING)
            && (control_audio_consumer_fence_consumed(
                    g_wavetable_retire_fence[i]) != 0U))
        {
            g_wavetable_retire_fence[i] = 0U;
            wavetable_pool_finalize_clear(i);
        }
    }
}

const wavetable_slot_t *wavetable_pool_get_slot(uint16_t wavetable_slot)
{
    if (wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS)
    {
        return 0;
    }
    return &g_wavetable_pool.slots[wavetable_slot];
}

wavetable_slot_state_t wavetable_pool_get_state(uint16_t wavetable_slot)
{
    const wavetable_slot_t *const slot = wavetable_pool_get_slot(wavetable_slot);
    return (slot != 0) ? slot->state : WAVETABLE_SLOT_ERROR;
}

const float *wavetable_pool_get_data(uint16_t wavetable_slot)
{
    const wavetable_slot_t *const slot = wavetable_pool_get_slot(wavetable_slot);
    return ((slot != 0) && (slot->state == WAVETABLE_SLOT_READY)) ? slot->data : 0;
}

const wavetable_mipmap_view_t *wavetable_pool_get_mipmap_view(uint16_t wavetable_slot)
{
    const wavetable_slot_t *const slot = wavetable_pool_get_slot(wavetable_slot);
    if ((slot == 0) || (slot->state != WAVETABLE_SLOT_READY)
        || (slot->mipmap.band_count == 0U) || (slot->mipmap.data == 0))
    {
        return 0;
    }
    return &slot->mipmap;
}

const wavetable_preview_t *wavetable_pool_get_preview(uint16_t wavetable_slot)
{
    const wavetable_slot_t *const slot = wavetable_pool_get_slot(wavetable_slot);
    if ((slot == 0)
            || (slot->state != WAVETABLE_SLOT_READY)
            || (slot->preview.state != WAVETABLE_PREVIEW_READY)
            || (slot->preview.generation != slot->generation))
    {
        return 0;
    }
    return &slot->preview;
}

const wavetable_preview_t *wavetable_pool_get_preview_for_global(uint16_t global_slot)
{
    uint16_t wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
    if (sample_global_pool_resolve_backend(global_slot,
                                           SAMPLE_GLOBAL_KIND_WAVETABLE,
                                           &wavetable_slot) == 0U)
    {
        return 0;
    }
    return wavetable_pool_get_preview(wavetable_slot);
}

uint32_t wavetable_pool_get_used_bytes(void)
{
    const uint32_t total = sample_page_cache_port_shared_total_bytes();
    const uint32_t free_bytes = sample_page_cache_port_shared_free_bytes();
    return (free_bytes >= total) ? 0U : (total - free_bytes);
}

uint32_t wavetable_pool_get_free_bytes(void)
{
    return sample_page_cache_port_shared_free_bytes();
}

wavetable_result_t wavetable_pool_get_last_result(void)
{
    return g_wavetable_pool.last_result;
}

const char *wavetable_pool_result_label(wavetable_result_t result)
{
    switch (result)
    {
        case WAVETABLE_RESULT_OK:
            return "LOAD OK";
        case WAVETABLE_RESULT_POOL_FULL:
        case WAVETABLE_RESULT_GLOBAL_SLOT_FULL:
            return "SLOT FULL";
        case WAVETABLE_RESULT_GLOBAL_BUDGET_FULL:
        case WAVETABLE_RESULT_RAM_POOL_FULL:
            return "MEM FULL";
        case WAVETABLE_RESULT_TOO_LARGE:
            return "TOO LARGE";
        case WAVETABLE_RESULT_PATH_TOO_LONG:
            return "PATH LONG";
        case WAVETABLE_RESULT_SD_BUSY:
            return "SD BUSY";
        case WAVETABLE_RESULT_SD_MOUNT_FAIL:
            return "SD UNAVAILABLE";
        case WAVETABLE_RESULT_OPEN_FAIL:
            return "OPEN FAIL";
        case WAVETABLE_RESULT_BAD_FILE:
            return "BAD WT";
        case WAVETABLE_RESULT_UNSUPPORTED:
            return "UNSUPPORTED";
        case WAVETABLE_RESULT_READ_FAIL:
            return "SD READ FAIL";
        case WAVETABLE_RESULT_WRITE_FAIL:
            return "SD WRITE FAIL";
        case WAVETABLE_RESULT_REGISTER_FAIL:
            return "REGISTER FAIL";
        case WAVETABLE_RESULT_INVALID_ARG:
        default:
            return "LOAD FAIL";
    }
}
