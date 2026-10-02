/* SD mutations are deliberately conservative: a path in any loaded bank,
 * active track, saved Patch or saved Project blocks rename and deletion. */
static char g_ui_asset_edit_source[WAV_LOADER_CATALOG_PATH_MAX];
static ui_settings_view_t g_ui_asset_edit_view;

static uint8_t ui_asset_path_matches(const char *path, const char *source,
                                     uint8_t folder)
{
    if (path == NULL || source == NULL) return 0U;
    const size_t length = strlen(source);
    for (size_t i = 0U; i < length; ++i)
    {
        char a = path[i], b = source[i];
        if (a == '\0') return 0U;
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return 0U;
    }
    return folder != 0U ? (path[length] == '/' || path[length] == '\0')
                        : (path[length] == '\0');
}

static uint8_t ui_asset_loaded_or_selected(const char *source, uint8_t folder)
{
    for (uint16_t sample = 0U; sample < MULTI_SAMPLE_POOL_MAX_SAMPLES; ++sample)
    {
        const multi_sample_desc_t *desc = multi_sample_pool_get_sample(sample);
        if (desc != NULL && desc->path[0] != '\0'
            && ui_asset_path_matches(desc->path, source, folder) != 0U)
            return 1U;
    }
    const uint32_t kinds[] = {PERSIST_ASSET_SAMPLE_STREAM, PERSIST_ASSET_SAMPLE_RAM,
                              PERSIST_ASSET_MULTI, PERSIST_ASSET_WAVETABLE};
    uint16_t slots[SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS];
    for (uint8_t k = 0U; k < 4U; ++k)
    {
        uint16_t count = 0U;
        if (k < 2U) count = project_control_list_samples(kinds[k], slots,
                                                          SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS);
        else if (k == 2U) count = project_control_list_multis(slots,
                                                              SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS);
        else count = project_control_list_wavetables(slots,
                                                     SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS);
        for (uint16_t i = 0U; i < count; ++i)
        {
            persist_control_asset_ref_t asset;
            if (project_control_get_logical_asset(kinds[k], slots[i], &asset) != 0U
                && ui_asset_path_matches(asset.canonical_path, source, folder) != 0U)
                return 1U;
        }
    }
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
        for (uint8_t role = 0U; role < PROJECT_CONTROL_ASSET_ROLE_COUNT; ++role)
        {
            persist_control_asset_ref_t asset;
            if (project_control_track_asset_get(entity,
                    (project_control_asset_role_t)role, &asset) != 0U
                && ui_asset_path_matches(asset.canonical_path, source, folder) != 0U)
                return 1U;
        }
    return 0U;
}

/* 0 = absent, 1 = referenced, 2 = read failure. Paths are stored verbatim
 * by persist_control_codec; matching a longer path is intentionally a veto. */
static uint8_t ui_asset_scan_document(const char *document, const char *source)
{
    FIL file;
    FRESULT fr = f_open(&file, document, FA_READ);
    if (fr == FR_NO_FILE || fr == FR_NO_PATH) return 0U;
    if (fr != FR_OK) return 2U;
    const size_t needle_length = strlen(source);
    uint8_t window[512U + WAV_LOADER_CATALOG_PATH_MAX];
    size_t carry = 0U;
    uint8_t result = 0U;
    for (;;)
    {
        UINT read = 0U;
        fr = f_read(&file, window + carry, 512U, &read);
        if (fr != FR_OK) { result = 2U; break; }
        const size_t total = carry + read;
        for (size_t i = 0U; i + needle_length <= total; ++i)
        {
            size_t j = 0U;
            for (; j < needle_length; ++j)
            {
                uint8_t a = window[i + j];
                uint8_t b = (uint8_t)source[j];
                if (a >= 'A' && a <= 'Z') a = (uint8_t)(a + ('a' - 'A'));
                if (b >= 'A' && b <= 'Z') b = (uint8_t)(b + ('a' - 'A'));
                if (a != b) break;
            }
            if (j == needle_length) { result = 1U; break; }
        }
        if (result != 0U || read == 0U) break;
        carry = needle_length > 1U ? needle_length - 1U : 0U;
        if (carry > total) carry = total;
        memmove(window, window + total - carry, carry);
    }
    if (f_close(&file) != FR_OK && result == 0U) result = 2U;
    return result;
}

