#include <stddef.h>
#include "pages/ui_page_template_tone.h"

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
        { .title = "STR", .param_bank = { .params = { PARAM_SAMPLER_CLIP_GRAIN, PARAM_SAMPLER_CLIP_HOP, PARAM_SAMPLER_CLIP_SEARCH, PARAM_COUNT } } },
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

static ui_template_page_state_t g_ui_template_tone_state = {
    .family = 0,
    .family_resolver = ui_page_template_tone_resolve_family,
    .active_subpage = 0U,
    .has_visited = 0U,
};

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
    const ui_track_type_t type = ui_get_track_type(ui_get_active_track());
    g_ui_template_tone_family_drum.nav_labels[0] = "MAIN";
    g_ui_template_tone_family_drum.nav_labels[1] = "-";
    g_ui_template_tone_family_drum.nav_labels[2] = "-";
    g_ui_template_tone_family_drum.nav_labels[3] = "-";
    ui_page_template_tone_set_subpage(0U, "MAIN", PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT);
    ui_page_template_tone_set_subpage(1U, "-", PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT);
    ui_page_template_tone_set_subpage(2U, "-", PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT);
    ui_page_template_tone_set_subpage(3U, "-", PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT);

    switch (type)
    {
        case UI_TRACK_TYPE_DRUM_TRX_BD:
            g_ui_template_tone_family_drum.nav_labels[0] = "BODY";
            g_ui_template_tone_family_drum.nav_labels[1] = "SWEEP";
            ui_page_template_tone_set_subpage(0U, "BODY", PARAM_DRUM_TRX_BD_PITCH, PARAM_DRUM_TRX_BD_DECAY, PARAM_DRUM_TRX_BD_ATTACK, PARAM_DRUM_TRX_BD_HARMONICS);
            ui_page_template_tone_set_subpage(1U, "SWEEP", PARAM_DRUM_TRX_BD_PITCH_SWEEP, PARAM_DRUM_TRX_BD_SWEEP_DECAY, PARAM_DRUM_TRX_BD_NOISE, PARAM_DRUM_TRX_BD_DRIVE);
            break;
        case UI_TRACK_TYPE_DRUM_TRX_CLAVES:
            g_ui_template_tone_family_drum.nav_labels[0] = "BODY";
            g_ui_template_tone_family_drum.nav_labels[1] = "COLOR";
            ui_page_template_tone_set_subpage(0U, "BODY", PARAM_DRUM_TRX_CLAVES_PITCH, PARAM_DRUM_TRX_CLAVES_INTERVAL, PARAM_DRUM_TRX_CLAVES_DECAY, PARAM_DRUM_TRX_CLAVES_BALANCE);
            ui_page_template_tone_set_subpage(1U, "COLOR", PARAM_DRUM_TRX_CLAVES_DRIVE, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT);
            break;
        case UI_TRACK_TYPE_DRUM_TRX_HIHAT:
            g_ui_template_tone_family_drum.nav_labels[0] = "BODY";
            g_ui_template_tone_family_drum.nav_labels[1] = "TONE";
            ui_page_template_tone_set_subpage(0U, "BODY", PARAM_DRUM_TRX_HIHAT_DECAY, PARAM_DRUM_TRX_HIHAT_GAP, PARAM_DRUM_TRX_HIHAT_PEAK, PARAM_DRUM_TRX_HIHAT_METAL);
            ui_page_template_tone_set_subpage(1U, "TONE", PARAM_DRUM_TRX_HIHAT_HP_TONE, PARAM_DRUM_TRX_HIHAT_LP_TONE, PARAM_COUNT, PARAM_COUNT);
            break;
        case UI_TRACK_TYPE_DRUM_TRX_SNARE:
            g_ui_template_tone_family_drum.nav_labels[0] = "BODY";
            g_ui_template_tone_family_drum.nav_labels[1] = "NOISE";
            ui_page_template_tone_set_subpage(0U, "BODY", PARAM_DRUM_TRX_SNARE_PITCH, PARAM_DRUM_TRX_SNARE_DECAY, PARAM_DRUM_TRX_SNARE_TUNE_INTERVAL, PARAM_DRUM_TRX_SNARE_BUMP);
            ui_page_template_tone_set_subpage(1U, "NOISE", PARAM_DRUM_TRX_SNARE_SNAP, PARAM_DRUM_TRX_SNARE_NOISE, PARAM_DRUM_TRX_SNARE_TONE_MIX, PARAM_DRUM_TRX_SNARE_DRIVE);
            break;
        case UI_TRACK_TYPE_DRUM_FM_KICK:
            g_ui_template_tone_family_drum.nav_labels[0] = "OSC";
            g_ui_template_tone_family_drum.nav_labels[1] = "SWEEP";
            g_ui_template_tone_family_drum.nav_labels[2] = "RATIO";
            g_ui_template_tone_family_drum.nav_labels[3] = "COLOR";
            ui_page_template_tone_set_subpage(0U, "OSC", PARAM_DRUM_FM_KICK_PITCH, PARAM_DRUM_FM_KICK_DECAY, PARAM_DRUM_FM_KICK_MOD_FREQ, PARAM_DRUM_FM_KICK_FM_AMOUNT);
            ui_page_template_tone_set_subpage(1U, "SWEEP", PARAM_DRUM_FM_KICK_PITCH_SWEEP, PARAM_DRUM_FM_KICK_SWEEP_DECAY, PARAM_DRUM_FM_KICK_MOD_DECAY, PARAM_COUNT);
            ui_page_template_tone_set_subpage(2U, "RATIO", PARAM_DRUM_FM_KICK_RATIO_MODE, PARAM_DRUM_FM_KICK_RATIO_INDEX, PARAM_DRUM_FM_KICK_MOD_ENV_SYNC, PARAM_COUNT);
            ui_page_template_tone_set_subpage(3U, "COLOR", PARAM_DRUM_FM_KICK_FEEDBACK, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT);
            break;
        case UI_TRACK_TYPE_DRUM_FM_SNARE:
            g_ui_template_tone_family_drum.nav_labels[0] = "OSC";
            g_ui_template_tone_family_drum.nav_labels[1] = "NOISE";
            g_ui_template_tone_family_drum.nav_labels[2] = "MOD";
            ui_page_template_tone_set_subpage(0U, "OSC", PARAM_DRUM_FM_SNARE_PITCH, PARAM_DRUM_FM_SNARE_DECAY, PARAM_DRUM_FM_SNARE_MOD_FREQ, PARAM_DRUM_FM_SNARE_FM_AMOUNT);
            ui_page_template_tone_set_subpage(1U, "NOISE", PARAM_DRUM_FM_SNARE_NOISE, PARAM_DRUM_FM_SNARE_NOISE_DECAY, PARAM_DRUM_FM_SNARE_HP_TONE, PARAM_COUNT);
            ui_page_template_tone_set_subpage(2U, "MOD", PARAM_DRUM_FM_SNARE_MOD_DECAY, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT);
            break;
        case UI_TRACK_TYPE_DRUM_FM_TOM:
            g_ui_template_tone_family_drum.nav_labels[0] = "OSC";
            g_ui_template_tone_family_drum.nav_labels[1] = "SWEEP";
            ui_page_template_tone_set_subpage(0U, "OSC", PARAM_DRUM_FM_TOM_PITCH, PARAM_DRUM_FM_TOM_DECAY, PARAM_DRUM_FM_TOM_MOD_FREQ, PARAM_DRUM_FM_TOM_FM_AMOUNT);
            ui_page_template_tone_set_subpage(1U, "SWEEP", PARAM_DRUM_FM_TOM_PITCH_SWEEP, PARAM_DRUM_FM_TOM_SWEEP_DECAY, PARAM_DRUM_FM_TOM_MOD_DECAY, PARAM_DRUM_FM_TOM_START_PHASE);
            break;
        case UI_TRACK_TYPE_DRUM_FM_RIMSHOT:
            g_ui_template_tone_family_drum.nav_labels[0] = "RIM";
            g_ui_template_tone_family_drum.nav_labels[1] = "BODY";
            g_ui_template_tone_family_drum.nav_labels[2] = "TONE";
            ui_page_template_tone_set_subpage(0U, "RIM", PARAM_DRUM_FM_RIMSHOT_RIM_PITCH, PARAM_DRUM_FM_RIMSHOT_RIM_DECAY, PARAM_DRUM_FM_RIMSHOT_RIM_FM_AMOUNT, PARAM_COUNT);
            ui_page_template_tone_set_subpage(1U, "BODY", PARAM_DRUM_FM_RIMSHOT_BODY_PITCH, PARAM_DRUM_FM_RIMSHOT_BODY_DECAY, PARAM_DRUM_FM_RIMSHOT_BODY_FM_AMOUNT, PARAM_DRUM_FM_RIMSHOT_BODY_MIX);
            ui_page_template_tone_set_subpage(2U, "TONE", PARAM_DRUM_FM_RIMSHOT_HP_TONE, PARAM_DRUM_FM_RIMSHOT_MOD_DECAY, PARAM_COUNT, PARAM_COUNT);
            break;
        case UI_TRACK_TYPE_DRUM_FM_CLAP:
            g_ui_template_tone_family_drum.nav_labels[0] = "TIME";
            g_ui_template_tone_family_drum.nav_labels[1] = "TONE";
            g_ui_template_tone_family_drum.nav_labels[2] = "COLOR";
            ui_page_template_tone_set_subpage(0U, "TIME", PARAM_DRUM_FM_CLAP_CLAP_COUNT, PARAM_DRUM_FM_CLAP_CLAP_SPACING, PARAM_DRUM_FM_CLAP_CLAP_DECAY, PARAM_DRUM_FM_CLAP_TAIL_DECAY);
            ui_page_template_tone_set_subpage(1U, "TONE", PARAM_DRUM_FM_CLAP_BASE_FREQ, PARAM_DRUM_FM_CLAP_MOD_FREQ, PARAM_DRUM_FM_CLAP_FM_AMOUNT, PARAM_DRUM_FM_CLAP_MOD_DECAY);
            ui_page_template_tone_set_subpage(2U, "COLOR", PARAM_DRUM_FM_CLAP_HP_TONE, PARAM_DRUM_FM_CLAP_FEEDBACK, PARAM_COUNT, PARAM_COUNT);
            break;
        case UI_TRACK_TYPE_DRUM_FM_COWBELL:
            g_ui_template_tone_family_drum.nav_labels[0] = "BODY";
            g_ui_template_tone_family_drum.nav_labels[1] = "MOD";
            ui_page_template_tone_set_subpage(0U, "BODY", PARAM_DRUM_FM_COWBELL_PITCH, PARAM_DRUM_FM_COWBELL_DECAY_SHORT, PARAM_DRUM_FM_COWBELL_DECAY_LONG, PARAM_DRUM_FM_COWBELL_ENV_MIX);
            ui_page_template_tone_set_subpage(1U, "MOD", PARAM_DRUM_FM_COWBELL_MOD_FREQ, PARAM_DRUM_FM_COWBELL_FM_AMOUNT, PARAM_DRUM_FM_COWBELL_MOD_DECAY, PARAM_DRUM_FM_COWBELL_FEEDBACK);
            break;
        case UI_TRACK_TYPE_DRUM_FM_CYMBAL:
            g_ui_template_tone_family_drum.nav_labels[0] = "BODY";
            g_ui_template_tone_family_drum.nav_labels[1] = "MOD";
            ui_page_template_tone_set_subpage(0U, "BODY", PARAM_DRUM_FM_CYMBAL_DECAY, PARAM_DRUM_FM_CYMBAL_SUSTAIN, PARAM_DRUM_FM_CYMBAL_BASE_CARRIER, PARAM_DRUM_FM_CYMBAL_BASE_MOD);
            ui_page_template_tone_set_subpage(1U, "MOD", PARAM_DRUM_FM_CYMBAL_FM_AMOUNT, PARAM_DRUM_FM_CYMBAL_MOD_DECAY, PARAM_DRUM_FM_CYMBAL_HP_TONE, PARAM_DRUM_FM_CYMBAL_FEEDBACK);
            break;
        default:
            break;
    }
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
