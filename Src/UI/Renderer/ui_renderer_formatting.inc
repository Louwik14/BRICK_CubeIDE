static void ui_renderer_template_format_active_pattern_label(char *out, uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    ui_pattern_stub_state_t pattern_state;
    memset(&pattern_state, 0, sizeof(pattern_state));
    ui_get_pattern_stub_state(&pattern_state);

    const char bank = (char)('A' + (pattern_state.active_bank & 0x0FU));
    (void)snprintf(out, out_len, "%c-%02u", bank, (unsigned int)(pattern_state.active_pattern + 1U));
}

static const uint8_t g_ui_template_frame_x[4] = {0U, 32U, 64U, 96U};
static const uint8_t g_ui_template_footer_x[4] = {0U, 32U, 64U, 96U};
static const uint8_t g_ui_template_footer_w[4] = {32U, 32U, 32U, 32U};
static const char *const g_ui_template_midi_note_names[12] = {"C", "C#", "D", "D#", "E", "F",
                                                              "F#", "G", "G#", "A", "A#", "B"};
static void ui_renderer_template_draw_bar_value_text(int widget_x,
                                                     int widget_y,
                                                     int widget_w,
                                                     int widget_h,
                                                     const char *value_txt);
static uint8_t ui_renderer_template_get_param_authority_track(param_id_t id);
static const char *const g_ui_template_lfo_sync_labels[MOD_LFO_SYNC_RATE_COUNT] = {
    "8BAR", "4BAR", "2BAR", "1BAR", "1/2", "1/2T", "1/4", "1/4T",
    "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64", "1/128"
};

static void ui_renderer_template_format_semitones(float value, char *out, uint32_t out_len)
{
    int32_t cents = (int32_t)((value >= 0.0f) ? ((value * 100.0f) + 0.5f) : ((value * 100.0f) - 0.5f));
    if ((cents % 100L) != 0L)
    {
        const char *sign = "";
        if (cents < 0)
        {
            sign = "-";
            cents = -cents;
        }
        (void)snprintf(out, out_len, "%s%ld.%02ld st", sign, (long)(cents / 100L), (long)(cents % 100L));
        return;
    }

    const int32_t semitones = cents / 100L;
    if (semitones == 0L)
    {
        (void)snprintf(out, out_len, "0 st");
        return;
    }

    (void)snprintf(out, out_len, "%+ld st", (long)semitones);
}

static void ui_renderer_template_format_fixed(float value, uint8_t decimals, const char *unit, char *out, uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    int32_t scale = 1;
    for (uint8_t i = 0U; i < decimals; ++i)
    {
        scale *= 10;
    }

    float scaled_f = value * (float)scale;
    if (scaled_f > 2147483647.0f)
    {
        scaled_f = 2147483647.0f;
    }
    else if (scaled_f < -2147483647.0f)
    {
        scaled_f = -2147483647.0f;
    }

    int32_t scaled = (int32_t)((scaled_f >= 0.0f) ? (scaled_f + 0.5f) : (scaled_f - 0.5f));
    const char *sign = "";
    if (scaled < 0)
    {
        sign = "-";
        scaled = -scaled;
    }

    const int32_t whole = scaled / scale;
    const int32_t frac = scaled % scale;
    const char *suffix = (unit != NULL) ? unit : "";

    /* No printf float in the embedded renderer: newlib float formatting allocates heap. */
    if (decimals == 1U)
    {
        (void)snprintf(out, out_len, "%s%ld.%01ld%s", sign, (long)whole, (long)frac, suffix);
    }
    else
    {
        (void)snprintf(out, out_len, "%s%ld.%02ld%s", sign, (long)whole, (long)frac, suffix);
    }
}

static const char *ui_renderer_template_matrix_source_label(float value)
{
    const uint8_t index = (uint8_t)(value + 0.5f);
    static const char *const labels[] = {
        "OFF",
        "LFO1",
        "LFO2",
        "LFO3",
        "ENV1",
        "ENV2",
        "ENV3",
        "MLT1",
        "MLT2",
        "SLW1",
        "SLW2"
    };

    return (index < (uint8_t)(sizeof(labels) / sizeof(labels[0]))) ? labels[index] : "OFF";
}

