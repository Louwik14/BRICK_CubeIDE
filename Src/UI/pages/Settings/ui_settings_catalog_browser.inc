static const char *ui_page_settings_preview_error_label(sd_preview_error_t error)
{
    switch (error)
    {
        case SD_PREVIEW_ERROR_NONE:
            return "PREVIEW STOP";
        case SD_PREVIEW_ERROR_INVALID_PATH:
            return "BAD PATH";
        case SD_PREVIEW_ERROR_BUSY:
        case SD_PREVIEW_ERROR_GATE_REFUSED:
            return "SD BUSY";
        case SD_PREVIEW_ERROR_MOUNT_FAIL:
            return "SD UNAVAILABLE";
        case SD_PREVIEW_ERROR_OPEN_FAIL:
            return "OPEN FAIL";
        case SD_PREVIEW_ERROR_PARSE_FAIL:
            return "WAV INVALID";
        case SD_PREVIEW_ERROR_UNSUPPORTED_FORMAT:
            return "WAV UNSUPP";
        case SD_PREVIEW_ERROR_READ_FAIL:
            return "SD READ FAIL";
        case SD_PREVIEW_ERROR_RECORD_ACTIVE:
            return "REC ACTIVE";
        default:
            return "PREVIEW FAIL";
    }
}

static void ui_page_settings_refresh_project_slots(void)
{
    g_ui_settings.project_slot_count = storage_settings_project_list_slots(g_ui_settings.project_slots, PROJECT_PRODUCT_SLOT_COUNT);
}

static void ui_page_settings_refresh_global_kind_slots(sample_global_kind_t kind)
{
    uint16_t count = 0U;
    const uint16_t capacity = SAMPLE_GLOBAL_POOL_FINAL_SLOTS;
    if (kind == SAMPLE_GLOBAL_KIND_CLASSIC)
        count = storage_settings_list_samples(PERSIST_ASSET_SAMPLE_STREAM,g_ui_settings.sampler_slots,capacity);
    else if (kind == SAMPLE_GLOBAL_KIND_RAM)
        count = storage_settings_list_samples(PERSIST_ASSET_SAMPLE_RAM,g_ui_settings.sampler_slots,capacity);
    else if (kind == SAMPLE_GLOBAL_KIND_WAVETABLE)
        count = storage_settings_list_wavetables(g_ui_settings.sampler_slots,capacity);
    else if (kind == SAMPLE_GLOBAL_KIND_MULTI)
        count = storage_settings_list_multis(g_ui_settings.sampler_slots,capacity);
    g_ui_settings.sampler_slot_count = count;
    if (g_ui_settings.sample_slot_selected >= capacity)
    {
        g_ui_settings.sample_slot_selected = 0U;
    }
    uint8_t selected_visible = 0U;
    for (uint16_t i = 0U; i < count; ++i)
    {
        if (g_ui_settings.sampler_slots[i] == g_ui_settings.sample_slot_selected)
        {
            selected_visible = 1U;
            break;
        }
    }
    if ((selected_visible == 0U) && (count != 0U))
    {
        g_ui_settings.sample_slot_selected = g_ui_settings.sampler_slots[0];
    }
}

static void ui_page_settings_select_neighbor_before_delete(void)
{
    for (uint16_t i = 0U; i < g_ui_settings.sampler_slot_count; ++i)
    {
        if (g_ui_settings.sampler_slots[i] == g_ui_settings.sample_slot_selected)
        {
            if (i + 1U < g_ui_settings.sampler_slot_count)
                g_ui_settings.sample_slot_selected = g_ui_settings.sampler_slots[i + 1U];
            else if (i != 0U)
                g_ui_settings.sample_slot_selected = g_ui_settings.sampler_slots[i - 1U];
            else
                g_ui_settings.sample_slot_selected = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
            return;
        }
    }
    g_ui_settings.sample_slot_selected = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
}

static void ui_page_settings_refresh_sampler_slots(void)
{
    ui_page_settings_refresh_global_kind_slots(SAMPLE_GLOBAL_KIND_CLASSIC);
}

