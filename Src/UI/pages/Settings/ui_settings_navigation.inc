static const char *ui_page_settings_view_title(ui_settings_view_t view)
{
    switch (view)
    {
        case UI_SETTINGS_VIEW_ROOT:
            return "SETTINGS";
        case UI_SETTINGS_VIEW_PROJECT:
            return "PROJECT";
        case UI_SETTINGS_VIEW_SAMPLE:
            return "SAMPLE";
        case UI_SETTINGS_VIEW_SAMPLE_RAM:
            return "SAMPLE > RAM";
        case UI_SETTINGS_VIEW_WAVETABLE:
            return "SAMPLE > WAVE";
        case UI_SETTINGS_VIEW_SAMPLER:
            return "SAMPLE > STREAM";
        case UI_SETTINGS_VIEW_MULTI_SAMPLE:
            return "SAMPLE > MULTI";
        case UI_SETTINGS_VIEW_SAMPLER_SLOT:
            return "STREAM > SLOT";
        case UI_SETTINGS_VIEW_SAMPLER_CATALOG:
            return "STREAM > SD";
        case UI_SETTINGS_VIEW_PROJECT_LOAD:
            return "PROJECT > LOAD";
        case UI_SETTINGS_VIEW_PROJECT_SAVE_AS:
            return "PROJECT > SAVE AS";
        case UI_SETTINGS_VIEW_PROJECT_MANAGE:
            return "PROJECT > MANAGE";
        case UI_SETTINGS_VIEW_PROJECT_MANAGE_SLOT:
            return "MANAGE > SLOT";
#if defined(BRICK6_VARIANT_LOWCOST)
        case UI_SETTINGS_VIEW_CALIBRATION:
            return "CALIBRATION";
#endif
        default:
            return "SETTINGS";
    }
}

static uint8_t ui_page_settings_view_item_count(ui_settings_view_t view)
{
    switch (view)
    {
        case UI_SETTINGS_VIEW_ROOT:
#if defined(BRICK6_VARIANT_LOWCOST)
            return 3U;
#else
            return 2U;
#endif
        case UI_SETTINGS_VIEW_PROJECT:
            return 3U;
        case UI_SETTINGS_VIEW_SAMPLE:
            return 4U;
        case UI_SETTINGS_VIEW_SAMPLE_RAM:
        case UI_SETTINGS_VIEW_WAVETABLE:
            return 0U;
        case UI_SETTINGS_VIEW_SAMPLER:
        case UI_SETTINGS_VIEW_MULTI_SAMPLE:
            return 0U;
        case UI_SETTINGS_VIEW_SAMPLER_SLOT:
            return (uint8_t)UI_SETTINGS_SAMPLER_ACTION_COUNT;
        case UI_SETTINGS_VIEW_SAMPLER_CATALOG:
            return (wav_loader_catalog_count() > 255U) ? 255U : (uint8_t)wav_loader_catalog_count();
        case UI_SETTINGS_VIEW_PROJECT_LOAD:
            return (uint8_t)(g_ui_settings.project_slot_count + 1U);
        case UI_SETTINGS_VIEW_PROJECT_MANAGE:
            return g_ui_settings.project_slot_count;
        case UI_SETTINGS_VIEW_PROJECT_SAVE_AS:
            return PROJECT_PRODUCT_SLOT_COUNT;
        case UI_SETTINGS_VIEW_PROJECT_MANAGE_SLOT:
            return (uint8_t)UI_SETTINGS_MANAGE_ACTION_COUNT;
#if defined(BRICK6_VARIANT_LOWCOST)
        case UI_SETTINGS_VIEW_CALIBRATION:
            return 2U;
#endif
        default:
            return 0U;
    }
}