static const char *ui_renderer_template_matrix_source_value_label(float value)
{
    const uint8_t index = (uint8_t)(value + 0.5f);
    static const char *const labels[] = {
        "Off",
        "LFO 1",
        "LFO 2",
        "LFO 3",
        "env flt",
        "env vca",
        "env mod",
        "MULT1",
        "MULT2",
        "SLEW1",
        "SLEW2"
    };

    return (index < (uint8_t)(sizeof(labels) / sizeof(labels[0]))) ? labels[index] : "Off";
}

void ui_format_param_127_00(float value, float min_value, float max_value, char *out, uint32_t out_len)
{
    float normalized = 0.0f;
    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    if (max_value > min_value)
    {
        if (value < min_value)
        {
            value = min_value;
        }
        else if (value > max_value)
        {
            value = max_value;
        }
        normalized = ((value - min_value) * 127.0f) / (max_value - min_value);
    }

    ui_renderer_template_format_fixed(normalized, 2U, "", out, out_len);
}

static uint8_t ui_renderer_template_get_edit_owner_track(void)
{
    ui_template_edit_context_t context;
    return (ui_template_edit_context_resolve_active(&context) != 0U)
        ? context.owner_entity : ui_get_active_lane();
}

static uint8_t ui_renderer_template_get_selected_entity(void)
{
    ui_template_edit_context_t context;
    return (ui_template_edit_context_resolve_active(&context) != 0U)
        ? context.selected_entity : ui_get_active_lane();
}

