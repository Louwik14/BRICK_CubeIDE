#include "pages/ui_page_midi_fx.h"

#include "Core/track_runtime.h"
#include "NoteFx/note_fx_state.h"
#include "Seq/seq_division_catalog.h"
#include "ui_template_page.h"

#include <stdio.h>

static const ui_template_family_t g_ui_template_midi_fx_family = {
    .family_title = "MIDI FX",
    .nav_labels = { "SLOT 1", "SLOT 2", "SLOT 3", "-" },
    .subpages = {
        {
            .title = "SLOT 1",
            .param_bank = { .params = { PARAM_MIDI_FX_S1_PARAM1, PARAM_MIDI_FX_S1_PARAM2, PARAM_MIDI_FX_S1_PARAM3, PARAM_MIDI_FX_S1_MODEL } },
        },
        {
            .title = "SLOT 2",
            .param_bank = { .params = { PARAM_MIDI_FX_S2_PARAM1, PARAM_MIDI_FX_S2_PARAM2, PARAM_MIDI_FX_S2_PARAM3, PARAM_MIDI_FX_S2_MODEL } },
        },
        {
            .title = "SLOT 3",
            .param_bank = { .params = { PARAM_MIDI_FX_S3_PARAM1, PARAM_MIDI_FX_S3_PARAM2, PARAM_MIDI_FX_S3_PARAM3, PARAM_MIDI_FX_S3_MODEL } },
        },
        {
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
    },
    .default_subpage = 0U,
};

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

    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_MIDI_FX);
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

    return (subpage_index < NOTE_FX_SLOT_COUNT) ? 1U : 0U;
}

static uint8_t ui_page_midi_fx_virtual_slot_text(uint8_t slot,
                                                  char *out_name,
                                                  uint32_t out_name_len,
                                                  char *out_value,
                                                  uint32_t out_value_len);

static ui_template_page_state_t g_ui_template_midi_fx_state = {
    .family = 0,
    .family_resolver = ui_page_midi_fx_resolve_family,
    .subpage_enabled = ui_page_midi_fx_subpage_enabled,
    .virtual_slot_text = ui_page_midi_fx_virtual_slot_text,
    .active_subpage = 0U,
    .has_visited = 0U,
};

static uint8_t ui_page_midi_fx_virtual_slot_text(uint8_t slot,
                                                       char *out_name,
                                                       uint32_t out_name_len,
                                                       char *out_value,
                                                       uint32_t out_value_len)
{
    static const char *const style_labels[] = { "ORDER", "UP", "DOWN", "UP/DOWN", "RANDOM" };
    static const char *const arp_names[] = { "RATE", "STYLE", "RANGE", "MODEL" };
    static const char *const euclid_names[] = { "LENGTH", "PULSE", "DIV", "MODEL" };
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

static void ui_page_midi_fx_handle_event(const ui_event_t *ev)
{
    if ((ev != 0) && (ev->type == UI_EVENT_ENCODER) && (ev->id < 3U))
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
}

void ui_page_template_midi_fx_register_families(void)
{
    for (uint8_t family = 0U; family < (uint8_t)UI_TRACK_FAMILY_COUNT; ++family)
    {
        const ui_track_family_t track_family = (ui_track_family_t)family;
        for (uint8_t type = 0U; type < (uint8_t)UI_TRACK_TYPE_COUNT; ++type)
        {
            const ui_track_type_t track_type = (ui_track_type_t)type;
            if (!ui_track_type_is_valid_for_family(track_family, track_type)
                    || (track_family == UI_TRACK_FAMILY_OFF)
                    || ((track_family == UI_TRACK_FAMILY_SAMPLER) && (track_type == UI_TRACK_TYPE_LOOPER)))
            {
                continue;
            }

            ui_template_family_register(UI_TEMPLATE_FAMILY_MIDI_FX,
                                        track_family,
                                        track_type,
                                        &g_ui_template_midi_fx_family);
        }
    }
}

const ui_page_t g_ui_page_midi_fx = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_page_midi_fx_handle_event,
    .tick = ui_template_page_tick,
    .sync_active_context = ui_template_page_sync_active_track_context,
    .render = ui_template_page_render,
    .context = &g_ui_template_midi_fx_state,
};