static void ui_page_settings_refresh_ram_slots(void)
{
    ui_page_settings_refresh_global_kind_slots(SAMPLE_GLOBAL_KIND_RAM);
}

static void ui_page_settings_refresh_wavetable_slots(void)
{
    ui_page_settings_refresh_global_kind_slots(SAMPLE_GLOBAL_KIND_WAVETABLE);
}

static void ui_page_settings_sd_busy_status(void)
{
    char status[16];
    (void)snprintf(status, sizeof(status), "SD %s", storage_settings_sd_busy_label());
    ui_page_settings_status(status);
}

static uint8_t ui_page_settings_stream_backend_from_global(uint16_t global_slot,
                                                           uint16_t *out_backend_slot)
{
    if (out_backend_slot != 0)
    {
        *out_backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }

    uint16_t runtime_global=SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    uint32_t runtime_kind=0U;
    if(storage_settings_resolve_sample_runtime(global_slot,&runtime_global,&runtime_kind)==0U||runtime_kind!=PERSIST_ASSET_SAMPLE_STREAM)return 0U;
    const sample_global_slot_t *const slot = storage_settings_get_global_slot(runtime_global);
    if ((slot == 0)
        || (slot->kind != SAMPLE_GLOBAL_KIND_CLASSIC)
        || (runtime_global >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS))
    {
        return 0U;
    }

    if (out_backend_slot != 0)
    {
        *out_backend_slot = runtime_global;
    }
    return 1U;
}

static uint16_t ui_page_settings_filtered_index_for_global(uint16_t global_slot)
{
    for (uint16_t i = 0U; i < g_ui_settings.sampler_slot_count; ++i)
    {
        if (g_ui_settings.sampler_slots[i] == global_slot)
        {
            return i;
        }
    }
    return 0U;
}

static uint8_t ui_page_settings_multi_backend_from_global(uint16_t global_slot,
                                                          uint16_t *out_backend_slot)
{
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (out_backend_slot != 0)
    {
        *out_backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }

    if ((storage_settings_resolve_multi_runtime(global_slot,&backend_slot)==0U)
        || (backend_slot >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS))
    {
        return 0U;
    }

    if (out_backend_slot != 0)
    {
        *out_backend_slot = backend_slot;
    }
    return 1U;
}

static uint8_t ui_page_settings_ram_backend_from_global(uint16_t global_slot,
                                                        uint16_t *out_backend_slot)
{
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (out_backend_slot != 0)
    {
        *out_backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }

    uint16_t runtime_global=SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    uint32_t runtime_kind=0U;
    if(storage_settings_resolve_sample_runtime(global_slot,&runtime_global,&runtime_kind)==0U||runtime_kind!=PERSIST_ASSET_SAMPLE_RAM)return 0U;
    const sample_global_slot_t *const slot = storage_settings_get_global_slot(runtime_global);
    if ((slot == 0)
        || (slot->kind != SAMPLE_GLOBAL_KIND_RAM)
        || (storage_settings_resolve_backend(runtime_global,
                                               SAMPLE_GLOBAL_KIND_RAM,
                                               &backend_slot) == 0U)
        || (backend_slot >= SAMPLER_RAM_POOL_MAX_SLOTS))
    {
        return 0U;
    }

    if (out_backend_slot != 0)
    {
        *out_backend_slot = backend_slot;
    }
    return 1U;
}

static uint8_t ui_page_settings_wavetable_backend_from_global(uint16_t global_slot,
                                                              uint16_t *out_backend_slot)
{
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (out_backend_slot != 0)
    {
        *out_backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }

    uint16_t runtime_global=SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if(storage_settings_resolve_wavetable_runtime(global_slot,&runtime_global)==0U)return 0U;
    const sample_global_slot_t *const slot = storage_settings_get_global_slot(runtime_global);
    if ((slot == 0)
        || (slot->kind != SAMPLE_GLOBAL_KIND_WAVETABLE)
        || (storage_settings_resolve_backend(runtime_global,
                                               SAMPLE_GLOBAL_KIND_WAVETABLE,
                                               &backend_slot) == 0U)
        || (backend_slot >= WAVETABLE_POOL_MAX_SLOTS))
    {
        return 0U;
    }

    if (out_backend_slot != 0)
    {
        *out_backend_slot = backend_slot;
    }
    return 1U;
}

