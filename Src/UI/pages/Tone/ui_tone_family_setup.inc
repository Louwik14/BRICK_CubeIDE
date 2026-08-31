static void ui_page_template_tone_set_subpage(uint8_t idx, const char *title, param_id_t p0, param_id_t p1, param_id_t p2, param_id_t p3)
{
    g_ui_template_tone_family_drum.subpages[idx].title = title;
    g_ui_template_tone_family_drum.subpages[idx].param_bank.params[0] = p0;
    g_ui_template_tone_family_drum.subpages[idx].param_bank.params[1] = p1;
    g_ui_template_tone_family_drum.subpages[idx].param_bank.params[2] = p2;
    g_ui_template_tone_family_drum.subpages[idx].param_bank.params[3] = p3;
}

static void ui_page_template_tone_sync_drum_family(void)
{
    const uint8_t active_track = ui_get_active_lane();
    if ((ui_get_track_family(active_track) == TRACK_FAMILY_DRUM)
            && (ui_get_track_type(active_track) == TRACK_TYPE_DRUM_MD))
    {
        float model_value = 0.0f;
        (void)param_registry_get_track_value(PARAM_DRUM_MD_MODEL, active_track, &model_value);
        const md_model_profile_t *const profile = md_model_profile_get(md_model_validate(model_value));
        const param_id_t p6 = (profile->slot_count >= 6U) ? PARAM_DRUM_MD_P6 : PARAM_COUNT;
        const param_id_t p7 = (profile->slot_count >= 7U) ? PARAM_DRUM_MD_P7 : PARAM_COUNT;
        const param_id_t p8 = (profile->slot_count >= 8U) ? PARAM_DRUM_MD_P8 : PARAM_COUNT;

        g_ui_template_tone_family_drum.nav_labels[0] = "MD1";
        g_ui_template_tone_family_drum.nav_labels[1] = "MD2";
        g_ui_template_tone_family_drum.nav_labels[2] = (profile->slot_count >= 8U) ? "MD3" : "-";
        g_ui_template_tone_family_drum.nav_labels[3] = "-";
        ui_page_template_tone_set_subpage(0U, profile->name,
                                          PARAM_DRUM_MD_MODEL, PARAM_DRUM_MD_P1,
                                          PARAM_DRUM_MD_P2, PARAM_DRUM_MD_P3);
        ui_page_template_tone_set_subpage(1U, profile->name,
                                          PARAM_DRUM_MD_P4, PARAM_DRUM_MD_P5,
                                          p6, p7);
        ui_page_template_tone_set_subpage(2U, profile->name,
                                          p8, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT);
        ui_page_template_tone_set_subpage(3U, "-", PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT);
        return;
    }

    if ((ui_get_track_family(active_track) == TRACK_FAMILY_DRUM)
            && (ui_get_track_type(active_track) == TRACK_TYPE_DRUM_BD_ANALOG))
    {
        g_ui_template_tone_family_drum.nav_labels[0] = "BD";
        g_ui_template_tone_family_drum.nav_labels[1] = "-";
        g_ui_template_tone_family_drum.nav_labels[2] = "-";
        g_ui_template_tone_family_drum.nav_labels[3] = "-";
        ui_page_template_tone_set_subpage(0U,
                                          "BD",
                                          PARAM_DRUM_TRX_BD_PITCH,
                                          PARAM_DRUM_TRX_BD_DECAY,
                                          PARAM_DRUM_TRX_BD_HARMONICS,
                                          PARAM_DRUM_TRX_BD_PITCH_SWEEP);
        ui_page_template_tone_set_subpage(1U, "-", PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT);
        ui_page_template_tone_set_subpage(2U, "-", PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT);
        ui_page_template_tone_set_subpage(3U, "-", PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT);
        return;
    }

    g_ui_template_tone_family_drum.nav_labels[0] = "MAIN";
    g_ui_template_tone_family_drum.nav_labels[1] = "-";
    g_ui_template_tone_family_drum.nav_labels[2] = "-";
    g_ui_template_tone_family_drum.nav_labels[3] = "-";
    ui_page_template_tone_set_subpage(0U, "MAIN", PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT);
    ui_page_template_tone_set_subpage(1U, "-", PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT);
    ui_page_template_tone_set_subpage(2U, "-", PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT);
    ui_page_template_tone_set_subpage(3U, "-", PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT);
}

