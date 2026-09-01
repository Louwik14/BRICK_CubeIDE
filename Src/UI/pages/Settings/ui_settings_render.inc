static void ui_page_settings_copy_bounded(char *out, uint32_t out_size, const char *src)
{
    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    out[0] = '\0';
    if (src == 0)
    {
        return;
    }

    uint32_t len = (uint32_t)strlen(src);
    if (len >= out_size)
    {
        len = out_size - 1U;
    }

    (void)memcpy(out, src, len);
    out[len] = '\0';
}

static void ui_page_settings_build_middle_ellipsis(char *out,
                                                   uint32_t out_size,
                                                   const char *src,
                                                   uint32_t keep_chars)
{
    static const char k_ellipsis[] = "~";

    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    out[0] = '\0';
    if ((src == 0) || (keep_chars == 0U))
    {
        return;
    }

    const uint32_t src_len = (uint32_t)strlen(src);
    if (src_len <= keep_chars)
    {
        ui_page_settings_copy_bounded(out, out_size, src);
        return;
    }

    if (keep_chars + 2U > out_size)
    {
        keep_chars = (out_size > 2U) ? (out_size - 2U) : 0U;
    }

    const uint32_t prefix_len = (keep_chars + 1U) / 2U;
    const uint32_t suffix_len = keep_chars - prefix_len;
    uint32_t pos = 0U;

    if (prefix_len != 0U)
    {
        (void)memcpy(&out[pos], src, prefix_len);
        pos += prefix_len;
    }

    (void)memcpy(&out[pos], k_ellipsis, 1U);
    pos += 1U;

    if (suffix_len != 0U)
    {
        (void)memcpy(&out[pos], &src[src_len - suffix_len], suffix_len);
        pos += suffix_len;
    }

    out[pos] = '\0';
}

static void ui_page_settings_fit_label(char *out, uint32_t out_size, const char *src, uint8_t max_px)
{
    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    out[0] = '\0';
    if (src == 0)
    {
        return;
    }

    ui_page_settings_copy_bounded(out, out_size, src);
    if (drv_display_text_width(out) <= max_px)
    {
        return;
    }

    const uint32_t src_len = (uint32_t)strlen(src);
    uint32_t max_keep = (out_size > 2U) ? (out_size - 2U) : 0U;
    if (max_keep > src_len)
    {
        max_keep = src_len;
    }

    for (uint32_t keep = max_keep; keep > 0U; --keep)
    {
        ui_page_settings_build_middle_ellipsis(out, out_size, src, keep);
        if (drv_display_text_width(out) <= max_px)
        {
            return;
        }
    }

    ui_page_settings_copy_bounded(out, out_size, "~");
    while ((out[0] != '\0') && (drv_display_text_width(out) > max_px))
    {
        const uint32_t len = (uint32_t)strlen(out);
        out[len - 1U] = '\0';
    }
}

static void ui_page_settings_sample_slot_label(uint16_t global_slot, char *out, uint32_t out_size, uint8_t max_px)
{
    char sample_name[SAMPLE_CLASSIC_PATH_MAX];
    static const char k_loaded[] = "LOADED";
    static const char k_prep[] = "PREP";
    static const char k_error[] = "ERROR";
    static const char k_missing[] = "MISS";
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;

    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    out[0] = '\0';
    if (out_size < 5U)
    {
        return;
    }

    (void)snprintf(out, out_size, "%03u ", (unsigned)global_slot);

    const uint8_t prefix_px = drv_display_text_width(out);
    const uint8_t name_px = (max_px > prefix_px) ? (uint8_t)(max_px - prefix_px) : 0U;

    if (ui_page_settings_stream_backend_from_global(global_slot, &backend_slot) == 0U)
    {
        ui_page_settings_fit_label(&out[4], (out_size > 4U) ? (out_size - 4U) : 0U, "BADREF", name_px);
        return;
    }

    const sample_classic_slot_state_t state = sample_global_pool_get_classic_state(backend_slot);
    const sample_global_slot_t *const slot = sample_global_pool_get_slot(global_slot);
    const char *name = (slot != 0) ? strrchr(slot->path, '/') : 0;
    name = (name != 0) ? (name + 1) : ((slot != 0) ? slot->path : "");

    ui_page_settings_make_sample_label(sample_name, sizeof(sample_name), name, 0U);
    if (sample_name[0] == '\0')
    {
        const char *state_label = (state == SAMPLE_CLASSIC_SLOT_LOADED) ? k_loaded
                                 : (state == SAMPLE_CLASSIC_SLOT_PREPARING) ? k_prep
                                 : (state == SAMPLE_CLASSIC_SLOT_ERROR) ? k_error
                                 : k_missing;
        ui_page_settings_fit_label(&out[4], (out_size > 4U) ? (out_size - 4U) : 0U, state_label, name_px);
        return;
    }

    ui_page_settings_fit_label(&out[4], (out_size > 4U) ? (out_size - 4U) : 0U, sample_name, name_px);
}

static void ui_page_settings_multi_left_label(const ui_settings_multi_entry_t *entry,
                                              char *out,
                                              uint32_t out_size,
                                              uint8_t max_px)
{
    char raw[48];
    if ((entry == 0) || (out == 0) || (out_size == 0U))
    {
        return;
    }

    if (entry->type == UI_SETTINGS_MULTI_ENTRY_NAV_FOLDER)
    {
        (void)snprintf(raw, sizeof(raw), "%s", entry->label);
    }
    else if (entry->type == UI_SETTINGS_MULTI_ENTRY_EMPTY_FOLDER)
    {
        (void)snprintf(raw, sizeof(raw), "%s EMPTY", entry->label);
    }
    else if (entry->prepared != 0U)
    {
        (void)snprintf(raw, sizeof(raw), "%s %03u", entry->label, (unsigned)entry->sample_count);
    }
    else
    {
        (void)snprintf(raw, sizeof(raw), "%s NEW", entry->label);
    }
    ui_page_settings_fit_label(out, out_size, raw, max_px);
}