static void ui_renderer_template_format_value(param_id_t id, float value, char *out, uint32_t out_len)
{
    const param_desc_t *desc = &param_registry[id];

    if (id == PARAM_CFG_TRACK)
    {
        (void)snprintf(out, out_len, "%s", ui_get_track_family_display_name((track_family_t)((uint8_t)(value + 0.5f))));
        return;
    }

    if (id == PARAM_CFG_TRACK_TYPE)
    {
        const track_family_t active_family = ui_get_track_family(ui_renderer_template_get_edit_owner_track());
        const track_type_t active_type = ui_get_track_type_from_family_index(active_family, (uint8_t)(value + 0.5f));
        (void)snprintf(out, out_len, "%s", ui_get_track_type_display_name(active_family, active_type));
        return;
    }

    if (id == PARAM_CFG_MIDI_CH)
    {
        const uint8_t channel = (uint8_t)(value + 0.5f);
        const uint8_t duplicate = ui_track_midi_channel_used_by_other(ui_renderer_template_get_edit_owner_track(), channel);
        (void)snprintf(out, out_len, "%u%s", (unsigned int)channel, (duplicate != 0U) ? "*" : "");
        return;
    }

    if (id == PARAM_MOD_MATRIX_SLOT)
    {
        (void)snprintf(out, out_len, "%u", (unsigned int)((uint8_t)(value + 0.5f) + 1U));
        return;
    }

    if ((id == PARAM_MOD_MATRIX_SOURCE)
            || (id == PARAM_MOD_MULTI_1_A)
            || (id == PARAM_MOD_MULTI_1_B)
            || (id == PARAM_MOD_MULTI_2_A)
            || (id == PARAM_MOD_MULTI_2_B)
            || (id == PARAM_MOD_SLEW_1_SOURCE)
            || (id == PARAM_MOD_SLEW_2_SOURCE))
    {
        (void)snprintf(out, out_len, "%s", ui_renderer_template_matrix_source_value_label(value));
        return;
    }

    if (id == PARAM_MOD_MATRIX_DEST)
    {
        if (mod_lfo_v1_dest_label(ui_renderer_template_get_edit_owner_track(), (uint16_t)(value + 0.5f), out, out_len) != 0U)
        {
            return;
        }
    }

    if ((id == PARAM_LFO1_RATE) || (id == PARAM_LFO2_RATE) || (id == PARAM_LFO3_RATE))
    {
        if (value < -0.0001f)
        {
            (void)snprintf(out, out_len, "%.2fHz", (double)(-value));
            return;
        }
        if (value > 0.0001f)
        {
            uint8_t idx = (uint8_t)(value + 0.5f);
            if (idx > 0U)
            {
                idx--;
            }
            if (idx < MOD_LFO_SYNC_RATE_COUNT)
            {
                (void)snprintf(out, out_len, "%s", g_ui_template_lfo_sync_labels[idx]);
                return;
            }
        }
        (void)snprintf(out, out_len, "OFF");
        return;
    }

    uint8_t play_index = 0U;
    seq_step_play_field_t play_field = SEQ_STEP_PLAY_FIELD_COUNT;
    const uint8_t is_play_param = seq_model_play_resolve_param(id,
                                                               &play_index,
                                                               &play_field);
    (void)play_index;
    if ((is_play_param != 0U) && (play_field == SEQ_STEP_PLAY_FIELD_VELOCITY))
    {
        if (value < 0.5f)
        {
            (void)snprintf(out, out_len, "OFF");
            return;
        }
    }

    if ((is_play_param != 0U) && (play_field == SEQ_STEP_PLAY_FIELD_NOTE))
    {
        int32_t note = (int32_t)(value + 0.5f);
        if (note < 0)
        {
            note = 0;
        }
        if (note > 127)
        {
            note = 127;
        }

        const int32_t note_index = note % 12;
        const int32_t octave = (note / 12) - 1;
        (void)snprintf(out, out_len, "%s%ld", g_ui_template_midi_note_names[note_index], (long)octave);
        return;
    }

    const float display_value = param_value_policy_canonical_to_display(
        id, ui_renderer_template_get_param_authority_track(id), value);

    if (id == PARAM_CFG_TEMPO)
    {
        ui_renderer_template_format_fixed(display_value, 2U, "", out, out_len);
        return;
    }

    if (id == PARAM_CFG_METRO)
    {
        if (value < 0.5f)
        {
            (void)snprintf(out, out_len, "OFF");
            return;
        }
        (void)snprintf(out, out_len, "%ld", (long)(value + 0.5f));
        return;
    }

    if ((desc->display_type == PARAM_DISPLAY_INT)
            && (desc->unit != NULL)
            && (strcmp(desc->unit, "st") == 0))
    {
        ui_renderer_template_format_semitones(display_value, out, out_len);
        return;
    }

    switch (desc->display_type)
    {
        case PARAM_DISPLAY_BOOL:
        {
            const uint32_t index = (value >= 0.5f) ? 1U : 0U;
            const char *label = ((desc->labels != NULL) && (desc->labels[index] != NULL)) ? desc->labels[index] : ((index != 0U) ? "On" : "Off");
            (void)snprintf(out, out_len, "%s", label);
            break;
        }

        case PARAM_DISPLAY_ENUM:
        {
            const int32_t index = (int32_t)(value + 0.5f);
            const char *label = NULL;
            if ((desc->labels != NULL) && (index >= 0))
            {
                label = desc->labels[index];
            }
            if (label != NULL)
            {
                (void)snprintf(out, out_len, "%s", label);
            }
            else
            {
                (void)snprintf(out, out_len, "%ld", (long)index);
            }
            break;
        }

        case PARAM_DISPLAY_PERCENT:
            ui_renderer_template_format_fixed(display_value, 2U, desc->unit, out, out_len);
            break;

        case PARAM_DISPLAY_DB:
            ui_renderer_template_format_fixed(display_value, 2U, desc->unit, out, out_len);
            break;

        case PARAM_DISPLAY_TIME_MS:
            ui_renderer_template_format_fixed(display_value, 2U, "ms", out, out_len);
            break;

        case PARAM_DISPLAY_RATIO:
            ui_renderer_template_format_fixed(display_value, 2U, "", out, out_len);
            break;

        case PARAM_DISPLAY_INT:
            (void)snprintf(out, out_len, "%ld", (long)(value + 0.5f));
            break;

        default:
            ui_renderer_template_format_fixed(display_value, 2U, desc->unit, out, out_len);
            break;
    }
}

