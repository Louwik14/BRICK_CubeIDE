static const ui_prism_param_label_t *ui_page_template_tone_prism_labels_for_param(param_id_t id, uint8_t *out_edit_index)
{
    const uint8_t active_track = ui_get_active_lane();
    if ((ui_get_track_family(active_track) != TRACK_FAMILY_SYNTH)
            || (ui_get_track_type(active_track) != TRACK_TYPE_PRISM))
    {
        return NULL;
    }

    const param_id_t edit_param = ((id == PARAM_PRISM_OSC2_PARAM1)
                                   || (id == PARAM_PRISM_OSC2_PARAM2)
                                   || (id == PARAM_PRISM_OSC2_MODEL)
                                   || (id == PARAM_PRISM_PITCH_MOD2)
                                   || (id == PARAM_PRISM_OSC2_AMOD))
        ? PARAM_PRISM_OSC2_MODEL
        : PARAM_PRISM_OSC1_MODEL;
    float edit = 0.0f;
    if (param_registry_get_track_value(edit_param, active_track, &edit) == 0U)
    {
        return NULL;
    }

    uint8_t edit_index = 0U;
    (void)param_prism_edit_index_from_value(edit, &edit_index);
    if (out_edit_index != NULL)
    {
        *out_edit_index = edit_index;
    }
    return param_prism_labels_for_edit_index(edit_index);
}

static uint16_t ui_page_template_tone_prism_u15(float value)
{
    if (value < 0.0f)
    {
        return 0U;
    }
    if (value > 1.0f)
    {
        return 32767U;
    }
    return (uint16_t)(value * 32767.0f + 0.5f);
}

static void ui_page_template_tone_prism_format_percent(uint16_t raw,
                                                      char *out,
                                                      uint32_t out_len)
{
    ui_format_param_127_00((float)raw, 0.0f, 32767.0f, out, out_len);
}

static void ui_page_template_tone_prism_format_bipolar_percent(uint16_t raw,
                                                              char *out,
                                                              uint32_t out_len)
{
    int32_t scaled = ((int32_t)raw * 200L + 16383L) / 32767L;
    scaled -= 100L;
    if ((scaled > -1L) && (scaled < 1L))
    {
        scaled = 0L;
    }
    (void)snprintf(out, out_len, "%+ld%%", (long)scaled);
}

static void ui_page_template_tone_prism_format_model(float value,
                                                    char *out,
                                                    uint32_t out_len)
{
    uint8_t index = 0U;
    const char *label = NULL;

    if (value > 0.0f)
    {
        index = (uint8_t)(value + 0.5f);
    }
    (void)param_prism_edit_index_from_value(value, &index);

    if (param_registry[PARAM_PRISM_OSC1_MODEL].labels != NULL)
    {
        label = param_registry[PARAM_PRISM_OSC1_MODEL].labels[index];
    }
    (void)snprintf(out, out_len, "%s", (label != NULL) ? label : "---");
}

static void ui_page_template_tone_prism_format_q7_interval(int32_t q7,
                                                          char *out,
                                                          uint32_t out_len)
{
    const int32_t abs_q7 = (q7 < 0) ? -q7 : q7;
    if (abs_q7 == 0L)
    {
        (void)snprintf(out, out_len, "0st");
        return;
    }

    if (abs_q7 < 128L)
    {
        const int32_t cents = (q7 * 100L + ((q7 >= 0) ? 64L : -64L)) / 128L;
        (void)snprintf(out, out_len, "%+ldct", (long)cents);
        return;
    }

    const int32_t semitones = (q7 + ((q7 >= 0) ? 64L : -64L)) / 128L;
    (void)snprintf(out, out_len, "%+ldst", (long)semitones);
}

static int16_t ui_page_template_tone_prism_triple_interval_q7(uint16_t raw)
{
    uint8_t index_a = (uint8_t)(raw >> 9);
    uint8_t index_b = (uint8_t)(((raw >> 8) + 1U) >> 1);
    uint16_t xfade = (uint16_t)(raw << 8);
    const uint8_t last = (uint8_t)((sizeof(g_prism_triple_intervals_q7) / sizeof(g_prism_triple_intervals_q7[0])) - 1U);

    if (index_a > last)
    {
        index_a = last;
    }
    if (index_b > last)
    {
        index_b = last;
    }

    return (int16_t)(g_prism_triple_intervals_q7[index_a]
                    + (((int32_t)(g_prism_triple_intervals_q7[index_b] - g_prism_triple_intervals_q7[index_a])
                        * (int32_t)xfade) >> 16));
}

