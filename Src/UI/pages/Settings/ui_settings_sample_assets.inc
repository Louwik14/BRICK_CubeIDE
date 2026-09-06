static void ui_page_settings_sample_catalog_refresh_action(uint8_t rebuild)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    char old_dir[WAV_LOADER_CATALOG_PATH_MAX];
    char old_selection[WAV_LOADER_CATALOG_PATH_MAX];
    (void)snprintf(old_dir, sizeof(old_dir), "%s", g_ui_settings.sample_dir);
    old_selection[0] = '\0';
    const ui_settings_sample_entry_t *const selected_entry = ui_page_settings_sample_selected_entry();
    if (selected_entry != 0)
    {
        (void)snprintf(old_selection, sizeof(old_selection), "%s", selected_entry->path);
    }

    ui_page_settings_status((rebuild != 0U) ? "REBUILD" : "REFRESH");
    if (storage_settings_request_catalog(rebuild) == 0U)
    {
        ui_page_settings_status(
            (storage_settings_sd_status() == SD_STORAGE_STATUS_NO_MEDIA)
                ? "SD ABSENTE" : "STOP AUDIO");
        return;
    }

    if ((storage_settings_catalog_last_sd_busy() != 0U) || (storage_settings_catalog_stale() != 0U))
    {
        ui_page_settings_sd_busy_status();
        return;
    }

    if (ui_page_settings_sample_restore_dir_after_refresh(old_dir, old_selection) == 0U)
    {
        g_ui_settings.sample_parent_id = WAV_LOADER_CATALOG_ROOT_PARENT;
        (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", UI_SETTINGS_SAMPLE_ROOT);
        g_ui_settings.sample_selected = 0U;
        g_ui_settings.sample_left_scroll = 0U;
        (void)ui_page_settings_sample_browser_refresh();
    }
    ui_page_settings_status((storage_settings_catalog_truncated() != 0U) ? "LIB FULL" : "LIB OK");
}

static int16_t ui_page_settings_sample_find_free_slot(void)
{
    if (storage_settings_global_entries_used() >= storage_settings_global_entry_capacity())
    {
        return -1;
    }
    return ui_page_settings_stream_find_free_backend_slot();
}

static const ui_settings_sample_entry_t *ui_page_settings_sample_selected_entry(void)
{
    if ((g_ui_settings.sample_selected < g_ui_settings.sample_page_start)
        || (g_ui_settings.sample_selected >= (uint16_t)(g_ui_settings.sample_page_start
                                                        + g_ui_settings.sample_entry_count)))
    {
        return 0;
    }
    return &g_ui_settings.sample_entries[g_ui_settings.sample_selected - g_ui_settings.sample_page_start];
}

static void ui_page_settings_sample_load_to_slot(uint16_t slot, const char *path)
{
    if (storage_settings_project_replacement_active() != 0U) return;
    uint16_t classic_slots[SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS];
    const uint16_t classic_count = storage_settings_list_samples(
        PERSIST_ASSET_SAMPLE_STREAM, classic_slots, SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS);
    for (uint16_t i = 0U; i < classic_count; ++i)
    {
        const sample_global_slot_t *const classic =
            storage_settings_get_global_slot(classic_slots[i]);
        if ((classic != 0) && (strcmp(classic->path, path) == 0))
        {
            g_ui_settings.sample_slot_selected = classic_slots[i];
            ui_page_settings_status("ALREADY LOADED");
            return;
        }
    }
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    if (storage_settings_request_classic_load(slot, path) != 0U)
    {
        ui_page_settings_status("LOADING");
    }
    else
        ui_page_settings_status(
            (storage_settings_sd_status() == SD_STORAGE_STATUS_NO_MEDIA)
                ? "SD ABSENTE" : "LOAD BUSY");
}

static void ui_page_settings_ram_load_to_slot(uint16_t slot, const char *path)
{
    if (storage_settings_project_replacement_active() != 0U) return;
    uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    uint16_t existing_logical = 0U;
    uint32_t existing_kind = 0U;
    if ((storage_settings_find_asset(PERSIST_ASSET_SAMPLE_RAM,path,&existing_logical) != 0U)
        && (storage_settings_resolve_sample_runtime(existing_logical,&global_slot,&existing_kind) != 0U)
        && (existing_kind == PERSIST_ASSET_SAMPLE_RAM))
    {
        g_ui_settings.sample_slot_selected = existing_logical;
        ui_page_settings_status("ALREADY LOADED");
        return;
    }
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    if (storage_settings_request_ram_load(slot, path) != 0U)
    {
        ui_page_settings_status("LOADING");
    }
    else
    {
        ui_page_settings_status(
            (storage_settings_sd_status() == SD_STORAGE_STATUS_NO_MEDIA)
                ? "SD ABSENTE" : "LOAD BUSY");
    }
}