static int16_t ui_page_settings_stream_find_free_backend_slot(void)
{
    const uint16_t slot = storage_settings_find_free_global_slot();
    return (slot == SAMPLE_GLOBAL_POOL_INVALID_INDEX) ? -1 : (int16_t)slot;
}

static int16_t ui_page_settings_ram_find_free_backend_slot(void)
{
    const uint16_t slot = storage_settings_find_free_ram_slot();
    return (slot < SAMPLER_RAM_POOL_MAX_SLOTS) ? (int16_t)slot : -1;
}

static int16_t ui_page_settings_wavetable_find_free_backend_slot(void)
{
    const uint16_t slot = storage_settings_find_free_wavetable_slot();
    return (slot < WAVETABLE_POOL_MAX_SLOTS) ? (int16_t)slot : -1;
}

static uint8_t ui_page_settings_wav_ext_is_wav(const char *name)
{
    const size_t len = (name != 0) ? strlen(name) : 0U;
    if (len < 4U)
    {
        return 0U;
    }

    return ((name[len - 4U] == '.')
            && ((name[len - 3U] == 'w') || (name[len - 3U] == 'W'))
            && ((name[len - 2U] == 'a') || (name[len - 2U] == 'A'))
            && ((name[len - 1U] == 'v') || (name[len - 1U] == 'V')))
        ? 1U
        : 0U;
}

static uint8_t ui_page_settings_wavetable_ext_is_wav(const char *name)
{
    const size_t len = (name != 0) ? strlen(name) : 0U;
    if (len < 4U)
    {
        return 0U;
    }

    return ((name[len - 4U] == '.')
            && ((name[len - 3U] == 'w') || (name[len - 3U] == 'W'))
            && ((name[len - 2U] == 'a') || (name[len - 2U] == 'A'))
            && ((name[len - 1U] == 'v') || (name[len - 1U] == 'V')))
        ? 1U
        : 0U;
}

static void ui_page_settings_make_sample_label(char *out, uint32_t out_size, const char *name, uint8_t is_dir)
{
    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    out[0] = '\0';
    if (name == 0)
    {
        return;
    }

    uint32_t len = (uint32_t)strlen(name);
    if ((is_dir == 0U) && (len > 4U) && (ui_page_settings_wav_ext_is_wav(name) != 0U))
    {
        len -= 4U;
    }

    if (len >= out_size)
    {
        len = out_size - 1U;
    }

    (void)memcpy(out, name, len);
    out[len] = '\0';
}

static void ui_page_settings_make_wavetable_label(char *out, uint32_t out_size, const char *name, uint8_t is_dir)
{
    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    out[0] = '\0';
    if (name == 0)
    {
        return;
    }

    uint32_t len = (uint32_t)strlen(name);
    if ((is_dir == 0U) && (len > 4U) && (ui_page_settings_wavetable_ext_is_wav(name) != 0U))
    {
        len -= 4U;
    }

    if (len >= out_size)
    {
        len = out_size - 1U;
    }

    (void)memcpy(out, name, len);
    out[len] = '\0';
}

