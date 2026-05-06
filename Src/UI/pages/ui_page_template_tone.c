#include <stddef.h>
#include <stdio.h>
#include "pages/ui_page_template_tone.h"

#include "Audio/fx_master_macro.h"
#include "Param/param_registry.h"
#include "ui_core.h"
#include "ui_template_page.h"

static const ui_template_family_t g_ui_template_tone_family_buffer = {
    .family_title = "TONE",
    .nav_labels = { "REC", "FADE", "STR", "SYNC" },
    .subpages = {
        { .title = "REC", .param_bank = { .params = { PARAM_BUFFER_REC_LEN, PARAM_BUFFER_Q_REC, PARAM_BUFFER_Q_PLAY, PARAM_BUFFER_RATE } } },
        { .title = "FADE", .param_bank = { .params = { PARAM_BUFFER_FADE_IN, PARAM_BUFFER_FADE_OUT, PARAM_BUFFER_XFADE, PARAM_BUFFER_PRESERVE_PITCH } } },
        { .title = "STR", .param_bank = { .params = { PARAM_BUFFER_TSTR, PARAM_BUFFER_GRAIN, PARAM_BUFFER_HOP, PARAM_COUNT } } },
        { .title = "SYNC", .param_bank = { .params = { PARAM_BUFFER_SYNC_LEN, PARAM_BUFFER_SRC_BPM, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_master_fx = {
    .family_title = "TONE",
    .nav_labels = { "FX1", "FX2", "FX3", "FX4" },
    .subpages = {
        { .title = "FX1", .param_bank = { .params = { PARAM_MASTER_FX1_TYPE, PARAM_MASTER_FX1_LEVEL, PARAM_MASTER_FX1_A, PARAM_MASTER_FX1_B } } },
        { .title = "FX2", .param_bank = { .params = { PARAM_MASTER_FX2_TYPE, PARAM_MASTER_FX2_LEVEL, PARAM_MASTER_FX2_A, PARAM_MASTER_FX2_B } } },
        { .title = "FX3", .param_bank = { .params = { PARAM_MASTER_FX3_TYPE, PARAM_MASTER_FX3_LEVEL, PARAM_MASTER_FX3_A, PARAM_MASTER_FX3_B } } },
        { .title = "FX4", .param_bank = { .params = { PARAM_MASTER_FX4_TYPE, PARAM_MASTER_FX4_LEVEL, PARAM_MASTER_FX4_A, PARAM_MASTER_FX4_B } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_sampler = {
    .family_title = "TONE",
    .nav_labels = { "PLAY", "FX", "-", "-" },
    .subpages = {
        { .title = "PLAY", .param_bank = { .params = { PARAM_SAMPLER_SAMPLE, PARAM_SAMPLER_GAIN, PARAM_SAMPLER_START, PARAM_SAMPLER_END } } },
        { .title = "FX", .param_bank = { .params = { PARAM_SAMPLER_MODE, PARAM_SAMPLER_TUNE, PARAM_SAMPLER_FADE_IN, PARAM_SAMPLER_FADE_OUT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_slicer = {
    .family_title = "TONE",
    .nav_labels = { "SLICE", "-", "-", "-" },
    .subpages = {
        { .title = "SLICE", .param_bank = { .params = { PARAM_SAMPLER_SAMPLE, PARAM_SAMPLER_SLICE_COUNT, PARAM_SAMPLER_TUNE, PARAM_SAMPLER_GAIN } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_clip = {
    .family_title = "TONE",
    .nav_labels = { "PLAY", "CLIP", "SYNC", "STR" },
    .subpages = {
        { .title = "PLAY", .param_bank = { .params = { PARAM_SAMPLER_SAMPLE, PARAM_SAMPLER_GAIN, PARAM_SAMPLER_CLIP_SOURCE_BPM, PARAM_COUNT } } },
        { .title = "CLIP", .param_bank = { .params = { PARAM_SAMPLER_CLIP_PLAY_MODE, PARAM_SAMPLER_CLIP_LOOP, PARAM_SAMPLER_CLIP_STRETCH_MODE, PARAM_SAMPLER_CLIP_PITCH } } },
        { .title = "SYNC", .param_bank = { .params = { PARAM_SAMPLER_CLIP_SYNC_LENGTH, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "STR", .param_bank = { .params = { PARAM_SAMPLER_CLIP_GRAIN, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_opal = {
    .family_title = "TONE",
    .nav_labels = { "OPAL", "-", "-", "-" },
    .subpages = {
        { .title = "OPAL", .param_bank = { .params = { PARAM_OPAL_PATCH, PARAM_OPAL_INDEX, PARAM_OPAL_TIME, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_braids = {
    .family_title = "TONE",
    .nav_labels = { "EDIT", "TONE", "-", "-" },
    .subpages = {
        { .title = "EDIT", .param_bank = { .params = { PARAM_BRAIDS_EDIT, PARAM_BRAIDS_FINE, PARAM_BRAIDS_COARSE, PARAM_BRAIDS_FM } } },
        { .title = "TONE", .param_bank = { .params = { PARAM_BRAIDS_TIMBRE, PARAM_BRAIDS_MODULATION, PARAM_BRAIDS_COLOR, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_midi = {
    .family_title = "TONE",
    .nav_labels = { "PROG", "CC1", "CC2", "CC3" },
    .subpages = {
        { .title = "PROG", .param_bank = { .params = { PARAM_MIDI_PROGRAM, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "CC1", .param_bank = { .params = { PARAM_MIDI_CC1_1, PARAM_MIDI_CC1_2, PARAM_MIDI_CC1_3, PARAM_MIDI_CC1_4 } } },
        { .title = "CC2", .param_bank = { .params = { PARAM_MIDI_CC2_1, PARAM_MIDI_CC2_2, PARAM_MIDI_CC2_3, PARAM_MIDI_CC2_4 } } },
        { .title = "CC3", .param_bank = { .params = { PARAM_MIDI_CC3_1, PARAM_MIDI_CC3_2, PARAM_MIDI_CC3_3, PARAM_MIDI_CC3_4 } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_hybrid = {
    .family_title = "TONE",
    .nav_labels = { "PROG", "CC1", "CC2", "CC3" },
    .subpages = {
        { .title = "PROG", .param_bank = { .params = { PARAM_HYBRID_GATE, PARAM_MIDI_PROGRAM, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "CC1", .param_bank = { .params = { PARAM_MIDI_CC1_1, PARAM_MIDI_CC1_2, PARAM_MIDI_CC1_3, PARAM_MIDI_CC1_4 } } },
        { .title = "CC2", .param_bank = { .params = { PARAM_MIDI_CC2_1, PARAM_MIDI_CC2_2, PARAM_MIDI_CC2_3, PARAM_MIDI_CC2_4 } } },
        { .title = "CC3", .param_bank = { .params = { PARAM_MIDI_CC3_1, PARAM_MIDI_CC3_2, PARAM_MIDI_CC3_3, PARAM_MIDI_CC3_4 } } },
    },
    .default_subpage = 0U,
};


static ui_template_family_t g_ui_template_tone_family_drum = {
    .family_title = "TONE",
    .nav_labels = { "MAIN", "-", "-", "-" },
    .subpages = {
        { .title = "MAIN", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t *ui_page_template_tone_resolve_family(void)
{
    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_TONE);
}

static uint8_t ui_page_template_tone_param_text(uint8_t slot,
                                                param_id_t id,
                                                float value,
                                                char *out_name,
                                                uint32_t out_name_len,
                                                char *out_value,
                                                uint32_t out_value_len);

static ui_template_page_state_t g_ui_template_tone_state = {
    .family = 0,
    .family_resolver = ui_page_template_tone_resolve_family,
    .param_text = ui_page_template_tone_param_text,
    .active_subpage = 0U,
    .has_visited = 0U,
};

static void ui_page_template_tone_master_fx_macro_labels(uint8_t fx_type,
                                                         const char **out_a,
                                                         const char **out_b)
{
    static const char *const k_labels[][2] = {
        { "---", "---" },
        { "TONE", "SHAPE" },
        { "BITS", "RATE" },
        { "RATE", "REL" },
        { "RATE", "SHAPE" },
        { "TIME", "FB" },
        { "RATE", "DEPTH" },
        { "TUNE", "FB" },
        { "FREQ", "COLOR" },
        { "SEMI", "FINE" },
        { "VOWL", "TONE" },
        { "SIZE", "RATE" },
        { "TIME", "HOLD" },
    };

    if ((out_a == NULL) || (out_b == NULL))
    {
        return;
    }

    if (fx_type >= (uint8_t)(sizeof(k_labels) / sizeof(k_labels[0])))
    {
        fx_type = 0U;
    }

    *out_a = k_labels[fx_type][0];
    *out_b = k_labels[fx_type][1];
}

static uint8_t ui_page_template_tone_master_fx_u7(float value)
{
    if (value < 0.0f)
    {
        return 0U;
    }
    if (value > 127.0f)
    {
        return 127U;
    }
    return (uint8_t)(value + 0.5f);
}

static uint8_t ui_page_template_tone_master_fx_index(uint8_t raw, uint8_t max_index)
{
    return (uint8_t)(((uint32_t)raw * (uint32_t)max_index + 63U) / 127U);
}

static uint8_t ui_page_template_tone_master_fx_percent(uint8_t raw, uint8_t max_percent)
{
    return (uint8_t)(((uint32_t)raw * (uint32_t)max_percent + 63U) / 127U);
}

static void ui_page_template_tone_master_fx_format_signed(uint8_t raw,
                                                          int32_t min_value,
                                                          int32_t max_value,
                                                          const char *unit,
                                                          char *out,
                                                          uint32_t out_len)
{
    const int32_t span = max_value - min_value;
    const int32_t value = min_value + (int32_t)(((uint32_t)raw * (uint32_t)span + 63U) / 127U);
    (void)snprintf(out, out_len, "%+ld%s", (long)value, (unit != NULL) ? unit : "");
}

static void ui_page_template_tone_master_fx_format_percent(uint8_t raw,
                                                           uint8_t max_percent,
                                                           char *out,
                                                           uint32_t out_len)
{
    (void)snprintf(out,
                   out_len,
                   "%u%%",
                   (unsigned int)ui_page_template_tone_master_fx_percent(raw, max_percent));
}

static void ui_page_template_tone_master_fx_format_choice(uint8_t raw,
                                                          const char *const *labels,
                                                          uint8_t label_count,
                                                          char *out,
                                                          uint32_t out_len)
{
    const uint8_t idx = ui_page_template_tone_master_fx_index(raw, (uint8_t)(label_count - 1U));
    (void)snprintf(out, out_len, "%s", labels[idx]);
}

static void ui_page_template_tone_master_fx_format_rate_div(uint8_t raw,
                                                            char *out,
                                                            uint32_t out_len)
{
    static const char *const k_labels[] = {
        "4/1", "2/1", "1/1", "1/2", "1/3",
        "1/4", "1/6", "1/8", "1/12", "1/16"
    };
    ui_page_template_tone_master_fx_format_choice(raw, k_labels, (uint8_t)(sizeof(k_labels) / sizeof(k_labels[0])), out, out_len);
}

static void ui_page_template_tone_master_fx_format_time_div(uint8_t raw,
                                                            char *out,
                                                            uint32_t out_len)
{
    static const char *const k_labels[] = { "1/8", "1/4", "1/3", "1/2", "3/4", "1/1", "3/2", "2/1" };
    ui_page_template_tone_master_fx_format_choice(raw, k_labels, (uint8_t)(sizeof(k_labels) / sizeof(k_labels[0])), out, out_len);
}

static void ui_page_template_tone_master_fx_format_stutter_size(uint8_t raw,
                                                                char *out,
                                                                uint32_t out_len)
{
    static const char *const k_labels[] = { "1/32", "1/16", "1/8", "1/6", "1/4", "1/3", "1/2", "3/4" };
    ui_page_template_tone_master_fx_format_choice(raw, k_labels, (uint8_t)(sizeof(k_labels) / sizeof(k_labels[0])), out, out_len);
}

static void ui_page_template_tone_master_fx_format_value(uint8_t fx_type,
                                                         uint8_t slot,
                                                         uint8_t raw,
                                                         char *out,
                                                         uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    if ((fx_type == FX_MASTER_MACRO_OFF) || (fx_type > FX_MASTER_MACRO_FREEZE))
    {
        (void)snprintf(out, out_len, "---");
        return;
    }

    switch (fx_type)
    {
        case FX_MASTER_MACRO_DRIVE:
            if (slot == 2U)
            {
                ui_page_template_tone_master_fx_format_signed(raw, -64, 63, "", out, out_len);
            }
            else
            {
                static const char *const k_shape[] = { "SOFT", "CLIP", "HARD", "FOLD" };
                ui_page_template_tone_master_fx_format_choice(raw, k_shape, (uint8_t)(sizeof(k_shape) / sizeof(k_shape[0])), out, out_len);
            }
            break;

        case FX_MASTER_MACRO_CRUSH:
            if (slot == 2U)
            {
                const uint8_t bits = (uint8_t)(16U - ui_page_template_tone_master_fx_index(raw, 12U));
                (void)snprintf(out, out_len, "%ubit", (unsigned int)((bits < 4U) ? 4U : bits));
            }
            else
            {
                const uint32_t hold = 1U + (((uint32_t)raw * (uint32_t)raw * 95U + 8064U) / 16129U);
                (void)snprintf(out, out_len, "%ux", (unsigned int)hold);
            }
            break;

        case FX_MASTER_MACRO_RING:
            if (slot == 2U)
            {
                const uint32_t freq = 1U + (((uint32_t)raw * (uint32_t)raw * 1499U + 8064U) / 16129U);
                if (freq >= 1000U)
                {
                    (void)snprintf(out, out_len, "%lu.%luk", (unsigned long)(freq / 1000U), (unsigned long)((freq % 1000U) / 100U));
                }
                else
                {
                    (void)snprintf(out, out_len, "%luHz", (unsigned long)freq);
                }
            }
            else
            {
                static const char *const k_color[] = { "SIN", "TRI", "SQR", "DIRT" };
                ui_page_template_tone_master_fx_format_choice(raw, k_color, (uint8_t)(sizeof(k_color) / sizeof(k_color[0])), out, out_len);
            }
            break;

        case FX_MASTER_MACRO_CHOP:
            if (slot == 2U)
            {
                ui_page_template_tone_master_fx_format_rate_div(raw, out, out_len);
            }
            else
            {
                static const char *const k_shape[] = { "SOFT", "GATE", "HARD" };
                ui_page_template_tone_master_fx_format_choice(raw, k_shape, (uint8_t)(sizeof(k_shape) / sizeof(k_shape[0])), out, out_len);
            }
            break;

        case FX_MASTER_MACRO_PUMP:
            if (slot == 2U)
            {
                ui_page_template_tone_master_fx_format_rate_div(raw, out, out_len);
            }
            else
            {
                ui_page_template_tone_master_fx_format_percent(raw, 100U, out, out_len);
            }
            break;

        case FX_MASTER_MACRO_COMB:
            if (slot == 2U)
            {
                const uint32_t freq = 90U + (((uint32_t)raw * 4000U + 63U) / 127U);
                (void)snprintf(out, out_len, "%luHz", (unsigned long)freq);
            }
            else
            {
                ui_page_template_tone_master_fx_format_percent(raw, 80U, out, out_len);
            }
            break;

        case FX_MASTER_MACRO_WOBBLE:
            if (slot == 2U)
            {
                const uint32_t hz_x10 = 1U + (((uint32_t)raw * (uint32_t)raw * 75U + 8064U) / 16129U);
                (void)snprintf(out, out_len, "%lu.%luHz", (unsigned long)(hz_x10 / 10U), (unsigned long)(hz_x10 % 10U));
            }
            else
            {
                ui_page_template_tone_master_fx_format_percent(raw, 100U, out, out_len);
            }
            break;

        case FX_MASTER_MACRO_ECHO:
            if (slot == 2U)
            {
                ui_page_template_tone_master_fx_format_time_div(raw, out, out_len);
            }
            else
            {
                ui_page_template_tone_master_fx_format_percent(raw, 74U, out, out_len);
            }
            break;

        case FX_MASTER_MACRO_FREEZE:
            if (slot == 2U)
            {
                ui_page_template_tone_master_fx_format_time_div(raw, out, out_len);
            }
            else
            {
                static const char *const k_hold[] = { "SHORT", "MID", "LONG", "INF" };
                ui_page_template_tone_master_fx_format_choice(raw, k_hold, (uint8_t)(sizeof(k_hold) / sizeof(k_hold[0])), out, out_len);
            }
            break;

        case FX_MASTER_MACRO_STUTTER:
            if (slot == 2U)
            {
                ui_page_template_tone_master_fx_format_stutter_size(raw, out, out_len);
            }
            else
            {
                static const char *const k_rate[] = { "0.5x", "0.75x", "1x", "1.5x", "2x", "3x", "4x", "6x" };
                ui_page_template_tone_master_fx_format_choice(raw, k_rate, (uint8_t)(sizeof(k_rate) / sizeof(k_rate[0])), out, out_len);
            }
            break;

        case FX_MASTER_MACRO_TALK:
            if (slot == 2U)
            {
                static const char *const k_vowels[] = { "A", "E", "I", "O", "U" };
                ui_page_template_tone_master_fx_format_choice(raw, k_vowels, (uint8_t)(sizeof(k_vowels) / sizeof(k_vowels[0])), out, out_len);
            }
            else
            {
                ui_page_template_tone_master_fx_format_percent(raw, 100U, out, out_len);
            }
            break;

        case FX_MASTER_MACRO_PITCH:
            if (slot == 2U)
            {
                ui_page_template_tone_master_fx_format_signed(raw, -12, 12, "st", out, out_len);
            }
            else
            {
                ui_page_template_tone_master_fx_format_signed(raw, -100, 100, "ct", out, out_len);
            }
            break;

        default:
            (void)snprintf(out, out_len, "%u", (unsigned int)raw);
            break;
    }
}

static uint8_t ui_page_template_tone_param_text(uint8_t slot,
                                                param_id_t id,
                                                float value,
                                                char *out_name,
                                                uint32_t out_name_len,
                                                char *out_value,
                                                uint32_t out_value_len)
{
    const uint8_t active_track = ui_get_active_track();
    if ((ui_get_track_family(active_track) == UI_TRACK_FAMILY_DRUM)
            && (ui_get_track_type(active_track) == UI_TRACK_TYPE_DRUM_BD_ANALOG))
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

    if ((ui_get_track_family(active_track) != UI_TRACK_FAMILY_MASTER)
            || (ui_get_track_type(active_track) != UI_TRACK_TYPE_MASTER_FX)
            || (slot < 1U)
            || (slot > 3U)
            || (g_ui_template_tone_state.active_subpage >= 4U))
    {
        return 0U;
    }

    const param_id_t type_param = (param_id_t)(PARAM_MASTER_FX1_TYPE + (g_ui_template_tone_state.active_subpage * 4U));
    const param_id_t value_param = (param_id_t)(type_param + slot);
    if (id != value_param)
    {
        return 0U;
    }

    float fx_type_value = 0.0f;
    const char *label_a = "A";
    const char *label_b = "B";

    (void)param_registry_get_track_value(type_param, active_track, &fx_type_value);
    ui_page_template_tone_master_fx_macro_labels((uint8_t)(fx_type_value + 0.5f), &label_a, &label_b);

    if ((out_name != NULL) && (out_name_len > 0U))
    {
        if (slot == 1U)
        {
            (void)snprintf(out_name, out_name_len, "LVL");
        }
        else
        {
            (void)snprintf(out_name, out_name_len, "%s", (slot == 2U) ? label_a : label_b);
        }
    }

    if ((out_value != NULL) && (out_value_len > 0U))
    {
        const uint8_t raw_value = ui_page_template_tone_master_fx_u7(value);
        if (slot == 1U)
        {
            ui_page_template_tone_master_fx_format_percent(raw_value, 100U, out_value, out_value_len);
        }
        else
        {
            ui_page_template_tone_master_fx_format_value((uint8_t)(fx_type_value + 0.5f),
                                                         slot,
                                                         raw_value,
                                                         out_value,
                                                         out_value_len);
        }
    }

    return 1U;
}

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
    const uint8_t active_track = ui_get_active_track();
    if ((ui_get_track_family(active_track) == UI_TRACK_FAMILY_DRUM)
            && (ui_get_track_type(active_track) == UI_TRACK_TYPE_DRUM_BD_ANALOG))
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
    for (uint8_t family = 0U; family < (uint8_t)UI_TRACK_FAMILY_COUNT; ++family)
    {
        const ui_track_family_t track_family = (ui_track_family_t)family;
        for (uint8_t type = 0U; type < (uint8_t)UI_TRACK_TYPE_COUNT; ++type)
        {
            const ui_track_type_t track_type = (ui_track_type_t)type;
            if (!ui_track_type_is_valid_for_family(track_family, track_type))
            {
                continue;
            }

            const ui_template_family_t *family_template = NULL;
            if ((track_family == UI_TRACK_FAMILY_MASTER) && (track_type == UI_TRACK_TYPE_BUFFER))
            {
                family_template = &g_ui_template_tone_family_buffer;
            }
            else if ((track_family == UI_TRACK_FAMILY_MASTER) && (track_type == UI_TRACK_TYPE_MASTER_FX))
            {
                family_template = &g_ui_template_tone_family_master_fx;
            }
            else if ((ui_track_family_is_engine(track_family) != 0) && (track_type == UI_TRACK_TYPE_BRAIDS))
            {
                family_template = &g_ui_template_tone_family_braids;
            }
            else if ((ui_track_family_is_engine(track_family) != 0) && (track_type == UI_TRACK_TYPE_OPAL))
            {
                family_template = &g_ui_template_tone_family_opal;
            }
            else if ((ui_track_family_is_engine(track_family) != 0) && (track_type == UI_TRACK_TYPE_SAMPLER))
            {
                family_template = &g_ui_template_tone_family_sampler;
            }
            else if ((track_family == UI_TRACK_FAMILY_SAMPLER) && (track_type == UI_TRACK_TYPE_SLICER))
            {
                family_template = &g_ui_template_tone_family_slicer;
            }
            else if ((track_family == UI_TRACK_FAMILY_SAMPLER) && (track_type == UI_TRACK_TYPE_CLIP))
            {
                family_template = &g_ui_template_tone_family_clip;
            }
            else if ((track_family == UI_TRACK_FAMILY_MIDI) && (track_type == UI_TRACK_TYPE_MIDI))
            {
                family_template = &g_ui_template_tone_family_midi;
            }
            else if ((ui_track_family_is_input(track_family) != 0U) && (track_type == UI_TRACK_TYPE_HYBRID))
            {
                family_template = &g_ui_template_tone_family_hybrid;
            }
            else if (track_family == UI_TRACK_FAMILY_DRUM)
            {
                family_template = &g_ui_template_tone_family_drum;
            }

            ui_template_family_register(UI_TEMPLATE_FAMILY_TONE, track_family, track_type, family_template);
        }
    }
}

static void ui_page_template_tone_enter(void)
{
    ui_page_template_tone_sync_drum_family();
    ui_template_page_enter();
}

static void ui_page_template_tone_handle_event(const ui_event_t *ev)
{
    ui_page_template_tone_sync_drum_family();
    ui_template_page_handle_event(ev);
    ui_page_template_tone_sync_drum_family();
    ui_template_page_select_subpage(&g_ui_template_tone_state, g_ui_template_tone_state.active_subpage);
}

static void ui_page_template_tone_tick(void)
{
    ui_page_template_tone_sync_drum_family();
    ui_template_page_select_subpage(&g_ui_template_tone_state, g_ui_template_tone_state.active_subpage);
    ui_template_page_tick();
}

static void ui_page_template_tone_sync_active_context(void)
{
    ui_page_template_tone_sync_drum_family();
    ui_template_page_select_subpage(&g_ui_template_tone_state, g_ui_template_tone_state.active_subpage);
    ui_template_page_sync_active_track_context();
}

static void ui_page_template_tone_render(void)
{
    ui_page_template_tone_sync_drum_family();
    ui_template_page_render();
}

const ui_page_t g_ui_page_template_tone = {
    .enter = ui_page_template_tone_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_page_template_tone_handle_event,
    .tick = ui_page_template_tone_tick,
    .sync_active_context = ui_page_template_tone_sync_active_context,
    .render = ui_page_template_tone_render,
    .context = &g_ui_template_tone_state,
};