static void ui_renderer_template_format_cpu_avg(char *out, uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    audio_boot_diag_snapshot_t audio_diag = {0};
    audio_boot_diag_read(&audio_diag);
    if (audio_diag.cpu_load_valid == 0U)
    {
        (void)snprintf(out, out_len, "--%%T---M---C0");
        return;
    }

    const uint32_t avg_permille = audio_diag.avg_permille;
    const uint32_t percent = avg_permille / 10U;
    const uint32_t hundredths = (avg_permille % 10U) * 10U;
    (void)snprintf(out,
                   out_len,
                   "%lu.%02lu",
                   (unsigned long)percent,
                   (unsigned long)hundredths);
}

static int ui_renderer_template_center_x(int x, int w, const char *txt)
{
    const int text_w = (int)drv_display_text_width(txt);
    int out = x + ((w - text_w) / 2);
    if (out < (x + 1))
    {
        out = x + 1;
    }
    return out;
}

static uint8_t ui_renderer_template_right_x(uint8_t right_margin_px, uint8_t text_w)
{
    if (text_w >= (uint8_t)(OLED_WIDTH - right_margin_px))
    {
        return 0U;
    }

    return (uint8_t)(OLED_WIDTH - right_margin_px - text_w);
}

static void ui_renderer_template_fit_text(char *txt, uint8_t max_px)
{
    if ((txt == NULL) || (max_px == 0U))
    {
        return;
    }

    if (drv_display_text_width(txt) <= max_px)
    {
        return;
    }

    uint32_t len = (uint32_t)strlen(txt);
    while ((len > 1U) && (drv_display_text_width(txt) > max_px))
    {
        if (len > 2U)
        {
            txt[len - 2U] = '.';
        }
        txt[len - 1U] = '\0';
        len--;
    }
}

static void ui_renderer_template_draw_open_corner_frame(int x, int y, int w, int h)
{
    const int c = 2;

    drv_display_draw_line(x + c,         y,             x + w - 1 - c, y);
    drv_display_draw_line(x + c,         y + h - 1,     x + w - 1 - c, y + h - 1);

    drv_display_draw_line(x,             y + c,         x,             y + h - 1 - c);
    drv_display_draw_line(x + w - 1,     y + c,         x + w - 1,     y + h - 1 - c);

    drv_display_draw_line(x,             y + c,         x + c,         y);
    drv_display_draw_line(x + w - 1 - c, y,             x + w - 1,     y + c);

    drv_display_draw_line(x,             y + h - 1 - c, x + c,         y + h - 1);
    drv_display_draw_line(x + w - 1 - c, y + h - 1,     x + w - 1,     y + h - 1 - c);
}

static void ui_renderer_template_draw_brick_frame(int x, int y, int w, int h)
{
    ui_renderer_template_draw_open_corner_frame(x, y, w, h);

    if ((w > 8) && (h > 8))
    {
        drv_display_draw_pixel(x + 2, y + 2, true);
        drv_display_draw_pixel(x + w - 3, y + 2, true);
        drv_display_draw_pixel(x + 2, y + h - 3, true);
        drv_display_draw_pixel(x + w - 3, y + h - 3, true);
    }
}

static void ui_renderer_template_draw_param_frame(int x, int y, int w, int h)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

static void ui_renderer_template_draw_inverted_label(uint8_t x, uint8_t y, const char *txt, const font_t *font)
{
    drv_display_set_font(font);
    const uint8_t w = (uint8_t)(drv_display_text_width(txt) + 2U);
    const uint8_t h = (uint8_t)(drv_display_font_height() + 2U);
    drv_display_fill_rect(x, y, w, h);
    drv_display_draw_text_inverted((uint8_t)(x + 1U), (uint8_t)(y + 1U), txt);
}

static void ui_renderer_template_fill_lock_label_frame(uint8_t text_x, uint8_t y, uint8_t text_w, uint8_t h)
{
    uint8_t box_x = text_x;
    uint8_t box_w = text_w;

    if (box_x > 0U)
    {
        box_x--;
        box_w++;
    }
    if ((uint16_t)box_x + (uint16_t)box_w < (uint16_t)OLED_WIDTH)
    {
        box_w++;
    }

    drv_display_fill_rect(box_x, y, box_w, h);
}