static void ui_page_template_tone_prism_format_interval(uint8_t edit_index,
                                                       param_id_t id,
                                                       uint16_t raw,
                                                       char *out,
                                                       uint32_t out_len)
{
    int32_t q7 = 0L;

    if ((edit_index == 4U) && (id == PARAM_PRISM_OSC1_PARAM2))
    {
        q7 = (int32_t)(raw >> 8);
    }
    else if (((edit_index == 7U) || (edit_index == 8U)) && (id == PARAM_PRISM_OSC1_PARAM1))
    {
        q7 = (int32_t)(raw >> 2);
    }
    else if ((edit_index >= 9U) && (edit_index <= 12U))
    {
        q7 = (int32_t)ui_page_template_tone_prism_triple_interval_q7(raw);
    }
    else if ((edit_index == 13U) && ((id == PARAM_PRISM_OSC1_PARAM1) || (id == PARAM_PRISM_OSC1_PARAM2)))
    {
        q7 = ((int32_t)raw - 16384L) >> 2;
    }
    else if ((edit_index == 28U) && (id == PARAM_PRISM_OSC1_PARAM2))
    {
        q7 = ((int32_t)raw - 16384L) >> 1;
    }
    else if (((edit_index == 19U) || (edit_index == 20U) || (edit_index == 21U))
            && ((id == PARAM_PRISM_OSC1_PARAM2) || (id == PARAM_PRISM_OSC2_PARAM2)))
    {
        uint8_t index = (uint8_t)(raw >> 8);
        if (index >= (uint8_t)(sizeof(g_prism_fm_frequency_quantizer)
                              / sizeof(g_prism_fm_frequency_quantizer[0])))
        {
            index = (uint8_t)((sizeof(g_prism_fm_frequency_quantizer)
                              / sizeof(g_prism_fm_frequency_quantizer[0])) - 1U);
        }
        q7 = ((int32_t)g_prism_fm_frequency_quantizer[index] - 16384L) >> 1;
    }
    else if ((edit_index == 21U)
            && ((id == PARAM_PRISM_OSC1_PARAM1) || (id == PARAM_PRISM_OSC2_PARAM1)))
    {
        q7 = ((int32_t)raw - 16384L) >> 1;
    }
    else
    {
        ui_page_template_tone_prism_format_percent(raw, out, out_len);
        return;
    }

    ui_page_template_tone_prism_format_q7_interval(q7, out, out_len);
}

static void ui_page_template_tone_prism_format_vowel(uint16_t raw,
                                                    char *out,
                                                    uint32_t out_len)
{
    uint8_t vowel = (uint8_t)(raw >> 12);
    const uint16_t balance = (uint16_t)(raw & 0x0fffU);
    if (vowel > 7U)
    {
        vowel = 7U;
    }

    if ((balance < 512U) || (vowel >= 7U))
    {
        (void)snprintf(out, out_len, "V%u", (unsigned int)(vowel + 1U));
    }
    else
    {
        (void)snprintf(out, out_len, "V%u>%u", (unsigned int)(vowel + 1U), (unsigned int)(vowel + 2U));
    }
}

static void ui_page_template_tone_prism_format_noise_mix(uint16_t raw,
                                                        char *out,
                                                        uint32_t out_len)
{
    if (raw < 4096U)
    {
        (void)snprintf(out, out_len, "LP");
    }
    else if (raw < 12288U)
    {
        (void)snprintf(out, out_len, "LP>BP");
    }
    else if (raw < 20480U)
    {
        (void)snprintf(out, out_len, "BP");
    }
    else if (raw < 28672U)
    {
        (void)snprintf(out, out_len, "BP>HP");
    }
    else
    {
        (void)snprintf(out, out_len, "HP");
    }
}

static void ui_page_template_tone_prism_format_morph(uint8_t edit_index,
                                                    param_id_t id,
                                                    uint16_t raw,
                                                    char *out,
                                                    uint32_t out_len)
{
    if ((edit_index == 17U) && (id == PARAM_PRISM_OSC1_PARAM1))
    {
        ui_page_template_tone_prism_format_vowel(raw, out, out_len);
    }
    else if ((edit_index == 27U) && (id == PARAM_PRISM_OSC1_PARAM2))
    {
        ui_page_template_tone_prism_format_noise_mix(raw, out, out_len);
    }
    else
    {
        ui_page_template_tone_prism_format_percent(raw, out, out_len);
    }
}

static void ui_page_template_tone_prism_format_stepped(uint8_t edit_index,
                                                      param_id_t id,
                                                      uint16_t raw,
                                                      char *out,
                                                      uint32_t out_len)
{
    if ((edit_index == 15U) && (id == PARAM_PRISM_OSC1_PARAM2))
    {
        (void)snprintf(out, out_len, "M%02X", (unsigned int)(raw >> 8));
    }
    else if (((edit_index == 20U) || (edit_index == 21U) || (edit_index == 22U)) && (id == PARAM_PRISM_OSC1_PARAM2))
    {
        uint8_t index = (uint8_t)(raw >> 8);
        if (index >= (uint8_t)(sizeof(g_prism_fm_frequency_quantizer) / sizeof(g_prism_fm_frequency_quantizer[0])))
        {
            index = (uint8_t)((sizeof(g_prism_fm_frequency_quantizer) / sizeof(g_prism_fm_frequency_quantizer[0])) - 1U);
        }
        ui_page_template_tone_prism_format_q7_interval(((int32_t)g_prism_fm_frequency_quantizer[index] - 16384L) >> 1,
                                                      out,
                                                      out_len);
    }
    else if ((edit_index == 25U) && (id == PARAM_PRISM_OSC1_PARAM2))
    {
        static const char *const k_labels[] = { "SMTH", "XFADE", "ROUGH", "LOFI" };
        uint8_t index = (uint8_t)(raw >> 13);
        if (index > 3U)
        {
            index = 3U;
        }
        (void)snprintf(out, out_len, "%s", k_labels[index]);
    }
    else if ((edit_index == 29U) && (id == PARAM_PRISM_OSC1_PARAM2))
    {
        uint8_t steps = (uint8_t)(1U + (raw >> 10));
        if (steps == 1U)
        {
            steps = 2U;
        }
        (void)snprintf(out, out_len, "%u", (unsigned int)steps);
    }
    else
    {
        (void)snprintf(out, out_len, "S%02u", (unsigned int)(1U + (raw >> 10)));
    }
}

