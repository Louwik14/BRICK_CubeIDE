const ui_template_family_t *ui_page_template_tone_resolve_for_track(uint8_t track, uint8_t scope_index)
{
    const uint8_t subset = (scope_index == UI_TEMPLATE_EFFECTIVE_SCOPE_CURRENT)
            ? g_ui_template_tone_subset : scope_index;
    if (g_ui_template_tone_global_master != 0U)
    {
        if (subset == 0U)
        {
            ui_template_tone_sync_modfx_pages();
            return &g_ui_template_tone_family_master_reverb;
        }
        if (subset == 1U)
        {
            return (param_get(PARAM_MIX_DELAY_TYPE) >= 0.5f)
                    ? &g_ui_template_tone_family_master_delay_dual
                    : &g_ui_template_tone_family_master_delay_classic;
        }

        const uint8_t model = (uint8_t)(param_get(PARAM_COMP_MODEL) + 0.5f);
        if (model == 1U) return &g_ui_template_tone_family_master_comp_deluge;
        if (model == 2U) return &g_ui_template_tone_family_master_comp_brick;
        return &g_ui_template_tone_family_master_comp_off;
    }
    if ((ui_get_track_family(track) == UI_TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(track) == UI_TRACK_TYPE_WAVE))
    {
        return (g_ui_template_tone_subset == 0U)
            ? &g_ui_template_tone_family_wave
            : &g_ui_template_tone_family_wave_classic;
    }
    if ((ui_get_track_family(track) == UI_TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(track) == UI_TRACK_TYPE_PRISM))
    {
        return (subset == 0U)
            ? &g_ui_template_tone_family_prism_live
            : &g_ui_template_tone_family_prism;
    }
    if ((ui_get_track_family(track) == UI_TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(track) == UI_TRACK_TYPE_STACK))
    {
        return &g_ui_template_tone_family_stack;
    }
    if ((ui_get_track_family(track) == UI_TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(track) == UI_TRACK_TYPE_FM)
            && (g_ui_template_tone_subset == 0U))
    {
        const uint8_t active_track = ui_get_active_lane();
        float selected_value = 0.0f;
        (void)param_registry_get_track_value(PARAM_FM_OPERATOR_SELECT, active_track, &selected_value);
        uint8_t selected = (uint8_t)(selected_value + 0.5f);
        if (selected >= PARAM_FM_OPERATOR_COUNT)
        {
            selected = (uint8_t)(PARAM_FM_OPERATOR_COUNT - 1U);
        }
        const param_id_t first = (param_id_t)(PARAM_FM_OPERATOR_FIRST
                                              + (selected * PARAM_FM_OPERATOR_PARAM_COUNT));
        g_ui_template_tone_family_fm.subpages[1].param_bank.params[1] = (param_id_t)(first + BRICK6_FM_OPERATOR_FREQ);
        g_ui_template_tone_family_fm.subpages[1].param_bank.params[2] = (param_id_t)(first + BRICK6_FM_OPERATOR_LEVEL);
        g_ui_template_tone_family_fm.subpages[1].param_bank.params[3] = (param_id_t)(first + BRICK6_FM_OPERATOR_DETUNE);
        return &g_ui_template_tone_family_fm;
    }
    if ((ui_get_track_family(track) == UI_TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(track) == UI_TRACK_TYPE_FM)
            && (g_ui_template_tone_subset != 0U))
    {
        const uint8_t active_track = ui_get_active_lane();
        float selected_value = 0.0f;
        (void)param_registry_get_track_value(PARAM_FM_OPERATOR_SELECT, active_track, &selected_value);
        uint8_t selected = (uint8_t)(selected_value + 0.5f);
        if (selected >= PARAM_FM_OPERATOR_COUNT)
        {
            selected = (uint8_t)(PARAM_FM_OPERATOR_COUNT - 1U);
        }
        const param_id_t first = (param_id_t)(PARAM_FM_OPERATOR_FIRST
                                              + (selected * PARAM_FM_OPERATOR_PARAM_COUNT));
        g_ui_template_tone_family_fm_operator.subpages[0].param_bank.params[1] = first;
        g_ui_template_tone_family_fm_operator.subpages[0].param_bank.params[2] = (param_id_t)(first + 1U);
        g_ui_template_tone_family_fm_operator.subpages[0].param_bank.params[3] = (param_id_t)(first + 2U);
        g_ui_template_tone_family_fm_operator.subpages[1].param_bank.params[0] = (param_id_t)(first + 3U);
        g_ui_template_tone_family_fm_operator.subpages[1].param_bank.params[1] = (param_id_t)(first + 4U);
        g_ui_template_tone_family_fm_operator.subpages[1].param_bank.params[2] = (param_id_t)(first + 5U);
        g_ui_template_tone_family_fm_operator.subpages[1].param_bank.params[3] = (param_id_t)(first + 6U);
        g_ui_template_tone_family_fm_operator.subpages[2].param_bank.params[0] = (param_id_t)(first + 7U);
        g_ui_template_tone_family_fm_operator.subpages[2].param_bank.params[1] = (param_id_t)(first + 8U);
        g_ui_template_tone_family_fm_operator.subpages[2].param_bank.params[2] = (param_id_t)(first + 9U);
        g_ui_template_tone_family_fm_operator.subpages[2].param_bank.params[3] = (param_id_t)(first + 10U);
        return &g_ui_template_tone_family_fm_operator;
    }
    const ui_track_config_t config = ui_get_track_config(track);
    return ui_template_family_resolve(UI_TEMPLATE_FAMILY_TONE, track, config.family, config.type);
}