static uint8_t ui_asset_scan_project_patterns(uint8_t slot,const char *source)
{
    char directory[64],document[96];DIR dir;FILINFO info;
    if(project_storage_patterns_dir(directory,sizeof(directory),slot)==0U)
        return 2U;
    FRESULT fr=f_opendir(&dir,directory);
    if(fr==FR_NO_FILE||fr==FR_NO_PATH)return 0U;
    if(fr!=FR_OK)return 2U;
    uint8_t result=0U;
    while(result==0U)
    {
        fr=f_readdir(&dir,&info);
        if(fr!=FR_OK){result=2U;break;}
        if(info.fname[0]=='\0')break;
        if((info.fattrib&AM_DIR)!=0U||strlen(info.fname)!=11U
            ||info.fname[0]!='B'||info.fname[3]!='_'
            ||info.fname[4]!='P'||strcmp(&info.fname[7],".B6C")!=0)continue;
        const int length=snprintf(document,sizeof(document),"%s/%s",
                                  directory,info.fname);
        if(length<=0||(uint32_t)length>=sizeof(document)){result=2U;break;}
        result=ui_asset_scan_document(document,source);
    }
    if(f_closedir(&dir)!=FR_OK&&result==0U)result=2U;
    return result;
}

static uint8_t ui_asset_has_saved_reference(const char *source)
{
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PREVIEW) == 0U) return 2U;
    uint8_t result = 2U;
    if (sd_access_fs_mount_if_needed() == 0U) goto done;
    result = 0U;
    for (uint16_t slot = 0U; slot < PATCH_PRODUCT_SLOT_COUNT && result == 0U; ++slot)
    {
        char path[48];
        if(project_storage_patch_file(path,sizeof(path),slot)!=0U)
            result = ui_asset_scan_document(path, source);
    }
    for (uint8_t slot = 0U; slot < PROJECT_PRODUCT_SLOT_COUNT && result == 0U; ++slot)
    {
        char project_path[64];
        if(project_storage_project_file(project_path,sizeof(project_path),slot)!=0U)
            result = ui_asset_scan_document(project_path, source);
        if(result==0U)result=ui_asset_scan_project_patterns(slot,source);
    }
done:
    sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
    return result;
}

static uint8_t ui_asset_mutation_allowed(const char *source, uint8_t folder)
{
    if (project_replacement_is_active() != 0U
        || seq_runtime_is_running() != 0U || seq_runtime_is_start_pending() != 0U
        || audio_recorder_is_active() != 0U || wav_convert_is_active() != 0U
        || sample_cache_has_pending_sd_work() != 0U
        || multi_sample_import_is_active() != 0U
        || multi_sample_load_has_pending() != 0U
        || sampler_ram_pool_load_async_busy() != 0U
        || wavetable_pool_load_async_busy() != 0U)
    {
        ui_page_settings_status("STOP AUDIO / BUSY");
        return 0U;
    }
    if (ui_asset_loaded_or_selected(source, folder) != 0U)
    {
        ui_page_settings_status("ASSET IN USE");
        return 0U;
    }
    const uint8_t references = ui_asset_has_saved_reference(source);
    if (references != 0U)
    {
        ui_page_settings_status(references == 1U ? "SAVED REFERENCE" : "SCAN FAIL");
        return 0U;
    }
    return 1U;
}

static uint8_t ui_asset_capture_source(void)
{
    ui_settings_menu_level_t *level = ui_page_settings_current_level();
    if (level == NULL || g_ui_settings.sample_focus != UI_SETTINGS_SAMPLE_FOCUS_LIBRARY)
        return 0U;
    const char *path = NULL;
    if (level->view == UI_SETTINGS_VIEW_MULTI_SAMPLE)
    {
        const ui_settings_multi_entry_t *entry = ui_page_settings_multi_selected_entry();
        if (entry != NULL && entry->type == UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM)
            path = entry->path;
    }
    else
    {
        const ui_settings_sample_entry_t *entry = ui_page_settings_sample_selected_entry();
        if (entry != NULL && entry->type == UI_SETTINGS_SAMPLE_ENTRY_FILE)
            path = entry->path;
    }
    if (path == NULL || strlen(path) >= sizeof(g_ui_asset_edit_source)) return 0U;
    ui_page_settings_copy_bounded(g_ui_asset_edit_source,
                                   sizeof(g_ui_asset_edit_source), path);
    g_ui_asset_edit_view = level->view;
    return 1U;
}

