#include <stddef.h>
#include <stdio.h>
#include "pages/ui_page_template_tone.h"

#include "Audio/fx_master_macro.h"
#include "Core/brick6_sampler_runtime.h"
#include "Param/param_registry.h"
#include "Param/param_wave_labels.h"
#include "Sampler/multi_sample_pool.h"
#include "ui_core.h"
#include "ui_renderer_template.h"
#include "ui_template_page.h"

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
    .nav_labels = { "PLAY", "LOOP", "-", "-" },
    .subpages = {
        { .title = "PLAY", .param_bank = { .params = { PARAM_SAMPLER_SAMPLE, PARAM_SAMPLER_MODE, PARAM_SAMPLER_START, PARAM_SAMPLER_END } } },
        { .title = "LOOP", .param_bank = { .params = { PARAM_SAMPLER_GAIN, PARAM_SAMPLER_TUNE, PARAM_SAMPLER_LOOP_START, PARAM_SAMPLER_SLICE_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};


static const ui_template_family_t g_ui_template_tone_family_clip = {
    .family_title = "TONE",
    .nav_labels = { "PLAY", "STRM", "SYNC", "STR" },
    .subpages = {
        { .title = "PLAY", .param_bank = { .params = { PARAM_SAMPLER_SAMPLE, PARAM_SAMPLER_GAIN, PARAM_SAMPLER_CLIP_SOURCE_BPM, PARAM_COUNT } } },
        { .title = "STRM", .param_bank = { .params = { PARAM_SAMPLER_CLIP_PLAY_MODE, PARAM_SAMPLER_CLIP_LOOP, PARAM_SAMPLER_CLIP_STRETCH_MODE, PARAM_SAMPLER_CLIP_PITCH } } },
        { .title = "SYNC", .param_bank = { .params = { PARAM_SAMPLER_CLIP_SYNC_LENGTH, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "STR", .param_bank = { .params = { PARAM_SAMPLER_CLIP_GRAIN, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_looper = {
    .family_title = "TONE",
    .nav_labels = { "LOOP", "STR", "-", "-" },
    .subpages = {
        { .title = "LOOP", .param_bank = { .params = { PARAM_LOOPER_ARM, PARAM_LOOPER_LEN, PARAM_LOOPER_PLAY, PARAM_LOOPER_XFADE } } },
        { .title = "STR", .param_bank = { .params = { PARAM_LOOPER_STRETCH, PARAM_LOOPER_PITCH, PARAM_LOOPER_GRAIN, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_multi = {
    .family_title = "TONE",
    .nav_labels = { "INST", "-", "-", "-" },
    .subpages = {
        { .title = "INST", .param_bank = { .params = { PARAM_SAMPLER_SAMPLE, PARAM_SAMPLER_GAIN, PARAM_SAMPLER_MULTI_LOOP, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_wave = {
    .family_title = "TONE",
    .nav_labels = { "EDIT", "TONE", "-", "-" },
    .subpages = {
        { .title = "EDIT", .param_bank = { .params = { PARAM_WAVE_EDIT, PARAM_WAVE_FINE, PARAM_WAVE_COARSE, PARAM_WAVE_FM } } },
        { .title = "TONE", .param_bank = { .params = { PARAM_WAVE_TIMBRE, PARAM_WAVE_MODULATION, PARAM_WAVE_COLOR, PARAM_WAVE_PHASE_RESET } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

typedef param_wave_label_value_kind_t ui_wave_value_kind_t;
typedef param_wave_param_label_t ui_wave_param_label_t;

#define UI_WAVE_VALUE_PERCENT          PARAM_WAVE_LABEL_VALUE_PERCENT
#define UI_WAVE_VALUE_BIPOLAR_PERCENT  PARAM_WAVE_LABEL_VALUE_BIPOLAR_PERCENT
#define UI_WAVE_VALUE_INTERVAL         PARAM_WAVE_LABEL_VALUE_INTERVAL
#define UI_WAVE_VALUE_STEPPED          PARAM_WAVE_LABEL_VALUE_STEPPED
#define UI_WAVE_VALUE_ENUM             PARAM_WAVE_LABEL_VALUE_ENUM
#define UI_WAVE_VALUE_MORPH            PARAM_WAVE_LABEL_VALUE_MORPH
#define UI_WAVE_VALUE_RATE             PARAM_WAVE_LABEL_VALUE_RATE
#define UI_WAVE_VALUE_NONE             PARAM_WAVE_LABEL_VALUE_NONE

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
static uiw_widget_type_t ui_page_template_tone_pick_widget(uint8_t slot,
                                                           param_id_t id,
                                                           const char *value_label,
                                                           uiw_widget_type_t suggested_widget);

static ui_template_page_state_t g_ui_template_tone_state = {
    .family = 0,
    .family_resolver = ui_page_template_tone_resolve_family,
    .widget_picker = ui_page_template_tone_pick_widget,
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

#define UI_WAVE_PARAM_LABEL_COUNT param_wave_label_count()

static const int16_t g_wave_triple_intervals_q7[] = {
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

static const uint16_t g_wave_fm_frequency_quantizer[] = {
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

static const ui_wave_param_label_t *ui_page_template_tone_wave_labels_for_active_track(uint8_t *out_edit_index)
{
    const uint8_t active_track = ui_get_active_track();
    uint8_t edit_index = 0U;

    if ((ui_get_track_family(active_track) != UI_TRACK_FAMILY_SYNTH)
            || (ui_get_track_type(active_track) != UI_TRACK_TYPE_WAVE)
            || (param_wave_edit_index_for_track(active_track, &edit_index) == 0U))
    {
        return NULL;
    }

    if (out_edit_index != NULL)
    {
        *out_edit_index = edit_index;
    }

    return param_wave_labels_for_edit_index(edit_index);
}

static uint16_t ui_page_template_tone_wave_u15(float value)
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

static void ui_page_template_tone_wave_format_percent(uint16_t raw,
                                                      char *out,
                                                      uint32_t out_len)
{
    ui_format_param_127_00((float)raw, 0.0f, 32767.0f, out, out_len);
}

static void ui_page_template_tone_wave_format_bipolar_percent(uint16_t raw,
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

static void ui_page_template_tone_wave_format_bipolar_float(float value,
                                                            float span,
                                                            const char *unit,
                                                            char *out,
                                                            uint32_t out_len)
{
    int32_t display = (int32_t)(((value - 0.5f) * span * 2.0f) + ((value >= 0.5f) ? 0.5f : -0.5f));
    if ((display > -1L) && (display < 1L))
    {
        display = 0L;
    }
    if (display == 0L)
    {
        (void)snprintf(out, out_len, "0%s", (unit != NULL) ? unit : "");
    }
    else
    {
        (void)snprintf(out, out_len, "%+ld%s", (long)display, (unit != NULL) ? unit : "");
    }
}

static void ui_page_template_tone_wave_format_model(float value,
                                                    char *out,
                                                    uint32_t out_len)
{
    uint8_t index = 0U;
    const char *label = NULL;

    if (value > 0.0f)
    {
        index = (uint8_t)(value + 0.5f);
    }
    (void)param_wave_edit_index_from_value(value, &index);

    if (param_registry[PARAM_WAVE_EDIT].labels != NULL)
    {
        label = param_registry[PARAM_WAVE_EDIT].labels[index];
    }
    (void)snprintf(out, out_len, "%s", (label != NULL) ? label : "---");
}

static void ui_page_template_tone_wave_format_q7_interval(int32_t q7,
                                                          char *out,
                                                          uint32_t out_len)
{
    const int32_t abs_q7 = (q7 < 0) ? -q7 : q7;
    if (abs_q7 < 64L)
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

static int16_t ui_page_template_tone_wave_triple_interval_q7(uint16_t raw)
{
    uint8_t index_a = (uint8_t)(raw >> 9);
    uint8_t index_b = (uint8_t)(((raw >> 8) + 1U) >> 1);
    uint16_t xfade = (uint16_t)(raw << 8);
    const uint8_t last = (uint8_t)((sizeof(g_wave_triple_intervals_q7) / sizeof(g_wave_triple_intervals_q7[0])) - 1U);

    if (index_a > last)
    {
        index_a = last;
    }
    if (index_b > last)
    {
        index_b = last;
    }

    return (int16_t)(g_wave_triple_intervals_q7[index_a]
                    + (((int32_t)(g_wave_triple_intervals_q7[index_b] - g_wave_triple_intervals_q7[index_a])
                        * (int32_t)xfade) >> 16));
}

static void ui_page_template_tone_wave_format_interval(uint8_t edit_index,
                                                       param_id_t id,
                                                       uint16_t raw,
                                                       char *out,
                                                       uint32_t out_len)
{
    int32_t q7 = 0L;

    if ((edit_index == 4U) && (id == PARAM_WAVE_COLOR))
    {
        q7 = (int32_t)(raw >> 8);
    }
    else if (((edit_index == 7U) || (edit_index == 8U)) && (id == PARAM_WAVE_TIMBRE))
    {
        q7 = (int32_t)(raw >> 2);
    }
    else if ((edit_index >= 9U) && (edit_index <= 12U))
    {
        q7 = (int32_t)ui_page_template_tone_wave_triple_interval_q7(raw);
    }
    else if ((edit_index == 13U) && ((id == PARAM_WAVE_TIMBRE) || (id == PARAM_WAVE_COLOR)))
    {
        q7 = ((int32_t)raw - 16384L) >> 2;
    }
    else if ((edit_index == 33U) && (id == PARAM_WAVE_COLOR))
    {
        q7 = ((int32_t)raw - 16384L) >> 1;
    }
    else
    {
        ui_page_template_tone_wave_format_percent(raw, out, out_len);
        return;
    }

    ui_page_template_tone_wave_format_q7_interval(q7, out, out_len);
}

static void ui_page_template_tone_wave_format_vowel(uint16_t raw,
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

static void ui_page_template_tone_wave_format_noise_mix(uint16_t raw,
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

static void ui_page_template_tone_wave_format_morph(uint8_t edit_index,
                                                    param_id_t id,
                                                    uint16_t raw,
                                                    char *out,
                                                    uint32_t out_len)
{
    if ((edit_index == 17U) && (id == PARAM_WAVE_TIMBRE))
    {
        ui_page_template_tone_wave_format_vowel(raw, out, out_len);
    }
    else if ((edit_index == 32U) && (id == PARAM_WAVE_COLOR))
    {
        ui_page_template_tone_wave_format_noise_mix(raw, out, out_len);
    }
    else
    {
        ui_page_template_tone_wave_format_percent(raw, out, out_len);
    }
}

static void ui_page_template_tone_wave_format_stepped(uint8_t edit_index,
                                                      param_id_t id,
                                                      uint16_t raw,
                                                      char *out,
                                                      uint32_t out_len)
{
    if ((edit_index == 15U) && (id == PARAM_WAVE_COLOR))
    {
        (void)snprintf(out, out_len, "M%02X", (unsigned int)(raw >> 8));
    }
    else if (((edit_index == 20U) || (edit_index == 21U) || (edit_index == 22U)) && (id == PARAM_WAVE_COLOR))
    {
        uint8_t index = (uint8_t)(raw >> 8);
        if (index >= (uint8_t)(sizeof(g_wave_fm_frequency_quantizer) / sizeof(g_wave_fm_frequency_quantizer[0])))
        {
            index = (uint8_t)((sizeof(g_wave_fm_frequency_quantizer) / sizeof(g_wave_fm_frequency_quantizer[0])) - 1U);
        }
        ui_page_template_tone_wave_format_q7_interval(((int32_t)g_wave_fm_frequency_quantizer[index] - 16384L) >> 1,
                                                      out,
                                                      out_len);
    }
    else if ((edit_index == 30U) && (id == PARAM_WAVE_COLOR))
    {
        static const char *const k_labels[] = { "SMTH", "XFADE", "ROUGH", "LOFI" };
        uint8_t index = (uint8_t)(raw >> 13);
        if (index > 3U)
        {
            index = 3U;
        }
        (void)snprintf(out, out_len, "%s", k_labels[index]);
    }
    else if ((edit_index == 34U) && (id == PARAM_WAVE_COLOR))
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

static void ui_page_template_tone_wave_format_enum(uint8_t edit_index,
                                                   param_id_t id,
                                                   uint16_t raw,
                                                   char *out,
                                                   uint32_t out_len)
{
    if ((edit_index == 28U) && (id == PARAM_WAVE_COLOR))
    {
        uint8_t bank = (uint8_t)(((uint32_t)raw * 20U) >> 15);
        if (bank > 19U)
        {
            bank = 19U;
        }
        (void)snprintf(out, out_len, "%u/20", (unsigned int)(bank + 1U));
    }
    else if ((edit_index == 31U) && (id == PARAM_WAVE_COLOR))
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

static void ui_page_template_tone_wave_format_value(uint8_t edit_index,
                                                    param_id_t id,
                                                    ui_wave_value_kind_t kind,
                                                    float value,
                                                    char *out,
                                                    uint32_t out_len)
{
    const uint16_t raw = ui_page_template_tone_wave_u15(value);

    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    switch (kind)
    {
        case UI_WAVE_VALUE_NONE:
            (void)snprintf(out, out_len, "---");
            break;
        case UI_WAVE_VALUE_BIPOLAR_PERCENT:
            ui_page_template_tone_wave_format_bipolar_percent(raw, out, out_len);
            break;
        case UI_WAVE_VALUE_INTERVAL:
            ui_page_template_tone_wave_format_interval(edit_index, id, raw, out, out_len);
            break;
        case UI_WAVE_VALUE_STEPPED:
            ui_page_template_tone_wave_format_stepped(edit_index, id, raw, out, out_len);
            break;
        case UI_WAVE_VALUE_ENUM:
            ui_page_template_tone_wave_format_enum(edit_index, id, raw, out, out_len);
            break;
        case UI_WAVE_VALUE_MORPH:
            ui_page_template_tone_wave_format_morph(edit_index, id, raw, out, out_len);
            break;
        case UI_WAVE_VALUE_RATE:
        case UI_WAVE_VALUE_PERCENT:
        default:
            ui_page_template_tone_wave_format_percent(raw, out, out_len);
            break;
    }
}

static uint8_t ui_page_template_tone_wave_kind_for_param(param_id_t id,
                                                         const ui_wave_param_label_t *labels,
                                                         const char **out_name,
                                                         ui_wave_value_kind_t *out_kind)
{
    if (labels == NULL)
    {
        return 0U;
    }

    if (id == PARAM_WAVE_TIMBRE)
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

    if (id == PARAM_WAVE_COLOR)
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

static uint8_t ui_page_template_tone_wave_param_text(param_id_t id,
                                                     float value,
                                                     char *out_name,
                                                     uint32_t out_name_len,
                                                     char *out_value,
                                                     uint32_t out_value_len)
{
    uint8_t edit_index = 0U;
    const ui_wave_param_label_t *const labels = ui_page_template_tone_wave_labels_for_active_track(&edit_index);
    const char *name = NULL;
    ui_wave_value_kind_t kind = UI_WAVE_VALUE_PERCENT;

    if (labels == NULL)
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_WAVE_EDIT:
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "MODEL");
            }
            ui_page_template_tone_wave_format_model(value, out_value, out_value_len);
            return 1U;

        case PARAM_WAVE_FINE:
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "FINE");
            }
            if ((out_value != NULL) && (out_value_len > 0U))
            {
                ui_page_template_tone_wave_format_bipolar_float(value, 100.0f, "ct", out_value, out_value_len);
            }
            return 1U;

        case PARAM_WAVE_COARSE:
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "PITCH");
            }
            if ((out_value != NULL) && (out_value_len > 0U))
            {
                ui_page_template_tone_wave_format_bipolar_float(value, 24.0f, "st", out_value, out_value_len);
            }
            return 1U;

        case PARAM_WAVE_FM:
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "FM AMT");
            }
            if ((out_value != NULL) && (out_value_len > 0U))
            {
                ui_page_template_tone_wave_format_percent(ui_page_template_tone_wave_u15(value), out_value, out_value_len);
            }
            return 1U;

        case PARAM_WAVE_MODULATION:
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "A MOD");
            }
            if ((out_value != NULL) && (out_value_len > 0U))
            {
                ui_page_template_tone_wave_format_bipolar_percent(ui_page_template_tone_wave_u15(value), out_value, out_value_len);
            }
            return 1U;

        default:
            break;
    }

    if (ui_page_template_tone_wave_kind_for_param(id, labels, &name, &kind) == 0U)
    {
        return 0U;
    }

    if ((out_name != NULL) && (out_name_len > 0U))
    {
        (void)snprintf(out_name, out_name_len, "%s", (name != NULL) ? name : "-");
    }

    ui_page_template_tone_wave_format_value(edit_index, id, kind, value, out_value, out_value_len);

    return 1U;
}

static uiw_widget_type_t ui_page_template_tone_pick_widget(uint8_t slot,
                                                           param_id_t id,
                                                           const char *value_label,
                                                           uiw_widget_type_t suggested_widget)
{
    const ui_wave_param_label_t *const labels = ui_page_template_tone_wave_labels_for_active_track(NULL);
    ui_wave_value_kind_t kind = UI_WAVE_VALUE_PERCENT;

    (void)slot;
    (void)value_label;

    if (ui_page_template_tone_wave_kind_for_param(id, labels, NULL, &kind) == 0U)
    {
        if (id == PARAM_WAVE_EDIT)
        {
            return UIW_WIDGET_ENUM_TEXT;
        }
        return suggested_widget;
    }

    switch (kind)
    {
        case UI_WAVE_VALUE_NONE:
            return UIW_WIDGET_EMPTY;
        case UI_WAVE_VALUE_STEPPED:
        case UI_WAVE_VALUE_ENUM:
            return UIW_WIDGET_ENUM_TEXT;
        case UI_WAVE_VALUE_PERCENT:
        case UI_WAVE_VALUE_BIPOLAR_PERCENT:
        case UI_WAVE_VALUE_INTERVAL:
        case UI_WAVE_VALUE_MORPH:
        case UI_WAVE_VALUE_RATE:
        default:
            return UIW_WIDGET_KNOB;
    }
}

static uint8_t ui_page_template_tone_master_fx_index(uint8_t raw, uint8_t max_index)
{
    return (uint8_t)(((uint32_t)raw * (uint32_t)max_index + 63U) / 127U);
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
    (void)max_percent;
    ui_format_param_127_00((float)raw, 0.0f, 127.0f, out, out_len);
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
    if ((ui_get_track_family(active_track) == UI_TRACK_FAMILY_SAMPLER)
            && (ui_get_track_type(active_track) == UI_TRACK_TYPE_SAMPLER))
    {
        const char *name = NULL;
        switch (id)
        {
            case PARAM_SAMPLER_SAMPLE:
                name = "SAMPLE";
                break;
            case PARAM_SAMPLER_MODE:
                name = "MODE";
                break;
            case PARAM_SAMPLER_START:
                name = "START";
                break;
            case PARAM_SAMPLER_END:
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

    if ((ui_get_track_family(active_track) == UI_TRACK_FAMILY_SAMPLER)
            && (ui_get_track_type(active_track) == UI_TRACK_TYPE_MULTI))
    {
        if (id == PARAM_SAMPLER_SAMPLE)
        {
            uint16_t instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
            const multi_sample_instrument_t *instrument = NULL;

            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "INST");
            }

            (void)brick6_sampler_runtime_get_multi_instrument(active_track, &instrument_id);
            if (instrument_id < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
            {
                instrument = multi_sample_pool_get_instrument(instrument_id);
            }

            if ((out_value != NULL) && (out_value_len > 0U))
            {
                if (instrument_id == MULTI_SAMPLE_POOL_INVALID_ID)
                {
                    (void)snprintf(out_value, out_value_len, "NONE");
                }
                else if (instrument == NULL)
                {
                    (void)snprintf(out_value, out_value_len, "LOAD");
                }
                else if (instrument->state == MULTI_SAMPLE_INSTRUMENT_ERROR)
                {
                    (void)snprintf(out_value, out_value_len, "ERR");
                }
                else if (instrument->state == MULTI_SAMPLE_INSTRUMENT_READY)
                {
                    (void)snprintf(out_value,
                                   out_value_len,
                                   "%s",
                                   (instrument->name[0] != '\0') ? instrument->name : "READY");
                }
                else
                {
                    (void)snprintf(out_value, out_value_len, "LOAD");
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

    if ((ui_get_track_family(active_track) == UI_TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(active_track) == UI_TRACK_TYPE_WAVE)
            && (ui_page_template_tone_wave_param_text(id, value, out_name, out_name_len, out_value, out_value_len) != 0U))
    {
        (void)slot;
        return 1U;
    }

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
            if ((track_family == UI_TRACK_FAMILY_MASTER) && (track_type == UI_TRACK_TYPE_MASTER_FX))
            {
                family_template = &g_ui_template_tone_family_master_fx;
            }
            else if ((ui_track_family_is_engine(track_family) != 0) && (track_type == UI_TRACK_TYPE_WAVE))
            {
                family_template = &g_ui_template_tone_family_wave;
            }
            else if ((ui_track_family_is_engine(track_family) != 0) && (track_type == UI_TRACK_TYPE_SAMPLER))
            {
                family_template = &g_ui_template_tone_family_sampler;
            }

            else if ((track_family == UI_TRACK_FAMILY_SAMPLER) && (track_type == UI_TRACK_TYPE_CLIP))
            {
                family_template = &g_ui_template_tone_family_clip;
            }
            else if ((track_family == UI_TRACK_FAMILY_SAMPLER) && (track_type == UI_TRACK_TYPE_LOOPER))
            {
                family_template = &g_ui_template_tone_family_looper;
            }
            else if ((track_family == UI_TRACK_FAMILY_SAMPLER) && (track_type == UI_TRACK_TYPE_MULTI))
            {
                family_template = &g_ui_template_tone_family_multi;
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
