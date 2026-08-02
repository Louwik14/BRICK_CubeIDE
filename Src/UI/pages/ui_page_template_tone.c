#include <stddef.h>
#include <stdio.h>
#include "pages/ui_page_template_tone.h"

#include "Audio/fx_master_macro.h"
#include "Audio/md_model.h"
#include "Core/brick6_sampler_runtime.h"
#include "Core/brick6_deluge_runtime.h"
#include "Core/brick6_stack_runtime.h"
#include "Param/param_registry.h"
#include "Param/param_prism_labels.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/multi_sample_pool.h"
#include "ui_core.h"
#include "ui_renderer_template.h"
#include "ui_template_page.h"

static uint8_t g_ui_template_tone_subset = 0U;

static const ui_template_family_t g_ui_template_tone_family_macro_fx = {
    .family_title = "TONE",
    .nav_labels = { "FX1", "FX2", "FX3", "FX4" },
    .subpages = {
        { .title = "FX1", .param_bank = { .params = { PARAM_MACRO_FX1_TYPE, PARAM_MACRO_FX1_LEVEL, PARAM_MACRO_FX1_A, PARAM_MACRO_FX1_B } } },
        { .title = "FX2", .param_bank = { .params = { PARAM_MACRO_FX2_TYPE, PARAM_MACRO_FX2_LEVEL, PARAM_MACRO_FX2_A, PARAM_MACRO_FX2_B } } },
        { .title = "FX3", .param_bank = { .params = { PARAM_MACRO_FX3_TYPE, PARAM_MACRO_FX3_LEVEL, PARAM_MACRO_FX3_A, PARAM_MACRO_FX3_B } } },
        { .title = "FX4", .param_bank = { .params = { PARAM_MACRO_FX4_TYPE, PARAM_MACRO_FX4_LEVEL, PARAM_MACRO_FX4_A, PARAM_MACRO_FX4_B } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_master_reverb_mutable = {
    .family_title = "MASTER 1/3",
    .nav_labels = { "REVERB 1", "REVERB 2", "-", "-" },
    .subpages = {
        { .title = "REVERB 1", .param_bank = { .params = { PARAM_MIX_REVERB_WET, PARAM_MIX_REVERB_SIZE, PARAM_MIX_REVERB_DECAY, PARAM_MIX_REVERB_PRED } } },
        { .title = "REVERB 2", .param_bank = { .params = { PARAM_MIX_REVERB_DAMP, PARAM_MIX_REVERB_HPF, PARAM_MIX_REVERB_LPF, PARAM_MIX_REVERB_SMEAR } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_master_delay_classic = {
    .family_title = "MASTER 2/3",
    .nav_labels = { "DELAY 1", "DELAY 2", "-", "-" },
    .subpages = {
        { .title = "DELAY 1", .param_bank = { .params = { PARAM_MIX_DELAY_TYPE, PARAM_MIX_DELAY_TIME, PARAM_MIX_DELAY_PINGPONG, PARAM_MIX_DELAY_VOL } } },
        { .title = "DELAY 2", .param_bank = { .params = { PARAM_MIX_DELAY_HPF, PARAM_MIX_DELAY_LPF, PARAM_MIX_DELAY_REV, PARAM_MIX_DELAY_FEEDBACK } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_master_delay_dual = {
    .family_title = "MASTER 2/3",
    .nav_labels = { "DELAY 1", "DELAY 2", "DELAY 3", "DELAY 4" },
    .subpages = {
        { .title = "DELAY 1", .param_bank = { .params = { PARAM_MIX_DELAY_TYPE, PARAM_MIX_DELAY_TIME, PARAM_MIX_DELAY_MODE, PARAM_MIX_DELAY_VOL } } },
        { .title = "DELAY 2", .param_bank = { .params = { PARAM_MIX_DELAY_HPF, PARAM_MIX_DELAY_LPF, PARAM_MIX_DELAY_REV, PARAM_MIX_DELAY_FEEDBACK } } },
        { .title = "DELAY 3", .param_bank = { .params = { PARAM_MIX_DELAY_TIME_R, PARAM_MIX_DELAY_WIDTH, PARAM_MIX_DELAY_FBW, PARAM_MIX_DELAY_MOD } } },
        { .title = "DELAY 4", .param_bank = { .params = { PARAM_MIX_DELAY_MOD_RATE, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_master_comp_off = {
    .family_title = "MASTER 3/3",
    .nav_labels = { "COMP MAIN", "COMP ENV", "-", "-" },
    .subpages = {
        { .title = "COMP MAIN", .param_bank = { .params = { PARAM_COMP_MODEL, PARAM_BUS_COMP_THRESHOLD_DB, PARAM_BUS_COMP_RATIO, PARAM_BUS_COMP_MAKEUP_DB } } },
        { .title = "COMP ENV", .param_bank = { .params = { PARAM_BUS_COMP_ATTACK_INDEX, PARAM_BUS_COMP_RELEASE_INDEX, PARAM_BUS_COMP_DRYWET, PARAM_BUS_COMP_HPF_HZ } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_master_comp_deluge = {
    .family_title = "MASTER 3/3",
    .nav_labels = { "COMP MAIN", "COMP ENV", "COMP CHAR", "-" },
    .subpages = {
        { .title = "COMP MAIN", .param_bank = { .params = { PARAM_COMP_MODEL, PARAM_BUS_COMP_THRESHOLD_DB, PARAM_BUS_COMP_RATIO, PARAM_BUS_COMP_MAKEUP_DB } } },
        { .title = "COMP ENV", .param_bank = { .params = { PARAM_BUS_COMP_ATTACK_INDEX, PARAM_BUS_COMP_RELEASE_INDEX, PARAM_BUS_COMP_DRYWET, PARAM_BUS_COMP_HPF_HZ } } },
        { .title = "COMP CHAR", .param_bank = { .params = { PARAM_COMP_DELUGE_SAT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_master_comp_brick = {
    .family_title = "MASTER 3/3",
    .nav_labels = { "COMP MAIN", "COMP ENV", "COMP CHAR", "-" },
    .subpages = {
        { .title = "COMP MAIN", .param_bank = { .params = { PARAM_COMP_MODEL, PARAM_BUS_COMP_THRESHOLD_DB, PARAM_BUS_COMP_RATIO, PARAM_BUS_COMP_MAKEUP_DB } } },
        { .title = "COMP ENV", .param_bank = { .params = { PARAM_BUS_COMP_ATTACK_INDEX, PARAM_BUS_COMP_RELEASE_INDEX, PARAM_BUS_COMP_DRYWET, PARAM_BUS_COMP_HPF_HZ } } },
        { .title = "COMP CHAR", .param_bank = { .params = { PARAM_COMP_DETECT, PARAM_COMP_KNEE_DB, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
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

static const ui_template_family_t g_ui_template_tone_family_prism = {
    .family_title = "TONE",
    .nav_labels = { "O1V", "O1E", "O2V", "O2E" },
    .subpages = {
        { .title = "OSC1 VOICE", .param_bank = { .params = { PARAM_PRISM_TIMBRE, PARAM_PRISM_COLOR, PARAM_PRISM_MODULATION, PARAM_PRISM_EDIT } } },
        { .title = "OSC1 EDIT", .param_bank = { .params = { PARAM_PRISM_LEVEL, PARAM_PRISM_COARSE, PARAM_PRISM_FM, PARAM_PRISM_PHASE_RESET } } },
        { .title = "OSC2 VOICE", .param_bank = { .params = { PARAM_PRISM_OSC2_TIMBRE, PARAM_PRISM_OSC2_COLOR, PARAM_PRISM_OSC2_MODULATION, PARAM_PRISM_OSC2_EDIT } } },
        { .title = "OSC2 EDIT", .param_bank = { .params = { PARAM_PRISM_OSC2_LEVEL, PARAM_PRISM_OSC2_COARSE, PARAM_PRISM_OSC2_FM, PARAM_PRISM_OSC2_PHASE_RESET } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_wave = {
    .family_title = "TONE 1/2",
    .nav_labels = { "O1W", "O1V", "O2W", "O2V" },
    .subpages = {
        { .title = "OSC1 WAVE", .param_bank = { .params = { PARAM_WAVE_OSC1_TABLE, PARAM_WAVE_OSC1_POS, PARAM_WAVE_OSC1_START, PARAM_WAVE_OSC1_END } } },
        { .title = "OSC1 VOICE", .param_bank = { .params = { PARAM_WAVE_OSC1_LEVEL, PARAM_WAVE_OSC1_TUNE, PARAM_WAVE_OSC1_PHASE, PARAM_WAVE_OSC1_FLIP } } },
        { .title = "OSC2 WAVE", .param_bank = { .params = { PARAM_WAVE_OSC2_TABLE, PARAM_WAVE_OSC2_POS, PARAM_WAVE_OSC2_START, PARAM_WAVE_OSC2_END } } },
        { .title = "OSC2 VOICE", .param_bank = { .params = { PARAM_WAVE_OSC2_LEVEL, PARAM_WAVE_OSC2_TUNE, PARAM_WAVE_OSC2_PHASE, PARAM_WAVE_OSC2_FLIP } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_wave_quality = {
    .family_title = "TONE 2/2",
    .nav_labels = { "QUAL", "-", "-", "-" },
    .subpages = {
        { .title = "QUALITY", .param_bank = { .params = { PARAM_WAVE_FRAME_INTERP, PARAM_WAVE_SAMPLE_INTERP, PARAM_WAVE_POS_UPDATE, PARAM_WAVE_POS_SMOOTH } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_stack = {
    .family_title = "TONE 1/2",
    .nav_labels = { "OSC1", "OSC2", "OSC3", "LVL" },
    .subpages = {
        { .title = "OSC1", .param_bank = { .params = { PARAM_STACK_OSC1_TIMBRE, PARAM_STACK_OSC1_COLOR, PARAM_STACK_OSC1_PARAM3, PARAM_STACK_OSC1_MODEL } } },
        { .title = "OSC2", .param_bank = { .params = { PARAM_STACK_OSC2_TIMBRE, PARAM_STACK_OSC2_COLOR, PARAM_STACK_OSC2_PARAM3, PARAM_STACK_OSC2_MODEL } } },
        { .title = "OSC3", .param_bank = { .params = { PARAM_STACK_OSC3_TIMBRE, PARAM_STACK_OSC3_COLOR, PARAM_STACK_OSC3_PARAM3, PARAM_STACK_OSC3_MODEL } } },
        { .title = "LVL", .param_bank = { .params = { PARAM_STACK_OSC1_LEVEL, PARAM_STACK_OSC2_LEVEL, PARAM_STACK_OSC3_LEVEL, PARAM_STACK_NOISE_LEVEL } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_stack_global = {
    .family_title = "TONE 2/2",
    .nav_labels = { "TUNE", "PHASE", "-", "-" },
    .subpages = {
        { .title = "TUNE", .param_bank = { .params = { PARAM_STACK_OSC1_TUNE, PARAM_STACK_OSC2_TUNE, PARAM_STACK_OSC3_TUNE, PARAM_STACK_OSC_DETUNE } } },
        { .title = "PHASE", .param_bank = { .params = { PARAM_STACK_PHASE_RESET, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_deluge = {
    .family_title = "TONE",
    .nav_labels = { "MAIN", "SHAPE", "-", "-" },
    .subpages = {
        { .title = "MAIN", .param_bank = { .params = { PARAM_DELUGE_MODEL, PARAM_DELUGE_LEVEL, PARAM_DELUGE_TUNE, PARAM_DELUGE_FINE } } },
        { .title = "SHAPE", .param_bank = { .params = { PARAM_DELUGE_WIDTH, PARAM_DELUGE_PHASE, PARAM_DELUGE_RETRIG, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static brick6_deluge_model_t ui_page_template_tone_deluge_active_model(void)
{
    float model_value = 0.0f;
    if (param_registry_get_track_value(PARAM_DELUGE_MODEL, ui_get_active_track(), &model_value) == 0U)
    {
        return BRICK6_DELUGE_MODEL_SQUARE;
    }

    uint8_t model = (uint8_t)((model_value < 0.0f) ? 0.0f : model_value + 0.5f);
    if (model >= (uint8_t)BRICK6_DELUGE_MODEL_COUNT)
    {
        model = (uint8_t)BRICK6_DELUGE_MODEL_SQUARE;
    }
    return (brick6_deluge_model_t)model;
}

typedef param_prism_label_value_kind_t ui_prism_value_kind_t;
typedef param_prism_param_label_t ui_prism_param_label_t;

#define UI_PRISM_VALUE_PERCENT          PARAM_PRISM_LABEL_VALUE_PERCENT
#define UI_PRISM_VALUE_BIPOLAR_PERCENT  PARAM_PRISM_LABEL_VALUE_BIPOLAR_PERCENT
#define UI_PRISM_VALUE_INTERVAL         PARAM_PRISM_LABEL_VALUE_INTERVAL
#define UI_PRISM_VALUE_STEPPED          PARAM_PRISM_LABEL_VALUE_STEPPED
#define UI_PRISM_VALUE_ENUM             PARAM_PRISM_LABEL_VALUE_ENUM
#define UI_PRISM_VALUE_MORPH            PARAM_PRISM_LABEL_VALUE_MORPH
#define UI_PRISM_VALUE_RATE             PARAM_PRISM_LABEL_VALUE_RATE
#define UI_PRISM_VALUE_NONE             PARAM_PRISM_LABEL_VALUE_NONE

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

const ui_template_family_t *ui_page_template_tone_resolve_for_track(uint8_t track, uint8_t scope_index)
{
    const uint8_t subset = (scope_index == UI_TEMPLATE_EFFECTIVE_SCOPE_CURRENT)
            ? g_ui_template_tone_subset : scope_index;
    if (track_topology_is_role(track, TRACK_TOPOLOGY_ROLE_MASTER) != 0U)
    {
        if (subset == 0U)
        {
            return &g_ui_template_tone_family_master_reverb_mutable;
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
    if (track_topology_is_role(track, TRACK_TOPOLOGY_ROLE_FX) != 0U)
    {
        return &g_ui_template_tone_family_macro_fx;
    }
    if ((ui_get_track_family(track) == UI_TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(track) == UI_TRACK_TYPE_DELUGE))
    {
        return &g_ui_template_tone_family_deluge;
    }
    if ((ui_get_track_family(track) == UI_TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(track) == UI_TRACK_TYPE_STACK)
            && (g_ui_template_tone_subset != 0U))
    {
        return &g_ui_template_tone_family_stack_global;
    }
    if ((ui_get_track_family(track) == UI_TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(track) == UI_TRACK_TYPE_WAVE)
            && (g_ui_template_tone_subset != 0U))
    {
        return &g_ui_template_tone_family_wave_quality;
    }
    const ui_track_config_t config = ui_get_track_config(track);
    return ui_template_family_resolve(UI_TEMPLATE_FAMILY_TONE, track, config.family, config.type);
}

static const ui_template_family_t *ui_page_template_tone_resolve_family(void)
{
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
static uint8_t ui_page_template_tone_macro_fx_type_for_param(param_id_t id, uint8_t *out_fx_type, uint8_t *out_macro);

static ui_template_page_state_t g_ui_template_tone_state = {
    .family = 0,
    .family_resolver = ui_page_template_tone_resolve_family,
    .widget_picker = ui_page_template_tone_pick_widget,
    .custom_widget_picker = ui_page_template_tone_pick_custom_widget,
    .param_text = ui_page_template_tone_param_text,
    .active_subpage = 0U,
    .has_visited = 0U,
};

static void ui_page_template_tone_macro_fx_labels(uint8_t fx_type,
                                                         const char **out_a,
                                                         const char **out_b)
{
    static const char *const k_labels[][2] = {
        { "---", "---" },
        { "DRIVE", "TONE" },
        { "BITS", "RATE" },
        { "RATE", "REL" },
        { "RATE", "SHAPE" },
        { "RATE", "DEPTH" },
        { "TUNE", "FB" },
        { "FREQ", "COLOR" },
        { "SIZE", "RATE" },
        { "TIME", "HOLD" },
        { "AMT", "FOCUS" },
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

static uint8_t ui_page_template_tone_macro_fx_u7(float value)
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

static const ui_prism_param_label_t *ui_page_template_tone_prism_labels_for_param(param_id_t id, uint8_t *out_edit_index)
{
    const uint8_t active_track = ui_get_active_track();
    if ((ui_get_track_family(active_track) != UI_TRACK_FAMILY_SYNTH)
            || (ui_get_track_type(active_track) != UI_TRACK_TYPE_PRISM))
    {
        return NULL;
    }

    const param_id_t edit_param = ((id == PARAM_PRISM_OSC2_TIMBRE)
                                   || (id == PARAM_PRISM_OSC2_COLOR)
                                   || (id == PARAM_PRISM_OSC2_EDIT)
                                   || (id == PARAM_PRISM_OSC2_FINE)
                                   || (id == PARAM_PRISM_OSC2_COARSE)
                                   || (id == PARAM_PRISM_OSC2_FM)
                                   || (id == PARAM_PRISM_OSC2_MODULATION)
                                   || (id == PARAM_PRISM_OSC2_LEVEL)
                                   || (id == PARAM_PRISM_OSC2_PHASE_RESET))
        ? PARAM_PRISM_OSC2_EDIT
        : PARAM_PRISM_EDIT;
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

static void ui_page_template_tone_prism_format_bipolar_float(float value,
                                                            float span,
                                                            const char *unit,
                                                            char *out,
                                                            uint32_t out_len)
{
    int32_t cents = (int32_t)((((value - 0.5f) * span * 2.0f) * 100.0f) + ((value >= 0.5f) ? 0.5f : -0.5f));
    if ((cents > -1L) && (cents < 1L))
    {
        cents = 0L;
    }
    if (cents == 0L)
    {
        (void)snprintf(out, out_len, "0%s", (unit != NULL) ? unit : "");
    }
    else if ((cents % 100L) == 0L)
    {
        (void)snprintf(out, out_len, "%+ld%s", (long)(cents / 100L), (unit != NULL) ? unit : "");
    }
    else
    {
        const char *sign = "+";
        if (cents < 0L)
        {
            sign = "-";
            cents = -cents;
        }
        (void)snprintf(out, out_len, "%s%ld.%02ld%s", sign, (long)(cents / 100L), (long)(cents % 100L), (unit != NULL) ? unit : "");
    }
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

    if (param_registry[PARAM_PRISM_EDIT].labels != NULL)
    {
        label = param_registry[PARAM_PRISM_EDIT].labels[index];
    }
    (void)snprintf(out, out_len, "%s", (label != NULL) ? label : "---");
}

static void ui_page_template_tone_prism_format_q7_interval(int32_t q7,
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

    if ((edit_index == 4U) && (id == PARAM_PRISM_COLOR))
    {
        q7 = (int32_t)(raw >> 8);
    }
    else if (((edit_index == 7U) || (edit_index == 8U)) && (id == PARAM_PRISM_TIMBRE))
    {
        q7 = (int32_t)(raw >> 2);
    }
    else if ((edit_index >= 9U) && (edit_index <= 12U))
    {
        q7 = (int32_t)ui_page_template_tone_prism_triple_interval_q7(raw);
    }
    else if ((edit_index == 13U) && ((id == PARAM_PRISM_TIMBRE) || (id == PARAM_PRISM_COLOR)))
    {
        q7 = ((int32_t)raw - 16384L) >> 2;
    }
    else if ((edit_index == 33U) && (id == PARAM_PRISM_COLOR))
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
    if ((edit_index == 17U) && (id == PARAM_PRISM_TIMBRE))
    {
        ui_page_template_tone_prism_format_vowel(raw, out, out_len);
    }
    else if ((edit_index == 32U) && (id == PARAM_PRISM_COLOR))
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
    if ((edit_index == 15U) && (id == PARAM_PRISM_COLOR))
    {
        (void)snprintf(out, out_len, "M%02X", (unsigned int)(raw >> 8));
    }
    else if (((edit_index == 20U) || (edit_index == 21U) || (edit_index == 22U)) && (id == PARAM_PRISM_COLOR))
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
    else if ((edit_index == 30U) && (id == PARAM_PRISM_COLOR))
    {
        static const char *const k_labels[] = { "SMTH", "XFADE", "ROUGH", "LOFI" };
        uint8_t index = (uint8_t)(raw >> 13);
        if (index > 3U)
        {
            index = 3U;
        }
        (void)snprintf(out, out_len, "%s", k_labels[index]);
    }
    else if ((edit_index == 34U) && (id == PARAM_PRISM_COLOR))
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
    if ((edit_index == 28U) && (id == PARAM_PRISM_COLOR))
    {
        uint8_t bank = (uint8_t)(((uint32_t)raw * 20U) >> 15);
        if (bank > 19U)
        {
            bank = 19U;
        }
        (void)snprintf(out, out_len, "%u/20", (unsigned int)(bank + 1U));
    }
    else if ((edit_index == 31U) && (id == PARAM_PRISM_COLOR))
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

    if ((id == PARAM_PRISM_TIMBRE) || (id == PARAM_PRISM_OSC2_TIMBRE))
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

    if ((id == PARAM_PRISM_COLOR) || (id == PARAM_PRISM_OSC2_COLOR))
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
        case PARAM_PRISM_EDIT:
        case PARAM_PRISM_OSC2_EDIT:
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "MODEL");
            }
            ui_page_template_tone_prism_format_model(value, out_value, out_value_len);
            return 1U;

        case PARAM_PRISM_FINE:
        case PARAM_PRISM_OSC2_FINE:
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "FINE");
            }
            if ((out_value != NULL) && (out_value_len > 0U))
            {
                ui_page_template_tone_prism_format_bipolar_float(value, 100.0f, "ct", out_value, out_value_len);
            }
            return 1U;

        case PARAM_PRISM_COARSE:
        case PARAM_PRISM_OSC2_COARSE:
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "TUNE");
            }
            if ((out_value != NULL) && (out_value_len > 0U))
            {
                ui_page_template_tone_prism_format_bipolar_float(value, 24.0f, "st", out_value, out_value_len);
            }
            return 1U;

        case PARAM_PRISM_FM:
        case PARAM_PRISM_OSC2_FM:
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "FM AMT");
            }
            if ((out_value != NULL) && (out_value_len > 0U))
            {
                ui_page_template_tone_prism_format_percent(ui_page_template_tone_prism_u15(value), out_value, out_value_len);
            }
            return 1U;

        case PARAM_PRISM_MODULATION:
        case PARAM_PRISM_OSC2_MODULATION:
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "A MOD");
            }
            if ((out_value != NULL) && (out_value_len > 0U))
            {
                ui_page_template_tone_prism_format_bipolar_percent(ui_page_template_tone_prism_u15(value), out_value, out_value_len);
            }
            return 1U;

        case PARAM_PRISM_LEVEL:
        case PARAM_PRISM_OSC2_LEVEL:
            if ((out_name != NULL) && (out_name_len > 0U))
            {
                (void)snprintf(out_name, out_name_len, "LVL");
            }
            if ((out_value != NULL) && (out_value_len > 0U))
            {
                ui_page_template_tone_prism_format_percent(ui_page_template_tone_prism_u15(value), out_value, out_value_len);
            }
            return 1U;

        case PARAM_PRISM_PHASE_RESET:
        case PARAM_PRISM_OSC2_PHASE_RESET:
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
    if ((id >= PARAM_STACK_OSC1_MODEL) && (id <= PARAM_STACK_OSC3_PARAM3))
    {
        const uint8_t rel = (uint8_t)(id - PARAM_STACK_OSC1_MODEL);
        *out_slot = (uint8_t)(rel / 5U);
        *out_param = (uint8_t)((rel % 5U) + 1U);
        return (*out_slot < BRICK6_STACK_SLOT_COUNT) ? 1U : 0U;
    }

    return 0U;
}

static const char *ui_page_template_tone_stack_timbre_label(uint8_t model)
{
    switch ((brick6_stack_model_t)model)
    {
        case BRICK6_STACK_MODEL_SINFD:
        case BRICK6_STACK_MODEL_TRIFD: return "FOLD";
        case BRICK6_STACK_MODEL_SINMORPH:
        case BRICK6_STACK_MODEL_TRIMORPH: return "MORPH";
        case BRICK6_STACK_MODEL_SHAPE: return "SHAPE";
        case BRICK6_STACK_MODEL_WAVETABLE: return "WAVE";
        case BRICK6_STACK_MODEL_SUB: return "SHAPE";
        case BRICK6_STACK_MODEL_FM: return "INDEX";
        case BRICK6_STACK_MODEL_FEEDBACK_FM: return "FB IDX";
        case BRICK6_STACK_MODEL_RING: return "MOD1";
        case BRICK6_STACK_MODEL_TRIPLE_SAW:
        case BRICK6_STACK_MODEL_TRIPLE_SQUARE: return "OSC2";
        case BRICK6_STACK_MODEL_SWARM: return "SPREAD";
        default: return "TIMBRE";
    }
}

static const char *ui_page_template_tone_stack_color_label(uint8_t model)
{
    switch ((brick6_stack_model_t)model)
    {
        case BRICK6_STACK_MODEL_SINFD:
        case BRICK6_STACK_MODEL_TRIFD: return "SYM";
        case BRICK6_STACK_MODEL_SINMORPH:
        case BRICK6_STACK_MODEL_TRIMORPH: return "TARGET";
        case BRICK6_STACK_MODEL_SHAPE: return "MORPH";
        case BRICK6_STACK_MODEL_WAVETABLE: return "BANK";
        case BRICK6_STACK_MODEL_SUB: return "SUB";
        case BRICK6_STACK_MODEL_FM:
        case BRICK6_STACK_MODEL_FEEDBACK_FM: return "RATIO";
        case BRICK6_STACK_MODEL_RING: return "MOD2";
        case BRICK6_STACK_MODEL_TRIPLE_SAW:
        case BRICK6_STACK_MODEL_TRIPLE_SQUARE: return "OSC3";
        case BRICK6_STACK_MODEL_SWARM: return "COLOR";
        default: return "COLOR";
    }
}

static const char *ui_page_template_tone_stack_param3_label(uint8_t model)
{
    switch ((brick6_stack_model_t)model)
    {
        case BRICK6_STACK_MODEL_SINFD:
        case BRICK6_STACK_MODEL_TRIFD: return "SHAPE";
        case BRICK6_STACK_MODEL_SINMORPH: return "ASYM";
        case BRICK6_STACK_MODEL_TRIMORPH: return "SKEW";
        default: return "PARAM3";
    }
}

static uint8_t ui_page_template_tone_stack_param_text(param_id_t id,
                                                      float value,
                                                      char *out_name,
                                                      uint32_t out_name_len,
                                                      char *out_value,
                                                      uint32_t out_value_len)
{
    const uint8_t active_track = ui_get_active_track();
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
            name = "MODEL";
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
            float model_value = 0.0f;
            const param_id_t model_param = (param_id_t)(PARAM_STACK_OSC1_MODEL + (stack_slot * 5U));
            (void)param_registry_get_track_value(model_param, active_track, &model_value);
            name = ui_page_template_tone_stack_timbre_label((uint8_t)(model_value + 0.5f));
            break;
        }
        case 4U:
        {
            float model_value = 0.0f;
            const param_id_t model_param = (param_id_t)(PARAM_STACK_OSC1_MODEL + (stack_slot * 5U));
            (void)param_registry_get_track_value(model_param, active_track, &model_value);
            name = ui_page_template_tone_stack_color_label((uint8_t)(model_value + 0.5f));
            break;
        }
        default:
        {
            float model_value = 0.0f;
            const param_id_t model_param = (param_id_t)(PARAM_STACK_OSC1_MODEL + (stack_slot * 5U));
            (void)param_registry_get_track_value(model_param, active_track, &model_value);
            name = ui_page_template_tone_stack_param3_label((uint8_t)(model_value + 0.5f));
            break;
        }
    }

    if ((out_name != NULL) && (out_name_len > 0U))
    {
        (void)snprintf(out_name, out_name_len, "%s", (name != NULL) ? name : "-");
    }
    if ((stack_param == 4U) && (out_value != NULL) && (out_value_len > 0U))
    {
        float model_value = 0.0f;
        const param_id_t model_param = (param_id_t)(PARAM_STACK_OSC1_MODEL + (stack_slot * 5U));
        (void)param_registry_get_track_value(model_param, active_track, &model_value);
        const brick6_stack_model_t model = (brick6_stack_model_t)(uint8_t)(model_value + 0.5f);
        if (model == BRICK6_STACK_MODEL_SINMORPH)
        {
            static const char *const targets[] = { "FULL RECT", "HALF RECT", "TRIANGLE", "FOLD" };
            uint8_t target = (uint8_t)(value * 3.0f + 0.5f);
            if (target > 3U)
            {
                target = 3U;
            }
            (void)snprintf(out_value, out_value_len, "%s", targets[target]);
        }
        else if (model == BRICK6_STACK_MODEL_TRIMORPH)
        {
            static const char *const targets[] = { "PULSE", "SAW", "SQUARE" };
            uint8_t target = (uint8_t)(value * 2.0f + 0.5f);
            if (target > 2U)
            {
                target = 2U;
            }
            (void)snprintf(out_value, out_value_len, "%s", targets[target]);
        }
    }
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

    if ((id == PARAM_MIX_REVERB_HPF)
            || (id == PARAM_MIX_REVERB_LPF)
            || (id == PARAM_MIX_DELAY_HPF)
            || (id == PARAM_MIX_DELAY_LPF))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_HPF_LPF_RESPONSE_GROUP;
    }
    (void)value_label;

    if ((id == PARAM_STACK_OSC1_MODEL)
            || (id == PARAM_STACK_OSC2_MODEL)
            || (id == PARAM_STACK_OSC3_MODEL))
    {
        return UIW_WIDGET_ENUM_TEXT;
    }

    uint8_t fx_type = 0U;
    uint8_t macro = 0U;
    if ((ui_page_template_tone_macro_fx_type_for_param(id, &fx_type, &macro) != 0U)
            && (fx_type == (uint8_t)FX_MASTER_MACRO_STUTTER)
            && (macro == 1U))
    {
        return UIW_WIDGET_SWITCH;
    }

    if (ui_page_template_tone_prism_kind_for_param(id, labels, NULL, &kind) == 0U)
    {
        if ((id == PARAM_PRISM_EDIT) || (id == PARAM_PRISM_OSC2_EDIT))
        {
            return UIW_WIDGET_ENUM_TEXT;
        }
        if ((id == PARAM_PRISM_FINE)
                || (id == PARAM_PRISM_COARSE)
                || (id == PARAM_PRISM_MODULATION)
                || (id == PARAM_PRISM_OSC2_FINE)
                || (id == PARAM_PRISM_OSC2_COARSE)
                || (id == PARAM_PRISM_OSC2_MODULATION))
        {
            return UIW_WIDGET_BIPOLAR_BAR;
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

    if ((ui_get_track_family(ui_get_active_track()) == UI_TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(ui_get_active_track()) == UI_TRACK_TYPE_WAVE)
            && (subpage != NULL)
            && (((subpage->param_bank.params[0] == PARAM_WAVE_OSC1_TABLE)
                    && (subpage->param_bank.params[1] == PARAM_WAVE_OSC1_POS)
                    && (subpage->param_bank.params[2] == PARAM_WAVE_OSC1_START)
                    && (subpage->param_bank.params[3] == PARAM_WAVE_OSC1_END))
                || ((subpage->param_bank.params[0] == PARAM_WAVE_OSC2_TABLE)
                    && (subpage->param_bank.params[1] == PARAM_WAVE_OSC2_POS)
                    && (subpage->param_bank.params[2] == PARAM_WAVE_OSC2_START)
                    && (subpage->param_bank.params[3] == PARAM_WAVE_OSC2_END))))
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

    const param_id_t model_param = (param_id_t)(PARAM_STACK_OSC1_MODEL + (stack_slot * 5U));
    float model_value = 0.0f;
    if (param_registry_get_track_value(model_param, ui_get_active_track(), &model_value) == 0U)
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
    }

    const brick6_stack_model_t model = (brick6_stack_model_t)(uint8_t)(model_value + 0.5f);
    if ((model == BRICK6_STACK_MODEL_SHAPE) && (stack_param == 4U))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_STACK_WAVEFORM;
    }
    if (((model == BRICK6_STACK_MODEL_SINFD)
            || (model == BRICK6_STACK_MODEL_TRIFD)
            || (model == BRICK6_STACK_MODEL_SINMORPH)
            || (model == BRICK6_STACK_MODEL_TRIMORPH))
            && ((stack_param == 3U) || (stack_param == 4U) || (stack_param == 5U)))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_STACK_WAVEFORM;
    }
    return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
}

static uint8_t ui_page_template_tone_macro_fx_type_for_param(param_id_t id, uint8_t *out_fx_type, uint8_t *out_macro)
{
    const uint8_t active_track = ui_get_active_track();
    if ((out_fx_type == NULL)
            || (id < PARAM_MACRO_FX1_TYPE)
            || (id > PARAM_MACRO_FX4_B)
            || (track_topology_is_role(active_track, TRACK_TOPOLOGY_ROLE_FX) == 0U))
    {
        return 0U;
    }

    const uint8_t offset = (uint8_t)(id - PARAM_MACRO_FX1_TYPE);
    const uint8_t fx_slot = (uint8_t)(offset / 4U);
    const uint8_t macro = (uint8_t)(offset % 4U);
    const param_id_t type_param = (param_id_t)(PARAM_MACRO_FX1_TYPE + (fx_slot * 4U));
    float fx_type_value = 0.0f;
    if (param_registry_get_track_value(type_param, active_track, &fx_type_value) == 0U)
    {
        return 0U;
    }

    *out_fx_type = (uint8_t)(fx_type_value + 0.5f);
    if (out_macro != NULL)
    {
        *out_macro = macro;
    }
    return 1U;
}

static uint8_t ui_page_template_tone_macro_fx_index(uint8_t raw, uint8_t max_index)
{
    return (uint8_t)(((uint32_t)raw * (uint32_t)max_index + 63U) / 127U);
}

static void ui_page_template_tone_macro_fx_format_signed(uint8_t raw,
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

static void ui_page_template_tone_macro_fx_format_percent(uint8_t raw,
                                                           uint8_t max_percent,
                                                           char *out,
                                                           uint32_t out_len)
{
    (void)max_percent;
    ui_format_param_127_00((float)raw, 0.0f, 127.0f, out, out_len);
}

static void ui_page_template_tone_macro_fx_format_choice(uint8_t raw,
                                                          const char *const *labels,
                                                          uint8_t label_count,
                                                          char *out,
                                                          uint32_t out_len)
{
    const uint8_t idx = ui_page_template_tone_macro_fx_index(raw, (uint8_t)(label_count - 1U));
    (void)snprintf(out, out_len, "%s", labels[idx]);
}

static void ui_page_template_tone_macro_fx_format_rate_div(uint8_t raw,
                                                            char *out,
                                                            uint32_t out_len)
{
    static const char *const k_labels[] = {
        "4/1", "2/1", "1/1", "1/2", "1/3",
        "1/4", "1/6", "1/8", "1/12", "1/16"
    };
    ui_page_template_tone_macro_fx_format_choice(raw, k_labels, (uint8_t)(sizeof(k_labels) / sizeof(k_labels[0])), out, out_len);
}

static void ui_page_template_tone_macro_fx_format_time_div(uint8_t raw,
                                                            char *out,
                                                            uint32_t out_len)
{
    static const char *const k_labels[] = { "1/8", "1/4", "1/3", "1/2", "3/4", "1/1", "3/2", "2/1" };
    ui_page_template_tone_macro_fx_format_choice(raw, k_labels, (uint8_t)(sizeof(k_labels) / sizeof(k_labels[0])), out, out_len);
}

static void ui_page_template_tone_macro_fx_format_stutter_size(uint8_t raw,
                                                                char *out,
                                                                uint32_t out_len)
{
    static const char *const k_labels[] = { "1/32", "1/16", "1/8", "1/6", "1/4", "1/3", "1/2", "3/4" };
    ui_page_template_tone_macro_fx_format_choice(raw, k_labels, (uint8_t)(sizeof(k_labels) / sizeof(k_labels[0])), out, out_len);
}

static void ui_page_template_tone_macro_fx_format_value(uint8_t fx_type,
                                                         uint8_t slot,
                                                         uint8_t raw,
                                                         char *out,
                                                         uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    if ((fx_type == FX_MASTER_MACRO_OFF) || (fx_type > FX_MASTER_MACRO_COLOR))
    {
        (void)snprintf(out, out_len, "---");
        return;
    }

    switch (fx_type)
    {
        case FX_MASTER_MACRO_DRIVE:
            if (slot == 2U)
            {
                const uint32_t drive_x10 = 10U + (((uint32_t)raw * (uint32_t)raw * 420U + 8064U) / 16129U);
                (void)snprintf(out, out_len, "%lu.%lux", (unsigned long)(drive_x10 / 10U), (unsigned long)(drive_x10 % 10U));
            }
            else
            {
                ui_page_template_tone_macro_fx_format_signed(raw, -64, 63, "", out, out_len);
            }
            break;

        case FX_MASTER_MACRO_CRUSH:
            if (slot == 2U)
            {
                const uint8_t bits = (uint8_t)(16U - ui_page_template_tone_macro_fx_index(raw, 12U));
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
                ui_page_template_tone_macro_fx_format_choice(raw, k_color, (uint8_t)(sizeof(k_color) / sizeof(k_color[0])), out, out_len);
            }
            break;

        case FX_MASTER_MACRO_CHOP:
            if (slot == 2U)
            {
                ui_page_template_tone_macro_fx_format_rate_div(raw, out, out_len);
            }
            else
            {
                static const char *const k_shape[] = { "SOFT", "GATE", "HARD" };
                ui_page_template_tone_macro_fx_format_choice(raw, k_shape, (uint8_t)(sizeof(k_shape) / sizeof(k_shape[0])), out, out_len);
            }
            break;

        case FX_MASTER_MACRO_PUMP:
            if (slot == 2U)
            {
                ui_page_template_tone_macro_fx_format_rate_div(raw, out, out_len);
            }
            else
            {
                ui_page_template_tone_macro_fx_format_percent(raw, 100U, out, out_len);
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
                ui_page_template_tone_macro_fx_format_percent(raw, 80U, out, out_len);
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
                ui_page_template_tone_macro_fx_format_percent(raw, 100U, out, out_len);
            }
            break;

        case FX_MASTER_MACRO_FREEZE:
            if (slot == 2U)
            {
                ui_page_template_tone_macro_fx_format_time_div(raw, out, out_len);
            }
            else
            {
                static const char *const k_hold[] = { "SHORT", "MID", "LONG", "INF" };
                ui_page_template_tone_macro_fx_format_choice(raw, k_hold, (uint8_t)(sizeof(k_hold) / sizeof(k_hold[0])), out, out_len);
            }
            break;

        case FX_MASTER_MACRO_STUTTER:
            if (slot == 2U)
            {
                ui_page_template_tone_macro_fx_format_stutter_size(raw, out, out_len);
            }
            else
            {
                static const char *const k_rate[] = { "0.5x", "0.75x", "1x", "1.5x", "2x", "3x", "4x", "6x" };
                ui_page_template_tone_macro_fx_format_choice(raw, k_rate, (uint8_t)(sizeof(k_rate) / sizeof(k_rate[0])), out, out_len);
            }
            break;

        case FX_MASTER_MACRO_COLOR:
            if (slot == 2U)
            {
                ui_page_template_tone_macro_fx_format_signed(raw, -64, 63, "", out, out_len);
            }
            else
            {
                const uint32_t focus = 2500U + (((uint32_t)raw * (uint32_t)raw * 11500U + 8064U) / 16129U);
                if (focus >= 10000U)
                {
                    (void)snprintf(out, out_len, "%luk", (unsigned long)((focus + 500U) / 1000U));
                }
                else
                {
                    (void)snprintf(out, out_len, "%lu.%luk", (unsigned long)(focus / 1000U), (unsigned long)((focus % 1000U) / 100U));
                }
            }
            break;

        default:
            (void)snprintf(out, out_len, "%u", (unsigned int)raw);
            break;
    }
}

static uint8_t ui_page_template_tone_deluge_param_text(param_id_t id,
                                                       float value,
                                                       char *out_name,
                                                       uint32_t out_name_len,
                                                       char *out_value,
                                                       uint32_t out_value_len)
{
    static const char *const k_model_labels[] = {
        "SINE", "TRI", "SQUARE", "A-SQUARE", "SAW", "A-SAW"
    };

    if (id == PARAM_DELUGE_MODEL)
    {
        uint8_t model = (uint8_t)((value < 0.0f) ? 0.0f : value + 0.5f);
        if (model >= (uint8_t)(sizeof(k_model_labels) / sizeof(k_model_labels[0])))
        {
            model = (uint8_t)BRICK6_DELUGE_MODEL_SQUARE;
        }
        if ((out_name != NULL) && (out_name_len > 0U)) { (void)snprintf(out_name, out_name_len, "MODEL"); }
        if ((out_value != NULL) && (out_value_len > 0U)) { (void)snprintf(out_value, out_value_len, "%s", k_model_labels[model]); }
        return 1U;
    }

    if (id == PARAM_DELUGE_WIDTH)
    {
        const brick6_deluge_model_t model = ui_page_template_tone_deluge_active_model();
        const uint8_t is_square = (model == BRICK6_DELUGE_MODEL_SQUARE) ? 1U : 0U;
        if ((out_name != NULL) && (out_name_len > 0U))
        {
            (void)snprintf(out_name, out_name_len, "%s", (is_square != 0U) ? "WIDTH" : "SKEW");
        }
        if ((out_value != NULL) && (out_value_len > 0U))
        {
            (void)snprintf(out_value, out_value_len, "%d%%", (int)(value * 100.0f + 0.5f));
        }
        return 1U;
    }

    if (id == PARAM_DELUGE_PHASE)
    {
        float retrig = 0.0f;
        (void)param_registry_get_track_value(PARAM_DELUGE_RETRIG, ui_get_active_track(), &retrig);
        if ((out_name != NULL) && (out_name_len > 0U)) { (void)snprintf(out_name, out_name_len, "PHASE"); }
        if ((out_value != NULL) && (out_value_len > 0U))
        {
            (void)snprintf(out_value, out_value_len, "%ddeg%s",
                           (int)(value + 0.5f),
                           (retrig >= 0.5f) ? "" : " FREE");
        }
        return 1U;
    }

    return 0U;
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
    if ((ui_get_track_family(active_track) == UI_TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(active_track) == UI_TRACK_TYPE_DELUGE)
            && (ui_page_template_tone_deluge_param_text(id, value, out_name, out_name_len, out_value, out_value_len) != 0U))
    {
        (void)slot;
        return 1U;
    }

    if ((ui_get_track_family(active_track) == UI_TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(active_track) == UI_TRACK_TYPE_WAVE))
    {
        const char *name = NULL;
        switch (id)
        {
            case PARAM_WAVE_OSC1_TABLE:
            case PARAM_WAVE_OSC2_TABLE:
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
            case PARAM_WAVE_OSC1_END:
            case PARAM_WAVE_OSC2_END:
                name = "END";
                break;
            case PARAM_WAVE_OSC1_LEVEL:
            case PARAM_WAVE_OSC2_LEVEL:
                name = "LEVEL";
                break;
            case PARAM_WAVE_OSC1_TUNE:
            case PARAM_WAVE_OSC2_TUNE:
                name = "TUNE";
                break;
            case PARAM_WAVE_OSC1_PHASE:
            case PARAM_WAVE_OSC2_PHASE:
                name = "PHASE";
                break;
            case PARAM_WAVE_OSC1_FLIP:
            case PARAM_WAVE_OSC2_FLIP:
                name = "FLIP";
                break;
            case PARAM_WAVE_FRAME_INTERP:
                name = "FRAME";
                break;
            case PARAM_WAVE_SAMPLE_INTERP:
                name = "SAMPLE";
                break;
            case PARAM_WAVE_POS_UPDATE:
                name = "POSUPD";
                break;
            case PARAM_WAVE_POS_SMOOTH:
                name = "SMOOTH";
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
                if ((id == PARAM_WAVE_OSC1_TABLE) || (id == PARAM_WAVE_OSC2_TABLE))
                {
                    const uint16_t global_slot = (uint16_t)((value < 0.0f) ? 0.0f : value + 0.5f);
                    const sample_global_slot_t *const asset = sample_global_pool_get_slot(global_slot);
                    if ((asset != NULL) && (asset->kind == SAMPLE_GLOBAL_KIND_WAVETABLE)
                            && (asset->state == SAMPLE_GLOBAL_STATE_READY))
                    {
                        (void)snprintf(out_value, out_value_len, "WT%03u", (unsigned int)global_slot);
                    }
                    else
                    {
                        (void)snprintf(out_value, out_value_len, "---");
                    }
                }
                else if ((id == PARAM_WAVE_OSC1_TUNE) || (id == PARAM_WAVE_OSC2_TUNE))
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
                else if ((id == PARAM_WAVE_FRAME_INTERP)
                        || (id == PARAM_WAVE_SAMPLE_INTERP)
                        || (id == PARAM_WAVE_POS_SMOOTH))
                {
                    (void)snprintf(out_value, out_value_len, "%s", (value >= 0.5f) ? "On" : "Off");
                }
                else if (id == PARAM_WAVE_POS_UPDATE)
                {
                    static const char *const k_labels[] = { "FULL", "8", "16", "32" };
                    uint8_t index = (uint8_t)((value < 0.0f) ? 0.0f : value + 0.5f);
                    if (index >= (uint8_t)(sizeof(k_labels) / sizeof(k_labels[0])))
                    {
                        index = 0U;
                    }
                    (void)snprintf(out_value, out_value_len, "%s", k_labels[index]);
                }
            }
            (void)slot;
            return 1U;
        }
    }

    if ((ui_get_track_family(active_track) == UI_TRACK_FAMILY_SAMPLER)
            && (ui_get_track_type(active_track) == UI_TRACK_TYPE_RAM))
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
            && (ui_get_track_type(active_track) == UI_TRACK_TYPE_PRISM)
            && (ui_page_template_tone_prism_param_text(id, value, out_name, out_name_len, out_value, out_value_len) != 0U))
    {
        (void)slot;
        return 1U;
    }

    if ((ui_get_track_family(active_track) == UI_TRACK_FAMILY_SYNTH)
            && (ui_get_track_type(active_track) == UI_TRACK_TYPE_STACK)
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

    if ((ui_get_track_family(active_track) == UI_TRACK_FAMILY_DRUM)
            && (ui_get_track_type(active_track) == UI_TRACK_TYPE_DRUM_MD)
            && (id >= PARAM_DRUM_MD_MODEL)
            && (id <= PARAM_DRUM_MD_P8))
    {
        float model_value = 0.0f;
        (void)param_registry_get_track_value(PARAM_DRUM_MD_MODEL, active_track, &model_value);
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

    if ((track_topology_is_role(active_track, TRACK_TOPOLOGY_ROLE_FX) == 0U)
            || (slot < 1U)
            || (slot > 3U)
            || (g_ui_template_tone_state.active_subpage >= 4U))
    {
        return 0U;
    }

    const param_id_t type_param = (param_id_t)(PARAM_MACRO_FX1_TYPE + (g_ui_template_tone_state.active_subpage * 4U));
    const param_id_t value_param = (param_id_t)(type_param + slot);
    if (id != value_param)
    {
        return 0U;
    }

    float fx_type_value = 0.0f;
    const char *label_a = "A";
    const char *label_b = "B";

    (void)param_registry_get_track_value(type_param, active_track, &fx_type_value);
    ui_page_template_tone_macro_fx_labels((uint8_t)(fx_type_value + 0.5f), &label_a, &label_b);

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
        const uint8_t raw_value = ui_page_template_tone_macro_fx_u7(value);
        if (slot == 1U)
        {
            const uint8_t fx_type = (uint8_t)(fx_type_value + 0.5f);
            if (fx_type == (uint8_t)FX_MASTER_MACRO_STUTTER)
            {
                (void)snprintf(out_value, out_value_len, "%s", (raw_value == 0U) ? "OFF" : "ON");
            }
            else if (fx_type == (uint8_t)FX_MASTER_MACRO_FREEZE)
            {
                if (raw_value == 0U)
                {
                    (void)snprintf(out_value, out_value_len, "OFF");
                }
                else
                {
                    ui_page_template_tone_macro_fx_format_percent(raw_value, 100U, out_value, out_value_len);
                }
            }
            else
            {
                ui_page_template_tone_macro_fx_format_percent(raw_value, 100U, out_value, out_value_len);
            }
        }
        else
        {
            ui_page_template_tone_macro_fx_format_value((uint8_t)(fx_type_value + 0.5f),
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
            && (ui_get_track_type(active_track) == UI_TRACK_TYPE_DRUM_MD))
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
            if ((ui_track_family_is_engine(track_family) != 0) && (track_type == UI_TRACK_TYPE_PRISM))
            {
                family_template = &g_ui_template_tone_family_prism;
            }
            else if ((ui_track_family_is_engine(track_family) != 0) && (track_type == UI_TRACK_TYPE_WAVE))
            {
                family_template = &g_ui_template_tone_family_wave;
            }
            else if ((ui_track_family_is_engine(track_family) != 0) && (track_type == UI_TRACK_TYPE_STACK))
            {
                family_template = &g_ui_template_tone_family_stack;
            }
            else if ((ui_track_family_is_engine(track_family) != 0) && (track_type == UI_TRACK_TYPE_DELUGE))
            {
                family_template = &g_ui_template_tone_family_deluge;
            }
            else if ((ui_track_family_is_engine(track_family) != 0) && (track_type == UI_TRACK_TYPE_RAM))
            {
                family_template = &g_ui_template_tone_family_sampler;
            }

            else if ((track_family == UI_TRACK_FAMILY_SAMPLER) && (track_type == UI_TRACK_TYPE_STREAM))
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
            else if ((track_family == UI_TRACK_FAMILY_EXTERNAL) && (track_type == UI_TRACK_TYPE_EXTERNAL))
            {
                family_template = &g_ui_template_tone_family_midi;
            }
            else if (track_family == UI_TRACK_FAMILY_DRUM)
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
    g_ui_template_tone_state.resolved_family = ui_page_template_tone_resolve_family();
    ui_template_page_select_subpage(&g_ui_template_tone_state, 0U);
}

void ui_page_template_tone_toggle_subset(void)
{
    const uint8_t active_track = ui_get_active_track();
    if (track_topology_is_role(active_track, TRACK_TOPOLOGY_ROLE_MASTER) != 0U)
    {
        g_ui_template_tone_subset = (uint8_t)((g_ui_template_tone_subset + 1U) % 3U);
        g_ui_template_tone_state.resolved_family = ui_page_template_tone_resolve_family();
        ui_template_page_select_subpage(&g_ui_template_tone_state, 0U);
        return;
    }
    if ((ui_get_track_family(active_track) != UI_TRACK_FAMILY_SYNTH)
            || ((ui_get_track_type(active_track) != UI_TRACK_TYPE_STACK)
                && (ui_get_track_type(active_track) != UI_TRACK_TYPE_WAVE)))
    {
        ui_page_template_tone_open_primary();
        return;
    }

    g_ui_template_tone_subset = (g_ui_template_tone_subset == 0U) ? 1U : 0U;
    g_ui_template_tone_state.resolved_family = ui_page_template_tone_resolve_family();
    ui_template_page_select_subpage(&g_ui_template_tone_state, 0U);
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