static void ui_page_settings_multi_slot_label(uint16_t global_slot, char *out, uint32_t out_size, uint8_t max_px)
{
    char raw[48];
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    const multi_sample_instrument_t *instrument = 0;
    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    if (ui_page_settings_multi_backend_from_global(global_slot, &backend_slot) != 0U)
    {
        instrument = multi_sample_pool_get_instrument(backend_slot);
    }

    if (instrument == 0)
    {
        (void)snprintf(raw, sizeof(raw), "%03u BADREF", (unsigned)global_slot);
    }
    else
    {
        (void)snprintf(raw,
                       sizeof(raw),
                       "%03u %s %03u",
                       (unsigned)global_slot,
                       instrument->name,
                       (unsigned)instrument->sample_count);
    }
    ui_page_settings_fit_label(out, out_size, raw, max_px);
}

static void ui_page_settings_ram_slot_label(uint16_t global_slot, char *out, uint32_t out_size, uint8_t max_px)
{
    char raw[48];
    char name_buf[24];
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    const sampler_ram_slot_t *ram = 0;
    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    if (ui_page_settings_ram_backend_from_global(global_slot, &backend_slot) != 0U)
    {
        ram = sampler_ram_pool_get_slot(backend_slot);
    }

    if (ram == 0)
    {
        (void)snprintf(raw, sizeof(raw), "%03u BADREF", (unsigned)global_slot);
    }
    else if (ram->state == SAMPLER_RAM_SLOT_READY)
    {
        const char *name = strrchr(ram->path, '/');
        name = (name != 0) ? (name + 1) : ram->path;
        ui_page_settings_make_sample_label(name_buf, sizeof(name_buf), name, 0U);
        (void)snprintf(raw,
                       sizeof(raw),
                       "%03u %s",
                       (unsigned)global_slot,
                       (name_buf[0] != '\0') ? name_buf : "READY");
    }
    else
    {
        const char *state = (ram->state == SAMPLER_RAM_SLOT_LOADING) ? "LOAD"
                          : (ram->state == SAMPLER_RAM_SLOT_ERROR) ? "ERROR"
                          : "EMPTY";
        (void)snprintf(raw, sizeof(raw), "%03u %s", (unsigned)global_slot, state);
    }
    ui_page_settings_fit_label(out, out_size, raw, max_px);
}

static void ui_page_settings_wavetable_slot_label(uint16_t global_slot,
                                                  char *out,
                                                  uint32_t out_size,
                                                  uint8_t max_px)
{
    char raw[48];
    char name_buf[24];
    uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    const wavetable_slot_t *table = 0;
    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    if (ui_page_settings_wavetable_backend_from_global(global_slot, &backend_slot) != 0U)
    {
        table = wavetable_pool_get_slot(backend_slot);
    }

    if (table == 0)
    {
        (void)snprintf(raw, sizeof(raw), "%03u BADREF", (unsigned)global_slot);
    }
    else if (table->state == WAVETABLE_SLOT_READY)
    {
        const char *name = strrchr(table->path, '/');
        name = (name != 0) ? (name + 1) : table->path;
        ui_page_settings_make_wavetable_label(name_buf, sizeof(name_buf), name, 0U);
        (void)snprintf(raw,
                       sizeof(raw),
                       "%03u %s",
                       (unsigned)global_slot,
                       (name_buf[0] != '\0') ? name_buf : "READY");
    }
    else
    {
        const char *state = (table->state == WAVETABLE_SLOT_LOADING) ? "LOAD"
                          : (table->state == WAVETABLE_SLOT_ERROR) ? "ERROR"
                          : "EMPTY";
        (void)snprintf(raw, sizeof(raw), "%03u %s", (unsigned)global_slot, state);
    }
    ui_page_settings_fit_label(out, out_size, raw, max_px);
}

static void ui_page_settings_draw_ram_name_label(void)
{
    char label_buf[24];
    const char *label = ui_page_settings_basename(g_ui_settings.sample_dir);
    const char *tag = "DIR";
    const ui_settings_sample_entry_t *const entry = ui_page_settings_sample_selected_entry();

    if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS)
    {
        uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        const sampler_ram_slot_t *const ram =
            (ui_page_settings_ram_backend_from_global(g_ui_settings.sample_slot_selected,
                                                      &backend_slot) != 0U)
                ? sampler_ram_pool_get_slot(backend_slot)
                : 0;
        if ((ram != 0) && (ram->path[0] != '\0'))
        {
            const char *name = strrchr(ram->path, '/');
            name = (name != 0) ? (name + 1) : ram->path;
            ui_page_settings_make_sample_label(label_buf, sizeof(label_buf), name, 0U);
            label = (label_buf[0] != '\0') ? label_buf : "READY";
        }
        else
        {
            label = "EMPTY";
        }
        ui_page_settings_draw_browser_context_label("SLOT", label);
        return;
    }

    if (entry != 0)
    {
        label = entry->label;
        tag = (entry->type == UI_SETTINGS_SAMPLE_ENTRY_FILE) ? "WAV" : "DIR";
    }
    ui_page_settings_draw_browser_context_label(tag, label);
}