static void ui_page_template_tone_prism_format_enum(uint8_t edit_index,
                                                   param_id_t id,
                                                   uint16_t raw,
                                                   char *out,
                                                   uint32_t out_len)
{
    if ((edit_index == 23U) && (id == PARAM_PRISM_OSC1_PARAM2))
    {
        uint8_t bank = (uint8_t)(((uint32_t)raw * 20U) >> 15);
        if (bank > 19U)
        {
            bank = 19U;
        }
        (void)snprintf(out, out_len, "%u/20", (unsigned int)(bank + 1U));
    }
    else if ((edit_index == 26U) && (id == PARAM_PRISM_OSC1_PARAM2))
    {
        uint8_t chord = (uint8_t)(raw >> 11);
        if (chord > 16U)
        {
            chord = 16U;
        }
        (void)snprintf(out, out_len, "CH%u", (unsigned int)(chord + 1U));
    }
    else
    {
        (void)snprintf(out, out_len, "E%02u", (unsigned int)(1U + (raw >> 10)));
    }
}

static void ui_page_template_tone_prism_format_value(uint8_t edit_index,
                                                    param_id_t id,
                                                    ui_prism_value_kind_t kind,
                                                    float value,
                                                    char *out,
                                                    uint32_t out_len)
{
    const uint16_t raw = ui_page_template_tone_prism_u15(value);

    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    switch (kind)
    {
        case UI_PRISM_VALUE_NONE:
            (void)snprintf(out, out_len, "---");
            break;
        case UI_PRISM_VALUE_BIPOLAR_PERCENT:
            ui_page_template_tone_prism_format_bipolar_percent(raw, out, out_len);
            break;
        case UI_PRISM_VALUE_INTERVAL:
            ui_page_template_tone_prism_format_interval(edit_index, id, raw, out, out_len);
            break;
        case UI_PRISM_VALUE_STEPPED:
            ui_page_template_tone_prism_format_stepped(edit_index, id, raw, out, out_len);
            break;
        case UI_PRISM_VALUE_ENUM:
            ui_page_template_tone_prism_format_enum(edit_index, id, raw, out, out_len);
            break;
        case UI_PRISM_VALUE_MORPH:
            ui_page_template_tone_prism_format_morph(edit_index, id, raw, out, out_len);
            break;
        case UI_PRISM_VALUE_RATE:
        case UI_PRISM_VALUE_PERCENT:
        default:
            ui_page_template_tone_prism_format_percent(raw, out, out_len);
            break;
    }
}

static uint8_t ui_page_template_tone_prism_kind_for_param(param_id_t id,
                                                         const ui_prism_param_label_t *labels,
                                                         const char **out_name,
                                                         ui_prism_value_kind_t *out_kind)
{
    if (labels == NULL)
    {
        return 0U;
    }

    if ((id == PARAM_PRISM_OSC1_PARAM1) || (id == PARAM_PRISM_OSC2_PARAM1))
    {
        if (out_name != NULL)
        {
            *out_name = labels->label_a;
        }
        if (out_kind != NULL)
        {
            *out_kind = labels->kind_a;
        }
        return 1U;
    }

    if ((id == PARAM_PRISM_OSC1_PARAM2) || (id == PARAM_PRISM_OSC2_PARAM2))
    {
        if (out_name != NULL)
        {
            *out_name = labels->label_b;
        }
        if (out_kind != NULL)
        {
            *out_kind = labels->kind_b;
        }
        return 1U;
    }

    return 0U;
}