static void ui_page_settings_wavetable_load_to_slot(uint16_t slot, const char *path)
{
    if (storage_settings_project_replacement_active() != 0U) return;
    uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    uint16_t existing_logical = 0U;
    if ((storage_settings_find_asset(PERSIST_ASSET_WAVETABLE,path,&existing_logical) != 0U)
        && (storage_settings_resolve_wavetable_runtime(existing_logical,&global_slot) != 0U))
    {
        g_ui_settings.sample_slot_selected = existing_logical;
        ui_page_settings_status("ALREADY LOADED");
        return;
    }
    if (storage_settings_request_wavetable_load(
            slot, path, WAVETABLE_SOURCE_GEOMETRY_2048) != 0U)
    {
        ui_page_settings_status("LOADING");
    }
    else
    {
        ui_page_settings_status(
            (storage_settings_sd_status() == SD_STORAGE_STATUS_NO_MEDIA)
                ? "SD ABSENTE" : "LOAD BUSY");
    }
}

static void ui_page_settings_sample_preview_left(void)
{
    const ui_settings_sample_entry_t *const entry = ui_page_settings_sample_selected_entry();
    if ((entry == 0) || (entry->type != UI_SETTINGS_SAMPLE_ENTRY_FILE))
    {
        return;
    }

    if ((storage_settings_preview_active() != 0U) && (strcmp(storage_settings_preview_path(), entry->path) == 0))
    {
        ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_USER);
        return;
    }

    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    if (storage_settings_request_preview(entry->path) != 0U)
    {
        g_ui_settings.preview_stop_origin = UI_SETTINGS_PREVIEW_STOP_ORIGIN_NONE;
        g_ui_settings.preview_was_active = storage_settings_preview_active();
        ui_page_settings_status("PREVIEW ON");
    }
    else
    {
        ui_page_settings_status(ui_page_settings_preview_error_label(storage_settings_preview_last_error()));
    }
}

static void ui_page_settings_sample_preview_right(void)
{
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (ui_page_settings_stream_backend_from_global(g_ui_settings.sample_slot_selected,
                                                    &backend_slot) == 0U)
    {
        ui_page_settings_status("NO STREAM");
        return;
    }

    const sample_classic_slot_state_t state = storage_settings_get_classic_state(backend_slot);
    if (state == SAMPLE_CLASSIC_SLOT_EMPTY)
    {
        ui_page_settings_status("SLOT EMPTY");
        return;
    }

    const sample_global_slot_t *const slot = storage_settings_get_global_slot(backend_slot);
    if ((slot == 0) || (slot->path[0] == '\0'))
    {
        ui_page_settings_status("NO PATH");
        return;
    }

    if ((storage_settings_preview_active() != 0U) && (strcmp(storage_settings_preview_path(), slot->path) == 0))
    {
        ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_USER);
        return;
    }

    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    if (storage_settings_request_preview(slot->path) != 0U)
    {
        g_ui_settings.preview_stop_origin = UI_SETTINGS_PREVIEW_STOP_ORIGIN_NONE;
        g_ui_settings.preview_was_active = storage_settings_preview_active();
        ui_page_settings_status("PREVIEW ON");
    }
    else
    {
        ui_page_settings_status(ui_page_settings_preview_error_label(storage_settings_preview_last_error()));
    }
}

static void ui_page_settings_sample_preview_current(void)
{
    if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY)
    {
        ui_page_settings_sample_preview_left();
        return;
    }

    ui_page_settings_sample_preview_right();
}

static void ui_page_settings_sample_ok_action(void)
{
    ui_page_settings_sample_copy_left(0U);
}

static void ui_page_settings_sample_confirm_replace(uint16_t slot, const char *path)
{
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_REPLACE;
    g_ui_settings.confirm_slot = slot;
    (void)snprintf(g_ui_settings.confirm_path, sizeof(g_ui_settings.confirm_path), "%s", path);
    ui_page_settings_status("OK YES RETURN NO");
}