static void ui_page_settings_asset_rename_done(ui_page_name_edit_result_t result,
                                                const char *name, void *user)
{
    (void)user;
    if (result != UI_PAGE_NAME_EDIT_RESULT_CONFIRM) return;
    char normalized[NAME_CONTRACT_BUFFER_BYTES];
    if (name_contract_normalize(name, normalized) != NAME_CONTRACT_RESULT_OK)
    { ui_page_settings_status("BAD NAME"); return; }
    const uint8_t multi = g_ui_asset_edit_view == UI_SETTINGS_VIEW_MULTI_SAMPLE;
    if (ui_asset_mutation_allowed(g_ui_asset_edit_source, multi) == 0U) return;
    const char *slash = strrchr(g_ui_asset_edit_source, '/');
    if (slash == NULL) { ui_page_settings_status("BAD PATH"); return; }
    const char *extension = multi ? "" : strrchr(slash + 1, '.');
    if (extension == NULL) extension = "";
    char target[WAV_LOADER_CATALOG_PATH_MAX];
    const int written = snprintf(target, sizeof(target), "%.*s/%s%s",
        (int)(slash - g_ui_asset_edit_source), g_ui_asset_edit_source,
        normalized, extension);
    if (written <= 0 || (size_t)written >= sizeof(target)
        || ui_asset_path_matches(target, g_ui_asset_edit_source, 0U) != 0U)
    { ui_page_settings_status("BAD NAME"); return; }
    if (g_ui_asset_edit_view == UI_SETTINGS_VIEW_WAVETABLE
        && wavetable_pool_purge_source_cache(g_ui_asset_edit_source) == 0U)
    { ui_page_settings_status("CACHE BUSY"); return; }
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PREVIEW) == 0U)
    { ui_page_settings_sd_busy_status(); return; }
    char retitle_path[MULTI_SAMPLE_POOL_PATH_MAX] = {0};
    FILINFO info;
    FRESULT fr = f_stat(target, &info);
    if (fr == FR_OK) { sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
        ui_page_settings_status("NAME EXISTS"); return; }
    if (fr != FR_NO_FILE && fr != FR_NO_PATH)
    { sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
        ui_page_settings_status("SD FAIL"); return; }
    fr = f_rename(g_ui_asset_edit_source, target);
    if (fr == FR_OK && multi != 0U)
    {
        char old_index[MULTI_SAMPLE_POOL_PATH_MAX];
        char new_index[MULTI_SAMPLE_POOL_PATH_MAX];
        const char *old_name = slash + 1;
        const int a = snprintf(old_index, sizeof(old_index), "%s/%s.brickmulti",
                               target, old_name);
        const int b = snprintf(new_index, sizeof(new_index), "%s/%s.brickmulti",
                               target, normalized);
        if (a <= 0 || b <= 0 || (size_t)a >= sizeof(old_index)
            || (size_t)b >= sizeof(new_index)) fr = FR_INVALID_NAME;
        else if (f_stat(old_index, &info) == FR_OK)
        {
            fr = f_rename(old_index, new_index);
            if (fr == FR_OK)
                ui_page_settings_copy_bounded(retitle_path,
                    sizeof(retitle_path), new_index);
        }
        if (fr != FR_OK) (void)f_rename(target, g_ui_asset_edit_source);
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
    if (fr != FR_OK) { ui_page_settings_status("RENAME FAIL"); return; }
    const uint8_t index_ok = retitle_path[0] == '\0'
        || multi_sample_index_retitle(retitle_path, normalized) == MULTI_SAMPLE_INDEX_OK;
    if (index_ok == 0U)
    {
        if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PREVIEW) != 0U)
        {
            char old_index[MULTI_SAMPLE_POOL_PATH_MAX];
            const int n = snprintf(old_index, sizeof(old_index), "%s/%s.brickmulti",
                                   target, slash + 1);
            if (n > 0 && (size_t)n < sizeof(old_index)
                && f_rename(retitle_path, old_index) == FR_OK
                && f_rename(target, g_ui_asset_edit_source) == FR_OK)
            {
                sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
                ui_page_settings_status("INDEX UPDATE FAIL");
                return;
            }
            sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
        }
    }
    ui_page_settings_asset_refresh(g_ui_asset_edit_view);
    ui_page_settings_status(index_ok != 0U ? "RENAMED" : "RENAME PARTIAL");
}