static uint8_t ui_page_template_tone_prism_param_text(param_id_t id,
                                                     float value,
                                                     char *out_name,
                                                     uint32_t out_name_len,
                                                     char *out_value,
                                                     uint32_t out_value_len)
{
    uint8_t edit_index = 0U;
    const ui_prism_param_label_t *const labels = ui_page_template_tone_prism_labels_for_param(id, &edit_index);
    const char *name = NULL;
    ui_prism_value_kind_t kind = UI_PRISM_VALUE_PERCENT;

    if (labels == NULL)
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_PRISM_OSC1_MODEL:
        case PARAM_PRISM_OSC2_MODEL:
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "MODEL");
            }
            ui_page_template_tone_prism_format_model(value, out_value, out_value_len);
            return 1U;

        case PARAM_PRISM_VOLUME:
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "VOL");
            }
            if ((out_value != NULL) && (out_value_len > 0U))
            {
                ui_page_template_tone_prism_format_percent(ui_page_template_tone_prism_u15(value), out_value, out_value_len);
            }
            return 1U;

        case PARAM_PRISM_TUNE:
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "TUNE");
            }
            if ((out_value != NULL) && (out_value_len > 0U))
            {
                (void)snprintf(out_value, out_value_len, "%+.2fst", (double)value);
            }
            return 1U;

        case PARAM_PRISM_DETUNE:
            if ((out_name != NULL) && (out_name_len > 0U)) (void)snprintf(out_name, out_name_len, "DETUNE");
            if ((out_value != NULL) && (out_value_len > 0U)) (void)snprintf(out_value, out_value_len, "%+.2fst", (double)value);
            return 1U;

        case PARAM_PRISM_DRIFT:
            if ((out_name != NULL) && (out_name_len > 0U)) (void)snprintf(out_name, out_name_len, "DRIFT");
            if ((out_value != NULL) && (out_value_len > 0U)) (void)snprintf(out_value, out_value_len, "%.0f%%", (double)(value * 100.0f));
            return 1U;

        case PARAM_PRISM_PITCH_MOD1:
        case PARAM_PRISM_PITCH_MOD2:
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len,
                               (id == PARAM_PRISM_PITCH_MOD1) ? "P.MOD1" : "P.MOD2");
            }
            if ((out_value != NULL) && (out_value_len > 0U))
            {
                ui_page_template_tone_prism_format_percent(ui_page_template_tone_prism_u15(value), out_value, out_value_len);
            }
            return 1U;

        case PARAM_PRISM_OSC1_AMOD:
        case PARAM_PRISM_OSC2_AMOD:
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "A MOD");
            }
            if ((out_value != NULL) && (out_value_len > 0U))
            {
                ui_page_template_tone_prism_format_bipolar_percent(ui_page_template_tone_prism_u15(value), out_value, out_value_len);
            }
            return 1U;

        case PARAM_PRISM_BALANCE:
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "BAL");
            }
            if ((out_value != NULL) && (out_value_len > 0U))
            {
                (void)snprintf(out_value, out_value_len, "%+.0f%%", (double)(value * 100.0f));
            }
            return 1U;

        case PARAM_PRISM_PHASE1_RESET:
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "PHASE");
            }
            if ((out_value != NULL) && (out_value_len > 0U))
            {
                (void)snprintf(out_value, out_value_len, "%s", (value >= 0.5f) ? "ON" : "OFF");
            }
            return 1U;

        default:
            break;
    }

    if (ui_page_template_tone_prism_kind_for_param(id, labels, &name, &kind) == 0U)
    {
        return 0U;
    }

    if ((out_name != NULL) && (out_name_len > 0U))
    {
        (void)snprintf(out_name, out_name_len, "%s", (name != NULL) ? name : "-");
    }

    ui_page_template_tone_prism_format_value(edit_index, id, kind, value, out_value, out_value_len);

    return 1U;
}

static uint8_t ui_page_template_tone_stack_slot_param(param_id_t id,
                                                      uint8_t *out_slot,
                                                      uint8_t *out_param)
{
    if ((out_slot == NULL) || (out_param == NULL))
    {
        return 0U;
    }

    if ((id >= PARAM_STACK_OSC1_LEVEL) && (id <= PARAM_STACK_OSC3_LEVEL))
    {
        *out_slot = (uint8_t)(id - PARAM_STACK_OSC1_LEVEL);
        *out_param = 0U;
        return 1U;
    }
    if ((id >= PARAM_STACK_OSC1_MODEL) && (id <= PARAM_STACK_OSC3_COLOR))
    {
        const uint8_t rel = (uint8_t)(id - PARAM_STACK_OSC1_MODEL);
        *out_slot = (uint8_t)(rel / 4U);
        *out_param = (uint8_t)((rel % 4U) + 1U);
        return (*out_slot < BRICK6_STACK_SLOT_COUNT) ? 1U : 0U;
    }

    return 0U;
}

static uint8_t ui_page_template_tone_stack_param_text(param_id_t id,
                                                      float value,
                                                      char *out_name,
                                                      uint32_t out_name_len,
                                                      char *out_value,
                                                      uint32_t out_value_len)
{
    const uint8_t active_track = ui_get_active_lane();
    uint8_t stack_slot = 0U;
    uint8_t stack_param = 0U;

    if (id == PARAM_STACK_NOISE_LEVEL)
    {
        if ((out_name != NULL) && (out_name_len > 0U))
        {
            (void)snprintf(out_name, out_name_len, "NOISE");
        }
        return 1U;
    }
    if (id == PARAM_STACK_OSC_DETUNE)
    {
        if ((out_name != NULL) && (out_name_len > 0U))
        {
            (void)snprintf(out_name, out_name_len, "OSC DT");
        }
        return 1U;
    }
    if (id == PARAM_STACK_PHASE_RESET)
    {
        if ((out_name != NULL) && (out_name_len > 0U))
        {
            (void)snprintf(out_name, out_name_len, "PHASE");
        }
        return 1U;
    }
    if (ui_page_template_tone_stack_slot_param(id, &stack_slot, &stack_param) == 0U)
    {
        return 0U;
    }

    const char *name = NULL;
    switch (stack_param)
    {
        case 0U:
            switch (stack_slot)
            {
                case 0U: name = "OSC1 LVL"; break;
                case 1U: name = "OSC2 LVL"; break;
                default: name = "OSC3 LVL"; break;
            }
            break;
        case 1U:
            name = (stack_slot == 0U) ? "OSC1"
                : ((stack_slot == 1U) ? "OSC2" : "OSC3");
            break;
        case 2U:
            switch (stack_slot)
            {
                case 0U: name = "TUNE 1"; break;
                case 1U: name = "TUNE 2"; break;
                default: name = "TUNE 3"; break;
            }
            break;
        case 3U:
        {
            float model_value;
            const param_id_t model_param = (param_id_t)(PARAM_STACK_OSC1_MODEL + (stack_slot * 4U));
            if ((param_registry_get_track_value(model_param, active_track, &model_value) == 0U)
                    || (param_stack_model_param_resolve(
                        (uint8_t)(model_value + 0.5f), 0U, &name) == 0U))
                return 0U;
            break;
        }
        case 4U:
        {
            float model_value;
            const param_id_t model_param = (param_id_t)(PARAM_STACK_OSC1_MODEL + (stack_slot * 4U));
            if ((param_registry_get_track_value(model_param, active_track, &model_value) == 0U)
                    || (param_stack_model_param_resolve(
                        (uint8_t)(model_value + 0.5f), 1U, &name) == 0U))
                return 0U;
            break;
        }
        default: name = "PARAM"; break;
    }

    if ((out_name != NULL) && (out_name_len > 0U))
    {
        (void)snprintf(out_name, out_name_len, "%s", (name != NULL) ? name : "-");
    }
    (void)value;
    (void)out_value;
    (void)out_value_len;
    return 1U;
}

