static void wavetable_pool_finalize_clear(uint16_t wavetable_slot)
{
    if (wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS)
    {
        return;
    }

    wavetable_slot_t *const slot = &g_wavetable_pool.slots[wavetable_slot];
    const wavetable_slot_t old = *slot;
    if ((g_wavetable_retire_delete[wavetable_slot] != 0U)
        && (old.persistent != 0U)
        && (wavetable_flash_store_delete(old.flash_offset) == 0U))
    {
        g_wavetable_retire_invariant_failed = 1U;
        return;
    }
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
    g_wavetable_retire_delete[wavetable_slot] = 0U;
    memset(&g_wavetable_pool.slots[wavetable_slot], 0, sizeof(g_wavetable_pool.slots[wavetable_slot]));
    g_wavetable_pool.slots[wavetable_slot].state = WAVETABLE_SLOT_EMPTY;
    g_wavetable_pool.slots[wavetable_slot].global_slot = WAVETABLE_POOL_INVALID_SLOT;
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
    const uint32_t generation = slot->generation;
    audio_wave_table_projection_withdraw_slot(wavetable_slot, generation);
    slot->state = WAVETABLE_SLOT_RETIRING;
    g_wavetable_retire_delete[wavetable_slot] = 1U;
    g_wavetable_retire_stop_committed[wavetable_slot] = 0U;
    __DMB();
}

void wavetable_pool_retire_all(void)
{
    /* Persistent immutable tables survive project replacement.  The project
       panic withdraws track selections; the global registry remains valid. */
}

void wavetable_pool_service_retire(void)
{
    if (g_wavetable_retire_invariant_failed != 0U) return;
    for (uint16_t i = 0U; i < WAVETABLE_POOL_MAX_SLOTS; ++i)
    {
        wavetable_slot_t *const slot = &g_wavetable_pool.slots[i];
        if (slot->state != WAVETABLE_SLOT_RETIRING) continue;
        uint64_t now_sample = 0U;
        if (!control_rt_now_sample(&now_sample)) continue;
        if (g_wavetable_retire_stop_committed[i] == 0U)
        {
            if (control_rt_publication_horizon_active() != 0U) continue;
            if (control_rt_publication_free() == 0U) continue;
            if (control_rt_publish_param_now((uint8_t)i,
                    CONTROL_AUDIO_PARAM_WAVE_RESOURCE_STOP,
                    slot->generation, 0U) == 0U)
            {
                g_wavetable_retire_invariant_failed = 1U;
                continue;
            }
            g_wavetable_retire_not_before_sample[i] = now_sample
                + CONTROL_AUDIO_RESOURCE_RETIRE_GRACE_FRAMES;
            g_wavetable_retire_stop_committed[i] = 1U;
        }
        if (now_sample < g_wavetable_retire_not_before_sample[i]) continue;
        g_wavetable_retire_stop_committed[i] = 0U;
        wavetable_pool_finalize_clear(i);
    }
}

uint8_t wavetable_pool_retire_idle(void)
{
    if (g_wavetable_retire_invariant_failed != 0U) return 0U;
    for (uint16_t i = 0U; i < WAVETABLE_POOL_MAX_SLOTS; ++i)
        if (g_wavetable_pool.slots[i].state == WAVETABLE_SLOT_RETIRING)
            return 0U;
    return 1U;
}

uint8_t wavetable_pool_retire_failed(void)
{
    return g_wavetable_retire_invariant_failed;
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
    if (wavetable_pool_resolve_global(global_slot, &wavetable_slot) == 0U)
        return 0;
    return wavetable_pool_get_preview(wavetable_slot);
}

uint8_t wavetable_pool_resolve_global(uint16_t global_slot,
                                      uint16_t *out_wavetable_slot)
{
    if ((out_wavetable_slot == NULL)
        || (global_slot >= WAVETABLE_POOL_MAX_SLOTS)) return 0U;
    const wavetable_slot_t *const slot = &g_wavetable_pool.slots[global_slot];
    if ((slot->state != WAVETABLE_SLOT_READY)
        || (slot->global_slot != global_slot)) return 0U;
    *out_wavetable_slot = global_slot;
    return 1U;
}

uint8_t wavetable_pool_find_by_path(const char *path,
                                    uint16_t *out_wavetable_slot)
{
    if ((path == NULL) || (out_wavetable_slot == NULL)) return 0U;
    for (uint16_t i = 0U; i < WAVETABLE_POOL_MAX_SLOTS; ++i)
    {
        const wavetable_slot_t *const slot = &g_wavetable_pool.slots[i];
        if ((slot->state == WAVETABLE_SLOT_READY)
            && (wavetable_pool_paths_equal(slot->path, path) != 0U))
        {
            *out_wavetable_slot = i;
            return 1U;
        }
    }
    return 0U;
}

uint64_t wavetable_pool_get_stable_id(uint16_t wavetable_slot)
{
    const wavetable_slot_t *const slot = wavetable_pool_get_slot(wavetable_slot);
    return ((slot != NULL) && (slot->state == WAVETABLE_SLOT_READY))
        ? slot->stable_id : 0U;
}

uint32_t wavetable_pool_get_used_bytes(void)
{
    return wavetable_flash_store_used_bytes();
}

uint32_t wavetable_pool_get_free_bytes(void)
{
    return wavetable_flash_store_free_bytes();
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
        case WAVETABLE_RESULT_TRANSPORT_ACTIVE:
            return "STOP TO LOAD";
        case WAVETABLE_RESULT_RECORDER_ACTIVE:
            return "STOP REC TO LOAD";
        case WAVETABLE_RESULT_INVALID_ARG:
        default:
            return "LOAD FAIL";
    }
}