static const char *ui_page_settings_item_label(ui_settings_view_t view, uint8_t index, char *out, uint32_t out_size)
{
    (void)out_size;
    switch (view)
    {
        case UI_SETTINGS_VIEW_ROOT:
            if (index == 0U)
            {
                return "SAMPLE";
            }
            if (index == 1U)
            {
                return "PROJECT";
            }
#if defined(BRICK6_VARIANT_LOWCOST)
            if (index == 2U)
            {
                return "CALIBRATION";
            }
#endif
            return "-";
        case UI_SETTINGS_VIEW_SAMPLE:
            if (index == 0U)
            {
                return "MULTI";
            }
            if (index == 1U)
            {
                return "RAM";
            }
            if (index == 2U)
            {
                return "WAVE";
            }
            return "STREAM";
        case UI_SETTINGS_VIEW_PROJECT:
            if (index == 0U)
            {
                return "LOAD";
            }
            if (index == 1U)
            {
                return "SAVE AS";
            }
            return "MANAGE";
        case UI_SETTINGS_VIEW_PROJECT_LOAD:
            if (index == 0U)
            {
                return "BLANK PROJECT";
            }
            if ((index - 1U) >= g_ui_settings.project_slot_count)
            {
                return "-";
            }
            (void)snprintf(out, out_size, "PROJECT %02u", (unsigned)g_ui_settings.project_slots[index - 1U]);
            return out;
        case UI_SETTINGS_VIEW_PROJECT_MANAGE:
            if (index >= g_ui_settings.project_slot_count)
            {
                return "-";
            }
            (void)snprintf(out, out_size, "PROJECT %02u", (unsigned)g_ui_settings.project_slots[index]);
            return out;
        case UI_SETTINGS_VIEW_PROJECT_SAVE_AS:
            if (project_product_slot_present(index) != 0U)
            {
                (void)snprintf(out, out_size, "PROJECT %02u *", (unsigned)index);
            }
            else
            {
                (void)snprintf(out, out_size, "PROJECT %02u", (unsigned)index);
            }
            return out;
        case UI_SETTINGS_VIEW_SAMPLER:
            if (index >= g_ui_settings.sampler_slot_count)
            {
                return "-";
            }
            {
                uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
                const uint16_t global_slot = g_ui_settings.sampler_slots[index];
                const sample_classic_slot_state_t state =
                    (ui_page_settings_stream_backend_from_global(global_slot, &backend_slot) != 0U)
                        ? sample_global_pool_get_classic_state(backend_slot)
                        : SAMPLE_CLASSIC_SLOT_ERROR;
                const char *state_label = "EMPTY";
                if (state == SAMPLE_CLASSIC_SLOT_LOADED)
                {
                    state_label = "LOADED";
                }
                else if (state == SAMPLE_CLASSIC_SLOT_PREPARING)
                {
                    state_label = "PREP";
                }
                else if (state == SAMPLE_CLASSIC_SLOT_ERROR)
                {
                    state_label = "ERROR";
                }
                else if (state == SAMPLE_CLASSIC_SLOT_MISSING)
                {
                    state_label = "MISSING";
                }
                (void)snprintf(out, out_size, "SLOT %03u [%s]", (unsigned)global_slot, state_label);
                return out;
            }
        case UI_SETTINGS_VIEW_SAMPLER_SLOT:
            if (index == (uint8_t)UI_SETTINGS_SAMPLER_ACTION_LOAD_OR_REPLACE)
            {
                return (sample_global_pool_get_classic_state(g_ui_settings.selected_slot) == SAMPLE_CLASSIC_SLOT_EMPTY) ? "LOAD FROM SD" : "REPLACE FROM SD";
            }
            if (index == (uint8_t)UI_SETTINGS_SAMPLER_ACTION_PREVIEW_OR_STOP)
            {
                return (sd_preview_is_active() != 0U) ? "STOP" : "PREVIEW";
            }
            if (index == (uint8_t)UI_SETTINGS_SAMPLER_ACTION_CLEAR)
            {
                return "CLEAR";
            }
            return "-";
        case UI_SETTINGS_VIEW_SAMPLER_CATALOG:
            {
                const wav_loader_catalog_entry_t *entry = wav_loader_catalog_get(index);
                if (entry == 0)
                {
                    return "-";
                }
                return entry->name;
            }
        case UI_SETTINGS_VIEW_PROJECT_MANAGE_SLOT:
            if (index == (uint8_t)UI_SETTINGS_MANAGE_ACTION_LOAD_FROM)
            {
                return "LOAD FROM";
            }
            if (index == (uint8_t)UI_SETTINGS_MANAGE_ACTION_SAVE_TO)
            {
                return "SAVE TO";
            }
            return "DELETE";
#if defined(BRICK6_VARIANT_LOWCOST)
        case UI_SETTINGS_VIEW_CALIBRATION:
            return (index == 0U) ? "HALL KBD" : "HALL VEL";
#endif
        default:
            return "-";
    }
}

static ui_settings_menu_level_t *ui_page_settings_current_level(void)
{
    if ((g_ui_settings.depth == 0U) || (g_ui_settings.depth > UI_SETTINGS_MAX_LEVELS))
    {
        return 0;
    }

    return &g_ui_settings.levels[g_ui_settings.depth - 1U];
}

static void ui_page_settings_status(const char *status)
{
    if (status == 0)
    {
        g_ui_settings.status_line[0] = '\0';
        g_ui_settings.status_until_ms = 0U;
        return;
    }

    (void)snprintf(g_ui_settings.status_line,
                   sizeof(g_ui_settings.status_line),
                   "%s",
                   status);
    g_ui_settings.status_until_ms = HAL_GetTick() + UI_SETTINGS_STATUS_DURATION_MS;
}

static uint8_t ui_page_settings_request_asset_register(uint32_t kind,
                                                       uint16_t runtime)
{
    const control_asset_intent_t intent = {
        .operation = CONTROL_ASSET_REGISTER_RUNTIME,
        .kind = kind,
        .runtime = runtime
    };
    if (control_domain_request_asset(&intent) == 0U) return 0U;
    g_ui_settings.asset_register_pending = 1U;
    g_ui_settings.asset_register_kind = kind;
    g_ui_settings.asset_register_runtime = runtime;
    return 1U;
}

