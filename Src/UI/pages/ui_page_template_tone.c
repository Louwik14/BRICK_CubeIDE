#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "pages/ui_page_template_tone.h"

#include "Audio/md_model.h"
#include "Audio/fx_modfx_global.h"
#include "Audio/synth_waveform_snapshot.h"
#include "Audio/Engines/fm_engine.h"
#include "Audio/Engines/stack_engine.h"
#include "Storage/project_control.h"
#include "Param/param_registry.h"
#include "Param/param_prism_labels.h"
#include "Param/param_stack_labels.h"
#include "Sampler/sample_global_pool.h"
#include "ui_core.h"
#include "ui_renderer_template.h"
#include "ui_page_manager.h"
#include "ui_navigation.h"
#include "ui_template_page.h"

static uint8_t g_ui_template_tone_subset = 0U;
static uint8_t g_ui_template_tone_global_master = 0U;

static uint8_t ui_template_tone_multi_logical_label(uint16_t logical,
                                                    char *out,
                                                    uint32_t out_len)
{
    persist_control_asset_ref_t asset;
    if ((out == NULL) || (out_len == 0U)
        || (project_control_get_logical_asset(PERSIST_ASSET_MULTI, logical, &asset) == 0U)
        || (asset.path_length == 0U))
    {
        return 0U;
    }

    uint16_t begin = 0U;
    uint16_t end = asset.path_length;
    for (uint16_t i = 0U; i < asset.path_length; ++i)
    {
        if ((asset.path[i] == '/') || (asset.path[i] == '\\') || (asset.path[i] == ':'))
        {
            begin = (uint16_t)(i + 1U);
        }
    }
    for (uint16_t i = begin; i < asset.path_length; ++i)
    {
        if (asset.path[i] == '.')
        {
            end = i;
            break;
        }
    }

    if (end <= begin)
    {
        return 0U;
    }
    uint32_t copy_len = (uint32_t)(end - begin);
    if (copy_len >= out_len)
    {
        copy_len = out_len - 1U;
    }
    memcpy(out, &asset.path[begin], copy_len);
    out[copy_len] = '\0';
    return 1U;
}