void ui_page_template_tone_register_families(void)
{
    for (uint8_t family = 0U; family < (uint8_t)TRACK_FAMILY_COUNT; ++family)
    {
        const track_family_t track_family = (track_family_t)family;
        for (uint8_t type = 0U; type < (uint8_t)TRACK_TYPE_COUNT; ++type)
        {
            const track_type_t track_type = (track_type_t)type;
            if (!ui_track_type_is_valid_for_family(track_family, track_type))
            {
                continue;
            }

            const ui_template_family_t *family_template = NULL;
            if (track_type == TRACK_TYPE_GROUP)
            {
                family_template = &g_ui_template_tone_family_group;
            }
            else if ((ui_track_family_is_engine(track_family) != 0) && (track_type == TRACK_TYPE_PRISM))
            {
                family_template = &g_ui_template_tone_family_prism;
            }
            else if ((ui_track_family_is_engine(track_family) != 0) && (track_type == TRACK_TYPE_WAVE))
            {
                family_template = &g_ui_template_tone_family_wave;
            }
            else if ((ui_track_family_is_engine(track_family) != 0) && (track_type == TRACK_TYPE_STACK))
            {
                family_template = &g_ui_template_tone_family_stack;
            }
            else if ((ui_track_family_is_engine(track_family) != 0) && (track_type == TRACK_TYPE_FM))
            {
                family_template = &g_ui_template_tone_family_fm;
            }
            else if ((ui_track_family_is_engine(track_family) != 0) && (track_type == TRACK_TYPE_RAM))
            {
                family_template = &g_ui_template_tone_family_sampler;
            }

            else if ((track_family == TRACK_FAMILY_SAMPLER) && (track_type == TRACK_TYPE_STREAM))
            {
                family_template = &g_ui_template_tone_family_clip;
            }
            else if ((track_family == TRACK_FAMILY_SAMPLER) && (track_type == TRACK_TYPE_LOOPER))
            {
                family_template = &g_ui_template_tone_family_looper;
            }
            else if ((track_family == TRACK_FAMILY_SAMPLER) && (track_type == TRACK_TYPE_MULTI))
            {
                family_template = &g_ui_template_tone_family_multi;
            }
            else if ((track_family == TRACK_FAMILY_MIDI) && (track_type == TRACK_TYPE_MIDI))
            {
                family_template = &g_ui_template_tone_family_midi;
            }
            else if ((track_family == TRACK_FAMILY_EXTERNAL) && (track_type == TRACK_TYPE_EXTERNAL))
            {
                family_template = &g_ui_template_tone_family_midi;
            }
            else if (track_family == TRACK_FAMILY_DRUM)
            {
                family_template = &g_ui_template_tone_family_drum;
            }

            ui_template_family_register(UI_TEMPLATE_FAMILY_TONE, track_family, track_type, family_template);
        }
    }
}

void ui_page_template_tone_open_primary(void)
{
    g_ui_template_tone_subset = 0U;
    g_ui_template_tone_state.navigation_subset = 0U;
    ui_template_page_select_subpage(&g_ui_template_tone_state, 0U);
}

void ui_page_template_tone_open_global_master(void)
{
    ui_page_set(UI_PAGE_TEMPLATE_TONE);
    g_ui_template_tone_global_master = 1U;
    ui_page_template_tone_open_primary();
}

uint8_t ui_page_template_tone_is_global_master(void)
{
    return g_ui_template_tone_global_master;
}

void ui_page_template_tone_toggle_subset(void)
{
    const uint8_t active_track = ui_get_active_lane();
    if (g_ui_template_tone_global_master != 0U)
    {
        g_ui_template_tone_subset = (uint8_t)((g_ui_template_tone_subset + 1U) % 3U);
        g_ui_template_tone_state.navigation_subset = g_ui_template_tone_subset;
        ui_navigation_restore_current_template_subpage();
        return;
    }
    if ((ui_get_track_family(active_track) == TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(active_track) == TRACK_TYPE_FM))
    {
        g_ui_template_tone_subset = (g_ui_template_tone_subset == 0U) ? 1U : 0U;
        g_ui_template_tone_state.navigation_subset = g_ui_template_tone_subset;
        ui_navigation_restore_current_template_subpage();
        return;
    }
    if ((ui_get_track_family(active_track) == TRACK_FAMILY_SYNTH)
            && ((ui_get_track_type(active_track) == TRACK_TYPE_WAVE)
                || (ui_get_track_type(active_track) == TRACK_TYPE_PRISM)))
    {
        const uint8_t page = g_ui_template_tone_state.active_subpage;
        g_ui_template_tone_subset = (g_ui_template_tone_subset == 0U) ? 1U : 0U;
        g_ui_template_tone_state.navigation_subset = g_ui_template_tone_subset;
        ui_template_page_select_subpage(&g_ui_template_tone_state, page);
        return;
    }
    if ((ui_get_track_family(active_track) != TRACK_FAMILY_SYNTH)
            || (ui_get_track_type(active_track) != TRACK_TYPE_STACK))
    {
        ui_page_template_tone_open_primary();
        return;
    }

    ui_page_template_tone_open_primary();
}

