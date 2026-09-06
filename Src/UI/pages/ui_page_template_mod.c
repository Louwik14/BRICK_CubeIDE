#include "pages/ui_page_template_mod.h"

#include <stdio.h>

#include "App/control_domain.h"
#include "Mod/mod_lfo_v1_control.h"
#include "Mod/mod_matrix_control.h"
#include "Mod/mod_destination_control.h"
#include "ui_core.h"
#include "ui_navigation.h"
#include "ui_template_page.h"

static uint8_t g_ui_template_mod_subset = 0U;
static ui_template_page_state_t g_ui_template_mod_state;
static uint8_t ui_page_template_mod_virtual_slot_text(uint8_t slot,char*out_name,
    uint32_t out_name_len,char*out_value,uint32_t out_value_len);

static const ui_template_family_t g_ui_template_mod_family_main = {
    .family_title = "MOD 1/2",
    .nav_labels = { "MATRIX", "LFO 1", "LFO 2", "LFO 3" },
    .subpages = {
        {
            .title = "MATRIX",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "LFO 1",
            .param_bank = { .params = { PARAM_LFO1_RATE, PARAM_LFO1_SHAPE, PARAM_LFO1_PHASE, PARAM_LFO1_TRIG } },
        },
        {
            .title = "LFO 2",
            .param_bank = { .params = { PARAM_LFO2_RATE, PARAM_LFO2_SHAPE, PARAM_LFO2_PHASE, PARAM_LFO2_TRIG } },
        },
        {
            .title = "LFO 3",
            .param_bank = { .params = { PARAM_LFO3_RATE, PARAM_LFO3_SHAPE, PARAM_LFO3_PHASE, PARAM_LFO3_TRIG } },
        },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_mod_family_ops = {
    .family_title = "MOD 2/2",
    .nav_labels = { "MULTI", "SLEW", "-", "-" },
    .subpages = {
        {
            .title = "MULTI",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "SLEW",
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

static const ui_template_family_t *ui_page_template_mod_resolve_family(void)
{
    if (g_ui_template_mod_subset != 0U)
    {
        return &g_ui_template_mod_family_ops;
    }

    return ui_template_family_resolve_effective_for_track(UI_TEMPLATE_FAMILY_MOD,
                                                           ui_get_active_lane(),
                                                           UI_TEMPLATE_EFFECTIVE_SCOPE_CURRENT);
}

static uint8_t ui_page_template_mod_is_lfo_param(param_id_t id, mod_lfo_param_t param)
{
    static const param_id_t lfo_params[MOD_LFO_COUNT_PER_TRACK][MOD_LFO_PARAM_COUNT] = {
        { PARAM_LFO1_RATE, PARAM_LFO1_SHAPE, PARAM_LFO1_TRIG, PARAM_LFO1_PHASE },
        { PARAM_LFO2_RATE, PARAM_LFO2_SHAPE, PARAM_LFO2_TRIG, PARAM_LFO2_PHASE },
        { PARAM_LFO3_RATE, PARAM_LFO3_SHAPE, PARAM_LFO3_TRIG, PARAM_LFO3_PHASE },
    };

    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
    {
        if (lfo_params[lfo][param] == id)
        {
            return 1U;
        }
    }

    return 0U;
}

static ui_template_custom_widget_kind_t ui_page_template_mod_pick_custom_widget(uint8_t slot,
                                                                                const ui_template_subpage_t *subpage,
                                                                                param_id_t id)
{
    (void)subpage;

    if ((slot == 0U) && (ui_page_template_mod_is_lfo_param(id, MOD_LFO_PARAM_RATE) != 0U))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_LFO_RATE;
    }
    if (((slot == 1U) && (ui_page_template_mod_is_lfo_param(id, MOD_LFO_PARAM_SHAPE) != 0U))
            || ((slot == 2U) && (ui_page_template_mod_is_lfo_param(id, MOD_LFO_PARAM_PHASE) != 0U)))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_LFO_SHAPE_PHASE_GROUP;
    }
    return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
}

static uiw_widget_type_t ui_page_template_mod_pick_widget(uint8_t slot,
                                                          param_id_t id,
                                                          const char *value_label,
                                                          uiw_widget_type_t suggested_widget)
{
    (void)slot;
    (void)value_label;

    if (ui_page_template_mod_is_lfo_param(id, MOD_LFO_PARAM_TRIG) != 0U)
    {
        return UIW_WIDGET_ENUM_TEXT;
    }

    return suggested_widget;
}

static uint8_t ui_page_template_mod_param_text(uint8_t slot,
                                               param_id_t id,
                                               float value,
                                               char *out_name,
                                               uint32_t out_name_len,
                                               char *out_value,
                                               uint32_t out_value_len)
{
    (void)slot;
    (void)value;
    if ((out_name == NULL) || (out_name_len == 0U))
    {
        return 0U;
    }

    if (ui_page_template_mod_is_lfo_param(id, MOD_LFO_PARAM_PHASE) != 0U)
    {
        uint8_t lfo_index = MOD_LFO_COUNT_PER_TRACK;
        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            if (g_ui_template_mod_family_main.subpages[lfo + 1U].param_bank.params[2] == id)
            {
                lfo_index = lfo;
                break;
            }
        }
        const uint8_t is_rnd = ((lfo_index < MOD_LFO_COUNT_PER_TRACK)
                && (mod_lfo_v1_shape_is_random(ui_get_active_lane(), lfo_index) != 0U)) ? 1U : 0U;
        (void)snprintf(out_name, out_name_len, "%s", (is_rnd != 0U) ? "Slew" : "Phase");
        if ((out_value != NULL) && (out_value_len > 0U) && (is_rnd != 0U))
        {
            (void)snprintf(out_value, out_value_len, "%u%%", (unsigned int)((value * 100.0f / 360.0f) + 0.5f));
        }
        return 1U;
    }

    return 1U;
}

static const char *ui_page_template_mod_source_label(uint8_t source)
{
    static const char *const labels[]={"OFF","LFO1","LFO2","LFO3","ENV1","ENV2","ENV3","MLT1","MLT2","SLW1","SLW2"};
    return (source<MOD_MATRIX_SOURCE_COUNT)?labels[source]:"OFF";
}

static void ui_page_template_mod_request(uint8_t operation,
                                          uint8_t track,
                                          uint8_t index,
                                          uint8_t input,
                                          float value)
{
    const control_mod_intent_t intent = {
        .operation = operation,
        .track = track,
        .index = index,
        .input = input,
        .value = value
    };
    (void)control_domain_request_mod(&intent);
}

static ui_template_custom_widget_kind_t ui_page_template_mod_pick_virtual_widget(
    uint8_t slot, const ui_template_subpage_t *subpage)
{
    if ((subpage == NULL) || (slot >= 4U)) return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
    if (g_ui_template_mod_subset == 0U
            && g_ui_template_mod_state.active_subpage == 0U)
    {
        static const ui_template_custom_widget_kind_t matrix_widgets[] = {
            UI_TEMPLATE_CUSTOM_WIDGET_MATRIX_SLOT,
            UI_TEMPLATE_CUSTOM_WIDGET_MATRIX_SOURCE,
            UI_TEMPLATE_CUSTOM_WIDGET_LFO_DEST,
            UI_TEMPLATE_CUSTOM_WIDGET_LFO_DEPTH
        };
        return matrix_widgets[slot];
    }
    if (g_ui_template_mod_subset != 0U
            && g_ui_template_mod_state.active_subpage <= 1U)
    {
        return ((slot & 1U) == 0U)
            ? UI_TEMPLATE_CUSTOM_WIDGET_MATRIX_SOURCE
            : UI_TEMPLATE_CUSTOM_WIDGET_LFO_DEPTH;
    }
    return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
}

static uint8_t ui_page_template_mod_virtual_slot_value(
    const ui_param_seq_plock_feedback_frame_t *frame_ctx,
    uint8_t slot,
    float *out_value,
    uint8_t *out_bipolar)
{
    (void)frame_ctx;
    if ((out_value == NULL) || (out_bipolar == NULL) || (slot >= 4U)) return 0U;
    const uint8_t track = ui_get_active_lane();
    *out_bipolar = 0U;
    if (g_ui_template_mod_subset == 0U
            && g_ui_template_mod_state.active_subpage == 0U)
    {
        if (slot == 0U) return mod_matrix_get_selected_slot(track, out_value);
        if (slot == 1U) return mod_matrix_get_selected_slot_source(track, out_value);
        if (slot == 2U) return mod_matrix_get_selected_slot_destination_index(track, out_value);
        *out_bipolar = 1U;
        return mod_matrix_get_selected_slot_depth(track, out_value);
    }
    if (g_ui_template_mod_subset != 0U
            && g_ui_template_mod_state.active_subpage == 0U)
    {
        return mod_matrix_get_multi_source(track, (uint8_t)(slot >> 1U),
                                           (uint8_t)(slot & 1U), out_value);
    }
    if (g_ui_template_mod_subset != 0U
            && g_ui_template_mod_state.active_subpage == 1U)
    {
        const uint8_t op = (uint8_t)(slot >> 1U);
        if ((slot & 1U) == 0U)
            return mod_matrix_get_slew_source(track, op, out_value);
        return mod_matrix_get_slew_amount(track, op, out_value);
    }
    return 0U;
}

static uint8_t ui_page_template_mod_virtual_slot_text(uint8_t slot,char*out_name,
    uint32_t out_name_len,char*out_value,uint32_t out_value_len)
{
    const uint8_t track=ui_get_active_lane();float value=0.0f;
    if(g_ui_template_mod_subset==0U&&g_ui_template_mod_state.active_subpage==0U){
        if(slot==0U){if(!mod_matrix_get_selected_slot(track,&value))return 0U;(void)snprintf(out_name,out_name_len,"SLOT");(void)snprintf(out_value,out_value_len,"%u",(unsigned)((uint8_t)value+1U));return 1U;}
        if(slot==1U){if(!mod_matrix_get_selected_slot_source(track,&value))return 0U;(void)snprintf(out_name,out_name_len,"SOURCE");(void)snprintf(out_value,out_value_len,"%s",ui_page_template_mod_source_label((uint8_t)value));return 1U;}
        if(slot==2U){if(!mod_matrix_get_selected_slot_destination_index(track,&value))return 0U;(void)snprintf(out_name,out_name_len,"DEST");return mod_destination_catalog_label(track,(uint16_t)value,out_value,out_value_len);}
        if(slot==3U){if(!mod_matrix_get_selected_slot_depth(track,&value))return 0U;(void)snprintf(out_name,out_name_len,"DEPTH");(void)snprintf(out_value,out_value_len,"%+.0f",(double)value);return 1U;}}
    if(g_ui_template_mod_subset!=0U&&g_ui_template_mod_state.active_subpage==0U){const uint8_t op=(uint8_t)(slot>>1U),input=(uint8_t)(slot&1U);if(!mod_matrix_get_multi_source(track,op,input,&value))return 0U;(void)snprintf(out_name,out_name_len,"M%u%c",(unsigned)(op+1U),input?'B':'A');(void)snprintf(out_value,out_value_len,"%s",ui_page_template_mod_source_label((uint8_t)value));return 1U;}
    if(g_ui_template_mod_subset!=0U&&g_ui_template_mod_state.active_subpage==1U){const uint8_t op=(uint8_t)(slot>>1U);if((slot&1U)==0U){if(!mod_matrix_get_slew_source(track,op,&value))return 0U;(void)snprintf(out_name,out_name_len,"S%u SRC",(unsigned)(op+1U));(void)snprintf(out_value,out_value_len,"%s",ui_page_template_mod_source_label((uint8_t)value));}else{if(!mod_matrix_get_slew_amount(track,op,&value))return 0U;(void)snprintf(out_name,out_name_len,"S%u AMT",(unsigned)(op+1U));(void)snprintf(out_value,out_value_len,"%u%%",(unsigned)(value*100.0f+0.5f));}return 1U;}
    return 0U;
}

static void ui_page_template_mod_handle_event(const ui_event_t *ev)
{
    if(ev!=0&&ev->type==UI_EVENT_ENCODER&&ev->id<4U&&ev->value!=0){const uint8_t track=ui_get_active_lane();float value=0.0f;const float step=(ev->value>0)?1.0f:-1.0f;
        if(g_ui_template_mod_subset==0U&&g_ui_template_mod_state.active_subpage==0U){if(ev->id==0U){(void)mod_matrix_get_selected_slot(track,&value);ui_page_template_mod_request(CONTROL_MOD_SET_SELECTED_SLOT,track,0U,0U,value+step);}else if(ev->id==1U){(void)mod_matrix_get_selected_slot_source(track,&value);ui_page_template_mod_request(CONTROL_MOD_SET_SELECTED_SOURCE,track,0U,0U,value+step);}else if(ev->id==2U){(void)mod_matrix_get_selected_slot_destination_index(track,&value);ui_page_template_mod_request(CONTROL_MOD_SET_SELECTED_DESTINATION,track,0U,0U,value+step);}else{(void)mod_matrix_get_selected_slot_depth(track,&value);ui_page_template_mod_request(CONTROL_MOD_SET_SELECTED_DEPTH,track,0U,0U,value+(float)ev->value);}return;}
        if(g_ui_template_mod_subset!=0U&&g_ui_template_mod_state.active_subpage==0U){const uint8_t op=(uint8_t)(ev->id>>1U),input=(uint8_t)(ev->id&1U);(void)mod_matrix_get_multi_source(track,op,input,&value);ui_page_template_mod_request(CONTROL_MOD_SET_MULTI_SOURCE,track,op,input,value+step);return;}
        if(g_ui_template_mod_subset!=0U&&g_ui_template_mod_state.active_subpage==1U){const uint8_t op=(uint8_t)(ev->id>>1U);if((ev->id&1U)==0U){(void)mod_matrix_get_slew_source(track,op,&value);ui_page_template_mod_request(CONTROL_MOD_SET_SLEW_SOURCE,track,op,0U,value+step);}else{(void)mod_matrix_get_slew_amount(track,op,&value);ui_page_template_mod_request(CONTROL_MOD_SET_SLEW_AMOUNT,track,op,0U,value+(float)ev->value*0.01f);}return;}}
    ui_template_page_handle_event(ev);
}

static ui_template_page_state_t g_ui_template_mod_state = {
    .family = 0,
    .family_resolver = ui_page_template_mod_resolve_family,
    .widget_picker = ui_page_template_mod_pick_widget,
    .custom_widget_picker = ui_page_template_mod_pick_custom_widget,
    .virtual_custom_widget_picker = ui_page_template_mod_pick_virtual_widget,
    .param_text = ui_page_template_mod_param_text,
    .virtual_slot_text = ui_page_template_mod_virtual_slot_text,
    .virtual_slot_value = ui_page_template_mod_virtual_slot_value,
    .active_subpage = 0U,
    .has_visited = 0U,
};

void ui_page_template_mod_open_primary(void)
{
    g_ui_template_mod_subset = 0U;
    g_ui_template_mod_state.navigation_subset = 0U;
    ui_template_page_select_subpage(&g_ui_template_mod_state, 0U);
}

void ui_page_template_mod_toggle_subset(void)
{
    g_ui_template_mod_subset = (g_ui_template_mod_subset == 0U) ? 1U : 0U;
    g_ui_template_mod_state.navigation_subset = g_ui_template_mod_subset;
    ui_navigation_restore_current_template_subpage();
}

void ui_page_template_mod_register_families(void)
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

            ui_template_family_register(UI_TEMPLATE_FAMILY_MOD,
                                        track_family,
                                        track_type,
                                        &g_ui_template_mod_family_main);
        }
    }
}

const ui_page_t g_ui_page_template_mod = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_page_template_mod_handle_event,
    .sync_active_context = ui_template_page_sync_active_track_context,
    .render = ui_template_page_render,
    .context = &g_ui_template_mod_state,
};