static void ui_page_settings_draw_wavetable_name_label(void)
{
    char label_buf[24];
    const char *label = ui_page_settings_basename(g_ui_settings.sample_dir);
    const char *tag = "DIR";
    const ui_settings_sample_entry_t *const entry = ui_page_settings_sample_selected_entry();

    if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS)
    {
        uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        const wavetable_slot_t *const table =
            (ui_page_settings_wavetable_backend_from_global(g_ui_settings.sample_slot_selected,
                                                            &backend_slot) != 0U)
                ? wavetable_pool_get_slot(backend_slot)
                : 0;
        if ((table != 0) && (table->path[0] != '\0'))
        {
            const char *name = strrchr(table->path, '/');
            name = (name != 0) ? (name + 1) : table->path;
            ui_page_settings_make_wavetable_label(label_buf, sizeof(label_buf), name, 0U);
            label = (label_buf[0] != '\0') ? label_buf : "READY";
        }
        else
        {
            label = "EMPTY";
        }
        ui_page_settings_draw_browser_context_label("SLOT", label);
        return;
    }

    if (entry != 0)
    {
        label = entry->label;
        tag = (entry->type == UI_SETTINGS_SAMPLE_ENTRY_FILE) ? "WAV" : "DIR";
    }
    ui_page_settings_draw_browser_context_label(tag, label);
}

static uint16_t ui_page_settings_clamp_scroll(uint16_t scroll,
                                              uint16_t selected,
                                              uint16_t count,
                                              uint8_t visible_lines)
{
    if ((count == 0U) || (count <= visible_lines))
    {
        return 0U;
    }

    const uint16_t max_start = (uint16_t)(count - visible_lines);
    if (scroll > max_start)
    {
        scroll = max_start;
    }
    if (selected < scroll)
    {
        scroll = selected;
    }
    else if (selected >= (uint16_t)(scroll + visible_lines))
    {
        scroll = (uint16_t)(selected - (visible_lines - 1U));
    }

    return scroll;
}

static void ui_page_settings_draw_centered_label(uint8_t x, uint8_t w, uint8_t y, const char *label)
{
    if ((label == 0) || (w == 0U))
    {
        return;
    }

    const uint8_t text_w = drv_display_text_width(label);
    const uint8_t text_x = (text_w >= w) ? x : (uint8_t)(x + ((w - text_w) / 2U));
    drv_display_draw_text(text_x, y, label);
}

static void ui_page_settings_draw_progress_bar(uint8_t x,
                                               uint8_t y,
                                               uint8_t w,
                                               uint8_t h,
                                               uint16_t done,
                                               uint16_t total)
{
    if ((w < 3U) || (h < 3U))
    {
        return;
    }

    if (total == 0U)
    {
        total = 1U;
    }
    if (done > total)
    {
        done = total;
    }

    drv_display_draw_rect(x, y, w, h);
    const uint8_t inner_w = (uint8_t)(w - 2U);
    const uint8_t fill_w = (uint8_t)(((uint32_t)inner_w * done) / total);
    if (fill_w != 0U)
    {
        drv_display_fill_rect((int)x + 1, (int)y + 1, fill_w, (int)h - 2);
    }
}

static void ui_page_settings_draw_page_footer_ex(const char *page3_label,
                                                 const char *page4_label)
{
    drv_display_set_font(&FONT_4X6);
    ui_page_settings_draw_centered_label(0U, 32U, UI_SETTINGS_FOOTER_LABEL_Y, "RETURN");
    ui_page_settings_draw_centered_label(32U, 32U, UI_SETTINGS_FOOTER_LABEL_Y, "OK");
    ui_page_settings_draw_centered_label(64U, 32U, UI_SETTINGS_FOOTER_LABEL_Y, page3_label);
    ui_page_settings_draw_centered_label(96U, 32U, UI_SETTINGS_FOOTER_LABEL_Y, page4_label);
}

static void ui_page_settings_draw_page_footer(const char *page4_label)
{
    ui_page_settings_draw_page_footer_ex("-", page4_label);
}

static void ui_page_settings_draw_sample_footer(void)
{
    ui_page_settings_draw_page_footer((button_down(BTN_SHIFT) != 0U) ? "REBUILD" : "REFRESH");
}

static const char *ui_page_settings_basename(const char *path)
{
    if ((path == 0) || (path[0] == '\0'))
    {
        return "-";
    }

    const char *const slash = strrchr(path, '/');
    return ((slash != 0) && (slash[1] != '\0')) ? (slash + 1) : path;
}

static void ui_page_settings_draw_browser_context_label(const char *tag, const char *label)
{
    char line[32];
    char tag_buf[6];

    drv_display_set_font(&FONT_4X6);
    ui_page_settings_fit_label(tag_buf, sizeof(tag_buf), tag, 16U);
    const uint8_t tag_box_w = (uint8_t)(drv_display_text_width(tag_buf) + 4U);
    drv_display_fill_rect(0, UI_SETTINGS_SAMPLE_BROWSER_PATH_LABEL_Y - 1U, tag_box_w, 7);
    drv_display_draw_text_inverted(2U, UI_SETTINGS_SAMPLE_BROWSER_PATH_LABEL_Y, tag_buf);
    ui_page_settings_fit_label(line, sizeof(line), label, 106U);
    drv_display_draw_text((uint8_t)(tag_box_w + 4U), UI_SETTINGS_SAMPLE_BROWSER_PATH_LABEL_Y, line);
    drv_display_draw_line(0,
                          UI_SETTINGS_SAMPLE_BROWSER_PATH_LINE_Y,
                          127,
                          UI_SETTINGS_SAMPLE_BROWSER_PATH_LINE_Y);
}