static void ui_renderer_template_draw_track_badge(uint8_t x, uint8_t y, const char *txt)
{
    drv_display_set_font(&FONT_5X7);
    const uint8_t w = 11U;
    const uint8_t h = (uint8_t)(drv_display_font_height() + 2U);
    drv_display_fill_rect(x, y, w, h);
    drv_display_draw_text_inverted((uint8_t)(ui_renderer_template_center_x(x, w, txt) - 1), (uint8_t)(y + 1U), txt);
}

static uiw_widget_type_t ui_renderer_template_resolve_widget_type(const ui_template_page_state_t *state,
                                                                  uint8_t slot,
                                                                  param_id_t id,
                                                                  const param_desc_t *desc,
                                                                  const char *enum_label,
                                                                  const char *value_label)
{
    uiw_widget_type_t widget = uiw_pick_widget_type(desc, enum_label);

    if ((state != NULL) && (state->widget_picker != NULL))
    {
        widget = state->widget_picker(slot, id, value_label, widget);
    }

    return widget;
}

static uint8_t ui_renderer_template_get_param_authority_track(param_id_t id)
{
    const uint8_t selected_entity = ui_renderer_template_get_selected_entity();
    if ((id == PARAM_AUDIO_FX_P1) || (id == PARAM_AUDIO_FX_P2)
            || (id == PARAM_AUDIO_FX_P3) || (id == PARAM_AUDIO_FX_MODEL)
            || (id == PARAM_AUDIO_FX_B_P1) || (id == PARAM_AUDIO_FX_B_P2)
            || (id == PARAM_AUDIO_FX_B_P3) || (id == PARAM_AUDIO_FX_B_MODEL))
    {
        /* Encoder flash is authored against the selected Audio-FX entity. */
        return selected_entity;
    }
    uint8_t track = ui_renderer_template_get_edit_owner_track();
    (void)ui_page_template_play_resolve_param_track(id, selected_entity, &track);
    return track;
}

static float ui_renderer_template_get_param_display_value(param_id_t id)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_NONE)
            || (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
    {
        return param_get(id);
    }

    return ui_param_get_active_track_display_value(id,
                                                    ui_renderer_template_get_param_authority_track(id));
}

static uint8_t ui_renderer_template_get_visible_param_value(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                            param_id_t id,
                                                            float *out_value,
                                                            uint8_t *out_inverted)
{
    if ((id >= PARAM_COUNT) || (out_value == 0))
    {
        return 0U;
    }

    if (out_inverted != 0)
    {
        *out_inverted = 0U;
    }

    float value = ui_renderer_template_get_param_display_value(id);
    uint8_t macro_lock_track = 0xFFU;
    float macro_lock_scene_value = 0.0f;
    const uint8_t has_macro_lock_value =
        ui_macro_interaction_get_param_lock_value(id,
                                                  &macro_lock_track,
                                                  &macro_lock_scene_value);
    const uint8_t macro_value_visible =
        (uint8_t)((has_macro_lock_value != 0U)
                && (macro_lock_track == ui_renderer_template_get_param_authority_track(id)));

    if (macro_value_visible != 0U)
    {
        value = macro_lock_scene_value;
    }
    else
    {
        (void)ui_param_try_get_seq_plock_feedback_with_frame(plock_frame_ctx, id, &value, out_inverted);
    }

    *out_value = value;
    return 1U;
}

static void ui_renderer_template_build_param_text(const ui_template_page_state_t *state,
                                                  uint8_t slot,
                                                  param_id_t id,
                                                  float value,
                                                  char *name_txt,
                                                  uint32_t name_len,
                                                  char *value_txt,
                                                  uint32_t value_len)
{
    const param_desc_t *desc = &param_registry[id];

    if ((name_txt != NULL) && (name_len > 0U))
    {
        (void)snprintf(name_txt, name_len, "%s", desc->name);
    }
    if ((value_txt != NULL) && (value_len > 0U))
    {
        ui_renderer_template_format_value(id, value, value_txt, value_len);
    }

    if ((state != NULL) && (state->param_text != NULL))
    {
        (void)state->param_text(slot,
                                id,
                                value,
                                name_txt,
                                name_len,
                                value_txt,
                                value_len);
    }
}