static uiw_widget_type_t ui_page_template_tone_pick_widget(uint8_t slot,
                                                           param_id_t id,
                                                           const char *value_label,
                                                           uiw_widget_type_t suggested_widget)
{
    const ui_prism_param_label_t *const labels = ui_page_template_tone_prism_labels_for_param(id, NULL);
    ui_prism_value_kind_t kind = UI_PRISM_VALUE_PERCENT;

    (void)slot;

    if ((g_ui_template_tone_subset == 0U)
            && (ui_get_track_family(ui_get_active_lane()) == TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(ui_get_active_lane()) == TRACK_TYPE_FM)
            && (id == PARAM_FM_ALGORITHM))
    {
        return UIW_WIDGET_ALGO_ICON;
    }

    if ((id >= PARAM_MODFX_RATE) && (id <= PARAM_MODFX_WIDTH))
    {
        float model_value;
        if (param_registry_query_global(PARAM_MODFX_MODEL, &model_value) == 0U)
            return UIW_WIDGET_NONE;
        const uint8_t model = (uint8_t)(model_value + 0.5f);
        if ((model == FX_MODFX_DAISY_STEREO)
                && (id == PARAM_MODFX_FEEDBACK))
            return UIW_WIDGET_BIPOLAR_BAR;
        if ((model == FX_MODFX_JUNOLOGUE)
                && (id == PARAM_MODFX_OFFSET))
            return UIW_WIDGET_ENUM_TEXT;
    }
    (void)value_label;

    if ((id == PARAM_STACK_OSC1_MODEL)
            || (id == PARAM_STACK_OSC2_MODEL)
            || (id == PARAM_STACK_OSC3_MODEL))
    {
        return UIW_WIDGET_ENUM_TEXT;
    }

    if (ui_page_template_tone_prism_kind_for_param(id, labels, NULL, &kind) == 0U)
    {
        if ((id == PARAM_PRISM_OSC1_MODEL) || (id == PARAM_PRISM_OSC2_MODEL))
        {
            return UIW_WIDGET_ENUM_TEXT;
        }
        if ((id == PARAM_PRISM_TUNE)
                || (id == PARAM_PRISM_OSC1_AMOD)
                || (id == PARAM_PRISM_DETUNE)
                || (id == PARAM_PRISM_BALANCE)
                || (id == PARAM_PRISM_OSC2_AMOD))
        {
            return UIW_WIDGET_BIPOLAR_BAR;
        }
        if (id == PARAM_PRISM_DRIFT)
        {
            return UIW_WIDGET_BAR;
        }
        return suggested_widget;
    }

    switch (kind)
    {
        case UI_PRISM_VALUE_NONE:
            return UIW_WIDGET_EMPTY;
        case UI_PRISM_VALUE_STEPPED:
        case UI_PRISM_VALUE_ENUM:
            return UIW_WIDGET_ENUM_TEXT;
        case UI_PRISM_VALUE_BIPOLAR_PERCENT:
        case UI_PRISM_VALUE_INTERVAL:
            return UIW_WIDGET_BIPOLAR_BAR;
        case UI_PRISM_VALUE_PERCENT:
        case UI_PRISM_VALUE_MORPH:
        case UI_PRISM_VALUE_RATE:
        default:
            return suggested_widget;
    }
}

