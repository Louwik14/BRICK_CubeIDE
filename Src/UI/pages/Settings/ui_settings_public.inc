void ui_page_settings_open(uint8_t return_page_id)
{
    g_ui_settings.return_page_id = return_page_id;
    ui_page_set(UI_PAGE_SETTINGS);
}

void ui_page_settings_open_sample_browser(uint8_t return_page_id)
{
    g_ui_settings.return_page_id = return_page_id;
    ui_page_set(UI_PAGE_SETTINGS);
    g_ui_settings.depth = 0U;
    ui_page_settings_push(UI_SETTINGS_VIEW_SAMPLE);
}

uint8_t ui_page_settings_is_open(void)
{
    return (ui_page_get_id() == UI_PAGE_SETTINGS) ? 1U : 0U;
}

uint8_t ui_page_settings_multi_clear_is_active(void)
{
    return multi_sample_pool_clear_is_active();
}

void ui_page_settings_handle_encoder(uint8_t encoder, int16_t delta)
{
    if ((multi_sample_pool_clear_is_active() != 0U)
        || (wav_convert_is_active() != 0U)
        || (g_ui_settings.convert_slot_valid != 0U))
    {
        return;
    }

    ui_settings_menu_level_t *const level = ui_page_settings_current_level();
    if ((level == 0) || (delta == 0) || (encoder >= UI_SETTINGS_ENCODER_COUNT))
    {
        return;
    }

    if ((level->view == UI_SETTINGS_VIEW_SAMPLER)
        || (level->view == UI_SETTINGS_VIEW_SAMPLE_RAM)
        || (level->view == UI_SETTINGS_VIEW_WAVETABLE)
        || (level->view == UI_SETTINGS_VIEW_MULTI_SAMPLE))
    {
        if ((level->view == UI_SETTINGS_VIEW_MULTI_SAMPLE)
            && (g_ui_settings.sample_confirm == (uint8_t)UI_SETTINGS_SAMPLE_CONFIRM_MULTI_PREPARING))
        {
            return;
        }

        g_ui_settings.encoder_accum[encoder] = (int16_t)(g_ui_settings.encoder_accum[encoder] + delta);
        const int16_t step = (int16_t)(g_ui_settings.encoder_accum[encoder] / UI_SETTINGS_ENCODER_DIVIDER);
        g_ui_settings.encoder_accum[encoder] = (int16_t)(g_ui_settings.encoder_accum[encoder] - (step * UI_SETTINGS_ENCODER_DIVIDER));
        if (step == 0)
        {
            return;
        }

        if (encoder == 0U)
        {
            g_ui_settings.sample_focus = (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_LIBRARY;
            const uint16_t entry_count = (level->view == UI_SETTINGS_VIEW_MULTI_SAMPLE)
                ? g_ui_settings.multi_entry_count
                : g_ui_settings.sample_child_count;
            if (entry_count == 0U)
            {
                g_ui_settings.sample_selected = 0U;
                return;
            }
            int32_t index = g_ui_settings.sample_selected;
            index += step;
            if (index < 0)
            {
                index = 0;
            }
            else if (index >= entry_count)
            {
                index = (int32_t)entry_count - 1;
            }
            const uint16_t old_selected = g_ui_settings.sample_selected;
            if ((uint16_t)index != old_selected)
            {
                if ((level->view == UI_SETTINGS_VIEW_SAMPLER)
                    || (level->view == UI_SETTINGS_VIEW_SAMPLE_RAM)
                    || (level->view == UI_SETTINGS_VIEW_WAVETABLE))
                {
                    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
                }
            }
            g_ui_settings.sample_selected = (uint16_t)index;
            if (((level->view == UI_SETTINGS_VIEW_SAMPLER)
                 || (level->view == UI_SETTINGS_VIEW_SAMPLE_RAM))
                && ((g_ui_settings.sample_selected < g_ui_settings.sample_page_start)
                    || (g_ui_settings.sample_selected >= (uint16_t)(g_ui_settings.sample_page_start
                                                                    + g_ui_settings.sample_entry_count))))
            {
                if ((ui_page_settings_sample_browser_refresh() == 0U)
                    && (wav_loader_catalog_view_busy() == 0U))
                {
                    g_ui_settings.sample_selected = old_selected;
                    (void)ui_page_settings_sample_browser_refresh();
                }
            }
            return;
        }

        if (encoder == 1U)
        {
            g_ui_settings.sample_focus = (uint8_t)UI_SETTINGS_SAMPLE_FOCUS_SLOTS;
            if (g_ui_settings.sampler_slot_count == 0U)
            {
                return;
            }
            int32_t index =
                (int32_t)ui_page_settings_filtered_index_for_global(g_ui_settings.sample_slot_selected);
            const int32_t slot_count = (int32_t)g_ui_settings.sampler_slot_count;
            index += step;
            if (index < 0)
            {
                index = 0;
            }
            else if (index >= slot_count)
            {
                index = slot_count - 1;
            }
            const uint16_t new_global = g_ui_settings.sampler_slots[(uint16_t)index];
            if (new_global != g_ui_settings.sample_slot_selected)
            {
                if ((level->view == UI_SETTINGS_VIEW_SAMPLER)
                    || (level->view == UI_SETTINGS_VIEW_SAMPLE_RAM)
                    || (level->view == UI_SETTINGS_VIEW_WAVETABLE))
                {
                    ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
                }
            }
            g_ui_settings.sample_slot_selected = new_global;
            return;
        }

        if (encoder == 2U)
        {
            return;
        }

        if ((encoder == 3U) && (level->view == UI_SETTINGS_VIEW_SAMPLER))
        {
            int32_t volume = (int32_t)g_ui_settings.sample_preview_volume + ((int32_t)step * 5);
            if (volume < 0)
            {
                volume = 0;
            }
            else if (volume > 100)
            {
                volume = 100;
            }

            g_ui_settings.sample_preview_volume = (uint8_t)volume;
            (void)control_domain_request_preview_gain(
                (float)g_ui_settings.sample_preview_volume * 0.01f);

            char status[20];
            (void)snprintf(status,
                           sizeof(status),
                           "PVOL %03u",
                           (unsigned int)g_ui_settings.sample_preview_volume);
            ui_page_settings_status(status);
        }
        return;
    }

    const uint8_t count = ui_page_settings_view_item_count(level->view);
    if (count == 0U)
    {
        level->selected_index = 0U;
        return;
    }

    g_ui_settings.encoder_accum[encoder] = (int16_t)(g_ui_settings.encoder_accum[encoder] + delta);
    const int16_t step = (int16_t)(g_ui_settings.encoder_accum[encoder] / UI_SETTINGS_ENCODER_DIVIDER);
    g_ui_settings.encoder_accum[encoder] = (int16_t)(g_ui_settings.encoder_accum[encoder] - (step * UI_SETTINGS_ENCODER_DIVIDER));
    if (step == 0)
    {
        return;
    }

    if ((level->view == UI_SETTINGS_VIEW_SAMPLER_CATALOG)
        && (g_ui_settings.sampler_catalog_mode == UI_SETTINGS_SAMPLER_CATALOG_MODE_PREVIEW))
    {
        ui_page_settings_preview_stop(UI_SETTINGS_PREVIEW_STOP_ORIGIN_SILENT);
    }

    int32_t index = level->selected_index;
    index += step;
    if (index < 0)
    {
        index = 0;
    }
    else if (index >= count)
    {
        index = (int32_t)count - 1;
    }

    level->selected_index = (uint8_t)index;
}

uint8_t ui_page_settings_handle_event(const ui_event_t *ev)
{
    if ((ui_page_settings_is_open() == 0U) || (ev == 0))
    {
        return 0U;
    }
    if (multi_sample_pool_clear_is_active() != 0U)
    {
        ui_page_settings_status("CLEAR...");
        return 1U;
    }

    if (ev->type == UI_EVENT_HALL_PRESS)
    {
        const ui_settings_menu_level_t *const level = ui_page_settings_current_level();
        if ((level != 0) && (level->view == UI_SETTINGS_VIEW_SAMPLER))
        {
            ui_page_settings_handle_event_internal(ev);
            return 1U;
        }
        return 0U;
    }

    if (ev->type != UI_EVENT_BUTTON_PRESS)
    {
        return 0U;
    }

    switch ((button_id_t)ev->id)
    {
        case BTN_PAGE_1:
        case BTN_PAGE_2:
        case BTN_PAGE_3:
        case BTN_PAGE_4:
        case BTN_SETTINGS:
            ui_page_settings_handle_event_internal(ev);
            return 1U;

        default:
            return 0U;
    }
}