static uint8_t ui_page_settings_sample_browser_refresh(void)
{
    if ((storage_settings_catalog_loaded() == 0U) || (storage_settings_catalog_stale() != 0U))
    {
        ui_page_settings_status("REFRESH LIB");
        return 0U;
    }

    const uint16_t catalog_child_count = storage_settings_catalog_child_count(g_ui_settings.sample_parent_id);
    const uint16_t parent_entry_count =
        (g_ui_settings.sample_parent_id != WAV_LOADER_CATALOG_ROOT_PARENT) ? 1U : 0U;
    if (storage_settings_catalog_view_busy() != 0U)
    {
        return 0U;
    }
    if (storage_settings_catalog_last_sd_busy() != 0U)
    {
        ui_page_settings_sd_busy_status();
        return 0U;
    }
    if (storage_settings_catalog_last_io_error() != 0U)
    {
        ui_page_settings_status("CAT FAIL");
        return 0U;
    }
    g_ui_settings.sample_child_count = (uint16_t)(catalog_child_count + parent_entry_count);
    if (g_ui_settings.sample_selected >= g_ui_settings.sample_child_count)
    {
        g_ui_settings.sample_selected = (g_ui_settings.sample_child_count == 0U)
            ? 0U
            : (uint16_t)(g_ui_settings.sample_child_count - 1U);
    }
    const uint16_t page_start =
        (uint16_t)(g_ui_settings.sample_selected
                   - (g_ui_settings.sample_selected % UI_SETTINGS_SAMPLE_BROWSER_MAX));

    if (g_ui_settings.sample_child_count > parent_entry_count)
    {
        const uint16_t first_catalog_child =
            (page_start > parent_entry_count) ? (uint16_t)(page_start - parent_entry_count) : 0U;
        if (storage_settings_catalog_get_child(g_ui_settings.sample_parent_id, first_catalog_child) == 0)
        {
            if (storage_settings_catalog_last_sd_busy() != 0U)
            {
                ui_page_settings_sd_busy_status();
            }
            else if (storage_settings_catalog_last_io_error() != 0U)
            {
                ui_page_settings_status("CAT FAIL");
            }
            else
            {
                ui_page_settings_status("CAT FAIL");
            }
            return 0U;
        }
    }

    g_ui_settings.sample_entry_count = 0U;
    g_ui_settings.sample_page_start = page_start;
    for (uint16_t child = page_start;
         (child < g_ui_settings.sample_child_count) && (g_ui_settings.sample_entry_count < UI_SETTINGS_SAMPLE_BROWSER_MAX);
         ++child)
    {
        if ((parent_entry_count != 0U) && (child == 0U))
        {
            ui_settings_sample_entry_t *const entry =
                &g_ui_settings.sample_entries[g_ui_settings.sample_entry_count];
            (void)snprintf(entry->path, sizeof(entry->path), "..");
            (void)snprintf(entry->label, sizeof(entry->label), "..");
            entry->catalog_index = WAV_LOADER_CATALOG_ROOT_PARENT;
            entry->type = UI_SETTINGS_SAMPLE_ENTRY_PARENT;
            g_ui_settings.sample_entry_count++;
            continue;
        }
        const uint16_t catalog_child = (uint16_t)(child - parent_entry_count);
        const wav_loader_catalog_entry_t *const catalog_entry =
            storage_settings_catalog_get_child(g_ui_settings.sample_parent_id, catalog_child);
        if (catalog_entry == 0)
        {
            if (storage_settings_catalog_last_sd_busy() != 0U)
            {
                ui_page_settings_sd_busy_status();
                return 0U;
            }
            if (storage_settings_catalog_last_io_error() != 0U)
            {
                ui_page_settings_status("CAT FAIL");
                return 0U;
            }
            break;
        }

        ui_settings_sample_entry_t *const entry =
            &g_ui_settings.sample_entries[g_ui_settings.sample_entry_count];
        const int path_len = snprintf(entry->path, sizeof(entry->path), "%s", catalog_entry->path);
        if ((path_len < 0) || ((uint32_t)path_len >= sizeof(entry->path)))
        {
            continue;
        }

        ui_page_settings_make_sample_label(entry->label,
                                           sizeof(entry->label),
                                           catalog_entry->name,
                                           (catalog_entry->type == WAV_LOADER_CATALOG_ENTRY_DIR) ? 1U : 0U);
        if (catalog_entry->type == WAV_LOADER_CATALOG_ENTRY_DIR)
        {
            char prefixed[24];
            (void)snprintf(prefixed,
                           sizeof(prefixed),
                           "> %.*s",
                           (int)(sizeof(prefixed) - 3U),
                           entry->label);
            (void)snprintf(entry->label, sizeof(entry->label), "%s", prefixed);
        }
        entry->catalog_index = storage_settings_catalog_get_child_index(g_ui_settings.sample_parent_id, catalog_child);
        entry->type = (catalog_entry->type == WAV_LOADER_CATALOG_ENTRY_DIR)
            ? UI_SETTINGS_SAMPLE_ENTRY_DIR
            : UI_SETTINGS_SAMPLE_ENTRY_FILE;
        g_ui_settings.sample_entry_count++;
    }

    if (storage_settings_catalog_truncated() != 0U)
    {
        ui_page_settings_status("LIB FULL");
    }
    if (g_ui_settings.sample_left_scroll >= g_ui_settings.sample_child_count)
    {
        g_ui_settings.sample_left_scroll = 0U;
    }

    return 1U;
}

