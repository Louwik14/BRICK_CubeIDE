#include "pages/ui_page_midi_fx.h"

#include "Param/engine_model_catalog.h"
#include "Param/audio_fx_param_catalog.h"
#include "Track/audio_fx_control_state.h"
#include "IPC/control_audio_visual.h"
#include "Track/track_runtime.h"
#include "Track/entity_topology.h"
#include "NoteFx/note_fx_state.h"
#include "Seq/seq_division_catalog.h"
#include "drv_display.h"
#include "font.h"
#include "Track/control_routing.h"
#include "ui_page_manager.h"
#include "ui_param.h"
#include "ui_template_page.h"

#include <stdio.h>

static const ui_template_family_t g_ui_template_midi_fx_family = {
    .family_title = "FX 1/2",
    .nav_labels = { "MIDI FX 1", "MIDI FX 2", "MIDI FX 3", "-" },
    .subpages = {
        {
            .title = "MIDI FX 1",
            .param_bank = { .params = { PARAM_MIDI_FX_S1_PARAM1, PARAM_MIDI_FX_S1_PARAM2, PARAM_MIDI_FX_S1_PARAM3, PARAM_MIDI_FX_S1_MODEL } },
        },
        {
            .title = "MIDI FX 2",
            .param_bank = { .params = { PARAM_MIDI_FX_S2_PARAM1, PARAM_MIDI_FX_S2_PARAM2, PARAM_MIDI_FX_S2_PARAM3, PARAM_MIDI_FX_S2_MODEL } },
        },
        {
            .title = "MIDI FX 3",
            .param_bank = { .params = { PARAM_MIDI_FX_S3_PARAM1, PARAM_MIDI_FX_S3_PARAM2, PARAM_MIDI_FX_S3_PARAM3, PARAM_MIDI_FX_S3_MODEL } },
        },
        {
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_audio_fx_family = {
    .family_title = "FX 2/2",
    .nav_labels = { "FX A", "FX B", "ROUTING", "-" },
    .subpages = {
        { .title = "FX A", .param_bank = { .params = { PARAM_AUDIO_FX_P1, PARAM_AUDIO_FX_P2, PARAM_AUDIO_FX_P3, PARAM_AUDIO_FX_MODEL } } },
        { .title = "FX B", .param_bank = { .params = { PARAM_AUDIO_FX_B_P1, PARAM_AUDIO_FX_B_P2, PARAM_AUDIO_FX_B_P3, PARAM_AUDIO_FX_B_MODEL } } },
        { .title = "ROUTING", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_audio_fx_group_master_family = {
    .family_title = "FX 2/2",
    .nav_labels = { "FX A", "FX B", "SPATIAL", "-" },
    .subpages = {
        { .title = "FX A", .param_bank = { .params = { PARAM_AUDIO_FX_P1, PARAM_AUDIO_FX_P2, PARAM_AUDIO_FX_P3, PARAM_AUDIO_FX_MODEL } } },
        { .title = "FX B", .param_bank = { .params = { PARAM_AUDIO_FX_B_P1, PARAM_AUDIO_FX_B_P2, PARAM_AUDIO_FX_B_P3, PARAM_AUDIO_FX_B_MODEL } } },
        { .title = "SPATIAL", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_audio_fx_group_child_family = {
    .family_title = "FX 2/2",
    .nav_labels = { "LOCAL SENDS", "-", "-", "-" },
    .subpages = {
        { .title = "GROUP FX", .param_bank = { .params = { PARAM_GROUP_FX_A_LEVEL, PARAM_GROUP_FX_B_LEVEL, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static brick_entity_id_t ui_page_audio_fx_selected_entity(void)
{
    ui_template_edit_context_t context;
    if (ui_template_edit_context_resolve_active(&context) != 0U)
        return (brick_entity_id_t)context.selected_entity;
    return (brick_entity_id_t)ui_get_active_lane();
}

static uint8_t ui_page_is_audio_fx_slot_param(param_id_t id)
{
    return (uint8_t)(((id >= PARAM_AUDIO_FX_MODEL)
                      && (id <= PARAM_AUDIO_FX_B_P3)) ? 1U : 0U);
}

static const ui_template_family_t *ui_page_audio_fx_resolve_family(void)
{
    entity_topology_descriptor_t topology;
    if (entity_topology_get(ui_page_audio_fx_selected_entity(), &topology) != 0U)
    {
        if (topology.role == ENTITY_ROLE_GROUP_CHILD)
            return &g_ui_template_audio_fx_group_child_family;
        if (topology.role == ENTITY_ROLE_GROUP_MASTER)
            return &g_ui_template_audio_fx_group_master_family;
    }
    return &g_ui_template_audio_fx_family;
}

static uint8_t ui_page_audio_fx_subpage_enabled(uint8_t subpage_index)
{
    return (ui_page_audio_fx_resolve_family()
            == &g_ui_template_audio_fx_group_child_family)
        ? (uint8_t)(subpage_index == 0U)
        : (uint8_t)(subpage_index < 3U);
}

static const ui_template_family_t g_ui_template_midi_fx_family_rout = {
    .family_title = "ROUT",
    .nav_labels = { "ROUT", "-", "-", "-" },
    .subpages = {
        {
            .title = "ROUT",
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

static const ui_template_family_t *ui_page_midi_fx_resolve_family(void)
{
    const uint8_t active_track = ui_get_active_lane();
    const ui_hall_rout_context_t rout_context =
        ui_hall_mode_resolve_rout_context(active_track, ui_get_hall_mode());

    if (rout_context == UI_HALL_ROUT_CONTEXT_SAMPLER_LOOPER)
    {
        return &g_ui_template_midi_fx_family_rout;
    }

    if (rout_context != UI_HALL_ROUT_CONTEXT_NONE)
    {
        return &g_ui_template_midi_fx_family_rout;
    }

    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_FX);
}

static uint8_t ui_page_midi_fx_subpage_enabled(uint8_t subpage_index)
{
    const ui_hall_rout_context_t rout_context =
        ui_hall_mode_resolve_rout_context(ui_get_active_lane(), ui_get_hall_mode());

    if (rout_context == UI_HALL_ROUT_CONTEXT_SAMPLER_LOOPER)
    {
        return (subpage_index == 0U) ? 1U : 0U;
    }

    if (rout_context != UI_HALL_ROUT_CONTEXT_NONE)
    {
        return (subpage_index == 0U) ? 1U : 0U;
    }

    return (subpage_index < 3U) ? 1U : 0U;
}

static uint8_t ui_page_midi_fx_virtual_slot_text(uint8_t slot,
                                                  char *out_name,
                                                  uint32_t out_name_len,
                                                  char *out_value,
                                                  uint32_t out_value_len);
static ui_template_custom_widget_kind_t ui_page_midi_fx_pick_custom_widget(
    uint8_t slot,
    const ui_template_subpage_t *subpage,
    param_id_t id);
static uint8_t ui_page_midi_fx_param_text(uint8_t slot,
                                          param_id_t id,
                                          float value,
                                          char *out_name,
                                          uint32_t out_name_len,
                                          char *out_value,
                                          uint32_t out_value_len);

static ui_template_page_state_t g_ui_template_midi_fx_state = {
    .family = 0,
    .family_resolver = ui_page_midi_fx_resolve_family,
    .subpage_enabled = ui_page_midi_fx_subpage_enabled,
    .virtual_slot_text = ui_page_midi_fx_virtual_slot_text,
    .custom_widget_picker = ui_page_midi_fx_pick_custom_widget,
    .param_text = ui_page_midi_fx_param_text,
    .active_subpage = 0U,
    .has_visited = 0U,
};

static ui_template_page_state_t g_ui_template_audio_fx_state = {
    .family = 0,
    .family_resolver = ui_page_audio_fx_resolve_family,
    .subpage_enabled = ui_page_audio_fx_subpage_enabled,
    .virtual_slot_text = ui_page_midi_fx_virtual_slot_text,
    .custom_widget_picker = ui_page_midi_fx_pick_custom_widget,
    .param_text = ui_page_midi_fx_param_text,
    .active_subpage = 0U,
    .has_visited = 0U,
};

static ui_template_page_state_t *ui_page_fx_state(void)
{
    return (ui_page_get_id() == UI_PAGE_AUDIO_FX)
        ? &g_ui_template_audio_fx_state : &g_ui_template_midi_fx_state;
}

static uint8_t ui_page_midi_fx_audio_subpage_active(void)
{
    const ui_template_subpage_t *const subpage =
        ui_template_page_get_active_subpage(ui_page_fx_state());
    return (uint8_t)((subpage != NULL)
        && (ui_page_is_audio_fx_slot_param(subpage->param_bank.params[0]) != 0U));
}

static void ui_page_midi_fx_sync_waveform_capture(void)
{
    ui_template_edit_context_t context;
    if ((ui_page_midi_fx_audio_subpage_active() == 0U)
            || (ui_template_edit_context_resolve_active(&context) == 0U))
    {
        (void)control_audio_visual_waveform_request(0U, 0U, 0U);
        return;
    }

    uint8_t fast_refresh = 0U;
    for (uint8_t slot = 0U; slot < 3U; ++slot)
    {
        const ui_template_subpage_t *const subpage =
            ui_template_page_get_active_subpage(ui_page_fx_state());
        const param_id_t id = (subpage != NULL)
            ? subpage->param_bank.params[slot] : PARAM_COUNT;
        if (ui_param_is_user_tweak_active(slot, id) != 0U)
        {
            fast_refresh = 1U;
            break;
        }
    }
    (void)control_audio_visual_waveform_request(
        (brick_entity_id_t)context.selected_entity, 1U, fast_refresh);
}

static ui_template_custom_widget_kind_t ui_page_midi_fx_pick_custom_widget(
    uint8_t slot,
    const ui_template_subpage_t *subpage,
    param_id_t id)
{
    if ((subpage != NULL)
            && (slot < 3U)
            && (id == subpage->param_bank.params[slot])
            && (ui_page_is_audio_fx_slot_param(id) != 0U))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_AUDIO_FX_GROUP;
    }
    return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
}

static uint8_t ui_page_midi_fx_param_text(uint8_t slot,
                                          param_id_t id,
                                          float value,
                                          char *out_name,
                                          uint32_t out_name_len,
                                          char *out_value,
                                          uint32_t out_value_len)
{
    (void)slot;
    if ((out_name == NULL) || (out_name_len == 0U))
    {
        return 0U;
    }

    if ((id == PARAM_GROUP_FX_A_LEVEL) || (id == PARAM_GROUP_FX_B_LEVEL))
    {
        (void)snprintf(out_name, out_name_len, "LEVEL %c",
                       (id == PARAM_GROUP_FX_B_LEVEL) ? 'B' : 'A');
        if ((out_value != NULL) && (out_value_len > 0U))
            (void)snprintf(out_value, out_value_len, "%u %%",
                           (unsigned)(value * 100.0f + 0.5f));
        return 1U;
    }

    const uint8_t slot_b = (uint8_t)((id == PARAM_AUDIO_FX_B_P1)
        || (id == PARAM_AUDIO_FX_B_P2) || (id == PARAM_AUDIO_FX_B_P3));
    const param_id_t display_id = (id == PARAM_AUDIO_FX_B_P1) ? PARAM_AUDIO_FX_P1
        : (id == PARAM_AUDIO_FX_B_P2) ? PARAM_AUDIO_FX_P2
        : (id == PARAM_AUDIO_FX_B_P3) ? PARAM_AUDIO_FX_P3 : id;
    if ((display_id != PARAM_AUDIO_FX_P1) && (display_id != PARAM_AUDIO_FX_P2)
            && (display_id != PARAM_AUDIO_FX_P3))
    {
        return 1U;
    }

    brick_entity_id_t entity = (brick_entity_id_t)ui_get_active_lane();
    ui_template_edit_context_t context;
    if (ui_template_edit_context_resolve_active(&context) != 0U)
        entity = (brick_entity_id_t)context.selected_entity;
    const uint8_t model = (uint8_t)(ui_param_get_active_track_display_value(
        slot_b != 0U ? PARAM_AUDIO_FX_B_MODEL : PARAM_AUDIO_FX_MODEL,
        (uint8_t)entity) + 0.5f);
    const char *label = NULL;
    id = display_id;
    const uint8_t param_index = (uint8_t)(id - PARAM_AUDIO_FX_P1);
    if (audio_fx_param_catalog_resolve(model, param_index, &label) == 0U)
    {
        label = "-";
        if ((out_value != NULL) && (out_value_len > 0U))
            (void)snprintf(out_value, out_value_len, "-");
    }
    if (id == PARAM_AUDIO_FX_P2)
    {
        if (model == AUDIO_FX_MODEL_RING){static const char *const w[]={"SINE","TRI","SAW","SQUARE"};if(out_value&&out_value_len)(void)snprintf(out_value,out_value_len,"%s",w[audio_fx_ring_wave_index_from_control((uint8_t)(value*127.0f+0.5f))]);}
    }
    else if (id == PARAM_AUDIO_FX_P3)
    {
        if (model == AUDIO_FX_MODEL_RING)
        {
            static const char *const m[]={"CLASSIC","DIGITAL","ANALOG","XOR","CMP","ANLG-L1"};
            if ((out_value != NULL) && (out_value_len > 0U))
                (void)snprintf(out_value,out_value_len,"%s",m[audio_fx_ring_model_index_from_control((uint8_t)(value+0.5f))]);
        }
        else if (model == AUDIO_FX_MODEL_LOFI)
        {
            static const char *const engine_labels[] = {
                "SOFT", "MID", "HARD"
            };
            const uint8_t engine =
                audio_fx_lofi_model_index_from_control(
                    (uint8_t)((value <= 0.0f) ? 0.0f
                        : (value >= 127.0f) ? 127.0f : value + 0.5f));
            if ((out_value != NULL) && (out_value_len > 0U))
                (void)snprintf(out_value, out_value_len, "%s",
                               engine_labels[engine]);
            (void)snprintf(out_name, out_name_len, "%s", label);
            return 1U;
        }
    }
    if ((model == 5U) && (out_value != NULL) && (out_value_len > 0U))
    {
        if (id == PARAM_AUDIO_FX_P1)
            (void)snprintf(out_value, out_value_len, "%+.1f dB",
                           (value * 24.0f) - 12.0f);
        else if (id == PARAM_AUDIO_FX_P2)
            (void)snprintf(out_value, out_value_len, "%+.2f",
                           (value * 2.0f) - 1.0f);
        else
            (void)snprintf(out_value, out_value_len, "%.2f",
                           (double)param_value_policy_canonical_to_display(
                               slot_b != 0U ? PARAM_AUDIO_FX_B_P3 : PARAM_AUDIO_FX_P3,
                               (uint8_t)entity, value));
    }
    else if ((model == 3U) && (out_value != NULL) && (out_value_len > 0U))
    {
        if (id == PARAM_AUDIO_FX_P2)
            (void)snprintf(out_value, out_value_len, "%+.1f dB",
                           (value * 24.0f) - 12.0f);
        else if (id == PARAM_AUDIO_FX_P3)
        {
            const float db = param_value_policy_canonical_to_display(
                slot_b != 0U ? PARAM_AUDIO_FX_B_P3 : PARAM_AUDIO_FX_P3,
                (uint8_t)entity, value);
            (void)snprintf(out_value, out_value_len, "%+.2f dB", (double)db);
        }
    }
    else if ((model == AUDIO_FX_MODEL_VIBE)
            && (out_value != NULL) && (out_value_len > 0U))
    {
        if (id == PARAM_AUDIO_FX_P1)
            (void)snprintf(out_value,out_value_len,"%.2f Hz",.01f+11.99f*value);
        else if (id == PARAM_AUDIO_FX_P2)
            (void)snprintf(out_value,out_value_len,"%u %%",(unsigned)(value*100.0f+0.5f));
        else
            (void)snprintf(out_value,out_value_len,"%.2f",(double)value);
    }
    else if ((model == AUDIO_FX_MODEL_DRIFT)
            && (out_value != NULL) && (out_value_len > 0U)
            && (id != PARAM_AUDIO_FX_P3))
    {
        if (id == PARAM_AUDIO_FX_P1)
            (void)snprintf(out_value,out_value_len,"%.2f ms",
                           (double)param_value_policy_canonical_to_display(
                               slot_b != 0U ? PARAM_AUDIO_FX_B_P1 : PARAM_AUDIO_FX_P1,
                               (uint8_t)entity,
                               value));
        else
            (void)snprintf(out_value,out_value_len,"%u",(unsigned)(value*127.0f+0.5f));
    }
    (void)snprintf(out_name, out_name_len, "%s", label);
    return 1U;
}

static uint8_t ui_page_midi_fx_virtual_slot_text(uint8_t slot,
                                                       char *out_name,
                                                       uint32_t out_name_len,
                                                       char *out_value,
                                                       uint32_t out_value_len)
{
    static const char *const style_labels[] = { "ORDER", "UP", "DOWN", "UP/DOWN", "RANDOM" };
    static const char *const arp_names[] = { "RATE", "STYLE", "RANGE", "MODEL" };
    static const char *const euclid_names[] = { "LENGTH", "PULSE", "DIV", "MODEL" };
    if (ui_page_get_id() == UI_PAGE_AUDIO_FX)
    {
        if (g_ui_template_audio_fx_state.active_subpage != 2U) return 0U;
        audio_fx_control_config_t config;
        if (audio_fx_control_state_get(ui_page_audio_fx_selected_entity(),&config)==0U)return 0U;
        static const char *const pos_labels[]={"PRE","MID","POST"};
        static const char *const mode_labels[]={"MONO","STEREO","MID","SIDE"};
        if(ui_page_audio_fx_resolve_family()==&g_ui_template_audio_fx_group_master_family){if(slot>=2U)return 0U;(void)snprintf(out_name,out_name_len,"MODE %c",slot?'B':'A');(void)snprintf(out_value,out_value_len,"%s",mode_labels[config.spatial_mode[slot]]);return 1U;}
        if(slot==0U){(void)snprintf(out_name,out_name_len,"FILTER POS");(void)snprintf(out_value,out_value_len,"%s",pos_labels[config.filter_position]);return 1U;}
        if(slot==1U){(void)snprintf(out_name,out_name_len,"FX ORDER");(void)snprintf(out_value,out_value_len,"%s",config.order==AUDIO_FX_ORDER_B_A?"B>A":"A>B");return 1U;}
        if(slot<4U){const uint8_t fx_slot=(uint8_t)(slot-2U);(void)snprintf(out_name,out_name_len,"MODE %c",fx_slot?'B':'A');(void)snprintf(out_value,out_value_len,"%s",mode_labels[config.spatial_mode[fx_slot]]);return 1U;}
        return 0U;
    }
    const uint8_t page_slot = g_ui_template_midi_fx_state.active_subpage;
    if ((slot >= NOTE_FX_PARAM_COUNT) || (page_slot >= NOTE_FX_SLOT_COUNT))
    {
        return 0U;
    }

    const param_id_t first = (param_id_t)(PARAM_MIDI_FX_S1_PARAM1 + (page_slot * NOTE_FX_PARAM_COUNT));
    float model = 0.0f;
    float value = 0.0f;
    if ((note_fx_state_get_param(ui_get_active_lane(), (param_id_t)(first + 3U), &model) == 0U)
            || (note_fx_state_get_param(ui_get_active_lane(), (param_id_t)(first + slot), &value) == 0U))
    {
        return 0U;
    }

    const uint8_t is_euclid = ((uint8_t)model == NOTE_FX_MODEL_EUCLID) ? 1U : 0U;
    (void)snprintf(out_name, out_name_len, "%s",
                   is_euclid != 0U ? euclid_names[slot] : arp_names[slot]);
    if ((slot < 3U) && ((uint8_t)model == NOTE_FX_MODEL_OFF))
    {
        (void)snprintf(out_value, out_value_len, "-");
        return 1U;
    }
    if (is_euclid != 0U && slot < 2U)
    {
        (void)snprintf(out_value, out_value_len, "%u", (unsigned int)(uint8_t)value);
    }
    else if (is_euclid != 0U && slot == 2U)
    {
        (void)snprintf(out_value, out_value_len, "%s",
                       seq_division_arp_label((uint8_t)value));
    }
    else if (slot == 0U)
    {
        (void)snprintf(out_value, out_value_len, "%s", seq_division_arp_label((uint8_t)value));
    }
    else if (slot == 1U)
    {
        const uint8_t index = ((uint8_t)value < 5U) ? (uint8_t)value : 0U;
        (void)snprintf(out_value, out_value_len, "%s", style_labels[index]);
    }
    else if (slot == 2U)
    {
        (void)snprintf(out_value, out_value_len, "%u", (unsigned int)(uint8_t)value);
    }
    else
    {
        const char *const model_label = ((uint8_t)value == NOTE_FX_MODEL_ARP)
            ? "ARP" : (((uint8_t)value == NOTE_FX_MODEL_EUCLID) ? "EUCLID" : "OFF");
        (void)snprintf(out_value, out_value_len, "%s", model_label);
    }
    return 1U;
}

static void ui_page_midi_fx_enter(void)
{
    ui_template_page_enter();
    ui_page_midi_fx_sync_waveform_capture();
}

static void ui_page_midi_fx_leave(void)
{
    (void)control_audio_visual_waveform_request(0U, 0U, 0U);
    ui_template_page_leave();
}

static void ui_page_midi_fx_handle_event(const ui_event_t *ev)
{
    if ((ui_page_get_id()==UI_PAGE_AUDIO_FX)&&(ev!=0)&&(ev->type==UI_EVENT_ENCODER)
            &&(ev->id<4U)&&(ev->value!=0)&&(g_ui_template_audio_fx_state.active_subpage==2U))
    {
        audio_fx_control_config_t config;const brick_entity_id_t entity=ui_page_audio_fx_selected_entity();
        if(audio_fx_control_state_get(entity,&config)==0U)return;
        const uint8_t group_master=(ui_page_audio_fx_resolve_family()==&g_ui_template_audio_fx_group_master_family)?1U:0U;
        if(group_master!=0U){if(ev->id<2U){int32_t v=(int32_t)config.spatial_mode[ev->id]+((ev->value>0)?1:-1);if(v<0)v=0;if(v>3)v=3;(void)audio_fx_control_set_spatial_mode(entity,(audio_fx_slot_t)ev->id,(uint8_t)v);}return;}
        if(ev->id==0U){int32_t v=(int32_t)config.filter_position+((ev->value>0)?1:-1);if(v<0)v=0;if(v>2)v=2;(void)audio_fx_control_set_filter_position(entity,(audio_fx_filter_pos_t)v);return;}
        if(ev->id==1U){(void)audio_fx_control_set_order(entity,(ev->value>0)?AUDIO_FX_ORDER_B_A:AUDIO_FX_ORDER_A_B);return;}
        {const uint8_t slot=(uint8_t)(ev->id-2U);int32_t v=(int32_t)config.spatial_mode[slot]+((ev->value>0)?1:-1);if(v<0)v=0;if(v>3)v=3;(void)audio_fx_control_set_spatial_mode(entity,(audio_fx_slot_t)slot,(uint8_t)v);return;}
    }
    if ((ui_page_get_id() == UI_PAGE_MIDI_FX)
            && (ev != 0) && (ev->type == UI_EVENT_ENCODER)
            && (ev->id < 3U)
            && (g_ui_template_midi_fx_state.active_subpage < NOTE_FX_SLOT_COUNT))
    {
        const param_id_t model = (param_id_t)(PARAM_MIDI_FX_S1_MODEL
            + (g_ui_template_midi_fx_state.active_subpage * NOTE_FX_PARAM_COUNT));
        float value = 0.0f;
        if ((note_fx_state_get_param(ui_get_active_lane(), model, &value) != 0U)
                && ((uint8_t)value == NOTE_FX_MODEL_OFF))
        {
            return;
        }
    }
    ui_template_page_handle_event(ev);
    ui_page_midi_fx_sync_waveform_capture();
}

static void ui_page_midi_fx_tick(void)
{
    ui_template_page_tick();
    ui_page_midi_fx_sync_waveform_capture();
}

static void ui_page_midi_fx_render(void)
{
    ui_template_page_render();

    const uint8_t active_track = ui_get_active_track();
    if (ui_hall_mode_resolve_rout_context(active_track, ui_get_hall_mode())
            == UI_HALL_ROUT_CONTEXT_NONE)
    {
        return;
    }

    drv_display_clear_rect(0, 16, 128, 48);
    drv_display_set_font(&FONT_5X7);
    for (uint8_t track = 0U; track < TRACK_COUNT; ++track)
    {
        const uint8_t column = (uint8_t)(track & 3U);
        const uint8_t row = (uint8_t)(track >> 2U);
        const uint8_t x = (uint8_t)(2U + (column * 32U));
        const uint8_t y = (uint8_t)(19U + (row * 22U));
        const uint8_t routed = control_routing_get_looper_source(active_track, track);
        char label[4];
        (void)snprintf(label, sizeof(label), "T%u", (unsigned int)(track + 1U));

        if (routed != 0U)
        {
            drv_display_fill_rect(x, y, 28, 18);
            drv_display_draw_text_inverted((uint8_t)(x + 8U), (uint8_t)(y + 5U), label);
        }
        else
        {
            drv_display_draw_rect(x, y, 28, 18);
            drv_display_draw_text((uint8_t)(x + 8U), (uint8_t)(y + 5U), label);
        }
    }
}

void ui_page_template_midi_fx_register_families(void)
{
    for (uint8_t family = 0U; family < (uint8_t)TRACK_FAMILY_COUNT; ++family)
    {
        const track_family_t track_family = (track_family_t)family;
        for (uint8_t type = 0U; type < (uint8_t)TRACK_TYPE_COUNT; ++type)
        {
            const track_type_t track_type = (track_type_t)type;
            if (!ui_track_type_is_valid_for_family(track_family, track_type)
                    || (track_family == TRACK_FAMILY_OFF)
                    || ((track_family == TRACK_FAMILY_SAMPLER) && (track_type == TRACK_TYPE_LOOPER)))
            {
                continue;
            }

            ui_template_family_register(UI_TEMPLATE_FAMILY_FX,
                                        track_family,
                                        track_type,
                                        &g_ui_template_midi_fx_family);
        }
    }
}

const ui_page_t g_ui_page_midi_fx = {
    .enter = ui_page_midi_fx_enter,
    .leave = ui_page_midi_fx_leave,
    .handle_event = ui_page_midi_fx_handle_event,
    .tick = ui_page_midi_fx_tick,
    .sync_active_context = ui_template_page_sync_active_track_context,
    .render = ui_page_midi_fx_render,
    .context = &g_ui_template_midi_fx_state,
};

const ui_page_t g_ui_page_audio_fx = {
    .enter = ui_page_midi_fx_enter,
    .leave = ui_page_midi_fx_leave,
    .handle_event = ui_page_midi_fx_handle_event,
    .tick = ui_page_midi_fx_tick,
    .render = ui_page_midi_fx_render,
    .context = &g_ui_template_audio_fx_state,
};
