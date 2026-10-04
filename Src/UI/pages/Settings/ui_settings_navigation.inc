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
        case UI_SETTINGS_VIEW_CALIBRATION:
            return "CALIBRATION";
        default:
            return "SETTINGS";
    }
}

static uint8_t ui_page_settings_view_item_count(ui_settings_view_t view)
{
    switch (view)
    {
        case UI_SETTINGS_VIEW_ROOT:
            return 3U;
        case UI_SETTINGS_VIEW_PROJECT:
            return (uint8_t)(g_ui_settings.project_slot_count + 1U);
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
        case UI_SETTINGS_VIEW_CALIBRATION:
            return 2U;
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
            if (index == 2U)
            {
                return "CALIBRATION";
            }
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
            if (index == 0U) return "[ + NEW PROJECT ]";
            --index;
            if (index >= g_ui_settings.project_slot_count) return "-";
            {project_product_metadata_t meta;
            if(project_product_metadata(g_ui_settings.project_slots[index],&meta)==0U)
                return "INVALID PROJECT";
            (void)snprintf(out,out_size,"%.*s",(int)(out_size-1U),meta.name);}
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
        case UI_SETTINGS_VIEW_CALIBRATION:
            return (index == 0U) ? "HALL KBD" : "HALL VEL";
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

static void ui_page_settings_preview_stop(ui_settings_preview_stop_origin_t origin)
{
    if (sd_preview_is_active() == 0U)
    {
        return;
    }

    sd_preview_stop();
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

static void ui_page_settings_project_save_done(ui_page_name_edit_result_t result,
                                                const char *name,void *user)
{
    (void)user;
    if(result!=UI_PAGE_NAME_EDIT_RESULT_CONFIRM)
    {
        g_ui_project_save_phase=UI_PROJECT_SAVE_IDLE;
        ui_page_settings_status(0);
        return;
    }
    if(g_ui_project_name_mode==2U)
    {
        const uint8_t started=project_product_rename(g_ui_project_save_slot,name);
        g_ui_project_save_phase=UI_PROJECT_SAVE_IDLE;
        ui_page_settings_status(started!=0U?"RENAMING":"RENAME FAIL");
        return;
    }
    if(project_product_save_as_named(name,&g_ui_project_save_slot)!=0U)
    {
        g_ui_project_save_phase=UI_PROJECT_SAVE_WRITING;
        ui_page_settings_status("SAVING");
        return;
    }
    g_ui_project_save_phase=UI_PROJECT_SAVE_IDLE;
    ui_page_settings_status((project_product_save_last_error()
        ==PROJECT_PRODUCT_SAVE_ERROR_TRANSPORT_ACTIVE)?"STOP TRANSPORT":"SAVE FAIL");
}

static void ui_page_settings_project_name_begin(uint8_t mode,uint8_t slot)
{
    if(mode==1U && ((seq_runtime_is_running()!=0U)||(seq_runtime_is_start_pending()!=0U)))
    {
        ui_page_settings_status("STOP TRANSPORT");
        return;
    }
    if(project_product_save_busy()!=0U||project_product_load_busy()!=0U
        ||project_product_rename_busy()!=0U)
    {
        ui_page_settings_status("BUSY");
        return;
    }
    project_product_metadata_t metadata;char initial[PROJECT_PRODUCT_NAME_BYTES+1U]={0};
    if(mode==2U && project_product_metadata(slot,&metadata)!=0U)
        memcpy(initial,metadata.name,sizeof(initial));
    else (void)snprintf(initial,sizeof(initial),"UNTITLED");
    g_ui_project_save_slot=slot;g_ui_project_name_mode=mode;
    g_ui_project_save_phase=UI_PROJECT_SAVE_EDIT;
    if(ui_page_name_edit_open(UI_PAGE_SETTINGS,"PROJECT",
            mode==2U?"RENAME":"NEW",initial,
            sizeof(initial),ui_page_settings_project_save_done,NULL)==0U)
    {
        g_ui_project_save_phase=UI_PROJECT_SAVE_IDLE;
        ui_page_settings_status("NAME ERROR");
    }
}

static void ui_page_settings_project_action(ui_browser_action_t action)
{
    ui_settings_menu_level_t *const level=ui_page_settings_current_level();
    const uint8_t is_new=(level!=NULL && level->selected_index==0U)?1U:0U;
    const uint8_t has_focus=(level!=NULL && level->selected_index>0U
        &&level->selected_index<=g_ui_settings.project_slot_count)?1U:0U;
    const uint8_t slot=has_focus?g_ui_settings.project_slots[level->selected_index-1U]:0U;
    if(action==UI_BROWSER_ACTION_NONE)return;
    if(action==UI_BROWSER_ACTION_RETURN)
    {
        ui_page_settings_back();
        return;
    }
    if(action==UI_BROWSER_ACTION_LOAD && is_new!=0U)
    {
        if(g_ui_settings.project_confirm_action!=2U)
        {
            g_ui_settings.project_confirm_action=2U;
            ui_page_settings_status("NEW?");
            return;
        }
        g_ui_settings.project_confirm_action=0U;
        if(project_product_new_blank()!=0U)
        {
            g_ui_settings.return_page_id=UI_PAGE_TEMPLATE_CFG;
            ui_page_set(UI_PAGE_TEMPLATE_CFG);
        }
        else ui_page_settings_status("BLANK FAIL");
        return;
    }
    if(action==UI_BROWSER_ACTION_SAVE && is_new!=0U)
    {
        g_ui_settings.project_confirm_action=0U;
        ui_page_settings_project_name_begin(1U,0U);
        return;
    }
    if(has_focus==0U)
    {
        ui_page_settings_status("NO PROJECT");
        return;
    }
    if(action==UI_BROWSER_ACTION_DELETE)
    {
        if(g_ui_settings.project_confirm_action!=1U
            ||g_ui_settings.project_confirm_slot!=slot)
        {
            g_ui_settings.project_confirm_action=1U;
            g_ui_settings.project_confirm_slot=slot;
            ui_page_settings_status("DELETE?");
            return;
        }
        g_ui_settings.project_confirm_action=0U;
        if(project_product_delete(slot)!=0U)
        {
            ui_page_settings_refresh_project_slots();
            if(level->selected_index>g_ui_settings.project_slot_count)
                level->selected_index=g_ui_settings.project_slot_count;
            ui_page_settings_status("DELETED");
        }
        else ui_page_settings_status("DELETE FAIL");
        return;
    }
    g_ui_settings.project_confirm_action=0U;
    switch(action)
    {
        case UI_BROWSER_ACTION_SAVE:
            if(seq_runtime_is_running()!=0U||seq_runtime_is_start_pending()!=0U)
                ui_page_settings_status("STOP TRANSPORT");
            else ui_page_settings_status(project_product_save_existing(slot)!=0U
                ?"SAVING":"SAVE FAIL");
            break;
        case UI_BROWSER_ACTION_LOAD:
            ui_page_settings_status(project_product_load(slot)!=0U
                ?"LOADING":"LOAD FAIL");
            break;
        case UI_BROWSER_ACTION_RENAME:
            ui_page_settings_project_name_begin(2U,slot);
            break;
        default: break;
    }
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
                ui_page_settings_refresh_project_slots();
                ui_page_settings_push(UI_SETTINGS_VIEW_PROJECT);
            }
            else if (level->selected_index == 2U)
            {
                ui_page_settings_push(UI_SETTINGS_VIEW_CALIBRATION);
            }
            break;

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
                if (project_control_remove_sample(
                        g_ui_settings.sample_slot_selected) == 0U)
                {
                    ui_page_settings_status("CLEAR BUSY");
                    return;
                }
                sample_global_pool_clear_classic(g_ui_settings.selected_slot);
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

                        if (sd_preview_begin(entry->path) != 0U)
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
    if(g_ui_project_save_phase!=UI_PROJECT_SAVE_IDLE)return;
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
    g_ui_settings.convert_load_kind = (uint8_t)UI_SETTINGS_CONVERT_LOAD_CLASSIC;
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
    if(g_ui_project_save_phase==UI_PROJECT_SAVE_IDLE)g_ui_settings.depth = 0U;
}

static ui_browser_context_t ui_page_settings_asset_context(void)
{
    return g_ui_settings.sample_focus == UI_SETTINGS_SAMPLE_FOCUS_LIBRARY
        ? UI_BROWSER_ASSET_SD : UI_BROWSER_ASSET_POOL;
}

static uint32_t ui_page_settings_asset_kind(ui_settings_view_t view)
{
    switch (view) {
        case UI_SETTINGS_VIEW_SAMPLE_RAM: return PERSIST_ASSET_SAMPLE_RAM;
        case UI_SETTINGS_VIEW_SAMPLER: return PERSIST_ASSET_SAMPLE_STREAM;
        case UI_SETTINGS_VIEW_MULTI_SAMPLE: return PERSIST_ASSET_MULTI;
        default: return PERSIST_ASSET_WAVETABLE;
    }
}

static const char *ui_page_settings_asset_focused_path(ui_settings_view_t view)
{
    if (g_ui_settings.sample_focus == UI_SETTINGS_SAMPLE_FOCUS_LIBRARY) {
        if (view == UI_SETTINGS_VIEW_MULTI_SAMPLE) {
            const ui_settings_multi_entry_t *entry = ui_page_settings_multi_selected_entry();
            return entry != 0 && entry->type == UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM
                && entry->index_path[0] != '\0' ? entry->index_path : NULL;
        }
        const ui_settings_sample_entry_t *entry = ui_page_settings_sample_selected_entry();
        return entry != 0 && entry->type == UI_SETTINGS_SAMPLE_ENTRY_FILE
            ? entry->path : NULL;
    }
    persist_control_asset_ref_t asset;
    if (project_control_get_logical_asset(ui_page_settings_asset_kind(view),
            g_ui_settings.sample_slot_selected, &asset) == 0U) return NULL;
    /* The returned path must survive this stack frame. */
    static char path[PERSIST_CONTROL_ASSET_PATH_BYTES + 1U];
    memcpy(path, asset.canonical_path, asset.path_length);
    path[asset.path_length] = '\0';
    return path;
}

static uint8_t ui_page_settings_asset_action_enabled(ui_settings_view_t view,
                                                      ui_browser_action_t action)
{
    if (action == UI_BROWSER_ACTION_NONE) return 0U;
    if (action == UI_BROWSER_ACTION_RETURN || action == UI_BROWSER_ACTION_REFRESH)
        return 1U;
    if (action != UI_BROWSER_ACTION_PREVIEW
        && (project_replacement_is_active() != 0U
            || project_control_asset_loads_pending() != 0U
            || multi_sample_import_is_active() != 0U
            || multi_sample_load_has_pending() != 0U
            || sampler_ram_pool_load_async_busy() != 0U
            || wavetable_pool_load_async_busy() != 0U))
        return 0U;
    if (action == UI_BROWSER_ACTION_PREVIEW)
    {
        if (view == UI_SETTINGS_VIEW_WAVETABLE
            || ui_page_settings_asset_focused_path(view) == NULL) return 0U;
        if (view == UI_SETTINGS_VIEW_MULTI_SAMPLE
            && g_ui_settings.sample_focus == UI_SETTINGS_SAMPLE_FOCUS_LIBRARY)
        {
            const ui_settings_multi_entry_t *entry = ui_page_settings_multi_selected_entry();
            return entry != NULL && entry->prepared != 0U;
        }
        return 1U;
    }
    if (action == UI_BROWSER_ACTION_LOAD
        && g_ui_settings.sample_focus == UI_SETTINGS_SAMPLE_FOCUS_LIBRARY)
    {
        if (view == UI_SETTINGS_VIEW_MULTI_SAMPLE)
        {
            const ui_settings_multi_entry_t *entry = ui_page_settings_multi_selected_entry();
            if (entry != NULL && entry->type != UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM)
                return 1U;
        }
        else
        {
            const ui_settings_sample_entry_t *entry = ui_page_settings_sample_selected_entry();
            if (entry != NULL && entry->type != UI_SETTINGS_SAMPLE_ENTRY_FILE)
                return 1U;
        }
    }
    const char *path = ui_page_settings_asset_focused_path(view);
    if (path == NULL) return 0U;
    const uint32_t kind = ui_page_settings_asset_kind(view);
    uint16_t logical = 0U;
    if (action == UI_BROWSER_ACTION_LOAD)
    {
        uint16_t runtime = 0U;
        if (project_control_find_asset(kind, path, &logical) == 0U)
            return view != UI_SETTINGS_VIEW_MULTI_SAMPLE
                || ui_page_settings_multi_find_loaded_path(path) < 0;
        if (kind == PERSIST_ASSET_MULTI)
            return project_control_resolve_multi_runtime(logical, &runtime) == 0U
                && ui_page_settings_multi_find_loaded_path(path) < 0;
        if (kind == PERSIST_ASSET_WAVETABLE)
            return project_control_resolve_wavetable_runtime(logical, &runtime) == 0U;
        return project_control_resolve_sample_runtime_kind(kind, logical, &runtime) == 0U;
    }
    if (action == UI_BROWSER_ACTION_UNLOAD) return 1U;
    if (action == UI_BROWSER_ACTION_RENAME || action == UI_BROWSER_ACTION_DELETE)
        return project_control_find_asset(kind, path, &logical) == 0U;
    return 1U;
}

static void ui_page_settings_asset_refresh(ui_settings_view_t view)
{
    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    if (view == UI_SETTINGS_VIEW_MULTI_SAMPLE)
    {
        char selected[MULTI_SAMPLE_POOL_PATH_MAX] = {0};
        const ui_settings_multi_entry_t *entry = ui_page_settings_multi_selected_entry();
        if (entry != 0) ui_page_settings_copy_bounded(selected, sizeof(selected), entry->path);
        if (ui_page_settings_sd_reprobe_for_refresh() == 0U
            || ui_page_settings_multi_browser_refresh() == 0U)
        {
            ui_page_settings_status("REFRESH FAIL");
            return;
        }
        for (uint16_t i = 0U; i < g_ui_settings.multi_entry_count; ++i)
            if (strcmp(g_ui_settings.multi_entries[i].path, selected) == 0)
            { g_ui_settings.sample_selected = i; break; }
    }
    else if (view == UI_SETTINGS_VIEW_WAVETABLE)
    {
        const ui_settings_sample_entry_t *entry = ui_page_settings_sample_selected_entry();
        g_ui_settings.wavetable_restore_path[0] = '\0';
        if (entry != NULL)
            ui_page_settings_copy_bounded(g_ui_settings.wavetable_restore_path,
                sizeof(g_ui_settings.wavetable_restore_path), entry->path);
        g_ui_settings.wavetable_restore_neighbor = g_ui_settings.sample_selected;
        g_ui_settings.wavetable_restore_pending = 1U;
        if (ui_page_settings_sd_reprobe_for_refresh() == 0U
            || ui_page_settings_wavetable_browser_refresh() == 0U)
        {
            g_ui_settings.wavetable_restore_pending = 0U;
            ui_page_settings_status("REFRESH FAIL");
        }
    }
    else ui_page_settings_sample_catalog_refresh_action(0U);
}

static void ui_page_settings_asset_action(ui_settings_view_t view,
                                          ui_browser_action_t action)
{
    if (ui_page_settings_asset_action_enabled(view, action) == 0U) return;
    switch (action)
    {
        case UI_BROWSER_ACTION_PREVIEW:
            if (view == UI_SETTINGS_VIEW_WAVETABLE)
                ui_page_settings_status("PREVIEW N/A");
            else ui_page_settings_preview_current();
            break;
        case UI_BROWSER_ACTION_LOAD:
        case UI_BROWSER_ACTION_UNLOAD:
            ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
            ui_page_settings_apply_action();
            break;
        case UI_BROWSER_ACTION_REFRESH:
            ui_page_settings_asset_refresh(view);
            break;
        case UI_BROWSER_ACTION_RENAME:
            ui_page_settings_asset_rename_begin();
            break;
        case UI_BROWSER_ACTION_DELETE:
            ui_page_settings_asset_delete_request();
            break;
        case UI_BROWSER_ACTION_RETURN:
            if (view == UI_SETTINGS_VIEW_MULTI_SAMPLE)
                ui_page_settings_multi_browser_parent_or_exit();
            else if (view == UI_SETTINGS_VIEW_WAVETABLE)
                ui_page_settings_wavetable_browser_parent_or_exit();
            else
                ui_page_settings_sample_browser_parent_or_exit();
            break;
        default: break;
    }
}

static void ui_page_settings_handle_event_internal(const ui_event_t *ev)
{
    if (ev != 0 && ev->type == UI_EVENT_BUTTON_RELEASE
        && ev->id == (uint8_t)BTN_PAGE_4)
    {
        ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
        return;
    }
    if((g_ui_project_save_phase==UI_PROJECT_SAVE_WRITING)
            ||(project_product_save_busy()!=0U))return;
    if ((wav_convert_is_active() != 0U) || (g_ui_settings.convert_slot_valid != 0U))
    {
        return;
    }

    if ((ev == 0) || (ev->type != UI_EVENT_BUTTON_PRESS))
    {
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
                    if (multi_sample_import_is_active() != 0U)
                        (void)multi_sample_import_cancel();
                    else
                        (void)multi_sample_cancel_load();
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

        ui_page_settings_asset_action(level->view,
            ui_browser_action_resolve_context((button_id_t)ev->id,
                button_down(BTN_SHIFT), ui_page_settings_asset_context()));
        return;
    }

    if(level!=NULL && level->view==UI_SETTINGS_VIEW_PROJECT)
    {
        if(ev->id==(uint8_t)BTN_SETTINGS)ui_page_settings_back();
        else ui_page_settings_project_action(ui_browser_action_resolve(
            (button_id_t)ev->id,button_down(BTN_SHIFT),1U));
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
    uint8_t project_slot = 0U;
    uint8_t project_success = 0U;
    if (project_product_save_take_result(&project_slot, &project_success) != 0U)
    {
        g_ui_project_save_phase=UI_PROJECT_SAVE_IDLE;
        if (project_success != 0U)
        {
            ui_page_settings_refresh_project_slots();
            ui_settings_menu_level_t *const level=ui_page_settings_current_level();
            if(level!=NULL && level->view==UI_SETTINGS_VIEW_PROJECT)
                for(uint8_t i=0U;i<g_ui_settings.project_slot_count;++i)
                    if(g_ui_settings.project_slots[i]==project_slot)
                    {level->selected_index=i;break;}
            ui_page_settings_status("SAVE OK");
        }
        else
        {
            ui_page_settings_status("SAVE FAIL");
        }
    }
    if(project_product_rename_take_result(&project_slot,&project_success)!=0U)
    {
        if(project_success!=0U)ui_page_settings_refresh_project_slots();
        ui_page_settings_status(project_success!=0U?"RENAME OK":"RENAME FAIL");
    }

    project_control_ram_load_result_t ram_completion;
    if (project_control_ram_load_take_result(&ram_completion) != 0U)
    {
        const sampler_ram_result_t ram_result =
            (sampler_ram_result_t)ram_completion.result;
        if (ram_completion.success != 0U)
        {
            g_ui_settings.sample_slot_selected = ram_completion.logical_slot;
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

    project_control_wavetable_load_result_t wavetable_completion;
    if (project_control_wavetable_load_take_result(&wavetable_completion) != 0U)
    {
        const wavetable_result_t wavetable_result =
            (wavetable_result_t)wavetable_completion.result;
        if (wavetable_completion.success != 0U)
        {
            g_ui_settings.sample_slot_selected = wavetable_completion.logical_slot;
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
        wav_loader_catalog_view_service();
    if (browser_result == WAV_LOADER_CATALOG_VIEW_PUBLISHED)
    {
        const ui_settings_menu_level_t *const level =
            (g_ui_settings.depth != 0U) ? &g_ui_settings.levels[g_ui_settings.depth - 1U] : 0;
        if ((level != 0)
            && ((level->view == UI_SETTINGS_VIEW_SAMPLER)
                || (level->view == UI_SETTINGS_VIEW_SAMPLE_RAM)))
        {
            (void)ui_page_settings_sample_browser_refresh();
        }
    }
    else if (browser_result == WAV_LOADER_CATALOG_VIEW_ERROR)
    {
        ui_page_settings_status("CAT FAIL");
    }
    ui_page_settings_wavetable_browser_service();

    if (g_ui_settings.convert_slot_valid != 0U)
    {
        if (g_ui_settings.convert_load_pending != 0U)
        {
            const uint16_t slot = g_ui_settings.convert_slot;
            char path[SAMPLE_CLASSIC_PATH_MAX];
            (void)snprintf(path, sizeof(path), "%s", g_ui_settings.convert_path);
            g_ui_settings.convert_slot_valid = 0U;
            g_ui_settings.convert_load_pending = 0U;
            g_ui_settings.convert_path[0] = '\0';
            if (g_ui_settings.convert_load_kind == (uint8_t)UI_SETTINGS_CONVERT_LOAD_RAM)
                ui_page_settings_ram_load_to_slot(slot, path);
            else
                ui_page_settings_sample_load_to_slot(slot, path);
            return;
        }
        wav_convert_service(65536U);
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
            wav_convert_clear_finished();
            g_ui_settings.convert_load_pending = 1U;
            ui_page_settings_status("LOAD WAIT");
            return;
        }
        if (convert_state == WAV_CONVERT_STATE_FAILED)
        {
            const char *const label =
                ui_page_settings_convert_error_label(wav_convert_get_last_error());
            g_ui_settings.convert_slot_valid = 0U;
            g_ui_settings.convert_load_pending = 0U;
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