static uint8_t ui_page_settings_wavetable_browser_refresh(void)
{
    for (uint8_t attempt = 0U; attempt < 3U; ++attempt)
    {
        g_ui_settings.sample_entry_count = 0U;
        g_ui_settings.sample_page_start = 0U;
        g_ui_settings.sample_child_count = 0U;

        if (strcmp(g_ui_settings.sample_dir, UI_SETTINGS_WAVETABLE_ROOT) != 0)
        {
            ui_settings_sample_entry_t *const entry = &g_ui_settings.sample_entries[g_ui_settings.sample_entry_count++];
            (void)snprintf(entry->path, sizeof(entry->path), "..");
            (void)snprintf(entry->label, sizeof(entry->label), "..");
            entry->catalog_index = WAV_LOADER_CATALOG_ROOT_PARENT;
            entry->type = UI_SETTINGS_SAMPLE_ENTRY_PARENT;
        }

        storage_catalog_snapshot_t snapshot;
        if (storage_settings_catalog_snapshot_begin(STORAGE_CATALOG_WAVETABLE, g_ui_settings.sample_dir,
                                            &snapshot) == 0U)
            break;
        for (uint16_t i = 0U; i < snapshot.count; ++i)
        {
            const storage_catalog_entry_t *const source = &snapshot.entries[i];
            ui_settings_sample_entry_t *const entry =
                &g_ui_settings.sample_entries[g_ui_settings.sample_entry_count++];
            memset(entry, 0, sizeof(*entry));
            ui_page_settings_copy_bounded(entry->path, sizeof(entry->path), source->path);
            ui_page_settings_make_wavetable_label(entry->label, sizeof(entry->label),
                                                  source->name, source->is_dir);
            entry->type = (source->is_dir != 0U)
                ? UI_SETTINGS_SAMPLE_ENTRY_DIR : UI_SETTINGS_SAMPLE_ENTRY_FILE;
            if (source->is_dir != 0U)
            {
                char prefixed[24];
                (void)snprintf(prefixed, sizeof(prefixed), "> %.*s",
                               (int)(sizeof(prefixed) - 3U), entry->label);
                (void)snprintf(entry->label, sizeof(entry->label), "%s", prefixed);
            }
            entry->catalog_index = WAV_LOADER_CATALOG_ROOT_PARENT;
        }
        if (storage_settings_catalog_snapshot_end(&snapshot) != 0U)
            goto wavetable_snapshot_valid;
    }

    (void)storage_settings_catalog_request(STORAGE_CATALOG_WAVETABLE, g_ui_settings.sample_dir);
    ui_page_settings_status("NO WAVETABLES");
    return 0U;

wavetable_snapshot_valid:

    g_ui_settings.sample_child_count = g_ui_settings.sample_entry_count;
    if (g_ui_settings.sample_selected >= g_ui_settings.sample_child_count)
    {
        g_ui_settings.sample_selected = (g_ui_settings.sample_child_count == 0U)
            ? 0U
            : (uint16_t)(g_ui_settings.sample_child_count - 1U);
    }
    if (g_ui_settings.sample_left_scroll >= g_ui_settings.sample_child_count)
    {
        g_ui_settings.sample_left_scroll = 0U;
    }
    if (g_ui_settings.sample_entry_count >= UI_SETTINGS_SAMPLE_BROWSER_MAX)
    {
        ui_page_settings_status("LIST FULL");
    }

    return 1U;
}