static void ui_page_settings_sample_request_replace(uint16_t slot, const char *path)
{
    if ((path == 0) || (path[0] == '\0'))
    {
        return;
    }

    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (ui_page_settings_stream_backend_from_global(slot, &backend_slot) == 0U)
    {
        ui_page_settings_status("SELECT STREAM");
        return;
    }

    ui_page_settings_sample_confirm_replace(backend_slot, path);
}

static void ui_page_settings_sample_confirm_clear(uint16_t slot)
{
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if ((ui_page_settings_stream_backend_from_global(slot, &backend_slot) == 0U)
        || (storage_settings_get_classic_state(backend_slot) == SAMPLE_CLASSIC_SLOT_EMPTY))
    {
        ui_page_settings_status("SLOT EMPTY");
        return;
    }

    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_CLEAR;
    g_ui_settings.confirm_slot = backend_slot;
    g_ui_settings.confirm_path[0] = '\0';
    ui_page_settings_status("OK YES RETURN NO");
}

static void ui_page_settings_sample_confirm_accept(void)
{
    if (storage_settings_project_replacement_active() != 0U)
    {
        ui_page_settings_status("BUSY");
        return;
    }
    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_CONVERT)
    {
        const uint16_t slot = g_ui_settings.confirm_slot;
        char path[SAMPLE_CLASSIC_PATH_MAX];
        if (strlen(g_ui_settings.confirm_path) >= sizeof(path))
        {
            g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
            g_ui_settings.confirm_path[0] = '\0';
            ui_page_settings_status("PATH TOO LONG");
            return;
        }
        ui_page_settings_copy_bounded(path, sizeof(path), g_ui_settings.confirm_path);
        g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
        g_ui_settings.confirm_path[0] = '\0';

        if ((seq_runtime_is_running() != 0U)
            || (seq_runtime_is_start_pending() != 0U)
            || (storage_settings_audio_recorder_active() != 0U))
        {
            ui_page_settings_status("STOP AUDIO TO CONVERT");
            return;
        }

        ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
        if (storage_settings_request_classic_with_conversion(slot, path) == 0U)
        {
            ui_page_settings_status(
                (storage_settings_sd_status() == SD_STORAGE_STATUS_NO_MEDIA)
                    ? "SD ABSENTE" : "CONVERT FAIL");
            return;
        }

        ui_page_settings_status("CONVERT 0%");
        return;
    }

    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_REPLACE)
    {
        const uint16_t slot = g_ui_settings.confirm_slot;
        char path[SAMPLE_CLASSIC_PATH_MAX];
        if (strlen(g_ui_settings.confirm_path) >= sizeof(path))
        {
            g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
            g_ui_settings.confirm_path[0] = '\0';
            ui_page_settings_status("PATH TOO LONG");
            return;
        }
        ui_page_settings_copy_bounded(path, sizeof(path), g_ui_settings.confirm_path);
        g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
        g_ui_settings.confirm_path[0] = '\0';
        ui_page_settings_sample_load_to_slot(slot, path);
        return;
    }

    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_CLEAR)
    {
        const uint16_t deleted_logical = g_ui_settings.sample_slot_selected;
        if (ui_page_settings_request_asset_remove(
                PERSIST_ASSET_SAMPLE_STREAM,
                deleted_logical,
                g_ui_settings.confirm_slot) == 0U)
        {
            ui_page_settings_status("CLEAR BUSY");
            return;
        }
        ui_page_settings_select_neighbor_before_delete();
        ui_page_settings_refresh_sampler_slots();
        g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
        ui_page_settings_status("CLEAR OK");
        return;
    }

    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARE)
    {
        const uint16_t slot = g_ui_settings.confirm_slot;
        char path[MULTI_SAMPLE_POOL_PATH_MAX];
        (void)snprintf(path, sizeof(path), "%s", g_ui_settings.confirm_path);
        ui_page_settings_multi_prepare_begin(slot,
                                             path,
                                             UI_SETTINGS_MULTI_PREP_PHASE_SCAN);

        const ui_settings_multi_entry_t *entry = ui_page_settings_multi_find_entry_by_path(path);
        if (entry == 0)
        {
            ui_page_settings_multi_prepare_finish(0);
            return;
        }
        if (storage_settings_begin_multi_import(slot, path,
                                                g_ui_settings.sample_dir) == 0U)
        {
            ui_page_settings_multi_prepare_finish(
                (storage_settings_sd_status() == SD_STORAGE_STATUS_NO_MEDIA)
                    ? "SD ABSENTE" : "LOAD FAIL");
            return;
        }
        return;
    }

    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_REPLACE)
    {
        const uint8_t slot = g_ui_settings.confirm_slot;
        char path[MULTI_SAMPLE_POOL_PATH_MAX];
        (void)snprintf(path, sizeof(path), "%s", g_ui_settings.confirm_path);
        const ui_settings_multi_entry_t *entry = ui_page_settings_multi_find_entry_by_path(path);
        if (entry == 0)
        {
            ui_page_settings_status("NO MULTI");
            return;
        }
        if (entry->prepared == 0U)
        {
            ui_page_settings_multi_prepare_begin(slot,
                                                 path,
                                                 UI_SETTINGS_MULTI_PREP_PHASE_SCAN);
            if (storage_settings_begin_multi_import(slot, path,
                                                    g_ui_settings.sample_dir) == 0U)
            {
                ui_page_settings_multi_prepare_finish(
                    (storage_settings_sd_status() == SD_STORAGE_STATUS_NO_MEDIA)
                        ? "SD ABSENTE" : "LOAD FAIL");
                return;
            }
            return;
        }
        entry = ui_page_settings_multi_find_entry_by_path(path);
        ui_page_settings_multi_load_entry_to_slot(slot, entry);
        return;
    }

    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_UNLOAD)
    {
        const uint16_t deleted_logical = g_ui_settings.sample_slot_selected;
        if (ui_page_settings_request_asset_remove(
                PERSIST_ASSET_MULTI,
                deleted_logical,
                g_ui_settings.confirm_slot) != 0U)
        {
            ui_page_settings_status("UNLOAD OK");
        }
        else
        {
            ui_page_settings_status("UNLOAD FAIL");
            return;
        }
        ui_page_settings_select_neighbor_before_delete();
        ui_page_settings_refresh_global_kind_slots(SAMPLE_GLOBAL_KIND_MULTI);
        g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
        return;
    }

    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_CLEAR_INDEX)
    {
        g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
        g_ui_settings.confirm_path[0] = '\0';

        if (storage_settings_project_replacement_active() != 0U)
        {
            ui_page_settings_status("BUSY");
            return;
        }

        if ((storage_settings_audio_recorder_active() != 0U)
            || (storage_settings_sample_cache_busy() != 0U)
            || (storage_settings_multi_load_pending() != 0U))
        {
            ui_page_settings_sd_busy_status();
            return;
        }

        for (uint16_t i = 0U; i < g_ui_settings.multi_entry_count; ++i)
        {
            const ui_settings_multi_entry_t *const entry = &g_ui_settings.multi_entries[i];
            if ((entry->type == UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM)
                && (entry->index_path[0] != '\0')
                && (ui_page_settings_multi_find_loaded_path(entry->index_path) >= 0))
            {
                ui_page_settings_status("UNLOAD FIRST");
                return;
            }
        }

        if (storage_settings_begin_multi_clear() == 0U)
        {
            ui_page_settings_status("CLEAR BUSY");
            return;
        }
        for (uint16_t i = 0U; i < g_ui_settings.multi_entry_count; ++i)
        {
            const ui_settings_multi_entry_t *const entry = &g_ui_settings.multi_entries[i];
            if ((entry->type == UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM)
                && (entry->index_path[0] != '\0')
                && (storage_settings_add_multi_clear_path(entry->index_path) == 0U))
            {
                storage_settings_cancel_multi();
                ui_page_settings_status("CLEAR BUSY");
                return;
            }
        }
        if (storage_settings_commit_multi_clear() == 0U)
        {
            storage_settings_cancel_multi();
            ui_page_settings_status("CLEAR BUSY");
            return;
        }
        ui_page_settings_status("CLEAR...");
        return;
    }

    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
}

