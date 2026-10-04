#include "UI/pages/ui_page_theme_browser.h"

#include <stdio.h>

#include "buttons_ids.h"
#include "Param/param_ids.h"
#include "UI/pages/ui_page_settings.h"
#include "UI/ui_page_manager.h"
#include "UI/ui_renderer_template.h"
#include "UI/ui_template_page.h"
#include "UI/ui_theme.h"

typedef struct
{
    ui_theme_id_t origin;
    ui_theme_id_t preview;
    uint8_t resolved;
} ui_theme_browser_state_t;

static ui_theme_browser_state_t g_theme_browser;

static const ui_template_family_t g_theme_browser_family = {
    .family_title = "FAKE",
    .set_page_index = 0U,
    .set_page_count = 3U,
    .nav_labels = { "RETURN", "LOAD", "", "" },
    .subpages = {
        { .title = "THEME", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "LOAD", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static uint8_t ui_theme_browser_subpage_enabled(uint8_t subpage)
{
    return (subpage < 2U) ? 1U : 0U;
}

static uint8_t ui_theme_browser_slot_text(uint8_t slot,
                                          char *name,
                                          uint32_t name_len,
                                          char *value,
                                          uint32_t value_len)
{
    switch (slot)
    {
        case 0U:
            (void)snprintf(name, name_len, "THEME");
            (void)snprintf(value, value_len, "%s", ui_theme_name(g_theme_browser.preview));
            return 1U;
        case 1U:
            (void)snprintf(name, name_len, "FOCUS");
            (void)snprintf(value, value_len, "SELECT");
            return 1U;
        case 2U:
            (void)snprintf(name, name_len, "VALUE");
            (void)snprintf(value, value_len, "12");
            return 1U;
        case 3U:
            (void)snprintf(name, name_len, "MODE");
            (void)snprintf(value, value_len, "EXT");
            return 1U;
        default:
            return 0U;
    }
}

static ui_template_custom_widget_kind_t ui_theme_browser_widget(
    uint8_t slot, const ui_template_subpage_t *subpage)
{
    (void)subpage;
    if (slot == 2U) return UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_MIDI_CHANNEL;
    if (slot == 3U) return UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_MIDI_SOURCE;
    return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
}

static uint8_t ui_theme_browser_slot_value(
    const ui_param_seq_plock_feedback_frame_t *frame,
    uint8_t slot,
    float *value,
    uint8_t *bipolar,
    uint8_t *inverted)
{
    (void)frame;
    if ((value == NULL) || (bipolar == NULL) || (inverted == NULL)) return 0U;
    *bipolar = 0U;
    *inverted = (slot == 1U) ? 1U : 0U;
    if (slot == 2U) *value = 12.0f;
    else if (slot == 3U) *value = 1.0f;
    else *value = 0.0f;
    return 1U;
}

static ui_template_page_state_t g_theme_browser_page = {
    .family = &g_theme_browser_family,
    .subpage_enabled = ui_theme_browser_subpage_enabled,
    .virtual_custom_widget_picker = ui_theme_browser_widget,
    .virtual_slot_text = ui_theme_browser_slot_text,
    .virtual_slot_value = ui_theme_browser_slot_value,
    .active_subpage = 0U,
    .has_visited = 0U,
    .blank_unavailable_nav_labels = 1U,
};

static void ui_theme_browser_enter(void)
{
    g_theme_browser.origin = ui_theme_get_id();
    g_theme_browser.preview = g_theme_browser.origin;
    g_theme_browser.resolved = 0U;
    g_theme_browser_page.active_subpage = 0U;
    ui_template_page_enter();
}

static void ui_theme_browser_leave(void)
{
    if (g_theme_browser.resolved == 0U)
        ui_theme_preview(g_theme_browser.origin);
    ui_renderer_template_draw_cancel();
}

static uint8_t ui_theme_browser_handle_encoder(uint8_t encoder, int16_t delta)
{
    if ((encoder != 0U) || (delta == 0)) return 1U;
    int32_t next = (int32_t)g_theme_browser.preview + (int32_t)delta;
    const int32_t count = (int32_t)UI_THEME_COUNT;
    next %= count;
    if (next < 0) next += count;
    g_theme_browser.preview = (ui_theme_id_t)next;
    ui_theme_preview(g_theme_browser.preview);
    ui_renderer_template_draw_cancel();
    return 1U;
}

static void ui_theme_browser_return(void)
{
    ui_theme_preview(g_theme_browser.origin);
    g_theme_browser.resolved = 1U;
    ui_page_settings_return_from_theme_browser();
}

static void ui_theme_browser_load(void)
{
    ui_theme_preview(g_theme_browser.preview);
    if (ui_theme_commit_preview() == 0U)
    {
        return;
    }
    g_theme_browser.resolved = 1U;
    ui_page_settings_return_from_theme_browser();
}

static void ui_theme_browser_handle_event(const ui_event_t *event)
{
    if ((event == NULL) || (event->type != UI_EVENT_BUTTON_PRESS)) return;
    if (event->id == (uint8_t)BTN_PAGE_1) ui_theme_browser_return();
    else if (event->id == (uint8_t)BTN_PAGE_2) ui_theme_browser_load();
}

void ui_page_theme_browser_open(void)
{
    ui_page_set(UI_PAGE_THEME_BROWSER);
}

const ui_page_t g_ui_page_theme_browser = {
    .enter = ui_theme_browser_enter,
    .leave = ui_theme_browser_leave,
    .handle_encoder = ui_theme_browser_handle_encoder,
    .handle_event = ui_theme_browser_handle_event,
    .tick = ui_template_page_tick,
    .render = ui_template_page_render,
    .render_pending = ui_template_page_render_pending,
    .render_cancel = ui_template_page_render_cancel,
    .context = &g_theme_browser_page,
};