static uint8_t ui_page_settings_multi_entry_is_folder_like(const ui_settings_multi_entry_t *entry)
{
    return ((entry != 0) && (entry->type != UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM)) ? 1U : 0U;
}

static void ui_page_settings_multi_sort_folders_first(void)
{
    for (uint16_t i = 1U; i < g_ui_settings.multi_entry_count; ++i)
    {
        ui_settings_multi_entry_t key = g_ui_settings.multi_entries[i];
        if (ui_page_settings_multi_entry_is_folder_like(&key) == 0U)
        {
            continue;
        }

        uint16_t j = i;
        while ((j > 0U)
               && (ui_page_settings_multi_entry_is_folder_like(&g_ui_settings.multi_entries[j - 1U]) == 0U))
        {
            g_ui_settings.multi_entries[j] = g_ui_settings.multi_entries[j - 1U];
            j--;
        }
        g_ui_settings.multi_entries[j] = key;
    }
}

static uint8_t ui_page_settings_multi_browser_refresh(void)
{
    for (uint8_t attempt = 0U; attempt < 3U; ++attempt)
    {
        g_ui_settings.multi_entry_count = 0U;
        storage_catalog_snapshot_t snapshot;
        if (storage_settings_catalog_snapshot_begin(STORAGE_CATALOG_MULTI, g_ui_settings.sample_dir,
                                            &snapshot) == 0U)
            break;
        for (uint16_t i = 0U; i < snapshot.count; ++i)
        {
            const storage_catalog_entry_t *const source = &snapshot.entries[i];
            ui_settings_multi_entry_t *const entry =
                &g_ui_settings.multi_entries[g_ui_settings.multi_entry_count++];
            memset(entry, 0, sizeof(*entry));
            ui_page_settings_copy_bounded(entry->path, sizeof(entry->path), source->path);
            ui_page_settings_copy_bounded(entry->index_path, sizeof(entry->index_path),
                                          source->index_path);
            entry->wav_count = source->wav_count;
            entry->sample_count = source->sample_count;
            entry->zone_count = source->zone_count;
            entry->slot_cost = source->slot_cost;
            entry->prepared = source->prepared;
            entry->type = (ui_settings_multi_entry_type_t)source->multi_type;
            ui_page_settings_make_sample_label(entry->label, sizeof(entry->label),
                                               source->name, 1U);
            if (entry->type != UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM)
            {
                char prefixed[MULTI_SAMPLE_POOL_NAME_MAX];
                (void)snprintf(prefixed, sizeof(prefixed), "> %.*s",
                               (int)(sizeof(prefixed) - 3U), entry->label);
                (void)snprintf(entry->label, sizeof(entry->label), "%s", prefixed);
            }
        }

        if (storage_settings_catalog_snapshot_end(&snapshot) != 0U)
            goto multi_snapshot_valid;
    }

    (void)storage_settings_catalog_request(STORAGE_CATALOG_MULTI, g_ui_settings.sample_dir);
    ui_page_settings_status("NO MULTI");
    return 0U;

multi_snapshot_valid:
    ui_page_settings_multi_sort_folders_first();

    if (g_ui_settings.sample_selected >= g_ui_settings.multi_entry_count)
    {
        g_ui_settings.sample_selected = (g_ui_settings.multi_entry_count == 0U)
            ? 0U
            : (uint16_t)(g_ui_settings.multi_entry_count - 1U);
    }
    if (g_ui_settings.sample_left_scroll >= g_ui_settings.multi_entry_count)
    {
        g_ui_settings.sample_left_scroll = 0U;
    }

    return 1U;
}