static void ui_page_settings_draw_stream_name_label(void)
{
    char slot_label[24];
    const ui_settings_sample_entry_t *const entry = ui_page_settings_sample_selected_entry();
    const char *label = ui_page_settings_basename(g_ui_settings.sample_dir);
    const char *tag = "DIR";

    if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS)
    {
        uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        const sample_global_slot_t *const slot =
            (ui_page_settings_stream_backend_from_global(g_ui_settings.sample_slot_selected,
                                                         &backend_slot) != 0U)
                ? sample_global_pool_get_slot(g_ui_settings.sample_slot_selected)
                : 0;
        const char *name = (slot != 0) ? strrchr(slot->path, '/') : 0;
        name = (name != 0) ? (name + 1) : ((slot != 0) ? slot->path : "");
        ui_page_settings_make_sample_label(slot_label, sizeof(slot_label), name, 0U);
        label = (slot_label[0] != '\0') ? slot_label : "EMPTY";
        tag = "SLOT";
        ui_page_settings_draw_browser_context_label(tag, label);
        return;
    }

    if (entry != 0)
    {
        label = entry->label;
        tag = (entry->type == UI_SETTINGS_SAMPLE_ENTRY_FILE) ? "WAV" : "DIR";
    }
    ui_page_settings_draw_browser_context_label(tag, label);
}

static void ui_page_settings_draw_multi_name_label(void)
{
    const char *label = "NO MULTI";
    const char *tag = "INS";
    const ui_settings_multi_entry_t *const entry = ui_page_settings_multi_selected_entry();

    if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS)
    {
        uint16_t backend_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        const multi_sample_instrument_t *const instrument =
            (ui_page_settings_multi_backend_from_global(g_ui_settings.sample_slot_selected,
                                                        &backend_slot) != 0U)
                ? multi_sample_pool_get_instrument(backend_slot)
                : 0;
        label = (instrument != 0) ? instrument->name : "EMPTY";
        ui_page_settings_draw_browser_context_label("SLOT", label);
        return;
    }

    if (entry != 0)
    {
        label = entry->label;
        tag = (entry->type == UI_SETTINGS_MULTI_ENTRY_MULTI_ITEM) ? "INS"
            : (entry->type == UI_SETTINGS_MULTI_ENTRY_NAV_FOLDER) ? "DIR"
            : "EMPTY";
    }
    ui_page_settings_draw_browser_context_label(tag, label);
}

static const char *ui_page_settings_multi_prepare_phase_label(void)
{
    switch ((ui_settings_multi_prepare_phase_t)g_ui_settings.multi_prepare_phase)
    {
        case UI_SETTINGS_MULTI_PREP_PHASE_SCAN:
            return "Scan multi";
        case UI_SETTINGS_MULTI_PREP_PHASE_COMMIT:
            return "Commit multi";
        case UI_SETTINGS_MULTI_PREP_PHASE_PREPARE:
            return "Prepare samples";
        case UI_SETTINGS_MULTI_PREP_PHASE_REFRESH:
            return "Refresh multi";
        default:
            return "Preparing multi";
    }
}

static void ui_page_settings_draw_sample_split_position(uint16_t sample_total, uint16_t slot_total)
{
    enum
    {
        UI_SETTINGS_SPLIT_X = 60,
        UI_SETTINGS_SPLIT_Y0 = UI_SETTINGS_SAMPLE_BROWSER_TEXT_Y0 - 1U,
        UI_SETTINGS_SPLIT_Y1 = 51,
        UI_SETTINGS_SPLIT_H = UI_SETTINGS_SPLIT_Y1 - UI_SETTINGS_SPLIT_Y0,
        UI_SETTINGS_SPLIT_CURSOR_MAX_H = 9,
        UI_SETTINGS_SPLIT_CURSOR_MIN_H = 3,
        UI_SETTINGS_SPLIT_CURSOR_REF_ITEMS = 64
    };

    drv_display_draw_line(UI_SETTINGS_SPLIT_X, UI_SETTINGS_SPLIT_Y0, UI_SETTINGS_SPLIT_X, UI_SETTINGS_SPLIT_Y1);

    uint16_t selected = 0U;
    uint16_t total = 0U;
    if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY)
    {
        selected = g_ui_settings.sample_selected;
        total = sample_total;
    }
    else if (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS)
    {
        selected = ui_page_settings_filtered_index_for_global(g_ui_settings.sample_slot_selected);
        total = slot_total;
    }
    else
    {
        return;
    }

    if (total <= 1U)
    {
        return;
    }
    if (selected >= total)
    {
        selected = (uint16_t)(total - 1U);
    }

    const uint16_t ref_total = (total > (uint16_t)UI_SETTINGS_SPLIT_CURSOR_REF_ITEMS)
        ? total
        : (uint16_t)UI_SETTINGS_SPLIT_CURSOR_REF_ITEMS;
    uint8_t cursor_h =
        (uint8_t)(((uint32_t)UI_SETTINGS_SPLIT_CURSOR_MAX_H
                   * UI_SETTINGS_SPLIT_CURSOR_REF_ITEMS
                   + ((uint32_t)ref_total / 2U))
                  / ref_total);
    if (cursor_h > (uint8_t)UI_SETTINGS_SPLIT_CURSOR_MAX_H)
    {
        cursor_h = (uint8_t)UI_SETTINGS_SPLIT_CURSOR_MAX_H;
    }
    if (cursor_h < (uint8_t)UI_SETTINGS_SPLIT_CURSOR_MIN_H)
    {
        cursor_h = (uint8_t)UI_SETTINGS_SPLIT_CURSOR_MIN_H;
    }
    if ((cursor_h & 1U) == 0U)
    {
        cursor_h--;
    }

    const uint16_t travel = (uint16_t)(UI_SETTINGS_SPLIT_H - cursor_h + 1U);
    const uint16_t top =
        (uint16_t)(UI_SETTINGS_SPLIT_Y0
                   + ((((uint32_t)selected * travel) + ((uint32_t)(total - 1U) / 2U))
                      / (uint32_t)(total - 1U)));
    const uint16_t bottom = (uint16_t)(top + cursor_h - 1U);
    const uint16_t mid = (uint16_t)(top + (cursor_h / 2U));
    drv_display_clear_rect(UI_SETTINGS_SPLIT_X - 1, (int)top, 3, cursor_h);
    drv_display_draw_line(UI_SETTINGS_SPLIT_X - 1, (int)top, UI_SETTINGS_SPLIT_X - 1, (int)bottom);
    drv_display_draw_line(UI_SETTINGS_SPLIT_X + 1, (int)top, UI_SETTINGS_SPLIT_X + 1, (int)bottom);
    drv_display_draw_pixel(UI_SETTINGS_SPLIT_X, (int)top, true);
    drv_display_draw_pixel(UI_SETTINGS_SPLIT_X, (int)bottom, true);
    if (cursor_h <= 3U)
    {
        drv_display_draw_pixel(UI_SETTINGS_SPLIT_X, (int)mid, false);
    }
}