static const ui_template_family_t *ui_page_template_tone_resolve_family(void)
{
    if (g_ui_template_tone_global_master != 0U)
    {
        return ui_page_template_tone_resolve_for_track(
            ui_get_active_lane(), UI_TEMPLATE_EFFECTIVE_SCOPE_CURRENT);
    }
    return ui_template_family_resolve_effective_active_track(UI_TEMPLATE_FAMILY_TONE);
}

static uint8_t ui_page_template_tone_param_text(uint8_t slot,
                                                param_id_t id,
                                                float value,
                                                char *out_name,
                                                uint32_t out_name_len,
                                                char *out_value,
                                                uint32_t out_value_len);
static uiw_widget_type_t ui_page_template_tone_pick_widget(uint8_t slot,
                                                           param_id_t id,
                                                           const char *value_label,
                                                           uiw_widget_type_t suggested_widget);
static ui_template_custom_widget_kind_t ui_page_template_tone_pick_custom_widget(uint8_t slot,
                                                                                 const ui_template_subpage_t *subpage,
                                                                                 param_id_t id);

static ui_template_page_state_t g_ui_template_tone_state = {
    .family = 0,
    .family_resolver = ui_page_template_tone_resolve_family,
    .widget_picker = ui_page_template_tone_pick_widget,
    .custom_widget_picker = ui_page_template_tone_pick_custom_widget,
    .param_text = ui_page_template_tone_param_text,
    .active_subpage = 0U,
    .has_visited = 0U,
    .preserve_subpage_on_family_change = 1U,
};


#define UI_PRISM_PARAM_LABEL_COUNT param_prism_label_count()

static const int16_t g_prism_triple_intervals_q7[] = {
    -3072, -3072, -3068, -2944, -2816, -2688, -2560, -2432,
    -2304, -2180, -2176, -2048, -1920, -1792, -1664, -1540,
    -1536, -1408, -1280, -1152, -1024, -900, -896, -768,
    -640, -512, -384, -256, -128, -24, -8, -4,
    0, 4, 8, 24, 128, 256, 384, 512,
    640, 768, 896, 900, 1024, 1152, 1280, 1408,
    1536, 1540, 1664, 1792, 1920, 2048, 2176, 2180,
    2304, 2432, 2560, 2688, 2816, 2944, 3068, 3072,
    3072
};

static const uint16_t g_prism_fm_frequency_quantizer[] = {
    7168, 7168, 7168, 7360, 7552, 7744, 7936, 8128,
    8320, 8512, 8704, 8896, 9088, 9280, 9472, 9664,
    9856, 10048, 10240, 10240, 10240, 10432, 10624, 10816,
    11008, 11200, 11392, 11584, 11776, 11968, 12160, 12352,
    12544, 12736, 12928, 13312, 13312, 13312, 13352, 13352,
    13352, 13726, 14100, 14474, 14848, 14848, 14848, 15080,
    15313, 15313, 15313, 15581, 15848, 16116, 16384, 16384,
    16384, 16424, 16424, 16424, 16798, 17172, 17546, 17920,
    17920, 17920, 18152, 18385, 18385, 18385, 18624, 18864,
    18864, 18864, 19160, 19456, 19456, 19456, 19496, 19496,
    19496, 19737, 19978, 19978, 19978, 20200, 20422, 20645,
    20867, 20867, 20867, 20992, 20992, 20992, 21253, 21253,
    21253, 21457, 21457, 21457, 21673, 21890, 21890, 21890,
    22209, 22528, 22528, 22528, 22789, 22789, 22789, 23021,
    23254, 23254, 23254, 23516, 23516, 23516, 23790, 24064,
    24064, 24064, 24448, 24832, 25216, 25600, 25600, 25600,
    25600
};