static void ui_page_settings_sample_browser_enter_root(void)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", UI_SETTINGS_SAMPLE_ROOT);
    g_ui_settings.sample_parent_id = WAV_LOADER_CATALOG_ROOT_PARENT;
    g_ui_settings.sample_selected = 0U;
    g_ui_settings.sample_slot_selected = 0U;
    g_ui_settings.sample_left_scroll = 0U;
    g_ui_settings.sample_right_scroll = 0U;
    g_ui_settings.sample_focus = (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY;
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
    g_ui_settings.confirm_slot = 0U;
    g_ui_settings.sample_preview_volume = 100U;
    g_ui_settings.confirm_path[0] = '\0';
    (void)control_domain_request_preview_gain(1.0f);
    (void)ui_page_settings_sample_browser_refresh();
}

static void ui_page_settings_multi_browser_enter_root(void)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", UI_SETTINGS_MULTI_ROOT);
    g_ui_settings.sample_selected = 0U;
    g_ui_settings.sample_slot_selected = 0U;
    g_ui_settings.sample_left_scroll = 0U;
    g_ui_settings.sample_right_scroll = 0U;
    g_ui_settings.sample_focus = (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY;
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
    g_ui_settings.confirm_slot = 0U;
    g_ui_settings.confirm_path[0] = '\0';
    g_ui_settings.multi_prepare_progress_done = 0U;
    g_ui_settings.multi_prepare_progress_total = 0U;
    g_ui_settings.multi_prepare_phase = (uint8_t)UI_SETTINGS_MULTI_PREP_PHASE_NONE;
    g_ui_settings.header_slot_flash_until_ms = 0U;
    g_ui_settings.header_mem_flash_until_ms = 0U;
    (void)ui_page_settings_multi_browser_refresh();
}

static void ui_page_settings_wavetable_browser_enter_root(void)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", UI_SETTINGS_WAVETABLE_ROOT);
    g_ui_settings.sample_selected = 0U;
    g_ui_settings.sample_slot_selected = 0U;
    g_ui_settings.sample_left_scroll = 0U;
    g_ui_settings.sample_right_scroll = 0U;
    g_ui_settings.sample_focus = (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY;
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
    g_ui_settings.confirm_slot = 0U;
    g_ui_settings.confirm_path[0] = '\0';
    g_ui_settings.header_slot_flash_until_ms = 0U;
    g_ui_settings.header_mem_flash_until_ms = 0U;
    (void)ui_page_settings_wavetable_browser_refresh();
}

static void ui_page_settings_multi_browser_parent_or_exit(void)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    if (strcmp(g_ui_settings.sample_dir, UI_SETTINGS_MULTI_ROOT) == 0)
    {
        ui_page_settings_back();
        return;
    }

    char *slash = strrchr(g_ui_settings.sample_dir, '/');
    if ((slash == 0) || (slash <= (g_ui_settings.sample_dir + strlen(UI_SETTINGS_MULTI_ROOT))))
    {
        (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", UI_SETTINGS_MULTI_ROOT);
    }
    else
    {
        *slash = '\0';
    }

    g_ui_settings.sample_selected = 0U;
    g_ui_settings.sample_left_scroll = 0U;
    (void)ui_page_settings_multi_browser_refresh();
}

static void ui_page_settings_wavetable_browser_parent_or_exit(void)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    if (strcmp(g_ui_settings.sample_dir, UI_SETTINGS_WAVETABLE_ROOT) == 0)
    {
        ui_page_settings_back();
        return;
    }

    char *slash = strrchr(g_ui_settings.sample_dir, '/');
    if ((slash == 0) || (slash <= (g_ui_settings.sample_dir + strlen(UI_SETTINGS_WAVETABLE_ROOT))))
    {
        (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", UI_SETTINGS_WAVETABLE_ROOT);
    }
    else
    {
        *slash = '\0';
    }

    g_ui_settings.sample_selected = 0U;
    g_ui_settings.sample_left_scroll = 0U;
    (void)ui_page_settings_wavetable_browser_refresh();
}

