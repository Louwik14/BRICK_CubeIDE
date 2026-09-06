#include "pages/ui_page_template_cfg.h"

#include <stdio.h>
#include <string.h>


#include "Track/track_runtime.h"
#include "Track/track_state.h"
#include "Track/polyphony_control.h"
#include "App/control_domain.h"
#include "Track/control_music_output.h"
#include "Track/track_input_ownership.h"
#include "Seq/metronome_control.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "ui_page_manager.h"
#include "ui_template_page.h"
#include "ui_core.h"
#include "ui_track_catalog.h"

static ui_template_page_state_t g_ui_template_cfg_state;

static uint8_t ui_page_template_cfg_has_voices(uint8_t track)
{
    const track_family_t family = ui_get_track_family(track);
    return (uint8_t)((family == TRACK_FAMILY_SYNTH)
        || ((family == TRACK_FAMILY_SAMPLER)
            && (ui_get_track_type(track) == TRACK_TYPE_MULTI)));
}


static const ui_template_family_t g_ui_template_cfg_family = {
    .family_title = "CFG",
    .nav_labels = { "MAIN", "MIDI", "-", "-" },
    .subpages = {
        {
            .title = "TRACK",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "MIDI",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_cfg_synth_family = {
    .family_title = "CFG",
    .nav_labels = { "MAIN", "MIDI", "-", "-" },
    .subpages = {
        { .title = "TRACK", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_CFG_POLY_SPREAD } } },
        { .title = "MIDI", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_cfg_multi_family = {
    .family_title = "CFG",
    .nav_labels = { "MAIN", "MIDI", "-", "-" },
    .subpages = {
        { .title = "TRACK", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_CFG_POLY_SPREAD } } },
        { .title = "MIDI", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_cfg_external_family = {
    .family_title = "CFG",
    .nav_labels = { "MAIN", "MIDI", "-", "-" },
    .subpages = {
        { .title = "TRACK", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "MIDI", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_rec_cfg_family = {
    .family_title = "REC CFG",
    .nav_labels = { "MAIN", "LEN", "-", "-" },
    .subpages = {
        {
            .title = "MAIN",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "LEN",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
    },
    .default_subpage = 0U,
};

static uiw_widget_type_t ui_page_template_cfg_pick_widget(uint8_t slot,
                                                          param_id_t id,
                                                          const char *value_label,
                                                          uiw_widget_type_t suggested_widget)
{
    (void)slot;

    (void)id;(void)value_label;return suggested_widget;
}

static ui_template_custom_widget_kind_t ui_page_template_cfg_pick_custom_widget(uint8_t slot,
                                                                                const ui_template_subpage_t *subpage,
                                                                                param_id_t id)
{
    (void)slot;(void)subpage;(void)id;return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
}

static ui_template_custom_widget_kind_t ui_page_template_cfg_pick_virtual_widget(
    uint8_t slot, const ui_template_subpage_t *subpage)
{
    const uint8_t track = ui_get_active_lane();
    const track_family_t family = ui_get_track_family(track);
    if (subpage == NULL) return UI_TEMPLATE_CUSTOM_WIDGET_NONE;

    if (subpage->title != NULL && strcmp(subpage->title, "MIDI") == 0)
    {
        if (slot == 0U) return UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_MIDI_CHANNEL;
        if (slot == 1U) return UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_MIDI_SOURCE;
        return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
    }

    if (slot == 0U) return UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_TRACK;
    if (family == TRACK_FAMILY_OFF)
    {
        return (slot < 4U) ? UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_INACTIVE
                           : UI_TEMPLATE_CUSTOM_WIDGET_NONE;
    }
    return (slot == 1U) ? UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_TYPE
                        : UI_TEMPLATE_CUSTOM_WIDGET_NONE;
}

static uint8_t ui_page_template_cfg_virtual_slot_value(
    const ui_param_seq_plock_feedback_frame_t *frame_ctx,
    uint8_t slot,
    float *out_value,
    uint8_t *out_bipolar)
{
    (void)frame_ctx;
    if ((out_value == NULL) || (out_bipolar == NULL)) return 0U;
    *out_bipolar = 0U;
    const uint8_t track = ui_get_active_lane();
    const track_family_t family = ui_get_track_family(track);
    if (g_ui_template_cfg_state.active_subpage == 1U)
    {
        if (slot == 0U) *out_value = (float)ui_get_track_midi_channel(track);
        else if (slot == 1U) *out_value = (float)ui_get_track_midi_source(track);
        else return 0U;
        return 1U;
    }
    if (slot == 0U)
    {
        *out_value = (float)family;
        return 1U;
    }
    if (family == TRACK_FAMILY_OFF)
    {
        return (slot < 4U) ? 1U : 0U;
    }
    if (slot == 1U)
    {
        *out_value = (float)ui_track_catalog_type_index_for_family(
            family, ui_get_track_type(track), track, track_state_get_configs());
        return 1U;
    }
    return 0U;
}

static const ui_template_family_t *ui_page_template_cfg_resolve_family(void)
{
    const uint8_t active_track = ui_get_active_lane();
    if (ui_get_track_family(active_track) == TRACK_FAMILY_OFF)
    {
        return &g_ui_template_cfg_family;
    }

    if (ui_get_track_family(active_track) == TRACK_FAMILY_SYNTH)
    {
        return &g_ui_template_cfg_synth_family;
    }

    if ((ui_get_track_family(active_track) == TRACK_FAMILY_SAMPLER)
            && (ui_get_track_type(active_track) == TRACK_TYPE_MULTI))
    {
        return &g_ui_template_cfg_multi_family;
    }

    if (ui_get_track_family(active_track) == TRACK_FAMILY_EXTERNAL)
    {
        return &g_ui_template_cfg_external_family;
    }

    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_CFG);
}

static uint8_t ui_page_template_cfg_virtual_slot_text(uint8_t slot,
                                                       char *out_name,
                                                       uint32_t out_name_len,
                                                       char *out_value,
                                                       uint32_t out_value_len)
{
    const uint8_t track=ui_get_active_lane();const track_family_t family=ui_get_track_family(track);
    if(g_ui_template_cfg_state.active_subpage==1U){if(slot==0U){(void)snprintf(out_name,out_name_len,"MIDI CH");(void)snprintf(out_value,out_value_len,"%u",(unsigned)ui_get_track_midi_channel(track));return 1U;}if(slot==1U){static const char*const labels[]={"INT","EXT","ALL"};(void)snprintf(out_name,out_name_len,"MIDI SRC");(void)snprintf(out_value,out_value_len,"%s",labels[ui_get_track_midi_source(track)]);return 1U;}return 0U;}
    if(slot==0U){(void)snprintf(out_name,out_name_len,"TRACK");(void)snprintf(out_value,out_value_len,"%s",ui_track_catalog_family_display_name(family));return 1U;}
    if(slot==1U){(void)snprintf(out_name,out_name_len,"TYPE");(void)snprintf(out_value,out_value_len,"%s",ui_track_catalog_type_display_name(family,ui_get_track_type(track)));return 1U;}
    if(slot==2U&&family==TRACK_FAMILY_EXTERNAL){const uint8_t input=ui_get_track_external_input(track);const char*label=(input==ENTITY_AUDIO_SOURCE_USB)?"USB":((input==ENTITY_AUDIO_SOURCE_LINE)?"LINE":"?");(void)snprintf(out_name,out_name_len,"INPUT");(void)snprintf(out_value,out_value_len,"%s",label);return 1U;}
    if(slot==2U&&(family==TRACK_FAMILY_SYNTH||(family==TRACK_FAMILY_SAMPLER&&ui_get_track_type(track)==TRACK_TYPE_MULTI))){(void)snprintf(out_name,out_name_len,"VOICES");(void)snprintf(out_value,out_value_len,"%u",(unsigned)polyphony_control_get_voice_count(track));return 1U;}
    return 0U;
}

static ui_template_page_state_t g_ui_template_cfg_state = {
    .family = 0,
    .family_resolver = ui_page_template_cfg_resolve_family,
    .widget_picker = ui_page_template_cfg_pick_widget,
    .custom_widget_picker = ui_page_template_cfg_pick_custom_widget,
    .virtual_custom_widget_picker = ui_page_template_cfg_pick_virtual_widget,
    .virtual_slot_text = ui_page_template_cfg_virtual_slot_text,
    .virtual_slot_value = ui_page_template_cfg_virtual_slot_value,
    .active_subpage = 0U,
    .has_visited = 0U,
};

void ui_page_template_cfg_register_families(void)
{
    for (uint8_t family = 0U; family < (uint8_t)TRACK_FAMILY_COUNT; family++)
    {
        const track_family_t track_family = (track_family_t)family;
        for (uint8_t type = 0U; type < (uint8_t)TRACK_TYPE_COUNT; type++)
        {
            const track_type_t track_type = (track_type_t)type;
            if (!ui_track_type_is_valid_for_family(track_family, track_type))
            {
                continue;
            }

            ui_template_family_register(UI_TEMPLATE_FAMILY_CFG, track_family, track_type, &g_ui_template_cfg_family);
        }
    }
}

const ui_page_t g_ui_page_template_cfg = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_template_page_handle_event,
    .sync_active_context = ui_template_page_sync_active_track_context,
    .render = ui_template_page_render,
    .context = &g_ui_template_cfg_state,
};

uint8_t ui_page_template_cfg_handle_encoder(uint8_t encoder,int16_t delta)
{
    if(ui_page_get_id()!=UI_PAGE_TEMPLATE_CFG||encoder>=4U||delta==0)return 0U;
    const uint8_t track=ui_get_active_lane();
    if(g_ui_template_cfg_state.active_subpage==1U){if(encoder==0U){int32_t v=(int32_t)ui_get_track_midi_channel(track)+((delta>0)?1:-1);if(v<1)v=1;if(v>16)v=16;return ui_set_track_midi_channel(track,(uint8_t)v)?1U:0U;}if(encoder==1U){int32_t v=(int32_t)ui_get_track_midi_source(track)+((delta>0)?1:-1);if(v<0)v=0;if(v>=TRACK_MIDI_SOURCE_COUNT)v=TRACK_MIDI_SOURCE_COUNT-1;return ui_set_track_midi_source(track,(track_midi_source_t)v)?1U:0U;}return 1U;}
    if(encoder==0U){const track_family_t next=ui_track_catalog_cfg_family_step(ui_get_track_family(track),(delta>0)?1:-1,track,track_state_get_configs());return ui_set_track_family(track,next)?1U:0U;}
    if(encoder==1U){const track_family_t family=ui_get_track_family(track);const uint8_t count=ui_track_catalog_type_count_for_family(family,track,track_state_get_configs());if(count==0U)return 1U;int32_t index=(int32_t)ui_track_catalog_type_index_for_family(family,ui_get_track_type(track),track,track_state_get_configs())+((delta>0)?1:-1);if(index<0)index=0;if(index>=count)index=count-1;return ui_set_track_type(track,ui_track_catalog_type_from_family_index(family,(uint8_t)index,track,track_state_get_configs()))?1U:0U;}
    if(encoder==2U&&ui_get_track_family(track)==TRACK_FAMILY_EXTERNAL){const uint8_t current=ui_get_track_external_input(track);const uint8_t next=(delta>0)?((current==ENTITY_AUDIO_SOURCE_LINE)?ENTITY_AUDIO_SOURCE_USB:ENTITY_AUDIO_SOURCE_LINE):((current==ENTITY_AUDIO_SOURCE_USB)?ENTITY_AUDIO_SOURCE_LINE:ENTITY_AUDIO_SOURCE_USB);return ui_set_track_external_input(track,next)?1U:0U;}
    if(encoder==2U&&ui_page_template_cfg_has_voices(track)){int32_t v=(int32_t)polyphony_control_get_voice_count(track)+((delta>0)?1:-1);const uint8_t minimum=control_music_output_count(track);if(v<minimum)v=minimum;if(v>8)v=8;return control_domain_request_polyphony(track,(uint8_t)v);}
    return 0U;
}

static uint8_t ui_page_template_rec_cfg_virtual_slot_text(uint8_t slot,
    char *out_name, uint32_t out_name_len, char *out_value, uint32_t out_value_len);

static ui_template_page_state_t g_ui_template_rec_cfg_state = {
    .family = &g_ui_template_rec_cfg_family,
    .family_resolver = 0,
    .virtual_slot_text = ui_page_template_rec_cfg_virtual_slot_text,
    .active_subpage = 0U,
    .has_visited = 0U,
};

static uint8_t ui_page_template_rec_cfg_virtual_slot_text(uint8_t slot,
    char *out_name, uint32_t out_name_len, char *out_value, uint32_t out_value_len)
{
    static const char *const start_labels[] = { "DEFAULT", "TRIG", "ROLL 1/4", "ROLL 1/2", "ROLL 1" };
    static const char *const sync_labels[] = { "INTERNAL", "MIDI", "USB" };
    if (g_ui_template_rec_cfg_state.active_subpage == 1U)
    {
        if (slot != 0U) return 0U;
        (void)snprintf(out_name,out_name_len,"LEN");
        (void)snprintf(out_value,out_value_len,"%s",seq_runtime_get_rec_len_mode()?"PATTERN":"OVERDUB");
        return 1U;
    }
    if (slot == 0U) { uint8_t v=seq_runtime_get_rec_start_mode(); (void)snprintf(out_name,out_name_len,"START"); (void)snprintf(out_value,out_value_len,"%s",start_labels[(v<5U)?v:0U]); return 1U; }
    if (slot == 1U) { (void)snprintf(out_name,out_name_len,"TEMPO"); (void)snprintf(out_value,out_value_len,"%.1f",(double)seq_runtime_get_tempo_bpm_milli()/1000.0); return 1U; }
    if (slot == 2U) { uint8_t v=(uint8_t)seq_runtime_get_clock_source(); (void)snprintf(out_name,out_name_len,"SYNC"); (void)snprintf(out_value,out_value_len,"%s",sync_labels[(v<3U)?v:0U]); return 1U; }
    if (slot == 3U) { (void)snprintf(out_name,out_name_len,"METRO"); (void)snprintf(out_value,out_value_len,"%u",(unsigned)metronome_control_get_level()); return 1U; }
    return 0U;
}

uint8_t ui_page_template_rec_cfg_handle_encoder(uint8_t encoder, int16_t delta)
{
    if ((ui_page_get_id()!=UI_PAGE_TEMPLATE_REC_CFG)||(encoder>=4U)||(delta==0)) return 0U;
    if (g_ui_template_rec_cfg_state.active_subpage == 1U)
    {
        if (encoder == 0U)
        {
            const control_seq_intent_t intent = {
                .operation = CONTROL_SEQ_SET_RECORD_LENGTH_MODE,
                .step = (delta > 0) ? 1U : 0U
            };
            return control_domain_request_seq(&intent);
        }
        return 1U;
    }
    if (encoder == 0U)
    {
        int32_t v=(int32_t)seq_runtime_get_rec_start_mode()+((delta>0)?1:-1);
        if (v < 0) v = 0;
        if (v > 4) v = 4;
        const control_seq_intent_t intent = {
            .operation = CONTROL_SEQ_SET_RECORD_START_MODE,
            .step = (uint8_t)v
        };
        return control_domain_request_seq(&intent);
    }
    if (encoder == 1U)
    {
        int32_t v=(int32_t)seq_runtime_get_tempo_bpm_milli()+(int32_t)delta*100;
        if (v < 40000) v = 40000;
        if (v > 300000) v = 300000;
        const control_seq_intent_t intent = {
            .operation = CONTROL_SEQ_SET_TEMPO,
            .value32 = (uint32_t)v
        };
        return control_domain_request_seq(&intent);
    }
    if (encoder == 2U)
    {
        int32_t v=(int32_t)seq_runtime_get_clock_source()+((delta>0)?1:-1);
        if (v < 0) v = 0;
        if (v > 2) v = 2;
        const control_seq_intent_t intent = {
            .operation = CONTROL_SEQ_SET_CLOCK_SOURCE,
            .step = (uint8_t)v
        };
        return control_domain_request_seq(&intent);
    }
    {
        int32_t v=(int32_t)metronome_control_get_level()+delta;
        if (v < 0) v = 0;
        if (v > 127) v = 127;
        const control_seq_intent_t intent = {
            .operation = CONTROL_SEQ_SET_METRONOME,
            .step = (uint8_t)v
        };
        return control_domain_request_seq(&intent);
    }
}

const ui_page_t g_ui_page_template_rec_cfg = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_template_page_handle_event,
    .sync_active_context = ui_template_page_sync_active_track_context,
    .render = ui_template_page_render,
    .context = &g_ui_template_rec_cfg_state,
};

void ui_page_template_rec_cfg_open_main(void)
{
    ui_template_page_select_subpage(&g_ui_template_rec_cfg_state, 0U);
}