static uint8_t ui_page_settings_request_asset_remove(uint32_t kind,
                                                     uint16_t logical,
                                                     uint16_t runtime)
{
    const control_asset_intent_t intent = {
        .operation = CONTROL_ASSET_REMOVE_RUNTIME,
        .kind = kind,
        .logical = logical,
        .runtime = runtime
    };
    return control_domain_request_asset(&intent);
}

static void ui_page_settings_asset_register_poll(void)
{
    if (g_ui_settings.asset_register_pending == 0U) return;
    const char *path = 0;
    if (g_ui_settings.asset_register_kind == PERSIST_ASSET_MULTI)
    {
        const multi_sample_instrument_t *const instrument =
            multi_sample_pool_get_instrument(g_ui_settings.asset_register_runtime);
        if (instrument != 0) path = instrument->index_path;
    }
    else
    {
        const sample_global_slot_t *const slot = sample_global_pool_get_slot(
            g_ui_settings.asset_register_runtime);
        if (slot != 0) path = slot->path;
    }
    if ((path == 0) || (path[0] == '\0')) return;

    uint16_t logical = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (project_control_find_asset(g_ui_settings.asset_register_kind,
                                   path, &logical) == 0U) return;
    g_ui_settings.sample_slot_selected = logical;
    g_ui_settings.asset_register_pending = 0U;
}

static void ui_page_settings_preview_stop(ui_settings_preview_stop_origin_t origin)
{
    if (sd_preview_is_active() == 0U)
    {
        return;
    }

    sd_preview_request_stop();
    g_ui_settings.preview_stop_origin = origin;
}

static void ui_page_settings_preview_apply_termination_status(void)
{
    const uint8_t active = sd_preview_is_active();

    if ((g_ui_settings.preview_was_active != 0U) && (active == 0U))
    {
        if (g_ui_settings.preview_stop_origin == UI_SETTINGS_PREVIEW_STOP_ORIGIN_USER)
        {
            ui_page_settings_status("PREVIEW STOP");
        }
        else if (g_ui_settings.preview_stop_origin == UI_SETTINGS_PREVIEW_STOP_ORIGIN_NONE)
        {
            const sd_preview_error_t error = sd_preview_get_last_error();
            ui_page_settings_status((error == SD_PREVIEW_ERROR_NONE)
                                        ? "PREVIEW END"
                                        : ui_page_settings_preview_error_label(error));
        }
    }

    g_ui_settings.preview_stop_origin = UI_SETTINGS_PREVIEW_STOP_ORIGIN_NONE;
    g_ui_settings.preview_was_active = active;
}

static void ui_page_settings_close(void)
{
    ui_page_set(g_ui_settings.return_page_id);
}

void ui_page_settings_close_to_return_page(void)
{
    if (ui_page_settings_is_open() != 0U)
    {
        ui_page_settings_close();
    }
}

static void ui_page_settings_push(ui_settings_view_t view)
{
    if (g_ui_settings.depth >= UI_SETTINGS_MAX_LEVELS)
    {
        return;
    }

    g_ui_settings.levels[g_ui_settings.depth].view = view;
    g_ui_settings.levels[g_ui_settings.depth].selected_index = 0U;
    g_ui_settings.depth++;
}

static void ui_page_settings_back(void)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    const ui_settings_menu_level_t *const level = ui_page_settings_current_level();
    if ((level != 0) && (level->view == UI_SETTINGS_VIEW_SAMPLER))
    {
        sd_preview_set_gain(1.0f);
    }

    if (g_ui_settings.depth <= 1U)
    {
        ui_page_settings_close();
        return;
    }

    g_ui_settings.depth--;
}