static void ui_page_settings_render_multi_browser(void)
{
    enum
    {
        UI_SETTINGS_SAMPLE_LEFT_TEXT_X = 1,
        UI_SETTINGS_SAMPLE_LEFT_TEXT_W = 58,
        UI_SETTINGS_SAMPLE_RIGHT_TEXT_X = 64,
        UI_SETTINGS_SAMPLE_RIGHT_TEXT_W = 62
    };

    ui_page_settings_multi_prepare_poll();

    ui_page_settings_draw_multi_sample_header();
    drv_display_draw_line(0, UI_SETTINGS_SAMPLE_BROWSER_HEADER_LINE_Y, 127, UI_SETTINGS_SAMPLE_BROWSER_HEADER_LINE_Y);
    ui_page_settings_draw_multi_name_label();

    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARE)
    {
        drv_display_set_font(&FONT_5X7);
        ui_page_settings_draw_centered_label(0U, 128U, 34U, "Prepare multi ?");
        ui_page_settings_draw_page_footer("-");
        return;
    }

    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_CLEAR_INDEX)
    {
        drv_display_set_font(&FONT_5X7);
        ui_page_settings_draw_centered_label(0U, 128U, 30U, "Clear indexes ?");
        drv_display_set_font(&FONT_4X6);
        ui_page_settings_draw_centered_label(0U, 128U, 42U, "WAV files stay");
        ui_page_settings_draw_page_footer("-");
        return;
    }

    if (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARING)
    {
        drv_display_set_font(&FONT_5X7);
        ui_page_settings_draw_centered_label(0U,
                                             128U,
                                             30U,
                                             ui_page_settings_multi_prepare_phase_label());
        ui_page_settings_draw_progress_bar(8U,
                                           56U,
                                           112U,
                                           6U,
                                           g_ui_settings.multi_prepare_progress_done,
                                           g_ui_settings.multi_prepare_progress_total);
        ui_page_settings_draw_page_footer("CANCEL");
        drv_display_set_font(&FONT_5X7);
        return;
    }

    ui_page_settings_draw_sample_split_position(g_ui_settings.multi_entry_count,
                                                g_ui_settings.sampler_slot_count);
    drv_display_set_font(&FONT_4X6);

    const uint8_t visible_lines = UI_SETTINGS_SAMPLE_BROWSER_VISIBLE_LINES;
    const uint16_t selected_right_index =
        ui_page_settings_filtered_index_for_global(g_ui_settings.sample_slot_selected);
    g_ui_settings.sample_left_scroll = ui_page_settings_clamp_scroll(g_ui_settings.sample_left_scroll,
                                                                     g_ui_settings.sample_selected,
                                                                     g_ui_settings.multi_entry_count,
                                                                     visible_lines);
    g_ui_settings.sample_right_scroll = ui_page_settings_clamp_scroll(g_ui_settings.sample_right_scroll,
                                                                      selected_right_index,
                                                                      g_ui_settings.sampler_slot_count,
                                                                      visible_lines);

    for (uint8_t line = 0U; line < visible_lines; ++line)
    {
        const uint8_t y = (uint8_t)(UI_SETTINGS_SAMPLE_BROWSER_TEXT_Y0
                                    + (line * UI_SETTINGS_SAMPLE_BROWSER_TEXT_PITCH));
        const uint16_t left_index = (uint16_t)(g_ui_settings.sample_left_scroll + line);
        const uint16_t right_index = (uint16_t)(g_ui_settings.sample_right_scroll + line);

        if (left_index < g_ui_settings.multi_entry_count)
        {
            char left[32];
            ui_page_settings_multi_left_label(&g_ui_settings.multi_entries[left_index],
                                              left,
                                              sizeof(left),
                                              (uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_W);
            if ((left_index == g_ui_settings.sample_selected)
                && (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY))
            {
                drv_display_fill_rect(0, y - 1U, 58, UI_SETTINGS_SAMPLE_BROWSER_SELECT_H);
                drv_display_draw_text_inverted((uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_X, y, left);
            }
            else
            {
                drv_display_draw_text((uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_X, y, left);
            }
        }

        if (right_index < g_ui_settings.sampler_slot_count)
        {
            char right[32];
            const uint16_t global_slot = g_ui_settings.sampler_slots[right_index];
            ui_page_settings_multi_slot_label(global_slot,
                                              right,
                                              sizeof(right),
                                              (uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_W);
            if ((global_slot == g_ui_settings.sample_slot_selected)
                && (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS))
            {
                drv_display_fill_rect(62, y - 1U, 66, UI_SETTINGS_SAMPLE_BROWSER_SELECT_H);
                drv_display_draw_text_inverted((uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_X, y, right);
            }
            else
            {
                drv_display_draw_text((uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_X, y, right);
            }
        }
    }

    if (g_ui_settings.status_line[0] != '\0')
    {
        drv_display_draw_text(0U, 54U, g_ui_settings.status_line);
        drv_display_set_font(&FONT_5X7);
        return;
    }

    ui_page_settings_draw_page_footer_ex("CLEAR", "-");
    drv_display_set_font(&FONT_5X7);
}

static void ui_page_settings_multi_prepare_flush_progress(void)
{
    drv_display_clear();
    ui_page_settings_render_multi_browser();
    drv_display_update();
}

static void ui_page_settings_render_sample_browser(void)
{
    enum
    {
        UI_SETTINGS_SAMPLE_LEFT_TEXT_X = 1,
        UI_SETTINGS_SAMPLE_LEFT_TEXT_W = 58,
        UI_SETTINGS_SAMPLE_RIGHT_TEXT_X = 64,
        UI_SETTINGS_SAMPLE_RIGHT_TEXT_W = 62
    };

    ui_page_settings_draw_global_sample_header("STREAM");
    drv_display_draw_line(0, UI_SETTINGS_SAMPLE_BROWSER_HEADER_LINE_Y, 127, UI_SETTINGS_SAMPLE_BROWSER_HEADER_LINE_Y);
    ui_page_settings_draw_stream_name_label();
    ui_page_settings_draw_sample_split_position(g_ui_settings.sample_child_count,
                                                g_ui_settings.sampler_slot_count);
    drv_display_set_font(&FONT_4X6);

    const uint8_t visible_lines = UI_SETTINGS_SAMPLE_BROWSER_VISIBLE_LINES;
    const uint16_t selected_right_index =
        ui_page_settings_filtered_index_for_global(g_ui_settings.sample_slot_selected);
    g_ui_settings.sample_left_scroll = ui_page_settings_clamp_scroll(g_ui_settings.sample_left_scroll,
                                                                     g_ui_settings.sample_selected,
                                                                     g_ui_settings.sample_child_count,
                                                                     visible_lines);
    g_ui_settings.sample_right_scroll = ui_page_settings_clamp_scroll(g_ui_settings.sample_right_scroll,
                                                                      selected_right_index,
                                                                      g_ui_settings.sampler_slot_count,
                                                                      visible_lines);
    const uint16_t left_start = g_ui_settings.sample_left_scroll;
    const uint8_t right_start = g_ui_settings.sample_right_scroll;

    for (uint8_t line = 0U; line < visible_lines; ++line)
    {
        const uint8_t y = (uint8_t)(UI_SETTINGS_SAMPLE_BROWSER_TEXT_Y0
                                    + (line * UI_SETTINGS_SAMPLE_BROWSER_TEXT_PITCH));
        const uint16_t left_index = (uint16_t)(left_start + line);
        const uint16_t right_index = (uint16_t)(right_start + line);

        if ((left_index >= g_ui_settings.sample_page_start)
            && (left_index < (uint16_t)(g_ui_settings.sample_page_start
                                        + g_ui_settings.sample_entry_count)))
        {
            char left[32];
            const ui_settings_sample_entry_t *const entry =
                &g_ui_settings.sample_entries[left_index - g_ui_settings.sample_page_start];
            ui_page_settings_fit_label(left,
                                       sizeof(left),
                                       entry->label,
                                       (uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_W);
            if ((left_index == g_ui_settings.sample_selected)
                && (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY))
            {
                drv_display_fill_rect(0, y - 1U, 58, UI_SETTINGS_SAMPLE_BROWSER_SELECT_H);
                drv_display_draw_text_inverted((uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_X, y, left);
            }
            else
            {
                drv_display_draw_text((uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_X, y, left);
            }
        }

        if (right_index < g_ui_settings.sampler_slot_count)
        {
            char right[32];
            const uint16_t global_slot = g_ui_settings.sampler_slots[right_index];
            ui_page_settings_sample_slot_label(global_slot,
                                               right,
                                               sizeof(right),
                                               (uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_W);
            if ((global_slot == g_ui_settings.sample_slot_selected)
                && (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS))
            {
                drv_display_fill_rect(62, y - 1U, 66, UI_SETTINGS_SAMPLE_BROWSER_SELECT_H);
                drv_display_draw_text_inverted((uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_X, y, right);
            }
            else
            {
                drv_display_draw_text((uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_X, y, right);
            }
        }
    }

    if (g_ui_settings.status_line[0] != '\0')
    {
        drv_display_draw_text(0U, 54U, g_ui_settings.status_line);
        drv_display_set_font(&FONT_5X7);
        return;
    }

    ui_page_settings_draw_sample_footer();
    drv_display_set_font(&FONT_5X7);
}

static void ui_page_settings_render_ram_browser(void)
{
    enum
    {
        UI_SETTINGS_SAMPLE_LEFT_TEXT_X = 1,
        UI_SETTINGS_SAMPLE_LEFT_TEXT_W = 58,
        UI_SETTINGS_SAMPLE_RIGHT_TEXT_X = 64
    };

    ui_page_settings_draw_global_sample_header("RAM");
    drv_display_draw_line(0, UI_SETTINGS_SAMPLE_BROWSER_HEADER_LINE_Y, 127, UI_SETTINGS_SAMPLE_BROWSER_HEADER_LINE_Y);
    ui_page_settings_draw_ram_name_label();
    ui_page_settings_draw_sample_split_position(g_ui_settings.sample_child_count,
                                                g_ui_settings.sampler_slot_count);
    drv_display_set_font(&FONT_4X6);

    const uint8_t visible_lines = UI_SETTINGS_SAMPLE_BROWSER_VISIBLE_LINES;
    const uint16_t selected_right_index =
        ui_page_settings_filtered_index_for_global(g_ui_settings.sample_slot_selected);
    g_ui_settings.sample_left_scroll = ui_page_settings_clamp_scroll(g_ui_settings.sample_left_scroll,
                                                                     g_ui_settings.sample_selected,
                                                                     g_ui_settings.sample_child_count,
                                                                     visible_lines);
    g_ui_settings.sample_right_scroll = ui_page_settings_clamp_scroll(g_ui_settings.sample_right_scroll,
                                                                      selected_right_index,
                                                                      g_ui_settings.sampler_slot_count,
                                                                      visible_lines);
    for (uint8_t line = 0U; line < visible_lines; ++line)
    {
        const uint8_t y = (uint8_t)(UI_SETTINGS_SAMPLE_BROWSER_TEXT_Y0
                                    + (line * UI_SETTINGS_SAMPLE_BROWSER_TEXT_PITCH));
        const uint16_t left_index = (uint16_t)(g_ui_settings.sample_left_scroll + line);
        const uint16_t right_index = (uint16_t)(g_ui_settings.sample_right_scroll + line);

        if ((left_index >= g_ui_settings.sample_page_start)
            && (left_index < (uint16_t)(g_ui_settings.sample_page_start
                                        + g_ui_settings.sample_entry_count)))
        {
            char left[32];
            const ui_settings_sample_entry_t *const entry =
                &g_ui_settings.sample_entries[left_index - g_ui_settings.sample_page_start];
            ui_page_settings_fit_label(left,
                                       sizeof(left),
                                       entry->label,
                                       (uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_W);
            if ((left_index == g_ui_settings.sample_selected)
                && (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY))
            {
                drv_display_fill_rect(0, y - 1U, 58, UI_SETTINGS_SAMPLE_BROWSER_SELECT_H);
                drv_display_draw_text_inverted((uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_X, y, left);
            }
            else
            {
                drv_display_draw_text((uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_X, y, left);
            }
        }

        if (right_index >= g_ui_settings.sampler_slot_count)
        {
            continue;
        }
        const uint16_t global_slot = g_ui_settings.sampler_slots[right_index];
        char right[24];
        ui_page_settings_ram_slot_label(global_slot, right, sizeof(right), 62U);
        if ((global_slot == g_ui_settings.sample_slot_selected)
            && (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS))
        {
            drv_display_fill_rect(62, y - 1U, 66, UI_SETTINGS_SAMPLE_BROWSER_SELECT_H);
            drv_display_draw_text_inverted((uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_X, y, right);
        }
        else
        {
            drv_display_draw_text((uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_X, y, right);
        }
    }

    if (g_ui_settings.status_line[0] != '\0')
    {
        drv_display_draw_text(0U, 54U, g_ui_settings.status_line);
        drv_display_set_font(&FONT_5X7);
        return;
    }

    ui_page_settings_draw_sample_footer();
    drv_display_set_font(&FONT_5X7);
}

static void ui_page_settings_render_wavetable_browser(void)
{
    enum
    {
        UI_SETTINGS_SAMPLE_LEFT_TEXT_X = 1,
        UI_SETTINGS_SAMPLE_LEFT_TEXT_W = 58,
        UI_SETTINGS_SAMPLE_RIGHT_TEXT_X = 64
    };

    ui_page_settings_draw_global_sample_header("WAVE");
    drv_display_draw_line(0, UI_SETTINGS_SAMPLE_BROWSER_HEADER_LINE_Y, 127, UI_SETTINGS_SAMPLE_BROWSER_HEADER_LINE_Y);
    ui_page_settings_draw_wavetable_name_label();
    ui_page_settings_draw_sample_split_position(g_ui_settings.sample_child_count,
                                                g_ui_settings.sampler_slot_count);
    drv_display_set_font(&FONT_4X6);

    const uint8_t visible_lines = UI_SETTINGS_SAMPLE_BROWSER_VISIBLE_LINES;
    const uint16_t selected_right_index =
        ui_page_settings_filtered_index_for_global(g_ui_settings.sample_slot_selected);
    g_ui_settings.sample_left_scroll = ui_page_settings_clamp_scroll(g_ui_settings.sample_left_scroll,
                                                                     g_ui_settings.sample_selected,
                                                                     g_ui_settings.sample_child_count,
                                                                     visible_lines);
    g_ui_settings.sample_right_scroll = ui_page_settings_clamp_scroll(g_ui_settings.sample_right_scroll,
                                                                      selected_right_index,
                                                                      g_ui_settings.sampler_slot_count,
                                                                      visible_lines);
    for (uint8_t line = 0U; line < visible_lines; ++line)
    {
        const uint8_t y = (uint8_t)(UI_SETTINGS_SAMPLE_BROWSER_TEXT_Y0
                                    + (line * UI_SETTINGS_SAMPLE_BROWSER_TEXT_PITCH));
        const uint16_t left_index = (uint16_t)(g_ui_settings.sample_left_scroll + line);
        const uint16_t right_index = (uint16_t)(g_ui_settings.sample_right_scroll + line);

        if (left_index < g_ui_settings.sample_entry_count)
        {
            char left[32];
            const ui_settings_sample_entry_t *const entry = &g_ui_settings.sample_entries[left_index];
            ui_page_settings_fit_label(left,
                                       sizeof(left),
                                       entry->label,
                                       (uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_W);
            if ((left_index == g_ui_settings.sample_selected)
                && (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY))
            {
                drv_display_fill_rect(0, y - 1U, 58, UI_SETTINGS_SAMPLE_BROWSER_SELECT_H);
                drv_display_draw_text_inverted((uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_X, y, left);
            }
            else
            {
                drv_display_draw_text((uint8_t)UI_SETTINGS_SAMPLE_LEFT_TEXT_X, y, left);
            }
        }

        if (right_index >= g_ui_settings.sampler_slot_count)
        {
            continue;
        }
        const uint16_t global_slot = g_ui_settings.sampler_slots[right_index];
        char right[24];
        ui_page_settings_wavetable_slot_label(global_slot, right, sizeof(right), 62U);
        if ((global_slot == g_ui_settings.sample_slot_selected)
            && (g_ui_settings.sample_focus == (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS))
        {
            drv_display_fill_rect(62, y - 1U, 66, UI_SETTINGS_SAMPLE_BROWSER_SELECT_H);
            drv_display_draw_text_inverted((uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_X, y, right);
        }
        else
        {
            drv_display_draw_text((uint8_t)UI_SETTINGS_SAMPLE_RIGHT_TEXT_X, y, right);
        }
    }

    if (g_ui_settings.status_line[0] != '\0')
    {
        drv_display_draw_text(0U, 54U, g_ui_settings.status_line);
        drv_display_set_font(&FONT_5X7);
        return;
    }

    ui_page_settings_draw_sample_footer();
    drv_display_set_font(&FONT_5X7);
}

static void ui_page_settings_render(void)
{
    ui_settings_menu_level_t *const level = ui_page_settings_current_level();
    if (level == 0)
    {
        drv_display_set_font(&FONT_5X7);
        drv_display_draw_text(0U, 0U, "SETTINGS");
        drv_display_set_font(&FONT_4X6);
        drv_display_draw_text(0U, 12U, "EMPTY");
        return;
    }

    if (level->view == UI_SETTINGS_VIEW_SAMPLER)
    {
        ui_page_settings_render_sample_browser();
        return;
    }
    if (level->view == UI_SETTINGS_VIEW_MULTI_SAMPLE)
    {
        ui_page_settings_render_multi_browser();
        return;
    }
    if (level->view == UI_SETTINGS_VIEW_SAMPLE_RAM)
    {
        ui_page_settings_render_ram_browser();
        return;
    }
    if (level->view == UI_SETTINGS_VIEW_WAVETABLE)
    {
        ui_page_settings_render_wavetable_browser();
        return;
    }
    drv_display_set_font(&FONT_5X7);
    drv_display_draw_text(0U, 0U, ui_page_settings_view_title(level->view));
    drv_display_draw_line(0, 9, 127, 9);
    drv_display_set_font(&FONT_4X6);

    const uint8_t count = ui_page_settings_view_item_count(level->view);
    const uint8_t selected = (count > 0U) ? (uint8_t)(level->selected_index % count) : 0U;
    const uint8_t visible_lines = 4U;
    uint8_t start = 0U;
    if (selected >= visible_lines)
    {
        start = (uint8_t)(selected - (visible_lines - 1U));
    }

    for (uint8_t line = 0U; line < visible_lines; ++line)
    {
        const uint8_t item = (uint8_t)(start + line);
        if (item >= count)
        {
            break;
        }

        char buf[24];
        const char *label = ui_page_settings_item_label(level->view, item, buf, sizeof(buf));
        const uint8_t y = (uint8_t)(12U + (line * 12U));

        if (item == selected)
        {
            drv_display_fill_rect(0, y - 1U, 128, 8);
            drv_display_draw_text_inverted(2U, y, label);
        }
        else
        {
            drv_display_draw_text(2U, y, label);
        }
    }

    if (g_ui_settings.status_line[0] != '\0')
    {
        drv_display_draw_text(0U, 54U, g_ui_settings.status_line);
    }
    else if (level->view == UI_SETTINGS_VIEW_PROJECT_MANAGE_SLOT)
    {
        char slot_line[24];
        (void)snprintf(slot_line, sizeof(slot_line), "SLOT %02u", (unsigned)g_ui_settings.selected_slot);
        drv_display_draw_text(0U, 42U, slot_line);
        ui_page_settings_draw_page_footer("-");
    }
    else if (level->view == UI_SETTINGS_VIEW_SAMPLER_SLOT)
    {
        char slot_line[24];
        const sample_classic_slot_state_t state = sample_global_pool_get_classic_state(g_ui_settings.selected_slot);
        const char *state_label = (state == SAMPLE_CLASSIC_SLOT_LOADED) ? "LOADED"
                                 : (state == SAMPLE_CLASSIC_SLOT_PREPARING) ? "PREP"
                                 : (state == SAMPLE_CLASSIC_SLOT_ERROR) ? "ERROR"
                                 : (state == SAMPLE_CLASSIC_SLOT_MISSING) ? "MISSING"
                                 : "EMPTY";
        (void)snprintf(slot_line, sizeof(slot_line), "SLOT %02u %s",
                       (unsigned)g_ui_settings.selected_slot,
                       state_label);
        drv_display_draw_text(0U, 42U, slot_line);
        ui_page_settings_draw_page_footer("-");
    }
    else if (level->view == UI_SETTINGS_VIEW_SAMPLER_CATALOG)
    {
        char slot_line[24];
        (void)snprintf(slot_line,
                       sizeof(slot_line),
                       "CATALOG %u%s",
                       (unsigned)wav_loader_catalog_count(),
                       (wav_loader_catalog_truncated() != 0U) ? "+" : "");
        drv_display_draw_text(0U, 42U, slot_line);
        ui_page_settings_draw_page_footer("-");
    }
    else if (g_ui_settings.status_line[0] == '\0')
    {
        ui_page_settings_draw_page_footer("-");
    }
}

const ui_page_t g_ui_page_settings = {
    .enter = ui_page_settings_enter,
    .leave = ui_page_settings_leave,
    .handle_event = ui_page_settings_handle_event_page,
    .tick = ui_page_settings_tick,
    .render = ui_page_settings_render,
};

