#include "pages/ui_page_midi_fx.h"

#include "Audio/audio_fx_runtime.h"
#include "Audio/audio_waveform_capture.h"
#include "Core/track_runtime.h"
#include "NoteFx/note_fx_state.h"
#include "Seq/seq_division_catalog.h"
#include "drv_display.h"
#include "font.h"
#include "ui_core_runtime_bridge.h"
#include "ui_param.h"
#include "ui_template_page.h"

#include <stdio.h>

static const ui_template_family_t g_ui_template_midi_fx_family = {
    .family_title = "FX",
    .nav_labels = { "MIDI FX 1", "MIDI FX 2", "MIDI FX 3", "AUDIO" },
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
            .title = "AUDIO",
            .param_bank = { .params = { PARAM_AUDIO_FX_P1, PARAM_AUDIO_FX_P2, PARAM_AUDIO_FX_P3, PARAM_AUDIO_FX_MODEL } },
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

    return (subpage_index < 4U) ? 1U : 0U;
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

static uint8_t ui_page_midi_fx_audio_subpage_active(void)
{
    const ui_template_subpage_t *const subpage =
        ui_template_page_get_active_subpage(&g_ui_template_midi_fx_state);
    return (uint8_t)((subpage != NULL)
        && (subpage->param_bank.params[0] == PARAM_AUDIO_FX_P1)
        && (subpage->param_bank.params[1] == PARAM_AUDIO_FX_P2)
        && (subpage->param_bank.params[2] == PARAM_AUDIO_FX_P3)
        && (subpage->param_bank.params[3] == PARAM_AUDIO_FX_MODEL));
}

static void ui_page_midi_fx_sync_waveform_capture(void)
{
    ui_template_edit_context_t context;
    if ((ui_page_midi_fx_audio_subpage_active() == 0U)
            || (ui_template_edit_context_resolve_active(&context) == 0U))
    {
        audio_waveform_capture_disable();
        return;
    }

    audio_waveform_capture_set_entity(
        (brick_entity_id_t)context.selected_entity);
    uint8_t fast_refresh = 0U;
    for (uint8_t slot = 0U; slot < 3U; ++slot)
    {
        const param_id_t id = (param_id_t)(PARAM_AUDIO_FX_P1 + slot);
        if (ui_param_is_user_tweak_active(slot, id) != 0U)
        {
            fast_refresh = 1U;
            break;
        }
    }
    audio_waveform_capture_set_fast_refresh(fast_refresh);
}

static ui_template_custom_widget_kind_t ui_page_midi_fx_pick_custom_widget(
    uint8_t slot,
    const ui_template_subpage_t *subpage,
    param_id_t id)
{
    if ((subpage != NULL)
            && (slot < 3U)
            && (id == ((slot == 0U) ? PARAM_AUDIO_FX_P1
                      : (slot == 1U) ? PARAM_AUDIO_FX_P2 : PARAM_AUDIO_FX_P3))
            && (subpage->param_bank.params[0] == PARAM_AUDIO_FX_P1)
            && (subpage->param_bank.params[1] == PARAM_AUDIO_FX_P2)
             && (subpage->param_bank.params[2] == PARAM_AUDIO_FX_P3)
            && (subpage->param_bank.params[3] == PARAM_AUDIO_FX_MODEL))
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

    if ((id != PARAM_AUDIO_FX_P1) && (id != PARAM_AUDIO_FX_P2)
            && (id != PARAM_AUDIO_FX_P3))
    {
        return 1U;
    }

    const uint8_t model = (uint8_t)(ui_param_get_active_track_display_value(
        PARAM_AUDIO_FX_MODEL, ui_get_active_lane()) + 0.5f);
    brick_entity_id_t entity = (brick_entity_id_t)ui_get_active_lane();
    ui_template_edit_context_t context;
    if (ui_template_edit_context_resolve_active(&context) != 0U)
        entity = (brick_entity_id_t)context.selected_entity;
    const uint8_t pre_supported = audio_fx_runtime_pre_filter_supported(entity);
    const char *label = NULL;
    if (id == PARAM_AUDIO_FX_P1)
    {
        label = (model == 1U) ? "BIT" : (model == 2U) ? "FOLD"
            : (model == 3U) ? "DRIVE" : (model == 5U) ? "AMOUNT" : "P1";
        if (model == 8U || model == 11U) label = "SUB";
        else if (model == 10U) label = "FREQ";
    }
    else if (id == PARAM_AUDIO_FX_P2)
    {
        label = (model == 1U) ? "SRR" : (model == 2U) ? "BIAS"
            : (model == 3U) ? "INPUT" : (model == 5U) ? "POINT" : "P2";
        if (model == 8U || model == 11U) label = "TONE";
        else if (model == 10U){static const char *const w[]={"SINE","TRI","SAW","SQUARE"};label="WAVE";if(out_value&&out_value_len)(void)snprintf(out_value,out_value_len,"%s",w[audio_fx_ring_wave_index_from_control((uint8_t)(value*127.0f+0.5f))]);}
    }
    else
    {
        if(model==2U) label="XMOD";
        else if (model == 8U || model == 11U)
            label = "MIX";
        else if (model == 10U)
        {
            static const char *const m[]={"CLASSIC","DIGITAL","ANALOG","XOR","CMP","ANLG-L1"};label = "MODEL";
            if ((out_value != NULL) && (out_value_len > 0U))
                (void)snprintf(out_value,out_value_len,"%s",m[audio_fx_ring_model_index_from_control((uint8_t)(value+0.5f))]);
        }
        else if ((model >= 7U) && (model <= 12U))
        {
            label = "-";
            if ((out_value != NULL) && (out_value_len > 0U))
                (void)snprintf(out_value,out_value_len,"-");
        }
        else if (model == 5U)
        {
            label = "SPEED";
        }
        else
        {
            if (model == 3U)
            {
                label="LEVEL";
            }
            static const char *const engine_labels[] = {
                "SOFT", "MID", "HARD"
            };
            if (model == 1U)
            {
                const uint8_t engine =
                    audio_fx_lofi_model_index_from_control(
                        (uint8_t)((value <= 0.0f) ? 0.0f
                            : (value >= 127.0f) ? 127.0f : value + 0.5f));
                label = "ENG";
                if ((out_value != NULL) && (out_value_len > 0U))
                    (void)snprintf(out_value, out_value_len, "%s",
                                   engine_labels[engine]);
                (void)snprintf(out_name, out_name_len, "%s", label);
                return 1U;
            }
            if (model != 3U)
            {
                const uint8_t effective_post = (uint8_t)((value >= 64.0f)
                    || (pre_supported == 0U));
                label = (effective_post != 0U) ? "POST" : "PRE";
                if (out_value != NULL && out_value_len > 0U)
                    (void)snprintf(out_value,out_value_len,"%s",effective_post?"POST":"PRE");
            }
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
                           value / 127.0f);
    }
    else if ((model == 3U) && (out_value != NULL) && (out_value_len > 0U))
    {
        if (id == PARAM_AUDIO_FX_P2)
            (void)snprintf(out_value, out_value_len, "%+.1f dB",
                           (value * 24.0f) - 12.0f);
        else if (id == PARAM_AUDIO_FX_P3)
        {
            const float db = (value <= 64.0f)
                ? -12.0f + value * (12.0f / 64.0f)
                : (value - 64.0f) * (6.0f / 63.0f);
            (void)snprintf(out_value, out_value_len, "%+.1f dB", db);
        }
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
    audio_waveform_capture_disable();
    ui_template_page_leave();
}

static void ui_page_midi_fx_handle_event(const ui_event_t *ev)
{
    if ((ev != 0) && (ev->type == UI_EVENT_ENCODER)
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
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const uint8_t column = (uint8_t)(track & 3U);
        const uint8_t row = (uint8_t)(track >> 2U);
        const uint8_t x = (uint8_t)(2U + (column * 32U));
        const uint8_t y = (uint8_t)(19U + (row * 22U));
        const uint8_t routed = ui_core_runtime_bridge_get_looper_route_enabled(active_track, track);
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