static void ui_page_settings_asset_rename_begin(void)
{
    if (ui_asset_capture_source() == 0U) return;
    const char *basename = strrchr(g_ui_asset_edit_source, '/');
    basename = basename != NULL ? basename + 1 : g_ui_asset_edit_source;
    char initial[NAME_CONTRACT_BUFFER_BYTES];
    size_t length = strlen(basename);
    if (g_ui_asset_edit_view != UI_SETTINGS_VIEW_MULTI_SAMPLE)
    {
        const char *dot = strrchr(basename, '.');
        if (dot != NULL) length = (size_t)(dot - basename);
    }
    if (length == 0U || length > NAME_CONTRACT_MAX_CHARS)
    { ui_page_settings_status("NAME TOO LONG"); return; }
    memcpy(initial, basename, length);
    initial[length] = '\0';
    if (ui_page_name_edit_open(UI_PAGE_SETTINGS, "ASSET", "RENAME", initial,
            sizeof(initial), ui_page_settings_asset_rename_done, NULL) == 0U)
        ui_page_settings_status("NAME ERROR");
}

static void ui_page_settings_asset_delete_request(void)
{
    if (ui_asset_capture_source() == 0U) return;
    g_ui_settings.sample_confirm = UI_SETTINGS_SAMPLE_CONFIRM_ASSET_DELETE;
    ui_page_settings_status("DELETE? OK/RETURN");
}

static uint8_t ui_asset_delete_multi_folder(const char *folder)
{
    DIR directory;
    FILINFO info;
    if (f_opendir(&directory, folder) != FR_OK) return 0U;
    uint8_t ok = 1U;
    uint16_t file_count = 0U;
    for (;;)
    {
        const FRESULT fr = f_readdir(&directory, &info);
        if (fr != FR_OK) { ok = 0U; break; }
        if (info.fname[0] == '\0') break;
        if (info.fname[0] == '.'
            && (info.fname[1] == '\0'
                || (info.fname[1] == '.' && info.fname[2] == '\0')))
            continue;
        if ((info.fattrib & AM_DIR) != 0U)
        { ok = 0U; break; }
        if (++file_count >= 512U) { ok = 0U; break; }
    }
    (void)f_closedir(&directory);
    if (ok == 0U) return 0U;
    for (uint16_t count = 0U; count < 512U; ++count)
    {
        if (f_opendir(&directory, folder) != FR_OK) return 0U;
        FRESULT fr;
        do { fr = f_readdir(&directory, &info); }
        while (fr == FR_OK && info.fname[0] == '.'
               && (info.fname[1] == '\0'
                   || (info.fname[1] == '.' && info.fname[2] == '\0')));
        (void)f_closedir(&directory);
        if (fr != FR_OK) return 0U;
        if (info.fname[0] == '\0') return f_unlink(folder) == FR_OK;
        char child[WAV_LOADER_CATALOG_PATH_MAX];
        const int n = snprintf(child, sizeof(child), "%s/%s", folder, info.fname);
        if (n <= 0 || (size_t)n >= sizeof(child) || f_unlink(child) != FR_OK)
            return 0U;
    }
    return 0U;
}

static void ui_page_settings_asset_delete_confirm(void)
{
    g_ui_settings.sample_confirm = UI_SETTINGS_SAMPLE_CONFIRM_NONE;
    const uint8_t multi = g_ui_asset_edit_view == UI_SETTINGS_VIEW_MULTI_SAMPLE;
    if (ui_asset_mutation_allowed(g_ui_asset_edit_source, multi) == 0U) return;
    if (g_ui_asset_edit_view == UI_SETTINGS_VIEW_WAVETABLE
        && wavetable_pool_purge_source_cache(g_ui_asset_edit_source) == 0U)
    { ui_page_settings_status("CACHE BUSY"); return; }
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PREVIEW) == 0U)
    { ui_page_settings_sd_busy_status(); return; }
    const uint8_t ok = multi != 0U
        ? ui_asset_delete_multi_folder(g_ui_asset_edit_source)
        : (f_unlink(g_ui_asset_edit_source) == FR_OK ? 1U : 0U);
    sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
    if (ok == 0U) { ui_page_settings_status("DELETE FAIL"); return; }
    ui_page_settings_asset_refresh(g_ui_asset_edit_view);
    ui_page_settings_status("DELETED");
}