static ui_template_family_t g_ui_template_tone_family_master_reverb = {
    .family_title = "MASTER 1/3",
    .nav_labels = { "REVERB 1", "REVERB 2", "MOD FX 1", "MOD FX 2" },
    .subpages = {
        { .title = "REVERB 1", .param_bank = { .params = { PARAM_MIX_REVERB_WET, PARAM_MIX_REVERB_ROOM_SIZE, PARAM_MIX_REVERB_DAMPING, PARAM_MIX_REVERB_WIDTH } } },
        { .title = "REVERB 2", .param_bank = { .params = { PARAM_MIX_REVERB_HPF, PARAM_MIX_REVERB_LPF, PARAM_MIX_REVERB_DELAYS, PARAM_MODFX_MODEL } } },
        { .title = "MOD FX 1", .param_bank = { .params = { PARAM_MODFX_RATE, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "MOD FX 2", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static void ui_template_tone_sync_modfx_pages(void)
{
    const uint8_t model = (uint8_t)(param_get(PARAM_MODFX_MODEL) + 0.5f);
    param_id_t *const p3 = g_ui_template_tone_family_master_reverb.subpages[2].param_bank.params;
    param_id_t *const p4 = g_ui_template_tone_family_master_reverb.subpages[3].param_bank.params;
    if (model == FX_MODFX_DAISY_STEREO)
    {
        p3[0] = PARAM_MODFX_RATE;
        p3[1] = PARAM_MODFX_RATE_B;
        p3[2] = PARAM_MODFX_OFFSET;
        p3[3] = PARAM_MODFX_DELAY_B;
        p4[0] = PARAM_MODFX_DEPTH;
        p4[1] = PARAM_MODFX_DEPTH_B;
        p4[2] = PARAM_MODFX_FEEDBACK;
        p4[3] = PARAM_MODFX_WIDTH;
        return;
    }
    p3[0] = (model == FX_MODFX_JUNOLOGUE) ? PARAM_MODFX_OFFSET : ((model == FX_MODFX_OFF) ? PARAM_COUNT : PARAM_MODFX_RATE);
    p3[1] = ((model == FX_MODFX_OFF) || (model == FX_MODFX_JUNOLOGUE)) ? PARAM_COUNT : PARAM_MODFX_DEPTH;
    p3[2] = PARAM_COUNT;
    p3[3] = ((model == FX_MODFX_OFF) || (model == FX_MODFX_JUNOLOGUE)) ? PARAM_COUNT : PARAM_MODFX_OFFSET;
    p4[0] = PARAM_COUNT; p4[1] = PARAM_COUNT; p4[2] = PARAM_COUNT; p4[3] = PARAM_COUNT;
}

static const ui_template_family_t g_ui_template_tone_family_master_delay_classic = {
    .family_title = "MASTER 2/3",
    .nav_labels = { "DELAY 1", "DELAY 2", "-", "-" },
    .subpages = {
        { .title = "DELAY 1", .param_bank = { .params = { PARAM_MIX_DELAY_TYPE, PARAM_MIX_DELAY_TIME, PARAM_MIX_DELAY_PINGPONG, PARAM_MIX_DELAY_VOL } } },
        { .title = "DELAY 2", .param_bank = { .params = { PARAM_MIX_DELAY_SPECTRAL_POSITION, PARAM_MIX_DELAY_SPECTRAL_WIDTH, PARAM_MIX_DELAY_REV, PARAM_MIX_DELAY_FEEDBACK } } },
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
        { .title = "DELAY 2", .param_bank = { .params = { PARAM_MIX_DELAY_SPECTRAL_POSITION, PARAM_MIX_DELAY_SPECTRAL_WIDTH, PARAM_MIX_DELAY_REV, PARAM_MIX_DELAY_FEEDBACK } } },
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
        { .title = "PLAY", .param_bank = { .params = { PARAM_SAMPLER_SAMPLE, PARAM_SAMPLER_MODE, PARAM_SAMPLER_START, PARAM_SAMPLER_LENGTH } } },
        { .title = "LOOP", .param_bank = { .params = { PARAM_SAMPLER_GAIN, PARAM_SAMPLER_TUNE, PARAM_SAMPLER_LOOP_START, PARAM_SAMPLER_SLICE_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_group = {
    .family_title = "TONE",
    .nav_labels = { "FILTER", "-", "-", "-" },
    .subpages = {
        { .title = "FILTER", .param_bank = { .params = { PARAM_FILTER_CUTOFF, PARAM_FILTER_RESONANCE, PARAM_FILTER_EG_AMT, PARAM_FILTER_MORPH } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
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
    .family_title = "TONE 2/2",
    .nav_labels = { "OSC1", "OSC2", "COMMON", "MOD/PH" },
    .subpages = {
        { .title = "OSC1", .param_bank = { .params = { PARAM_PRISM_OSC1_MODEL, PARAM_PRISM_OSC1_PARAM1, PARAM_PRISM_OSC1_PARAM2, PARAM_PRISM_OSC1_AMOD } } },
        { .title = "OSC2", .param_bank = { .params = { PARAM_PRISM_OSC2_MODEL, PARAM_PRISM_OSC2_PARAM1, PARAM_PRISM_OSC2_PARAM2, PARAM_PRISM_OSC2_AMOD } } },
        { .title = "COMMON", .param_bank = { .params = { PARAM_PRISM_VOLUME, PARAM_PRISM_BALANCE, PARAM_PRISM_TUNE, PARAM_PRISM_DETUNE } } },
        { .title = "MOD / PHASE", .param_bank = { .params = { PARAM_PRISM_PITCH_MOD1, PARAM_PRISM_PHASE1_RESET, PARAM_PRISM_PITCH_MOD2, PARAM_PRISM_DRIFT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_prism_live = {
    .family_title = "TONE 1/2",
    .nav_labels = { "OSC1", "OSC2", "COMMON", "MOD/PH" },
    .subpages = {
        { .title = "OSC1", .param_bank = { .params = { PARAM_PRISM_OSC1_MODEL, PARAM_PRISM_OSC1_PARAM1, PARAM_PRISM_OSC1_PARAM2, PARAM_PRISM_OSC1_AMOD } } },
        { .title = "OSC2", .param_bank = { .params = { PARAM_PRISM_OSC2_MODEL, PARAM_PRISM_OSC2_PARAM1, PARAM_PRISM_OSC2_PARAM2, PARAM_PRISM_OSC2_AMOD } } },
        { .title = "COMMON", .param_bank = { .params = { PARAM_PRISM_VOLUME, PARAM_PRISM_BALANCE, PARAM_PRISM_TUNE, PARAM_PRISM_DETUNE } } },
        { .title = "MOD / PHASE", .param_bank = { .params = { PARAM_PRISM_PITCH_MOD1, PARAM_PRISM_PHASE1_RESET, PARAM_PRISM_PITCH_MOD2, PARAM_PRISM_DRIFT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_wave = {
    .family_title = "TONE 1/2",
    .nav_labels = { "OSC1", "OSC2", "COMMON", "-" },
    .subpages = {
        { .title = "OSC1", .param_bank = { .params = { PARAM_WAVE_OSC1_TABLE, PARAM_WAVE_OSC1_POS, PARAM_WAVE_OSC1_START, PARAM_WAVE_OSC1_LEN } } },
        { .title = "OSC2", .param_bank = { .params = { PARAM_WAVE_OSC2_TABLE, PARAM_WAVE_OSC2_POS, PARAM_WAVE_OSC2_START, PARAM_WAVE_OSC2_LEN } } },
        { .title = "COMMON", .param_bank = { .params = { PARAM_WAVE_VOLUME, PARAM_WAVE_BALANCE, PARAM_WAVE_TUNE, PARAM_WAVE_DETUNE } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_wave_classic = {
    .family_title = "TONE 2/2",
    .nav_labels = { "OSC1", "OSC2", "COMMON", "-" },
    .subpages = {
        { .title = "OSC1", .param_bank = { .params = { PARAM_WAVE_OSC1_TABLE, PARAM_WAVE_OSC1_POS, PARAM_WAVE_OSC1_START, PARAM_WAVE_OSC1_LEN } } },
        { .title = "OSC2", .param_bank = { .params = { PARAM_WAVE_OSC2_TABLE, PARAM_WAVE_OSC2_POS, PARAM_WAVE_OSC2_START, PARAM_WAVE_OSC2_LEN } } },
        { .title = "COMMON", .param_bank = { .params = { PARAM_WAVE_VOLUME, PARAM_WAVE_BALANCE, PARAM_WAVE_TUNE, PARAM_WAVE_DETUNE } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_stack = {
    .family_title = "TONE",
    .nav_labels = { "OSC1/2", "OSC3", "MIX", "PHASE" },
    .subpages = {
        { .title = "OSC1 / OSC2", .param_bank = { .params = { PARAM_STACK_OSC1_MODEL, PARAM_STACK_OSC1_TUNE, PARAM_STACK_OSC2_MODEL, PARAM_STACK_OSC2_TUNE } } },
        { .title = "OSC3", .param_bank = { .params = { PARAM_STACK_OSC3_MODEL, PARAM_STACK_OSC3_TIMBRE, PARAM_STACK_OSC3_COLOR, PARAM_STACK_OSC3_TUNE } } },
        { .title = "MIX", .param_bank = { .params = { PARAM_STACK_OSC1_LEVEL, PARAM_STACK_OSC2_LEVEL, PARAM_STACK_OSC3_LEVEL, PARAM_STACK_NOISE_LEVEL } } },
        { .title = "PHASE", .param_bank = { .params = { PARAM_STACK_PHASE_RESET, PARAM_STACK_OSC_DETUNE, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static ui_template_family_t g_ui_template_tone_family_fm = {
    .family_title = "TONE 1/2",
    .nav_labels = { "GLOBAL", "OP QUICK", "PITCH R", "PITCH L" },
    .subpages = {
        { .title = "GLOBAL", .param_bank = { .params = { PARAM_FM_ALGORITHM, PARAM_FM_ENV_ATTACK, PARAM_FM_ENV_DECAY, PARAM_FM_TRANSPOSE } } },
        { .title = "OP QUICK", .param_bank = { .params = { PARAM_FM_OPERATOR_SELECT, PARAM_FM_OP1_FREQ, PARAM_FM_OP1_LEVEL, PARAM_FM_OP1_DETUNE } } },
        { .title = "PITCH R", .param_bank = { .params = { PARAM_FM_PITCH_R1, PARAM_FM_PITCH_R2, PARAM_FM_PITCH_R3, PARAM_FM_PITCH_R4 } } },
        { .title = "PITCH L", .param_bank = { .params = { PARAM_FM_PITCH_L1, PARAM_FM_PITCH_L2, PARAM_FM_PITCH_L3, PARAM_FM_PITCH_L4 } } },
    },
    .default_subpage = 0U,
};

static ui_template_family_t g_ui_template_tone_family_fm_operator = {
    .family_title = "TONE 2/2",
    .nav_labels = { "VOICE", "ENV", "MOD", "-" },
    .subpages = {
        { .title = "VOICE", .param_bank = { .params = { PARAM_FM_OPERATOR_SELECT, PARAM_FM_OP1_LEVEL, PARAM_FM_OP1_FREQ, PARAM_FM_OP1_DETUNE } } },
        { .title = "ENV", .param_bank = { .params = { PARAM_FM_OP1_ENV_ATTACK, PARAM_FM_OP1_ENV_DECAY, PARAM_FM_OP1_ENV_SUSTAIN, PARAM_FM_OP1_ENV_RELEASE } } },
        { .title = "MOD", .param_bank = { .params = { PARAM_FM_OP1_ON, PARAM_FM_OP1_MODE, PARAM_FM_OP1_VEL, PARAM_FM_OP1_KEY } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

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


/* TONE resolution/navigation, engine-specific widgets and lifecycle remain in their original sequence.
 * Private fragments share this translation unit to preserve UI state and call order. */

#include "Tone/ui_tone_navigation.inc"

#include "Tone/ui_tone_synth_widgets.inc"

#include "Tone/ui_tone_family_setup.inc"

#include "Tone/ui_tone_page_lifecycle.inc"