static ui_template_custom_widget_kind_t ui_page_template_tone_pick_custom_widget(uint8_t slot,
                                                                                 const ui_template_subpage_t *subpage,
                                                                                 param_id_t id)
{
    (void)slot;

    if ((g_ui_template_tone_subset == 0U)
            && (ui_get_track_family(ui_get_active_lane()) == TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(ui_get_active_lane()) == TRACK_TYPE_FM)
            && (subpage != NULL)
            && (((subpage->param_bank.params[0] == PARAM_FM_PITCH_R1)
                    && (subpage->param_bank.params[1] == PARAM_FM_PITCH_R2)
                    && (subpage->param_bank.params[2] == PARAM_FM_PITCH_R3)
                    && (subpage->param_bank.params[3] == PARAM_FM_PITCH_R4))
                || ((subpage->param_bank.params[0] == PARAM_FM_PITCH_L1)
                    && (subpage->param_bank.params[1] == PARAM_FM_PITCH_L2)
                    && (subpage->param_bank.params[2] == PARAM_FM_PITCH_L3)
                    && (subpage->param_bank.params[3] == PARAM_FM_PITCH_L4))))
    {
        (void)id;
        return UI_TEMPLATE_CUSTOM_WIDGET_FM_PITCH_EG_GROUP;
    }

    if ((ui_get_track_family(ui_get_active_lane()) == TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(ui_get_active_lane()) == TRACK_TYPE_WAVE)
            && (subpage != NULL)
            && (((subpage->param_bank.params[0] == UI_PARAM_LOCAL_WAVE_OSC1)
                    && (subpage->param_bank.params[1] == PARAM_WAVE_OSC1_POS)
                    && (subpage->param_bank.params[2] == PARAM_WAVE_OSC1_START)
                    && (subpage->param_bank.params[3] == PARAM_WAVE_OSC1_LEN))
                || ((subpage->param_bank.params[0] == UI_PARAM_LOCAL_WAVE_OSC2)
                    && (subpage->param_bank.params[1] == PARAM_WAVE_OSC2_POS)
                    && (subpage->param_bank.params[2] == PARAM_WAVE_OSC2_START)
                    && (subpage->param_bank.params[3] == PARAM_WAVE_OSC2_LEN))))
    {
        (void)id;
        return UI_TEMPLATE_CUSTOM_WIDGET_WAVE_WAVETABLE;
    }

    uint8_t stack_slot = 0U;
    uint8_t stack_param = 0U;
    if (ui_page_template_tone_stack_slot_param(id, &stack_slot, &stack_param) == 0U)
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
    }

    const param_id_t model_param = (param_id_t)(PARAM_STACK_OSC1_MODEL + (stack_slot * 4U));
    float model_value = 0.0f;
    if (param_registry_get_track_value(model_param, ui_get_active_lane(), &model_value) == 0U)
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
    }

    const brick6_stack_model_t model = (brick6_stack_model_t)(uint8_t)(model_value + 0.5f);
    if ((model == BRICK6_STACK_MODEL_SHAPE) && (stack_param == 4U))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_STACK_WAVEFORM;
    }
    return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
}

