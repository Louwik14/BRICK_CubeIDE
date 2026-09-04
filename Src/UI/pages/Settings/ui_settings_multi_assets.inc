static const ui_settings_multi_entry_t *ui_page_settings_multi_selected_entry(void)
{
    if (g_ui_settings.sample_selected >= g_ui_settings.multi_entry_count)
    {
        return 0;
    }
    return &g_ui_settings.multi_entries[g_ui_settings.sample_selected];
}

static const ui_settings_multi_entry_t *ui_page_settings_multi_find_entry_by_path(const char *path)
{
    if ((path == 0) || (path[0] == '\0'))
    {
        return 0;
    }
    for (uint8_t i = 0U; i < g_ui_settings.multi_entry_count; ++i)
    {
        if (strcmp(g_ui_settings.multi_entries[i].path, path) == 0)
        {
            return &g_ui_settings.multi_entries[i];
        }
    }
    return 0;
}

static uint16_t ui_page_settings_multi_required_slots(const ui_settings_multi_entry_t *entry)
{
    if (entry == 0)
    {
        return 0U;
    }
    return (entry->slot_cost != 0U) ? entry->slot_cost : entry->wav_count;
}

static uint32_t ui_page_settings_multi_required_cost_bytes(const ui_settings_multi_entry_t *entry)
{
    const uint32_t slot_bytes = ui_page_settings_multi_slot_bytes();
    return (uint32_t)ui_page_settings_multi_required_slots(entry) * slot_bytes;
}

static uint8_t ui_page_settings_multi_has_slot_capacity(const ui_settings_multi_entry_t *entry,
                                                        uint8_t target_slot)
{
    return sample_global_pool_validate_budget(SAMPLE_GLOBAL_KIND_MULTI,
                                              target_slot,
                                              ui_page_settings_multi_required_cost_bytes(entry));
}

static void ui_page_settings_flash_sample_header_slots(void)
{
    g_ui_settings.header_slot_flash_until_ms =
        HAL_GetTick() + UI_SETTINGS_HEADER_FLASH_DURATION_MS;
}

static void ui_page_settings_multi_prepare_begin(uint8_t slot,
                                                 const char *path,
                                                 ui_settings_multi_prepare_phase_t phase)
{
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARING;
    g_ui_settings.confirm_slot = slot;
    if (path != 0)
    {
        ui_page_settings_copy_bounded(g_ui_settings.confirm_path,
                                      sizeof(g_ui_settings.confirm_path),
                                      path);
    }
    g_ui_settings.multi_prepare_phase = (uint8_t)phase;
    g_ui_settings.multi_prepare_progress_done = 0U;
    g_ui_settings.multi_prepare_progress_total = 1U;
    g_ui_settings.status_line[0] = '\0';
    ui_page_settings_multi_prepare_flush_progress();
}

static void ui_page_settings_multi_prepare_finish(const char *status)
{
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE;
    g_ui_settings.confirm_path[0] = '\0';
    g_ui_settings.multi_prepare_phase = (uint8_t)UI_SETTINGS_MULTI_PREP_PHASE_NONE;
    g_ui_settings.multi_prepare_progress_done = 0U;
    g_ui_settings.multi_prepare_progress_total = 0U;
    if (status != 0)
    {
        ui_page_settings_status(status);
    }
}

static const char *ui_page_settings_multi_import_error_label(
    multi_sample_import_result_t result);