static uint8_t ui_renderer_template_prepare_param_slot_texts(const ui_template_page_state_t *state,
                                                             const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                             uint8_t slot,
                                                             param_id_t id,
                                                             float *out_value,
                                                             uint8_t *out_draw_name_inverted,
                                                             char *name_txt,
                                                             uint32_t name_len,
                                                             char *value_txt,
                                                             uint32_t value_len,
                                                             char *bottom_txt,
                                                             uint32_t bottom_len,
                                                             uint8_t *out_flash_active)
{
    if ((id >= PARAM_COUNT) || (out_value == NULL) || (out_draw_name_inverted == NULL)
            || (name_txt == NULL) || (name_len == 0U)
            || (value_txt == NULL) || (value_len == 0U)
            || (bottom_txt == NULL) || (bottom_len == 0U))
    {
        return 0U;
    }

    *out_draw_name_inverted = 0U;
    (void)ui_renderer_template_get_visible_param_value(plock_frame_ctx, id, out_value, out_draw_name_inverted);

    ui_renderer_template_build_param_text(state,
                                          slot,
                                          id,
                                          *out_value,
                                          name_txt,
                                          name_len,
                                          value_txt,
                                          value_len);

    float flash_value = 0.0f;
    ui_param_value_flash_kind_t flash_kind = UI_PARAM_VALUE_FLASH_DIRECT;
    const uint8_t audio_fx_param = (uint8_t)((id == PARAM_AUDIO_FX_P1)
        || (id == PARAM_AUDIO_FX_P2) || (id == PARAM_AUDIO_FX_P3)
        || (id == PARAM_AUDIO_FX_B_P1) || (id == PARAM_AUDIO_FX_B_P2)
        || (id == PARAM_AUDIO_FX_B_P3));
    const uint8_t synth_immediate_param = (uint8_t)((id == PARAM_WAVE_VOLUME)
        || (id == PARAM_WAVE_BALANCE)
        || (id == PARAM_WAVE_TUNE)
        || (id == PARAM_WAVE_DETUNE)
        || (id == PARAM_WAVE_OSC1_POS)
        || (id == PARAM_WAVE_OSC2_POS)
        || (id == PARAM_WAVE_OSC1_START)
        || (id == PARAM_WAVE_OSC2_START)
        || (id == PARAM_WAVE_OSC1_LEN)
        || (id == PARAM_WAVE_OSC2_LEN)
        || (id == PARAM_PRISM_OSC1_PARAM1)
        || (id == PARAM_PRISM_OSC1_PARAM2)
        || (id == PARAM_PRISM_OSC1_AMOD)
        || (id == PARAM_PRISM_OSC2_PARAM1)
        || (id == PARAM_PRISM_OSC2_PARAM2)
        || (id == PARAM_PRISM_OSC2_AMOD)
        || (id == PARAM_PRISM_VOLUME)
        || (id == PARAM_PRISM_BALANCE)
        || (id == PARAM_PRISM_TUNE)
        || (id == PARAM_PRISM_DETUNE)
        || (id == PARAM_PRISM_DRIFT)
        || (id == PARAM_PRISM_PITCH_MOD1)
        || (id == PARAM_PRISM_PITCH_MOD2));
    const uint8_t flash_active = ((audio_fx_param != 0U) || (synth_immediate_param != 0U))
        ? ui_param_is_user_tweak_active(slot, id)
        : ui_param_get_slot_value_flash(slot,
                                        id,
                                        ui_renderer_template_get_param_authority_track(id),
                                        &flash_value,
                                        &flash_kind);
    if (out_flash_active != NULL)
    {
        *out_flash_active = flash_active;
    }
    if (flash_active != 0U)
    {
        char flash_name[24];
        (void)flash_kind;
        if ((audio_fx_param != 0U) || (synth_immediate_param != 0U)) flash_value = *out_value;
        ui_renderer_template_build_param_text(state,
                                              slot,
                                              id,
                                              flash_value,
                                              flash_name,
                                              (uint32_t)sizeof(flash_name),
                                              bottom_txt,
                                              bottom_len);
    }
    else
    {
        (void)snprintf(bottom_txt, bottom_len, "%s", (*out_draw_name_inverted != 0U) ? value_txt : name_txt);
    }

    return 1U;
}