static void ui_page_settings_apply_action(void)
{
    ui_settings_menu_level_t *const level = ui_page_settings_current_level();
    if (level == 0)
    {
        return;
    }

    switch (level->view)
    {
        case UI_SETTINGS_VIEW_ROOT:
            if (level->selected_index == 0U)
            {
                ui_page_settings_push(UI_SETTINGS_VIEW_SAMPLE);
            }
            else if (level->selected_index == 1U)
            {
                ui_page_settings_push(UI_SETTINGS_VIEW_PROJECT);
            }
#if defined(BRICK6_VARIANT_LOWCOST)
            else if (level->selected_index == 2U)
            {
                ui_page_settings_push(UI_SETTINGS_VIEW_CALIBRATION);
            }
#endif
            break;

#if defined(BRICK6_VARIANT_LOWCOST)
        case UI_SETTINGS_VIEW_CALIBRATION:
            if (level->selected_index == 0U)
            {
                ui_page_calibration_open(UI_PAGE_SETTINGS);
            }
            else
            {
                ui_page_user_calibration_open(UI_PAGE_SETTINGS);
            }
            break;
#endif

        case UI_SETTINGS_VIEW_SAMPLE:
            if (level->selected_index == 0U)
            {
                ui_page_settings_refresh_global_kind_slots(SAMPLE_GLOBAL_KIND_MULTI);
                ui_page_settings_multi_browser_enter_root();
                ui_page_settings_push(UI_SETTINGS_VIEW_MULTI_SAMPLE);
            }
            else if (level->selected_index == 1U)
            {
                ui_page_settings_refresh_ram_slots();
                ui_page_settings_sample_browser_enter_root();
                ui_page_settings_push(UI_SETTINGS_VIEW_SAMPLE_RAM);
            }
            else if (level->selected_index == 2U)
            {
                ui_page_settings_refresh_wavetable_slots();
                ui_page_settings_wavetable_browser_enter_root();
                ui_page_settings_push(UI_SETTINGS_VIEW_WAVETABLE);
            }
            else
            {
                ui_page_settings_refresh_sampler_slots();
                ui_page_settings_sample_browser_enter_root();
                ui_page_settings_push(UI_SETTINGS_VIEW_SAMPLER);
            }
            break;

        case UI_SETTINGS_VIEW_PROJECT:
            if (level->selected_index == 0U)
            {
                ui_page_settings_refresh_project_slots();
                ui_page_settings_push(UI_SETTINGS_VIEW_PROJECT_LOAD);
            }
            else if (level->selected_index == 1U)
            {
                ui_page_settings_push(UI_SETTINGS_VIEW_PROJECT_SAVE_AS);
            }
            else
            {
                ui_page_settings_refresh_project_slots();
                if (g_ui_settings.project_slot_count == 0U)
                {
                    ui_page_settings_status("NO PROJECT");
                    break;
                }
                ui_page_settings_push(UI_SETTINGS_VIEW_PROJECT_MANAGE);
            }
            break;

        case UI_SETTINGS_VIEW_PROJECT_LOAD:
            if (level->selected_index == 0U)
            {
                if (control_domain_request_project(&(control_project_intent_t){CONTROL_PROJECT_BLANK, 0U}) != 0U)
                {
                    g_ui_settings.return_page_id = UI_PAGE_TEMPLATE_CFG;
                    ui_page_set(UI_PAGE_TEMPLATE_CFG);
                }
                else
                {
                    ui_page_settings_status("BLANK FAIL");
                }
            }
            else if (((level->selected_index - 1U) < g_ui_settings.project_slot_count)
                     && (control_domain_request_project(&(control_project_intent_t){CONTROL_PROJECT_LOAD, g_ui_settings.project_slots[level->selected_index - 1U]}) != 0U))
            {
                    ui_page_settings_status("LOADING");
            }
            else
            {
                ui_page_settings_status("LOAD FAIL");
            }
            break;

        case UI_SETTINGS_VIEW_PROJECT_SAVE_AS:
            if (control_domain_request_project(&(control_project_intent_t){CONTROL_PROJECT_SAVE, level->selected_index}) != 0U)
            {
                ui_page_settings_status("SAVING");
            }
            else
            {                ui_page_settings_status("SAVE FAIL");
            }
            break;

        case UI_SETTINGS_VIEW_PROJECT_MANAGE:
            if (level->selected_index < g_ui_settings.project_slot_count)
            {
                g_ui_settings.selected_slot = g_ui_settings.project_slots[level->selected_index];
                ui_page_settings_push(UI_SETTINGS_VIEW_PROJECT_MANAGE_SLOT);
            }
            break;

        case UI_SETTINGS_VIEW_PROJECT_MANAGE_SLOT:
            if (level->selected_index == (uint8_t)UI_SETTINGS_MANAGE_ACTION_LOAD_FROM)
            {
                if (control_domain_request_project(&(control_project_intent_t){CONTROL_PROJECT_LOAD, g_ui_settings.selected_slot}) != 0U)
                {                    ui_page_settings_status("LOADING");
                }
                else
                {                    ui_page_settings_status("LOAD FROM FAIL");
                }
            }
            else if (level->selected_index == (uint8_t)UI_SETTINGS_MANAGE_ACTION_SAVE_TO)
            {
                if (control_domain_request_project(&(control_project_intent_t){CONTROL_PROJECT_SAVE, g_ui_settings.selected_slot}) != 0U)
                {                    ui_page_settings_status("SAVING");
                }
                else
                {                    ui_page_settings_status("SAVE TO FAIL");
                }
            }
            else if (control_domain_request_project(&(control_project_intent_t){CONTROL_PROJECT_DELETE, g_ui_settings.selected_slot}) != 0U)
            {                ui_page_settings_status("DELETING");
            }
            else
            {                ui_page_settings_status("DELETE BUSY");
            }
            break;

        case UI_SETTINGS_VIEW_SAMPLER:
            if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY)
            {
                ui_page_settings_sample_copy_left((uint8_t)(button_down(BTN_SHIFT) != 0U));
            }
            else
            {
                ui_page_settings_sample_copy_right((uint8_t)(button_down(BTN_SHIFT) != 0U));
            }
            break;

        case UI_SETTINGS_VIEW_MULTI_SAMPLE:
            if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY)
            {
                ui_page_settings_multi_copy_left((uint8_t)(button_down(BTN_SHIFT) != 0U));
            }
            else
            {
                ui_page_settings_multi_copy_right((uint8_t)(button_down(BTN_SHIFT) != 0U));
            }
            break;

        case UI_SETTINGS_VIEW_SAMPLE_RAM:
            if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY)
            {
                ui_page_settings_ram_copy_left((uint8_t)(button_down(BTN_SHIFT) != 0U));
            }
            else
            {
                ui_page_settings_ram_copy_right((uint8_t)(button_down(BTN_SHIFT) != 0U));
            }
            break;

        case UI_SETTINGS_VIEW_WAVETABLE:
            if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY)
            {
                ui_page_settings_wavetable_copy_left((uint8_t)(button_down(BTN_SHIFT) != 0U));
            }
            else
            {
                ui_page_settings_wavetable_copy_right((uint8_t)(button_down(BTN_SHIFT) != 0U));
            }
            break;

        case UI_SETTINGS_VIEW_SAMPLER_SLOT:
            if (level->selected_index == (uint8_t)UI_SETTINGS_SAMPLER_ACTION_LOAD_OR_REPLACE)
            {
                ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
                if ((wav_loader_catalog_loaded() != 0U)
                    && (wav_loader_catalog_stale() == 0U)
                    && (wav_loader_catalog_count() != 0U))
                {
                    g_ui_settings.sampler_catalog_mode = UI_SETTINGS_SAMPLER_CATALOG_MODE_LOAD;
                    if (wav_loader_catalog_truncated() != 0U)
                    {
                        ui_page_settings_status("LIB FULL");
                    }
                    else if (wav_loader_catalog_path_truncated() != 0U)
                    {
                        ui_page_settings_status("PATH LONG");
                    }
                    ui_page_settings_push(UI_SETTINGS_VIEW_SAMPLER_CATALOG);
                }
                else
                {
                    ui_page_settings_status("REFRESH LIB");
                }
            }
            else if (level->selected_index == (uint8_t)UI_SETTINGS_SAMPLER_ACTION_PREVIEW_OR_STOP)
            {
                if (sd_preview_is_active() != 0U)
                {
                    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_USER);
                }
                else
                {
                    if ((wav_loader_catalog_loaded() != 0U)
                        && (wav_loader_catalog_stale() == 0U)
                        && (wav_loader_catalog_count() != 0U))
                    {
                        g_ui_settings.sampler_catalog_mode = UI_SETTINGS_SAMPLER_CATALOG_MODE_PREVIEW;
                        if (wav_loader_catalog_truncated() != 0U)
                        {
                            ui_page_settings_status("LIB FULL");
                        }
                        else if (wav_loader_catalog_path_truncated() != 0U)
                        {
                            ui_page_settings_status("PATH LONG");
                        }
                        ui_page_settings_push(UI_SETTINGS_VIEW_SAMPLER_CATALOG);
                    }
                    else
                    {
                        ui_page_settings_status("REFRESH LIB");
                    }
                }
            }
            else if (level->selected_index == (uint8_t)UI_SETTINGS_SAMPLER_ACTION_CLEAR)
            {
                if (project_replacement_is_active() != 0U) return;
                ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
                (void)ui_page_settings_request_asset_remove(
                    PERSIST_ASSET_SAMPLE_STREAM,
                    g_ui_settings.sample_slot_selected,
                    g_ui_settings.selected_slot);
                ui_page_settings_refresh_sampler_slots();
                ui_page_settings_status("CLEAR OK");
            }
            break;

        case UI_SETTINGS_VIEW_SAMPLER_CATALOG:
            if (level->selected_index < wav_loader_catalog_count())
            {
                const wav_loader_catalog_entry_t *entry = wav_loader_catalog_get(level->selected_index);
                if ((entry != 0) && (entry->state == WAV_LOADER_CATALOG_READY))
                {
                    if (g_ui_settings.sampler_catalog_mode == UI_SETTINGS_SAMPLER_CATALOG_MODE_PREVIEW)
                    {
                        if ((sd_preview_is_active() != 0U)
                            && (strcmp(sd_preview_get_path(), entry->path) == 0))
                        {
                            ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_USER);
                            break;
                        }

                        if (sd_preview_request_begin(entry->path) != 0U)
                        {
                            g_ui_settings.preview_stop_origin = UI_SETTINGS_PREVIEW_STOP_ORIGIN_NONE;
                            g_ui_settings.preview_was_active = sd_preview_is_active();
                            ui_page_settings_status("PREVIEW ON");
                        }
                        else
                        {
                            ui_page_settings_status(ui_page_settings_preview_error_label(sd_preview_get_last_error()));
                        }
                    }
                    else
                    {
                        ui_page_settings_sample_load_to_slot(g_ui_settings.selected_slot, entry->path);
                    }
                }
            }
            break;

        default:
            break;
    }
}