static void ui_page_settings_multi_prepare_poll(void)
{
    if (g_ui_settings.sample_confirm != (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARING)
    {
        return;
    }

    const ui_settings_multi_prepare_phase_t phase =
        (ui_settings_multi_prepare_phase_t)g_ui_settings.multi_prepare_phase;
    if (phase == UI_SETTINGS_MULTI_PREP_PHASE_COMMIT)
    {
        if (multi_sample_pool_get_state(g_ui_settings.confirm_slot)
            != MULTI_SAMPLE_INSTRUMENT_EMPTY)
        {
            return;
        }
        const ui_settings_multi_entry_t *const entry =
            ui_page_settings_multi_find_entry_by_path(g_ui_settings.confirm_path);
        if ((entry == 0) || (entry->index_path[0] == '\0'))
        {
            ui_page_settings_multi_prepare_finish("NO INDEX");
            return;
        }
        const multi_sample_load_result_t result =
            multi_sample_load_request_instrument(MULTI_SAMPLE_POOL_INVALID_ID,
                                                  entry->index_path,
                                                  g_ui_settings.confirm_slot);
        if ((result != MULTI_SAMPLE_LOAD_OK)
            && (result != MULTI_SAMPLE_LOAD_ALREADY_READY))
        {
            ui_page_settings_multi_prepare_finish(
                ui_page_settings_multi_load_error_label(result));
            return;
        }
        g_ui_settings.multi_prepare_phase = (uint8_t)UI_SETTINGS_MULTI_PREP_PHASE_PREPARE;
        g_ui_settings.multi_prepare_progress_done = 0U;
        g_ui_settings.multi_prepare_progress_total = 1U;
        ui_page_settings_multi_prepare_poll();
        return;
    }

    if (phase == UI_SETTINGS_MULTI_PREP_PHASE_SCAN)
    {
        g_ui_settings.multi_prepare_progress_done = multi_sample_import_progress_done();
        g_ui_settings.multi_prepare_progress_total = multi_sample_import_progress_total();
        if (multi_sample_import_is_busy() != 0U)
        {
            return;
        }
        const multi_sample_import_result_t import_result =
            multi_sample_import_get_last_result();
        if (import_result != MULTI_SAMPLE_IMPORT_OK)
        {
            ui_page_settings_multi_prepare_finish(
                ui_page_settings_multi_import_error_label(import_result));
            return;
        }
        (void)ui_page_settings_multi_browser_refresh();
        const ui_settings_multi_entry_t *const entry =
            ui_page_settings_multi_find_entry_by_path(g_ui_settings.confirm_path);
        if ((entry == 0) || (entry->index_path[0] == '\0'))
        {
            ui_page_settings_multi_prepare_finish("NO INDEX");
            return;
        }
        ui_page_settings_multi_load_entry_to_slot((uint8_t)g_ui_settings.confirm_slot,
                                                   entry);
        return;
    }

    multi_sample_load_diag_t diag;
    multi_sample_get_load_diag(&diag);
    const multi_sample_instrument_state_t state =
        multi_sample_pool_get_state(g_ui_settings.confirm_slot);

    if (diag.state == MULTI_SAMPLE_INSTRUMENT_ERROR)
    {
        ui_page_settings_multi_prepare_finish(
            ui_page_settings_multi_load_error_label(diag.last_error));
        return;
    }

    if (state == MULTI_SAMPLE_INSTRUMENT_LOADING)
    {
        g_ui_settings.multi_prepare_phase = (uint8_t)UI_SETTINGS_MULTI_PREP_PHASE_PREPARE;
        g_ui_settings.multi_prepare_progress_done = diag.pages_ready;
        g_ui_settings.multi_prepare_progress_total =
            (diag.pages_requested == 0U) ? 1U : diag.pages_requested;
        return;
    }

    if (state == MULTI_SAMPLE_INSTRUMENT_READY)
    {
        g_ui_settings.multi_prepare_phase = (uint8_t)UI_SETTINGS_MULTI_PREP_PHASE_REFRESH;
        g_ui_settings.multi_prepare_progress_done = 1U;
        g_ui_settings.multi_prepare_progress_total = 1U;
        ui_page_settings_refresh_global_kind_slots(SAMPLE_GLOBAL_KIND_MULTI);
        const multi_sample_instrument_t *instrument =
            multi_sample_pool_get_instrument(g_ui_settings.confirm_slot);
        uint16_t logical = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        if ((instrument != NULL)
            && (project_control_find_asset(PERSIST_ASSET_MULTI,
                                           instrument->index_path,
                                           &logical) != 0U))
            g_ui_settings.sample_slot_selected = logical;
        (void)ui_page_settings_multi_browser_refresh();
        ui_page_settings_multi_prepare_finish("LOAD OK");
        return;
    }

    if (state == MULTI_SAMPLE_INSTRUMENT_ERROR)
    {
        ui_page_settings_multi_prepare_finish(ui_page_settings_multi_load_error_label(diag.last_error));
    }
}

static uint32_t ui_page_settings_multi_slot_bytes(void)
{
    return SAMPLE_PREP_MULTI_START_SLOT_PAGES * SAMPLE_PAGE_BYTES;
}

static uint32_t ui_page_settings_global_memory_used_bytes(void)
{
    return sample_global_pool_get_used_bytes();
}

static uint16_t ui_page_settings_global_entry_count_used(void)
{
    return sample_global_pool_get_used_entries();
}

static void ui_page_settings_draw_global_sample_header(const char *title)
{
    ui_page_settings_draw_sample_header(title,
                                        ui_page_settings_global_entry_count_used(),
                                        sample_global_pool_get_entry_capacity(),
                                        ui_page_settings_global_memory_used_bytes(),
                                        SAMPLE_GLOBAL_POOL_BUDGET_BYTES);
}

static void ui_page_settings_draw_multi_sample_header(void)
{
    const uint32_t slot_bytes = ui_page_settings_multi_slot_bytes();
    const uint16_t total_slots = (uint16_t)(SAMPLE_GLOBAL_POOL_BUDGET_BYTES / slot_bytes);
    const uint16_t used_slots = multi_sample_pool_get_slot_capacity_used();
    ui_page_settings_draw_sample_header("MULTI",
                                        used_slots,
                                        total_slots,
                                        ui_page_settings_global_memory_used_bytes(),
                                        SAMPLE_GLOBAL_POOL_BUDGET_BYTES);
}

static void ui_page_settings_format_mb(char *out, uint32_t out_size, uint32_t bytes)
{
    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    const uint32_t deci_mb = (uint32_t)(((uint64_t)bytes * 10ULL
                                         + (UI_SETTINGS_MB_BYTES / 2U))
                                        / UI_SETTINGS_MB_BYTES);
    (void)snprintf(out,
                   out_size,
                   "%lu.%lu",
                   (unsigned long)(deci_mb / 10U),
                   (unsigned long)(deci_mb % 10U));
}

static void ui_page_settings_draw_sample_header(const char *title,
                                                uint16_t used_slots,
                                                uint16_t max_slots,
                                                uint32_t used_bytes,
                                                uint32_t max_bytes)
{
    char used_mb[8];
    char max_mb[8];
    char slots[12];
    char mem[20];

    ui_page_settings_format_mb(used_mb, sizeof(used_mb), used_bytes);
    ui_page_settings_format_mb(max_mb, sizeof(max_mb), max_bytes);
    (void)snprintf(slots,
                   sizeof(slots),
                   "%u/%u",
                   (unsigned)used_slots,
                   (unsigned)max_slots);
    (void)snprintf(mem,
                   sizeof(mem),
                   "%s/%sMB",
                   used_mb,
                   max_mb);

    drv_display_set_font(&FONT_4X6);
    drv_display_draw_text(0U, 0U, title);
    drv_display_draw_line(UI_SETTINGS_HEADER_SEP1_X, 0, UI_SETTINGS_HEADER_SEP1_X, 5);
    const uint8_t slots_w = drv_display_text_width(slots);
    const uint32_t now = HAL_GetTick();
    const uint8_t flash_slots =
        ((int32_t)(g_ui_settings.header_slot_flash_until_ms - now) > 0)
        && (((now / 120U) & 1U) == 0U)
            ? 1U
            : 0U;
    if (flash_slots != 0U)
    {
        drv_display_fill_rect(UI_SETTINGS_HEADER_SLOT_X - 1U, 0, slots_w + 2U, 6);
        drv_display_draw_text_inverted(UI_SETTINGS_HEADER_SLOT_X, 0U, slots);
    }
    else
    {
        drv_display_draw_text(UI_SETTINGS_HEADER_SLOT_X, 0U, slots);
    }
    const uint8_t sep2_x = (uint8_t)(UI_SETTINGS_HEADER_SLOT_X + slots_w + 4U);
    drv_display_draw_line(sep2_x, 0, sep2_x, 5);
    const uint8_t mem_w = drv_display_text_width(mem);
    const uint8_t mem_x = (mem_w >= UI_SETTINGS_HEADER_MEM_RIGHT_X)
        ? 0U
        : (uint8_t)(UI_SETTINGS_HEADER_MEM_RIGHT_X - mem_w);
    const uint8_t flash_mem =
        ((int32_t)(g_ui_settings.header_mem_flash_until_ms - now) > 0)
        && (((now / 120U) & 1U) == 0U)
            ? 1U
            : 0U;
    if (flash_mem != 0U)
    {
        drv_display_fill_rect((mem_x > 0U) ? (uint8_t)(mem_x - 1U) : 0U,
                              0,
                              mem_w + 2U,
                              6);
        drv_display_draw_text_inverted(mem_x, 0U, mem);
    }
    else
    {
        drv_display_draw_text(mem_x, 0U, mem);
    }
}

static void ui_page_settings_append_u16(char *out, uint32_t out_size, uint16_t value)
{
    char digits[5];
    uint8_t digit_count = 0U;

    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    do
    {
        digits[digit_count++] = (char)('0' + (value % 10U));
        value = (uint16_t)(value / 10U);
    } while ((value != 0U) && (digit_count < sizeof(digits)));

    const uint32_t len = (uint32_t)strlen(out);
    uint32_t pos = len;
    while ((digit_count != 0U) && ((pos + 1U) < out_size))
    {
        out[pos++] = digits[--digit_count];
    }
    out[pos] = '\0';
}

static int16_t ui_page_settings_multi_find_free_slot(void)
{
    for (uint8_t slot = 0U; slot < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++slot)
    {
        if (multi_sample_pool_get_state(slot) == MULTI_SAMPLE_INSTRUMENT_EMPTY)
        {
            return (int16_t)slot;
        }
    }
    return -1;
}

static int16_t ui_page_settings_multi_find_loaded_path(const char *index_path)
{
    if ((index_path == 0) || (index_path[0] == '\0'))
    {
        return -1;
    }

    for (uint8_t slot = 0U; slot < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++slot)
    {
        const multi_sample_instrument_t *const instrument = multi_sample_pool_get_instrument(slot);
        if ((instrument != 0) && (instrument->index_path[0] != '\0')
            && (strcmp(instrument->index_path, index_path) == 0))
        {
            return (int16_t)slot;
        }
    }
    return -1;
}

static uint8_t ui_page_settings_multi_register_global(uint16_t instrument_id)
{
    if ((instrument_id != MULTI_SAMPLE_POOL_INVALID_ID)
            && (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS))
    {
        return 0U;
    }

    return ui_page_settings_request_asset_register(PERSIST_ASSET_MULTI,
                                                   instrument_id);
}

static const char *ui_page_settings_multi_import_error_label(multi_sample_import_result_t result)
{
    switch (result)
    {
        case MULTI_SAMPLE_IMPORT_SD_BUSY:
            return "SD BUSY";
        case MULTI_SAMPLE_IMPORT_SD_MOUNT_FAIL:
            return "SD UNAVAILABLE";
        case MULTI_SAMPLE_IMPORT_OPEN_DIR_FAIL:
            return "NO SAMPLES";
        case MULTI_SAMPLE_IMPORT_NO_WAV:
            return "NO WAV";
        case MULTI_SAMPLE_IMPORT_TOO_MANY_SAMPLES:
            return "TOO MANY";
        case MULTI_SAMPLE_IMPORT_PATH_TOO_LONG:
            return "PATH LONG";
        case MULTI_SAMPLE_IMPORT_WAV_OPEN_FAIL:
            return "OPEN FAIL";
        case MULTI_SAMPLE_IMPORT_WAV_PARSE_FAIL:
            return "WAV INVALID";
        case MULTI_SAMPLE_IMPORT_WAV_UNSUPPORTED:
            return "WAV UNSUPP";
        case MULTI_SAMPLE_IMPORT_DUPLICATE_ZONE:
        {
            const char *const diag = multi_sample_import_get_last_diagnostic();
            return ((diag != 0) && (diag[0] != '\0')) ? diag : "DUP ZONE";
        }
        case MULTI_SAMPLE_IMPORT_ZONE_LIMIT:
            return "ZONE FULL";
        case MULTI_SAMPLE_IMPORT_INDEX_WRITE_FAIL:
            return "INDEX FAIL";
        default:
            return "PREP FAIL";
    }
}

static const char *ui_page_settings_multi_load_error_label(multi_sample_load_result_t result)
{
    switch (result)
    {
        case MULTI_SAMPLE_LOAD_ALREADY_READY:
            return "ALREADY";
        case MULTI_SAMPLE_LOAD_SD_BUSY:
            return "SD BUSY";
        case MULTI_SAMPLE_LOAD_INDEX_FAIL:
            return "INDEX FAIL";
        case MULTI_SAMPLE_LOAD_POOL_FAIL:
            return "POOL FAIL";
        case MULTI_SAMPLE_LOAD_PATH_TOO_LONG:
            return "PATH LONG";
        case MULTI_SAMPLE_LOAD_REGISTER_FAIL:
            return "REGISTER FAIL";
        case MULTI_SAMPLE_LOAD_NOT_ENOUGH_CACHE:
            return "CACHE FULL";
        case MULTI_SAMPLE_LOAD_PAGE_ERROR:
            return "PAGE FAIL";
        case MULTI_SAMPLE_LOAD_TRANSPORT_ACTIVE:
            return "STOP AUDIO";
        case MULTI_SAMPLE_LOAD_CANCELLED:
            return "CANCEL";
        case MULTI_SAMPLE_LOAD_INVALID_ARG:
            return "INVALID LOAD";
        case MULTI_SAMPLE_LOAD_INDEX_LIMIT:
            return "INDEX LIMIT";
        case MULTI_SAMPLE_LOAD_FORMAT_MISMATCH:
            return "FORMAT FAIL";
        case MULTI_SAMPLE_LOAD_PREP_BUDGET_EXCEEDED:
            return "PREP FULL";
        default:
            return "LOAD FAIL";
    }
}

static uint8_t ui_page_settings_multi_prepare_entry(const ui_settings_multi_entry_t *entry)
{
    if (project_replacement_is_active() != 0U) return 0U;
    if (entry == 0)
    {
        return 0U;
    }
    if (entry->prepared != 0U)
        return 2U;
    return multi_sample_import_request_folder(entry->path);
}

static void ui_page_settings_multi_confirm_prepare(uint8_t slot)
{
    if (project_replacement_is_active() != 0U) return;
    const ui_settings_multi_entry_t *const entry = ui_page_settings_multi_selected_entry();
    if ((entry == 0) || (entry->type != UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM))
    {
        return;
    }
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARE;
    g_ui_settings.confirm_slot = slot;
    ui_page_settings_copy_bounded(g_ui_settings.confirm_path,
                                  sizeof(g_ui_settings.confirm_path),
                                  entry->path);
    g_ui_settings.status_line[0] = '\0';
}

static void ui_page_settings_multi_confirm_replace(uint8_t slot)
{
    if (project_replacement_is_active() != 0U) return;
    const ui_settings_multi_entry_t *const entry = ui_page_settings_multi_selected_entry();
    if (entry == 0)
    {
        return;
    }
    if (ui_page_settings_multi_has_slot_capacity(entry, slot) == 0U)
    {
        ui_page_settings_flash_sample_header_slots();
        ui_page_settings_status("POOL FULL");
        return;
    }
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_REPLACE;
    g_ui_settings.confirm_slot = slot;
    ui_page_settings_copy_bounded(g_ui_settings.confirm_path,
                                  sizeof(g_ui_settings.confirm_path),
                                  entry->path);
    ui_page_settings_status("OK=YES RETURN=NO");
}

static void ui_page_settings_multi_confirm_unload(uint8_t slot)
{
    if (project_replacement_is_active() != 0U) return;
    if (multi_sample_pool_get_state(slot) == MULTI_SAMPLE_INSTRUMENT_EMPTY)
    {
        ui_page_settings_status("SLOT EMPTY");
        return;
    }
    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_UNLOAD;
    g_ui_settings.confirm_slot = slot;
    g_ui_settings.confirm_path[0] = '\0';
    ui_page_settings_status("OK=YES RETURN=NO");
}

static void ui_page_settings_multi_confirm_clear_indexes(void)
{
    if (project_replacement_is_active() != 0U) return;
    if (g_ui_settings.multi_entry_count == 0U)
    {
        ui_page_settings_status("NO MULTI");
        return;
    }

    uint8_t has_index_target = 0U;
    for (uint16_t i = 0U; i < g_ui_settings.multi_entry_count; ++i)
    {
        const ui_settings_multi_entry_t *const entry = &g_ui_settings.multi_entries[i];
        if ((entry->type == UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM)
            && (entry->index_path[0] != '\0'))
        {
            has_index_target = 1U;
            break;
        }
    }

    if (has_index_target == 0U)
    {
        ui_page_settings_status("NO MULTI");
        return;
    }

    g_ui_settings.sample_confirm = (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_CLEAR_INDEX;
    g_ui_settings.confirm_slot = 0U;
    ui_page_settings_copy_bounded(g_ui_settings.confirm_path,
                                  sizeof(g_ui_settings.confirm_path),
                                  g_ui_settings.sample_dir);
    ui_page_settings_status("OK=YES RETURN=NO");
}

static void ui_page_settings_multi_load_entry_to_slot(uint8_t slot, const ui_settings_multi_entry_t *entry)
{
    if (project_replacement_is_active() != 0U) return;
    const uint8_t already_blocking =
        (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARING) ? 1U : 0U;

    if ((entry == 0)
        || (entry->type != UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM)
        || (entry->index_path[0] == '\0'))
    {
        ui_page_settings_status("NO INDEX");
        return;
    }

    uint16_t registered_logical = 0U;
    uint16_t registered_runtime = 0U;
    if ((project_control_find_asset(PERSIST_ASSET_MULTI,entry->index_path,&registered_logical) != 0U)
        && (project_control_resolve_multi_runtime(registered_logical,&registered_runtime) != 0U))
    {
        g_ui_settings.sample_slot_selected = registered_logical;
        ui_page_settings_status("ALREADY LOADED");
        return;
    }

    const int16_t existing = ui_page_settings_multi_find_loaded_path(entry->index_path);
    if (existing >= 0)
    {
        (void)ui_page_settings_multi_register_global((uint16_t)existing);
        ui_page_settings_status("REUSED");
        return;
    }

    if (ui_page_settings_multi_has_slot_capacity(entry, slot) == 0U)
    {
        char status[24];
        ui_page_settings_copy_bounded(status, sizeof(status), "FULL need ");
        ui_page_settings_append_u16(status,
                                    sizeof(status),
                                    ui_page_settings_multi_required_slots(entry));
        ui_page_settings_flash_sample_header_slots();
        ui_page_settings_status(status);
        return;
    }

    /* Gate before stopping voices, clearing a slot or changing UI load state. */
    if ((seq_runtime_is_running() != 0U)
        || (seq_runtime_is_start_pending() != 0U))
    {
        if (already_blocking != 0U)
        {
            ui_page_settings_multi_prepare_finish("STOP AUDIO");
        }
        else
        {
            ui_page_settings_status("STOP AUDIO");
        }
        return;
    }

    if (already_blocking == 0U)
    {
        ui_page_settings_multi_prepare_begin(slot,
                                             entry->path,
                                             UI_SETTINGS_MULTI_PREP_PHASE_COMMIT);
    }
    else
    {
        g_ui_settings.confirm_slot = slot;
        g_ui_settings.multi_prepare_phase = (uint8_t)UI_SETTINGS_MULTI_PREP_PHASE_COMMIT;
        g_ui_settings.multi_prepare_progress_done = 0U;
        g_ui_settings.multi_prepare_progress_total = 1U;
        ui_page_settings_multi_prepare_flush_progress();
    }

    if (multi_sample_pool_get_state(slot) != MULTI_SAMPLE_INSTRUMENT_EMPTY)
    {
        if (ui_page_settings_request_asset_remove(
                PERSIST_ASSET_MULTI,
                g_ui_settings.sample_slot_selected,
                slot) == 0U)
        {
            ui_page_settings_multi_prepare_finish("CLEAR BUSY");
            return;
        }
    }
    ui_page_settings_multi_prepare_poll();
}

static void ui_page_settings_multi_load_selected_to_slot(uint8_t slot)
{
    const ui_settings_multi_entry_t *entry = ui_page_settings_multi_selected_entry();
    if (entry == 0)
    {
        ui_page_settings_status("NO MULTI");
        return;
    }
    if (entry->type != UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM)
    {
        ui_page_settings_status("NO MULTI");
        return;
    }

    if (entry->prepared == 0U)
    {
        if (ui_page_settings_multi_has_slot_capacity(entry, slot) == 0U)
        {
            ui_page_settings_flash_sample_header_slots();
            ui_page_settings_status("POOL FULL");
            return;
        }
        ui_page_settings_multi_confirm_prepare(slot);
        return;
    }
    ui_page_settings_multi_load_entry_to_slot(slot, entry);
}

static uint8_t ui_page_settings_multi_enter_selected_folder(const ui_settings_multi_entry_t *entry)
{
    char old_dir[WAV_LOADER_CATALOG_PATH_MAX];

    if ((entry == 0)
        || ((entry->type != UI_SETTINGS_MULTI_ENTRY_NAV_FOLDER)
            && (entry->type != UI_SETTINGS_MULTI_ENTRY_EMPTY_FOLDER)))
    {
        return 0U;
    }

    (void)snprintf(old_dir, sizeof(old_dir), "%s", g_ui_settings.sample_dir);
    (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", entry->path);
    g_ui_settings.sample_selected = 0U;
    g_ui_settings.sample_left_scroll = 0U;
    if (ui_page_settings_multi_browser_refresh() == 0U)
    {
        (void)snprintf(g_ui_settings.sample_dir, sizeof(g_ui_settings.sample_dir), "%s", old_dir);
        (void)ui_page_settings_multi_browser_refresh();
        return 0U;
    }
    if (g_ui_settings.multi_entry_count == 0U)
    {
        ui_page_settings_status("EMPTY");
    }
    return 1U;
}

static void ui_page_settings_multi_copy_left(uint8_t shift_down)
{
    const ui_settings_multi_entry_t *const entry = ui_page_settings_multi_selected_entry();
    if (entry == 0)
    {
        ui_page_settings_status("NO MULTI");
        return;
    }

    if (ui_page_settings_multi_enter_selected_folder(entry) != 0U)
    {
        return;
    }

    if (shift_down != 0U)
    {
        uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        if (ui_page_settings_multi_backend_from_global(g_ui_settings.sample_slot_selected,
                                                       &backend_slot) == 0U)
        {
            ui_page_settings_status("SELECT MULTI");
            return;
        }
        if (multi_sample_pool_get_state(backend_slot) != MULTI_SAMPLE_INSTRUMENT_EMPTY)
        {
            ui_page_settings_multi_confirm_replace((uint8_t)backend_slot);
            return;
        }
        ui_page_settings_multi_load_selected_to_slot((uint8_t)backend_slot);
        return;
    }

    const int16_t existing = ui_page_settings_multi_find_loaded_path(entry->index_path);
    if (existing >= 0)
    {
        (void)ui_page_settings_multi_register_global((uint16_t)existing);
        ui_page_settings_status("REUSED");
        return;
    }

    const int16_t free_slot = ui_page_settings_multi_find_free_slot();
    if (free_slot < 0)
    {
        ui_page_settings_flash_sample_header_slots();
        ui_page_settings_status("POOL FULL");
        return;
    }
    ui_page_settings_multi_load_selected_to_slot((uint8_t)free_slot);
}

static void ui_page_settings_multi_copy_right(uint8_t shift_down)
{
    if (shift_down != 0U)
    {
        uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        if (ui_page_settings_multi_backend_from_global(g_ui_settings.sample_slot_selected,
                                                       &backend_slot) == 0U)
        {
            ui_page_settings_status("SELECT MULTI");
            return;
        }
        if (multi_sample_pool_get_state(backend_slot) != MULTI_SAMPLE_INSTRUMENT_EMPTY)
        {
            ui_page_settings_multi_confirm_replace((uint8_t)backend_slot);
            return;
        }
        ui_page_settings_multi_load_selected_to_slot((uint8_t)backend_slot);
        return;
    }

    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (ui_page_settings_multi_backend_from_global(g_ui_settings.sample_slot_selected,
                                                   &backend_slot) == 0U)
    {
        ui_page_settings_status("SELECT MULTI");
        return;
    }
    ui_page_settings_multi_confirm_unload((uint8_t)backend_slot);
}