static void ui_page_settings_sample_confirm_cancel(void)
{
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
    g_ui_settings.confirm_path[0] = '\0';
    ui_page_settings_status("CANCEL");
}

static void ui_page_settings_sample_copy_left(uint8_t shift_down)
{
    const ui_settings_sample_entry_t *const entry = ui_page_settings_sample_selected_entry();
    if (entry == 0)
    {
        ui_page_settings_status("NO WAV");
        return;
    }

    if (entry->type == UI_SETTINGS_SAMPLE_ENTRY_PARENT)
    {
        ui_page_settings_sample_browser_parent_or_exit();
        return;
    }

    if (entry->type == UI_SETTINGS_SAMPLE_ENTRY_DIR)
    {
        ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
        const uint16_t old_parent_id = g_ui_settings.sample_parent_id;
        char old_dir[WAV_LOADER_CATALOG_PATH_MAX];
        (void)snprintf(old_dir, sizeof(old_dir), "%s", g_ui_settings.sample_dir);
        (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", entry->path);
        g_ui_settings.sample_parent_id = entry->catalog_index;
        g_ui_settings.sample_selected = 0U;
        if ((ui_page_settings_sample_browser_refresh() == 0U)
            && (storage_settings_catalog_view_busy() == 0U))
        {
            g_ui_settings.sample_parent_id = old_parent_id;
            (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", old_dir);
        }
        return;
    }

    if (shift_down != 0U)
    {
        ui_page_settings_sample_request_replace(g_ui_settings.sample_slot_selected, entry->path);
        return;
    }

    const int16_t free_slot = ui_page_settings_sample_find_free_slot();
    if (free_slot < 0)
    {
        ui_page_settings_status("POOL FULL");
        return;
    }

    ui_page_settings_sample_load_to_slot((uint16_t)free_slot, entry->path);
}

static void ui_page_settings_sample_copy_right(uint8_t shift_down)
{
    if (shift_down != 0U)
    {
        const ui_settings_sample_entry_t *const entry = ui_page_settings_sample_selected_entry();
        if ((entry == 0) || (entry->type != UI_SETTINGS_SAMPLE_ENTRY_FILE))
        {
            ui_page_settings_status("SELECT WAV");
            return;
        }
        ui_page_settings_sample_request_replace(g_ui_settings.sample_slot_selected, entry->path);
        return;
    }

    ui_page_settings_sample_confirm_clear(g_ui_settings.sample_slot_selected);
}

static void ui_page_settings_ram_request_replace(uint16_t global_slot, const char *path)
{
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if ((path == 0) || (path[0] == '\0'))
    {
        return;
    }
    if (ui_page_settings_ram_backend_from_global(global_slot, &backend_slot) == 0U)
    {
        ui_page_settings_status("SELECT RAM");
        return;
    }
    ui_page_settings_ram_load_to_slot(backend_slot, path);
}

static void ui_page_settings_ram_copy_left(uint8_t shift_down)
{
    const ui_settings_sample_entry_t *const entry = ui_page_settings_sample_selected_entry();
    if (entry == 0)
    {
        ui_page_settings_status("NO WAV");
        return;
    }

    if (entry->type == UI_SETTINGS_SAMPLE_ENTRY_PARENT)
    {
        ui_page_settings_sample_browser_parent_or_exit();
        return;
    }

    if (entry->type == UI_SETTINGS_SAMPLE_ENTRY_DIR)
    {
        ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
        const uint16_t old_parent_id = g_ui_settings.sample_parent_id;
        char old_dir[WAV_LOADER_CATALOG_PATH_MAX];
        (void)snprintf(old_dir, sizeof(old_dir), "%s", g_ui_settings.sample_dir);
        (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", entry->path);
        g_ui_settings.sample_parent_id = entry->catalog_index;
        g_ui_settings.sample_selected = 0U;
        if ((ui_page_settings_sample_browser_refresh() == 0U)
            && (storage_settings_catalog_view_busy() == 0U))
        {
            g_ui_settings.sample_parent_id = old_parent_id;
            (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", old_dir);
        }
        return;
    }

    if (shift_down != 0U)
    {
        ui_page_settings_ram_request_replace(g_ui_settings.sample_slot_selected, entry->path);
        return;
    }

    if (storage_settings_global_entries_used() >= storage_settings_global_entry_capacity())
    {
        ui_page_settings_flash_sample_header_slots();
        ui_page_settings_status("POOL FULL");
        return;
    }

    const int16_t free_slot = ui_page_settings_ram_find_free_backend_slot();
    if (free_slot < 0)
    {
        ui_page_settings_status("RAM FULL");
        return;
    }

    ui_page_settings_ram_load_to_slot((uint16_t)free_slot, entry->path);
}

static void ui_page_settings_ram_copy_right(uint8_t shift_down)
{
    if (shift_down != 0U)
    {
        const ui_settings_sample_entry_t *const entry = ui_page_settings_sample_selected_entry();
        if ((entry == 0) || (entry->type != UI_SETTINGS_SAMPLE_ENTRY_FILE))
        {
            ui_page_settings_status("SELECT WAV");
            return;
        }
        ui_page_settings_ram_request_replace(g_ui_settings.sample_slot_selected, entry->path);
        return;
    }

    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (ui_page_settings_ram_backend_from_global(g_ui_settings.sample_slot_selected,
                                                 &backend_slot) == 0U)
    {
        ui_page_settings_status("SELECT RAM");
        return;
    }
    const uint16_t deleted_logical = g_ui_settings.sample_slot_selected;
    if (ui_page_settings_request_asset_remove(
            PERSIST_ASSET_SAMPLE_RAM, deleted_logical, backend_slot) == 0U)
    {
        ui_page_settings_status("CLEAR BUSY");
        return;
    }
    ui_page_settings_select_neighbor_before_delete();
    ui_page_settings_refresh_ram_slots();
    ui_page_settings_status("CLEAR OK");
}

static void ui_page_settings_wavetable_request_replace(uint16_t global_slot, const char *path)
{
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if ((path == 0) || (path[0] == '\0'))
    {
        return;
    }
    if (ui_page_settings_wavetable_backend_from_global(global_slot, &backend_slot) == 0U)
    {
        ui_page_settings_status("SELECT WAVE");
        return;
    }
    ui_page_settings_wavetable_load_to_slot(backend_slot, path);
}

static void ui_page_settings_wavetable_copy_left(uint8_t shift_down)
{
    const ui_settings_sample_entry_t *const entry = ui_page_settings_sample_selected_entry();
    if (entry == 0)
    {
        ui_page_settings_status("NO WAV");
        return;
    }

    if (entry->type == UI_SETTINGS_SAMPLE_ENTRY_PARENT)
    {
        ui_page_settings_wavetable_browser_parent_or_exit();
        return;
    }

    if (entry->type == UI_SETTINGS_SAMPLE_ENTRY_DIR)
    {
        char restore_dir[WAV_LOADER_CATALOG_PATH_MAX];
        (void)snprintf(restore_dir, sizeof(restore_dir), "%s", g_ui_settings.sample_dir);
        (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", entry->path);
        g_ui_settings.sample_selected = 0U;
        g_ui_settings.sample_left_scroll = 0U;
        if (ui_page_settings_wavetable_browser_refresh() == 0U)
        {
            (void)snprintf(g_ui_settings.sample_dir,
                           sizeof(g_ui_settings.sample_dir),
                           "%s",
                           restore_dir);
            (void)ui_page_settings_wavetable_browser_refresh();
        }
        return;
    }

    if (shift_down != 0U)
    {
        ui_page_settings_wavetable_request_replace(g_ui_settings.sample_slot_selected, entry->path);
        return;
    }

    if (storage_settings_global_entries_used() >= storage_settings_global_entry_capacity())
    {
        ui_page_settings_flash_sample_header_slots();
        ui_page_settings_status("POOL FULL");
        return;
    }

    const int16_t free_slot = ui_page_settings_wavetable_find_free_backend_slot();
    if (free_slot < 0)
    {
        ui_page_settings_status("WAVE FULL");
        return;
    }

    ui_page_settings_wavetable_load_to_slot((uint16_t)free_slot, entry->path);
}

static void ui_page_settings_wavetable_copy_right(uint8_t shift_down)
{
    if (shift_down != 0U)
    {
        const ui_settings_sample_entry_t *const entry = ui_page_settings_sample_selected_entry();
        if ((entry == 0) || (entry->type != UI_SETTINGS_SAMPLE_ENTRY_FILE))
        {
            ui_page_settings_status("SELECT WAV");
            return;
        }
        ui_page_settings_wavetable_request_replace(g_ui_settings.sample_slot_selected, entry->path);
        return;
    }

    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (ui_page_settings_wavetable_backend_from_global(g_ui_settings.sample_slot_selected,
                                                       &backend_slot) == 0U)
    {
        ui_page_settings_status("SELECT WAVE");
        return;
    }
    const uint16_t deleted_logical = g_ui_settings.sample_slot_selected;
    if (ui_page_settings_request_asset_remove(
            PERSIST_ASSET_WAVETABLE, deleted_logical, backend_slot) == 0U)
    {
        ui_page_settings_status("CLEAR BUSY");
        return;
    }
    ui_page_settings_select_neighbor_before_delete();
    ui_page_settings_refresh_wavetable_slots();
    ui_page_settings_status("CLEAR OK");
}