static void ui_page_settings_enter(void)
{
    const uint8_t return_page_id = g_ui_settings.return_page_id;
    memset(&g_ui_settings, 0, sizeof(g_ui_settings));
    g_ui_settings.return_page_id = return_page_id;
    g_ui_settings.depth = 0U;
    g_ui_settings.selected_slot = 0U;
    g_ui_settings.sampler_catalog_mode = UI_SETTINGS_SAMPLER_CATALOG_MODE_LOAD;
    g_ui_settings.sampler_slot_count = 0U;
    g_ui_settings.project_slot_count = 0U;
    g_ui_settings.preview_was_active = 0U;
    g_ui_settings.preview_stop_origin = UI_SETTINGS_PREVIEW_STOP_ORIGIN_NONE;
    g_ui_settings.convert_slot_valid = 0U;
    g_ui_settings.convert_slot = 0U;
    g_ui_settings.convert_path[0] = '\0';
    g_ui_settings.header_slot_flash_until_ms = 0U;
    g_ui_settings.header_mem_flash_until_ms = 0U;
    for (uint8_t i = 0U; i < UI_SETTINGS_ENCODER_COUNT; ++i)
    {
        g_ui_settings.encoder_accum[i] = 0;
    }
    ui_page_settings_refresh_sampler_slots();
    ui_page_settings_status(0);
    ui_page_settings_push(UI_SETTINGS_VIEW_ROOT);
}