static void ui_page_settings_sample_browser_parent_or_exit(void)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    if (strcmp(g_ui_settings.sample_dir, UI_SETTINGS_SAMPLE_ROOT) == 0)
    {
        ui_page_settings_back();
        return;
    }

    if (g_ui_settings.sample_parent_id != WAV_LOADER_CATALOG_ROOT_PARENT)
    {
        const wav_loader_catalog_entry_t *const parent = storage_settings_catalog_get(g_ui_settings.sample_parent_id);
        if ((parent == 0) && (storage_settings_catalog_last_sd_busy() != 0U))
        {
            ui_page_settings_sd_busy_status();
            return;
        }
        g_ui_settings.sample_parent_id = (parent != 0) ? parent->parent_id : WAV_LOADER_CATALOG_ROOT_PARENT;
    }

    char *slash = strrchr(g_ui_settings.sample_dir, '/');
    if ((slash == 0) || (slash <= (g_ui_settings.sample_dir + strlen(UI_SETTINGS_SAMPLE_ROOT))))
    {
        (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", UI_SETTINGS_SAMPLE_ROOT);
    }
    else
    {
        *slash = '\0';
    }

    g_ui_settings.sample_selected = 0U;
    g_ui_settings.sample_left_scroll = 0U;
    (void)ui_page_settings_sample_browser_refresh();
}

static uint8_t ui_page_settings_sample_restore_dir_after_refresh(const char *dir_path,
                                                                 const char *selection_path)
{
    if ((dir_path == 0) || (dir_path[0] == '\0'))
    {
        return 0U;
    }

    char probe[WAV_LOADER_CATALOG_PATH_MAX];
    (void)snprintf(probe, sizeof(probe), "%s", dir_path);
    while (1)
    {
        uint16_t dir_index = WAV_LOADER_CATALOG_ROOT_PARENT;
        wav_loader_catalog_entry_t dir_entry;
        uint8_t found = 0U;
        if (strcmp(probe, UI_SETTINGS_SAMPLE_ROOT) == 0)
        {
            found = 1U;
            dir_index = WAV_LOADER_CATALOG_ROOT_PARENT;
        }
        else if ((storage_settings_catalog_find_path(probe, &dir_index, &dir_entry) != 0U)
                 && (dir_entry.type == WAV_LOADER_CATALOG_ENTRY_DIR))
        {
            found = 1U;
        }

        if (found != 0U)
        {
            (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", probe);
            g_ui_settings.sample_parent_id = dir_index;
            g_ui_settings.sample_selected = 0U;
            if ((selection_path != 0) && (selection_path[0] != '\0'))
            {
                uint16_t selected_index = WAV_LOADER_CATALOG_ROOT_PARENT;
                wav_loader_catalog_entry_t selected_entry;
                if ((storage_settings_catalog_find_path(selection_path, &selected_index, &selected_entry) != 0U)
                    && (selected_entry.parent_id == dir_index))
                {
                    const uint16_t parent_entry_count =
                        (dir_index != WAV_LOADER_CATALOG_ROOT_PARENT) ? 1U : 0U;
                    const uint16_t child_count = storage_settings_catalog_child_count(dir_index);
                    for (uint16_t child = 0U; child < child_count; ++child)
                    {
                        if (storage_settings_catalog_get_child_index(dir_index, child) == selected_index)
                        {
                            g_ui_settings.sample_selected = (uint16_t)(child + parent_entry_count);
                            break;
                        }
                    }
                }
            }
            g_ui_settings.sample_left_scroll = 0U;
            (void)ui_page_settings_sample_browser_refresh();
            return 1U;
        }

        char *slash = strrchr(probe, '/');
        if ((slash == 0) || (slash <= (probe + strlen(UI_SETTINGS_SAMPLE_ROOT))))
        {
            (void)snprintf(probe, sizeof(probe), "%s", UI_SETTINGS_SAMPLE_ROOT);
            if (strcmp(dir_path, UI_SETTINGS_SAMPLE_ROOT) == 0)
            {
                return 0U;
            }
            dir_path = UI_SETTINGS_SAMPLE_ROOT;
        }
        else
        {
            *slash = '\0';
        }
    }
}