static uint8_t ui_page_template_tone_param_text(uint8_t slot,
                                                param_id_t id,
                                                float value,
                                                char *out_name,
                                                uint32_t out_name_len,
                                                char *out_value,
                                                uint32_t out_value_len)
{
    const uint8_t active_track = ui_get_active_lane();
    if ((id >= PARAM_MODFX_RATE) && (id <= PARAM_MODFX_WIDTH))
    {
        const char *name = (id==PARAM_MODFX_RATE)?"RATE":((id==PARAM_MODFX_DEPTH)?"DEPTH":((id==PARAM_MODFX_FEEDBACK)?"FEEDBACK":"OFFSET"));
        float model_value;
        const uint8_t have_model = param_registry_query_global(
            PARAM_MODFX_MODEL, &model_value);
        if (have_model == 0U)
            return 0U;
        if (have_model != 0U)
        {
            const uint8_t model = (uint8_t)(model_value + 0.5f);
            if (model == FX_MODFX_DAISY_STEREO)
                name = (id==PARAM_MODFX_RATE)?"RATE A":((id==PARAM_MODFX_RATE_B)?"RATE B":((id==PARAM_MODFX_OFFSET)?"DELAY A":((id==PARAM_MODFX_DELAY_B)?"DELAY B":((id==PARAM_MODFX_DEPTH)?"DEPTH A":((id==PARAM_MODFX_DEPTH_B)?"DEPTH B":((id==PARAM_MODFX_WIDTH)?"WIDTH":"FEEDBACK A-B"))))));
            else if ((model == FX_MODFX_JUNOLOGUE)
                    && (id == PARAM_MODFX_OFFSET)) name = "MODE";
        }
        if ((out_name != NULL) && (out_name_len > 0U))
        {
            (void)snprintf(out_name, out_name_len, "%s", name);
        }
        if ((out_value != NULL) && (out_value_len > 0U))
        {
            const float shown = param_value_policy_canonical_to_display(
                id, active_track, value);
            const char *unit = NULL;
            if ((isnan(shown) != 0)
                    || (param_value_policy_display_unit(
                        id, active_track, &unit) == 0U))
                return 0U;
            if ((have_model != 0U)
                    && ((uint8_t)(model_value + 0.5f) == FX_MODFX_JUNOLOGUE)
                    && (id == PARAM_MODFX_OFFSET))
            {
                static const char *const modes[] = { "I", "I+II", "II" };
                int32_t mode = (int32_t)(shown + 0.5f);
                if (mode < 0) mode = 0;
                if (mode > 2) mode = 2;
                (void)snprintf(out_value, out_value_len, "%s", modes[mode]);
            }
            else if ((have_model != 0U)
                    && ((uint8_t)(model_value + 0.5f) == FX_MODFX_DAISY_STEREO)
                    && (id == PARAM_MODFX_FEEDBACK))
                (void)snprintf(out_value, out_value_len, "%+.2f%s",
                               (double)shown, unit);
            else
                (void)snprintf(out_value, out_value_len, "%.2f%s",
                               (double)shown, unit);
        }
        (void)slot;
        return 1U;
    }
    if ((g_ui_template_tone_subset == 0U)
            && (ui_get_track_family(active_track) == TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(active_track) == TRACK_TYPE_FM))
    {
        const char *name = NULL;
        switch (id)
        {
            case PARAM_FM_ALGORITHM: name = "ALGO"; break;
            case PARAM_FM_ENV_ATTACK: name = "ATK"; break;
            case PARAM_FM_ENV_DECAY: name = "DECAY"; break;
            case PARAM_FM_TRANSPOSE: name = "TRANSPOSE"; break;
            case UI_PARAM_LOCAL_FM_OPERATOR: name = "OP"; break;
            case PARAM_FM_PITCH_R1: name = "R1"; break;
            case PARAM_FM_PITCH_R2: name = "R2"; break;
            case PARAM_FM_PITCH_R3: name = "R3"; break;
            case PARAM_FM_PITCH_R4: name = "R4"; break;
            case PARAM_FM_PITCH_L1: name = "L1"; break;
            case PARAM_FM_PITCH_L2: name = "L2"; break;
            case PARAM_FM_PITCH_L3: name = "L3"; break;
            case PARAM_FM_PITCH_L4: name = "L4"; break;
            default: break;
        }
        if ((id >= PARAM_FM_OPERATOR_FIRST) && (id <= PARAM_FM_OPERATOR_LAST)
                && ((((uint16_t)id - (uint16_t)PARAM_FM_OPERATOR_FIRST)
                     % PARAM_FM_OPERATOR_PARAM_COUNT) == BRICK6_FM_OPERATOR_FREQ))
        {
            name = "RATIO";
        }
        if (name != NULL)
        {
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "%s", name);
            }
            if ((id >= PARAM_FM_PITCH_L1) && (id <= PARAM_FM_PITCH_L4)
                    && (out_value != NULL) && (out_value_len > 0U))
            {
                const long display_value = (long)((value >= 0.0f) ? (value + 0.5f) : (value - 0.5f));
                (void)snprintf(out_value, out_value_len, "%+ld", display_value);
            }
            (void)slot;
            return 1U;
        }
    }

    if ((ui_get_track_family(active_track) == TRACK_FAMILY_SYNTH)
            && ((ui_get_track_type(active_track) == TRACK_TYPE_WAVE)
                || (ui_get_track_type(active_track) == TRACK_TYPE_PRISM)))
    {
        const char *name = NULL;
        switch (id)
        {
            case UI_PARAM_LOCAL_WAVE_OSC1:
            case UI_PARAM_LOCAL_WAVE_OSC2:
                name = "TABLE";
                break;
            case PARAM_WAVE_OSC1_POS:
            case PARAM_WAVE_OSC2_POS:
                name = "POS";
                break;
            case PARAM_WAVE_OSC1_START:
            case PARAM_WAVE_OSC2_START:
                name = "START";
                break;
            case PARAM_WAVE_OSC1_LEN:
            case PARAM_WAVE_OSC2_LEN:
                name = "LEN";
                break;
            case PARAM_WAVE_VOLUME:
                name = "VOL";
                break;
            case PARAM_WAVE_BALANCE:
                name = "BAL";
                break;
            case PARAM_WAVE_TUNE:
                name = "TUNE";
                break;
            case PARAM_WAVE_DETUNE:
                name = "DETUNE";
                break;
            default:
                break;
        }

        if (name != NULL)
        {
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "%s", name);
            }
            if ((out_value != NULL) && (out_value_len > 0U))
            {
                if ((id == UI_PARAM_LOCAL_WAVE_OSC1) || (id == UI_PARAM_LOCAL_WAVE_OSC2))
                {
                    const uint16_t logical_slot = (uint16_t)((value < 0.0f) ? 0.0f : value + 0.5f);
                    uint16_t runtime_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
                    const sample_global_slot_t *asset = NULL;
                    if (project_control_resolve_wavetable_runtime(
                            logical_slot, &runtime_slot) != 0U)
                    {
                        asset = sample_global_pool_get_slot(runtime_slot);
                    }
                    if ((asset != NULL) && (asset->kind == SAMPLE_GLOBAL_KIND_WAVETABLE)
                            && (asset->state == SAMPLE_GLOBAL_STATE_READY))
                    {
                        (void)snprintf(out_value, out_value_len, "WT%03u", (unsigned int)logical_slot);
                    }
                    else
                    {
                        (void)snprintf(out_value, out_value_len, "NO TABLE");
                    }
                }
                else if ((id == PARAM_WAVE_TUNE) || (id == PARAM_WAVE_DETUNE))
                {
                    const int32_t cents = (int32_t)((value * 100.0f) + ((value >= 0.0f) ? 0.5f : -0.5f));
                    if ((cents % 100L) == 0L)
                    {
                        (void)snprintf(out_value, out_value_len, "%+ldst", (long)(cents / 100L));
                    }
                    else
                    {
                        const char sign = (cents < 0L) ? '-' : '+';
                        const int32_t abs_cents = (cents < 0L) ? -cents : cents;
                        (void)snprintf(out_value, out_value_len, "%c%ld.%02ldst", sign, (long)(abs_cents / 100L), (long)(abs_cents % 100L));
                    }
                }
                else if (id == PARAM_WAVE_BALANCE)
                {
                    (void)snprintf(out_value, out_value_len, "%+.0f%%", value * 100.0f);
                }
                else if ((id == PARAM_WAVE_OSC1_POS)
                        || (id == PARAM_WAVE_OSC2_POS)
                        || (id == PARAM_WAVE_OSC1_START)
                        || (id == PARAM_WAVE_OSC2_START)
                        || (id == PARAM_WAVE_OSC1_LEN)
                        || (id == PARAM_WAVE_OSC2_LEN)
                        || (id == PARAM_WAVE_VOLUME))
                {
                    (void)snprintf(out_value, out_value_len, "%.0f%%", value * 100.0f);
                }
            }
            (void)slot;
            return 1U;
        }
    }

    if ((ui_get_track_family(active_track) == TRACK_FAMILY_SAMPLER)
            && (ui_get_track_type(active_track) == TRACK_TYPE_RAM))
    {
        const char *name = NULL;
        switch (id)
        {
            case UI_PARAM_LOCAL_ASSET:
                name = "SAMPLE";
                break;
            case PARAM_SAMPLER_MODE:
                name = "MODE";
                break;
            case PARAM_SAMPLER_START:
                name = "START";
                break;
            case PARAM_SAMPLER_LENGTH:
                name = "END";
                break;
            case PARAM_SAMPLER_GAIN:
                name = "GAIN";
                break;
            case PARAM_SAMPLER_TUNE:
                name = "TUNE";
                break;
            case PARAM_SAMPLER_LOOP_START:
                name = "LOOP";
                break;
            case PARAM_SAMPLER_SLICE_COUNT:
                name = "SLICE";
                break;
            default:
                break;
        }
        if (name != NULL)
        {
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "%s", name);
            }
            (void)slot;
            (void)value;
            (void)out_value;
            (void)out_value_len;
            return 1U;
        }
    }

    if ((ui_get_track_family(active_track) == TRACK_FAMILY_SAMPLER)
            && (ui_get_track_type(active_track) == TRACK_TYPE_MULTI))
    {
        if (id == UI_PARAM_LOCAL_ASSET)
        {
            float selector = 0.0f;

            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "INST");
            }

            if ((out_value != NULL) && (out_value_len > 0U))
            {
                const uint8_t has_selection =
                    (!isnan(selector = ui_param_get_active_track_display_value(
                        UI_PARAM_LOCAL_ASSET, active_track)))
                    && (ui_template_tone_multi_logical_label(
                            (uint16_t)(selector + 0.5f), out_value, out_value_len) != 0U);
                if (has_selection == 0U)
                {
                    (void)snprintf(out_value, out_value_len, "NONE");
                }
            }

            (void)slot;
            (void)value;
            return 1U;
        }

        if (id == PARAM_SAMPLER_GAIN)
        {
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "GAIN");
            }
            if ((out_value != NULL) && (out_value_len > 0U))
            {
                float display = value;
                if (display < 0.0f)
                {
                    display = 0.0f;
                }
                else if (display > 2.0f)
                {
                    display = 2.0f;
                }
                ui_format_param_127_00(display, 0.0f, 2.0f, out_value, out_value_len);
            }
            return 1U;
        }
    }

    if ((ui_get_track_family(active_track) == TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(active_track) == TRACK_TYPE_PRISM)
            && (ui_page_template_tone_prism_param_text(id, value, out_name, out_name_len, out_value, out_value_len) != 0U))
    {
        (void)slot;
        return 1U;
    }

    if ((ui_get_track_family(active_track) == TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(active_track) == TRACK_TYPE_STACK)
            && (ui_page_template_tone_stack_param_text(id,
                                                       value,
                                                       out_name,
                                                       out_name_len,
                                                       out_value,
                                                       out_value_len) != 0U))
    {
        (void)slot;
        return 1U;
    }

    if ((ui_get_track_family(active_track) == TRACK_FAMILY_DRUM)
            && (ui_get_track_type(active_track) == TRACK_TYPE_DRUM_MD)
            && (id >= PARAM_DRUM_MD_MODEL)
            && (id <= PARAM_DRUM_MD_P8))
    {
        float model_value;
        if (param_registry_get_track_value(PARAM_DRUM_MD_MODEL, active_track, &model_value) == 0U)
            return 0U;
        const md_model_profile_t *const profile = md_model_profile_get(md_model_validate(model_value));
        if (id == PARAM_DRUM_MD_MODEL)
        {
            (void)snprintf(out_name, out_name_len, "MODEL");
            (void)snprintf(out_value, out_value_len, "%s", profile->name);
        }
        else
        {
            const uint8_t md_slot = (uint8_t)(id - PARAM_DRUM_MD_P1);
            if (md_slot >= profile->slot_count)
            {
                return 0U;
            }
            (void)snprintf(out_name, out_name_len, "%s", profile->slot_labels[md_slot]);
            (void)snprintf(out_value, out_value_len, "%u", (unsigned)(value + 0.5f));
        }
        (void)slot;
        return 1U;
    }

    if ((ui_get_track_family(active_track) == TRACK_FAMILY_DRUM)
            && (ui_get_track_type(active_track) == TRACK_TYPE_DRUM_BD_ANALOG))
    {
        const char *name = NULL;
        switch (id)
        {
            case PARAM_DRUM_TRX_BD_PITCH:
                name = "Pitch";
                break;
            case PARAM_DRUM_TRX_BD_DECAY:
                name = "Decay";
                break;
            case PARAM_DRUM_TRX_BD_HARMONICS:
                name = "Tone";
                break;
            case PARAM_DRUM_TRX_BD_PITCH_SWEEP:
                name = "FM";
                break;
            default:
                break;
        }

        if (name == NULL)
        {
            return 0U;
        }

        if ((out_name != NULL) && (out_name_len > 0U))
        {
            (void)snprintf(out_name, out_name_len, "%s", name);
        }
        (void)slot;
        (void)value;
        (void)out_value;
        (void)out_value_len;
        return 1U;
    }

    return 0U;
}