static void ui_page_settings_leave(void)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    sd_preview_set_gain(1.0f);
    g_ui_settings.depth = 0U;
}

static void ui_page_settings_handle_event_internal(const ui_event_t *ev)
{
    if ((wav_convert_is_active() != 0U) || (g_ui_settings.convert_slot_valid != 0U))
    {
        return;
    }

    if ((ev == 0) || (ev->type != UI_EVENT_BUTTON_PRESS))
    {
        if ((ev != 0)
            && (ev->type == UI_EVENT_HALL_PRESS)
            && (ui_page_settings_current_level() != 0)
            && (ui_page_settings_current_level()->view == UI_SETTINGS_VIEW_SAMPLER))
        {
            ui_page_settings_sample_preview_current();
        }
        return;
    }

    ui_settings_menu_level_t *const level = ui_page_settings_current_level();
    if ((level != 0)
        && ((level->view == UI_SETTINGS_VIEW_SAMPLER)
            || (level->view == UI_SETTINGS_VIEW_SAMPLE_RAM)
            || (level->view == UI_SETTINGS_VIEW_WAVETABLE)
            || (level->view == UI_SETTINGS_VIEW_MULTI_SAMPLE)))
    {
        if (g_ui_settings.sample_confirm != (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_NONE)
        {
            if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARING)
            {
                if (ev->id == (uint8_t)BTN_PAGE_1)
                {
                    (void)multi_sample_cancel_load();
                    ui_page_settings_multi_prepare_finish("CANCEL");
                }
                return;
            }
            if (ev->id == (uint8_t)BTN_PAGE_2)
            {
                ui_page_settings_sample_confirm_accept();
                return;
            }
            if (ev->id == (uint8_t)BTN_PAGE_1)
            {
                ui_page_settings_sample_confirm_cancel();
                return;
            }
            if ((ev->id == (uint8_t)BTN_COPY) || (ev->id == (uint8_t)BTN_PASTE))
            {
                return;
            }
        }

        if ((ev->id == (uint8_t)BTN_COPY) || (ev->id == (uint8_t)BTN_PASTE))
        {
            return;
        }

        if ((level->view == UI_SETTINGS_VIEW_SAMPLER)
            || (level->view == UI_SETTINGS_VIEW_SAMPLE_RAM)
            || (level->view == UI_SETTINGS_VIEW_WAVETABLE))
        {
            if (ev->id == (uint8_t)BTN_PAGE_1)
            {
                if (level->view == UI_SETTINGS_VIEW_WAVETABLE)
                {
                    ui_page_settings_wavetable_browser_parent_or_exit();
                }
                else
                {
                    ui_page_settings_sample_browser_parent_or_exit();
                }
                return;
            }
            if ((ev->id == (uint8_t)BTN_PAGE_2)
                && (button_down(BTN_SHIFT) == 0U))
            {
                ui_page_settings_apply_action();
                return;
            }
            if (ev->id == (uint8_t)BTN_PAGE_3)
            {
                return;
            }
            if (ev->id == (uint8_t)BTN_PAGE_4)
            {
                if (level->view == UI_SETTINGS_VIEW_WAVETABLE)
                {
                    (void)ui_page_settings_wavetable_browser_refresh();
                }
                else
                {
                    ui_page_settings_sample_catalog_refresh_action((uint8_t)(button_down(BTN_SHIFT) != 0U));
                }
                return;
            }
        }

        if ((level->view == UI_SETTINGS_VIEW_SAMPLER)
            && (ev->id == (uint8_t)BTN_PAGE_1))
        {
            ui_page_settings_sample_browser_parent_or_exit();
            return;
        }

        if ((level->view == UI_SETTINGS_VIEW_SAMPLER)
            && (ev->id == (uint8_t)BTN_PAGE_2)
            && (button_down(BTN_SHIFT) == 0U))
        {
            ui_page_settings_sample_ok_action();
            return;
        }

        if ((level->view == UI_SETTINGS_VIEW_SAMPLER)
            && (ev->id == (uint8_t)BTN_PAGE_3))
        {
            return;
        }

        if ((level->view == UI_SETTINGS_VIEW_SAMPLER)
            && (ev->id == (uint8_t)BTN_PAGE_4))
        {
            ui_page_settings_sample_catalog_refresh_action((uint8_t)(button_down(BTN_SHIFT) != 0U));
            return;
        }

        if (level->view == UI_SETTINGS_VIEW_MULTI_SAMPLE)
        {
            if (ev->id == (uint8_t)BTN_PAGE_1)
            {
                ui_page_settings_multi_browser_parent_or_exit();
                return;
            }
            if ((ev->id == (uint8_t)BTN_PAGE_2) && (button_down(BTN_SHIFT) == 0U))
            {
                ui_page_settings_apply_action();
                return;
            }
            if (ev->id == (uint8_t)BTN_PAGE_3)
            {
                ui_page_settings_multi_confirm_clear_indexes();
                return;
            }
            if (ev->id == (uint8_t)BTN_PAGE_4)
            {
                return;
            }
        }

        if (ev->id == (uint8_t)BTN_SETTINGS)
        {
            ui_page_settings_back();
            return;
        }

        return;
    }

    if (ev->id == (uint8_t)BTN_PAGE_1)
    {
        ui_page_settings_back();
        return;
    }

    if (ev->id == (uint8_t)BTN_PAGE_2)
    {
        ui_page_settings_apply_action();
        return;
    }

    if ((ev->id == (uint8_t)BTN_PAGE_3) || (ev->id == (uint8_t)BTN_PAGE_4)
        || (ev->id == (uint8_t)BTN_COPY) || (ev->id == (uint8_t)BTN_PASTE))
    {
        return;
    }

    if (ev->id == (uint8_t)BTN_SETTINGS)
    {
        ui_page_settings_back();
        return;
    }
}

static void ui_page_settings_handle_event_page(const ui_event_t *ev)
{
    (void)ui_page_settings_handle_event(ev);
}

static void ui_page_settings_tick(void)
{
    ui_page_settings_asset_register_poll();
    ui_page_settings_multi_clear_service();
    const ui_settings_menu_level_t *const catalog_level =
        (g_ui_settings.depth != 0U)
            ? &g_ui_settings.levels[g_ui_settings.depth - 1U] : 0;
    if (catalog_level != 0)
    {
        if (catalog_level->view == UI_SETTINGS_VIEW_WAVETABLE)
            (void)ui_page_settings_wavetable_browser_refresh();
        else if (catalog_level->view == UI_SETTINGS_VIEW_MULTI_SAMPLE)
            (void)ui_page_settings_multi_browser_refresh();
    }
    if (g_ui_settings.classic_load_pending != 0U)
    {
        const sample_cache_slot_readiness_t readiness =
            sample_cache_get_slot_readiness(g_ui_settings.classic_load_slot);
        if (readiness == SAMPLE_CACHE_SLOT_PLAYABLE)
        {
            if (ui_page_settings_request_asset_register(
                    PERSIST_ASSET_SAMPLE_STREAM,
                    g_ui_settings.classic_load_slot) != 0U)
            {
                g_ui_settings.sample_slot_selected = g_ui_settings.classic_load_slot;
                ui_page_settings_refresh_sampler_slots();
                ui_page_settings_status("LOAD OK");
            }
            else
            {
                (void)ui_page_settings_request_asset_remove(
                    PERSIST_ASSET_SAMPLE_STREAM,
                    g_ui_settings.classic_load_slot,
                    g_ui_settings.classic_load_slot);
                ui_page_settings_status("BANK FAIL");
            }
            g_ui_settings.classic_load_pending = 0U;
        }
        else if (readiness == SAMPLE_CACHE_SLOT_ERROR)
        {
            g_ui_settings.classic_load_pending = 0U;
            ui_page_settings_status("LOAD FAIL");
        }
    }
    uint8_t project_slot = 0U;
    uint8_t project_success = 0U;
    if (project_product_save_take_result(&project_slot, &project_success) != 0U)
    {
        (void)project_slot;
        if (project_success != 0U)
        {
            ui_page_settings_refresh_project_slots();
            ui_page_settings_status("SAVE OK");
        }
        else
        {
            ui_page_settings_status("SAVE FAIL");
        }
    }

    sampler_ram_result_t ram_result;
    uint16_t ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
    uint16_t ram_global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    const char *ram_path = 0;
    if (sampler_ram_pool_load_async_take_result(&ram_result, &ram_slot,
                                                &ram_global, &ram_path) != 0U)
    {
        if (ram_result == SAMPLER_RAM_RESULT_OK)
        {
            (void)ui_page_settings_request_asset_register(
                PERSIST_ASSET_SAMPLE_RAM, ram_global);
        }
        else if ((ram_result == SAMPLER_RAM_RESULT_GLOBAL_SLOT_FULL)
                 || (ram_result == SAMPLER_RAM_RESULT_POOL_FULL))
        {
            ui_page_settings_flash_sample_header_slots();
        }
        else if ((ram_result == SAMPLER_RAM_RESULT_GLOBAL_BUDGET_FULL)
                 || (ram_result == SAMPLER_RAM_RESULT_RAM_POOL_FULL)
                 || (ram_result == SAMPLER_RAM_RESULT_TOO_LARGE))
        {
            ui_page_settings_flash_sample_header_memory();
        }
        ui_page_settings_refresh_ram_slots();
        ui_page_settings_status(sampler_ram_pool_result_label(ram_result));
    }

    wavetable_result_t wavetable_result;
    uint16_t wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
    uint16_t wavetable_global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    const char *wavetable_path = 0;
    if (wavetable_pool_load_async_take_result(&wavetable_result, &wavetable_slot,
                                              &wavetable_global, &wavetable_path) != 0U)
    {
        if (wavetable_result == WAVETABLE_RESULT_OK)
        {
            (void)ui_page_settings_request_asset_register(
                PERSIST_ASSET_WAVETABLE, wavetable_global);
        }
        else if ((wavetable_result == WAVETABLE_RESULT_GLOBAL_SLOT_FULL)
                 || (wavetable_result == WAVETABLE_RESULT_POOL_FULL))
        {
            ui_page_settings_flash_sample_header_slots();
        }
        else if ((wavetable_result == WAVETABLE_RESULT_GLOBAL_BUDGET_FULL)
                 || (wavetable_result == WAVETABLE_RESULT_RAM_POOL_FULL)
                 || (wavetable_result == WAVETABLE_RESULT_TOO_LARGE))
        {
            ui_page_settings_flash_sample_header_memory();
        }
        ui_page_settings_refresh_wavetable_slots();
        ui_page_settings_status(wavetable_pool_result_label(wavetable_result));
    }

    const wav_loader_catalog_view_service_result_t browser_result =
        wav_loader_catalog_view_last_result();
    if (browser_result == WAV_LOADER_CATALOG_VIEW_PUBLISHED)
    {
        const ui_settings_menu_level_t *const level =
            (g_ui_settings.depth != 0U) ? &g_ui_settings.levels[g_ui_settings.depth - 1U] : 0;
        if ((level != 0)
            && ((level->view == UI_SETTINGS_VIEW_SAMPLE)
                || (level->view == UI_SETTINGS_VIEW_SAMPLE_RAM)))
        {
            (void)ui_page_settings_sample_browser_refresh();
        }
    }
    else if (browser_result == WAV_LOADER_CATALOG_VIEW_ERROR)
    {
        ui_page_settings_status("CAT FAIL");
    }

    if ((wav_convert_is_active() != 0U) || (g_ui_settings.convert_slot_valid != 0U))
    {
        const wav_convert_state_t convert_state = wav_convert_get_state();
        if (convert_state == WAV_CONVERT_STATE_ACTIVE)
        {
            (void)snprintf(g_ui_settings.status_line,
                           sizeof(g_ui_settings.status_line),
                           "CONVERT %u%%",
                           (unsigned)wav_convert_get_progress_percent());
            g_ui_settings.status_until_ms = HAL_GetTick() + UI_SETTINGS_STATUS_DURATION_MS;
            return;
        }
        if (convert_state == WAV_CONVERT_STATE_DONE)
        {
            const uint16_t slot = g_ui_settings.convert_slot;
            char path[SAMPLE_CLASSIC_PATH_MAX];
            (void)snprintf(path, sizeof(path), "%s", g_ui_settings.convert_path);
            g_ui_settings.convert_slot_valid = 0U;
            g_ui_settings.convert_path[0] = '\0';
            wav_convert_clear_finished();
            wav_loader_catalog_mark_stale();
            ui_page_settings_sample_load_to_slot(slot, path);
            return;
        }
        if (convert_state == WAV_CONVERT_STATE_FAILED)
        {
            const char *const label =
                ui_page_settings_convert_error_label(wav_convert_get_last_error());
            g_ui_settings.convert_slot_valid = 0U;
            g_ui_settings.convert_path[0] = '\0';
            wav_convert_clear_finished();
            ui_page_settings_status(label);
            return;
        }
    }

    ui_page_settings_preview_apply_termination_status();

    if ((g_ui_settings.status_line[0] != '\0')
        && ((int32_t)(g_ui_settings.status_until_ms - HAL_GetTick()) <= 0))
    {
        ui_page_settings_status(0);
    }
}

