#include "pages/ui_page_patch_assign.h"

#include <stdio.h>
#include <string.h>

#include "App/Hall/hall_engine.h"
#include "buttons.h"
#include "drv_display.h"
#include "font.h"
#include "Storage/patch_sd_bank.h"
#include "Storage/patch_v1.h"
#include "pages/ui_page_name_edit.h"
#include "ui_core.h"
#include "ui_event.h"
#include "ui_page_manager.h"

typedef struct
{
    uint16_t selected_slot;
    uint8_t target_track;
    uint16_t target_mask;
    uint8_t previous_page;
    ui_hall_mode_t previous_hall_mode;
    uint8_t delete_confirm;
    char status[24];
} ui_page_patch_assign_state_t;

static ui_page_patch_assign_state_t g_patch_assign = {
    .selected_slot = 0U,
    .target_track = 0U,
    .target_mask = 1U,
    .previous_page = UI_PAGE_TEMPLATE_CFG,
    .previous_hall_mode = UI_HALL_MODE_SEQ,
    .delete_confirm = 0U,
    .status = { 0 },
};

#define PATCH_ASSIGN_HEADER_LINE_Y 7U
#define PATCH_ASSIGN_FAMILY_BAND_Y 9U
#define PATCH_ASSIGN_FAMILY_BAND_LINE_Y 16U
#define PATCH_ASSIGN_LIST_Y0 22U
#define PATCH_ASSIGN_LIST_PITCH 8U
#define PATCH_ASSIGN_LIST_VISIBLE_LINES 4U
#define PATCH_ASSIGN_SELECT_H 8U
#define PATCH_ASSIGN_FOOTER_LABEL_Y 58U
#define PATCH_ASSIGN_SCROLL_X 126U

typedef enum
{
    PATCH_ASSIGN_FAMILY_ALL = 0,
    PATCH_ASSIGN_FAMILY_SYNTH,
    PATCH_ASSIGN_FAMILY_SAMPLER,
    PATCH_ASSIGN_FAMILY_DRUM,
    PATCH_ASSIGN_FAMILY_INPUT,
    PATCH_ASSIGN_FAMILY_COUNT
} patch_assign_family_filter_t;

typedef enum
{
    PATCH_ASSIGN_TYPE_ALL = 0,
    PATCH_ASSIGN_TYPE_PRISM,
    PATCH_ASSIGN_TYPE_WAVE,
    PATCH_ASSIGN_TYPE_STACK,
    PATCH_ASSIGN_TYPE_DELUGE,
    PATCH_ASSIGN_TYPE_RAM,
    PATCH_ASSIGN_TYPE_STREAM,
    PATCH_ASSIGN_TYPE_MULTI,
    PATCH_ASSIGN_TYPE_LOOPER,
    PATCH_ASSIGN_TYPE_AUDIO,
    PATCH_ASSIGN_TYPE_DRUM_MD,
    PATCH_ASSIGN_TYPE_DRUM_BD_ANALOG,
    PATCH_ASSIGN_TYPE_COUNT
} patch_assign_type_filter_t;

static patch_assign_family_filter_t g_patch_assign_family_filter = PATCH_ASSIGN_FAMILY_ALL;
static patch_assign_type_filter_t g_patch_assign_type_filter = PATCH_ASSIGN_TYPE_ALL;

static void ui_page_patch_assign_cancel_actions(void);
static void ui_page_patch_assign_name_done(ui_page_name_edit_result_t result,
                                           const char *name,
                                           void *user);

static void ui_page_patch_assign_set_status(const char *status)
{
    memset(g_patch_assign.status, 0, sizeof(g_patch_assign.status));
    if (status != 0)
    {
        (void)snprintf(g_patch_assign.status, sizeof(g_patch_assign.status), "%s", status);
    }
}

static void ui_page_patch_assign_fit_label(char *out,
                                           uint32_t out_size,
                                           const char *in,
                                           uint8_t max_px)
{
    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    memset(out, 0, out_size);
    if (in == 0)
    {
        return;
    }

    (void)snprintf(out, out_size, "%s", in);
    drv_display_set_font(&FONT_4X6);
    if (drv_display_text_width(out) <= max_px)
    {
        return;
    }

    const uint32_t len = strlen(out);
    if (len <= 1U)
    {
        return;
    }

    for (uint32_t keep = len - 1U; keep > 0U; --keep)
    {
        if ((keep + 1U) >= out_size)
        {
            continue;
        }
        out[keep] = '~';
        out[keep + 1U] = '\0';
        if (drv_display_text_width(out) <= max_px)
        {
            return;
        }
        out[keep] = '\0';
    }
}

static void ui_page_patch_assign_draw_centered_label(uint8_t x,
                                                     uint8_t w,
                                                     uint8_t y,
                                                     const char *label)
{
    if ((label == 0) || (w == 0U))
    {
        return;
    }

    drv_display_set_font(&FONT_4X6);
    const uint8_t text_w = drv_display_text_width(label);
    const uint8_t text_x = (text_w >= w) ? x : (uint8_t)(x + ((w - text_w) / 2U));
    drv_display_draw_text(text_x, y, label);
}

static const char *ui_page_patch_assign_family_filter_label(patch_assign_family_filter_t filter)
{
    switch (filter)
    {
        case PATCH_ASSIGN_FAMILY_ALL: return "ALL";
        case PATCH_ASSIGN_FAMILY_INPUT: return "INPUT";
        case PATCH_ASSIGN_FAMILY_SYNTH: return "SYNTH";
        case PATCH_ASSIGN_FAMILY_SAMPLER: return "SAMPLER";
        case PATCH_ASSIGN_FAMILY_DRUM: return "DRUM";
        default: return "ALL";
    }
}

static const char *ui_page_patch_assign_type_filter_label(patch_assign_type_filter_t filter)
{
    switch (filter)
    {
        case PATCH_ASSIGN_TYPE_ALL: return "ALL";
        case PATCH_ASSIGN_TYPE_PRISM: return "PRISM";
        case PATCH_ASSIGN_TYPE_WAVE: return "WAVE";
        case PATCH_ASSIGN_TYPE_STACK: return "STACK";
        case PATCH_ASSIGN_TYPE_DELUGE: return "DELUGE";
        case PATCH_ASSIGN_TYPE_RAM: return "RAM";
        case PATCH_ASSIGN_TYPE_STREAM: return "STREAM";
        case PATCH_ASSIGN_TYPE_MULTI: return "MULTI";
        case PATCH_ASSIGN_TYPE_LOOPER: return "LOOPER";
        case PATCH_ASSIGN_TYPE_AUDIO: return "AUDIO";
        case PATCH_ASSIGN_TYPE_DRUM_MD: return "DRUM MD";
        case PATCH_ASSIGN_TYPE_DRUM_BD_ANALOG: return "BD ANA";
        default: return "ALL";
    }
}

static uint8_t ui_page_patch_assign_family_is_input(ui_track_family_t family)
{
    return ui_track_family_is_input(family) ? 1U : 0U;
}

static patch_assign_family_filter_t ui_page_patch_assign_family_filter_from_track(ui_track_family_t family)
{
    if (ui_page_patch_assign_family_is_input(family) != 0U)
    {
        return PATCH_ASSIGN_FAMILY_INPUT;
    }

    switch (family)
    {
        case UI_TRACK_FAMILY_SYNTH: return PATCH_ASSIGN_FAMILY_SYNTH;
        case UI_TRACK_FAMILY_SAMPLER: return PATCH_ASSIGN_FAMILY_SAMPLER;
        case UI_TRACK_FAMILY_DRUM: return PATCH_ASSIGN_FAMILY_DRUM;
        default: return PATCH_ASSIGN_FAMILY_ALL;
    }
}

static patch_assign_type_filter_t ui_page_patch_assign_type_filter_from_track(ui_track_family_t family,
                                                                              ui_track_type_t type)
{
    switch (ui_page_patch_assign_family_filter_from_track(family))
    {
        case PATCH_ASSIGN_FAMILY_SYNTH:
            switch (type)
            {
                case UI_TRACK_TYPE_PRISM: return PATCH_ASSIGN_TYPE_PRISM;
                case UI_TRACK_TYPE_WAVE: return PATCH_ASSIGN_TYPE_WAVE;
                case UI_TRACK_TYPE_STACK: return PATCH_ASSIGN_TYPE_STACK;
                case UI_TRACK_TYPE_DELUGE: return PATCH_ASSIGN_TYPE_DELUGE;
                default: return PATCH_ASSIGN_TYPE_ALL;
            }

        case PATCH_ASSIGN_FAMILY_SAMPLER:
            switch (type)
            {
                case UI_TRACK_TYPE_RAM: return PATCH_ASSIGN_TYPE_RAM;
                case UI_TRACK_TYPE_STREAM: return PATCH_ASSIGN_TYPE_STREAM;
                case UI_TRACK_TYPE_MULTI: return PATCH_ASSIGN_TYPE_MULTI;
                case UI_TRACK_TYPE_LOOPER: return PATCH_ASSIGN_TYPE_LOOPER;
                default: return PATCH_ASSIGN_TYPE_ALL;
            }

        case PATCH_ASSIGN_FAMILY_INPUT:
            switch (type)
            {
                case UI_TRACK_TYPE_AUDIO: return PATCH_ASSIGN_TYPE_AUDIO;
                default: return PATCH_ASSIGN_TYPE_ALL;
            }

        case PATCH_ASSIGN_FAMILY_DRUM:
            switch (type)
            {
                case UI_TRACK_TYPE_DRUM_MD: return PATCH_ASSIGN_TYPE_DRUM_MD;
                case UI_TRACK_TYPE_DRUM_BD_ANALOG: return PATCH_ASSIGN_TYPE_DRUM_BD_ANALOG;
                default: return PATCH_ASSIGN_TYPE_ALL;
            }

        case PATCH_ASSIGN_FAMILY_ALL:
        default:
            return PATCH_ASSIGN_TYPE_ALL;
    }
}

static uint8_t ui_page_patch_assign_type_filter_allowed(patch_assign_family_filter_t family,
                                                        patch_assign_type_filter_t type)
{
    if (type == PATCH_ASSIGN_TYPE_ALL)
    {
        return 1U;
    }

    switch (family)
    {
        case PATCH_ASSIGN_FAMILY_SYNTH:
            return ((type == PATCH_ASSIGN_TYPE_PRISM)
                    || (type == PATCH_ASSIGN_TYPE_WAVE)
                    || (type == PATCH_ASSIGN_TYPE_STACK)
                    || (type == PATCH_ASSIGN_TYPE_DELUGE)) ? 1U : 0U;

        case PATCH_ASSIGN_FAMILY_SAMPLER:
            return ((type == PATCH_ASSIGN_TYPE_RAM)
                    || (type == PATCH_ASSIGN_TYPE_STREAM)
                    || (type == PATCH_ASSIGN_TYPE_MULTI)
                    || (type == PATCH_ASSIGN_TYPE_LOOPER)) ? 1U : 0U;

        case PATCH_ASSIGN_FAMILY_DRUM:
            return ((type == PATCH_ASSIGN_TYPE_DRUM_MD)
                    || (type == PATCH_ASSIGN_TYPE_DRUM_BD_ANALOG)) ? 1U : 0U;

        case PATCH_ASSIGN_FAMILY_INPUT:
            return (type == PATCH_ASSIGN_TYPE_AUDIO) ? 1U : 0U;

        case PATCH_ASSIGN_FAMILY_ALL:
        default:
            return 0U;
    }
}

static patch_assign_type_filter_t ui_page_patch_assign_type_filter_next(int16_t delta)
{
    patch_assign_type_filter_t list[PATCH_ASSIGN_TYPE_COUNT];
    uint8_t count = 0U;
    list[count++] = PATCH_ASSIGN_TYPE_ALL;

    for (uint8_t raw = 1U; raw < (uint8_t)PATCH_ASSIGN_TYPE_COUNT; ++raw)
    {
        const patch_assign_type_filter_t candidate = (patch_assign_type_filter_t)raw;
        if (ui_page_patch_assign_type_filter_allowed(g_patch_assign_family_filter, candidate) != 0U)
        {
            list[count++] = candidate;
        }
    }

    uint8_t index = 0U;
    for (uint8_t i = 0U; i < count; ++i)
    {
        if (list[i] == g_patch_assign_type_filter)
        {
            index = i;
            break;
        }
    }

    int32_t next = (int32_t)index + (int32_t)delta;
    if (next < 0)
    {
        next = 0;
    }
    if (next >= (int32_t)count)
    {
        next = (int32_t)count - 1;
    }

    return list[next];
}

static uint8_t ui_page_patch_assign_family_matches(ui_track_family_t family)
{
    switch (g_patch_assign_family_filter)
    {
        case PATCH_ASSIGN_FAMILY_ALL:
            return 1U;
        case PATCH_ASSIGN_FAMILY_INPUT:
            return ui_page_patch_assign_family_is_input(family);
        case PATCH_ASSIGN_FAMILY_SYNTH:
            return (family == UI_TRACK_FAMILY_SYNTH) ? 1U : 0U;
        case PATCH_ASSIGN_FAMILY_SAMPLER:
            return (family == UI_TRACK_FAMILY_SAMPLER) ? 1U : 0U;
        case PATCH_ASSIGN_FAMILY_DRUM:
            return (family == UI_TRACK_FAMILY_DRUM) ? 1U : 0U;
        default:
            return 1U;
    }
}

static uint8_t ui_page_patch_assign_type_matches(ui_track_type_t type)
{
    if (g_patch_assign_type_filter == PATCH_ASSIGN_TYPE_ALL)
    {
        return 1U;
    }

    switch (g_patch_assign_type_filter)
    {
        case PATCH_ASSIGN_TYPE_PRISM:
            return (type == UI_TRACK_TYPE_PRISM) ? 1U : 0U;
        case PATCH_ASSIGN_TYPE_WAVE:
            return (type == UI_TRACK_TYPE_WAVE) ? 1U : 0U;
        case PATCH_ASSIGN_TYPE_STACK:
            return (type == UI_TRACK_TYPE_STACK) ? 1U : 0U;
        case PATCH_ASSIGN_TYPE_DELUGE:
            return (type == UI_TRACK_TYPE_DELUGE) ? 1U : 0U;
        case PATCH_ASSIGN_TYPE_RAM:
            return (type == UI_TRACK_TYPE_RAM) ? 1U : 0U;
        case PATCH_ASSIGN_TYPE_STREAM:
            return (type == UI_TRACK_TYPE_STREAM) ? 1U : 0U;
        case PATCH_ASSIGN_TYPE_MULTI:
            return (type == UI_TRACK_TYPE_MULTI) ? 1U : 0U;
        case PATCH_ASSIGN_TYPE_LOOPER:
            return (type == UI_TRACK_TYPE_LOOPER) ? 1U : 0U;
        case PATCH_ASSIGN_TYPE_AUDIO:
            return (type == UI_TRACK_TYPE_AUDIO) ? 1U : 0U;
        case PATCH_ASSIGN_TYPE_DRUM_MD:
            return (type == UI_TRACK_TYPE_DRUM_MD) ? 1U : 0U;
        case PATCH_ASSIGN_TYPE_DRUM_BD_ANALOG:
            return (type == UI_TRACK_TYPE_DRUM_BD_ANALOG) ? 1U : 0U;
        case PATCH_ASSIGN_TYPE_ALL:
        default:
            return 1U;
    }
}

static uint8_t ui_page_patch_assign_meta_matches_filter(const patch_v1_metadata_t *meta)
{
    if (meta == 0)
    {
        return 0U;
    }

    const ui_track_family_t family = (ui_track_family_t)meta->family;
    const ui_track_type_t type = (ui_track_type_t)meta->type;

    return ((ui_page_patch_assign_family_matches(family) != 0U)
            && (ui_page_patch_assign_type_matches(type) != 0U)) ? 1U : 0U;
}

static uint8_t ui_page_patch_assign_slot_visible_in_phase(uint16_t slot, uint8_t phase)
{
    const patch_sd_slot_state_t state = patch_sd_bank_get_slot_state(slot);
    patch_v1_metadata_t meta;

    if (phase == 0U)
    {
        return ((state == PATCH_SD_SLOT_VALID)
                && (patch_sd_bank_get_slot_metadata(slot, &meta) != 0U)
                && (ui_page_patch_assign_meta_matches_filter(&meta) != 0U))
            ? 1U
            : 0U;
    }

    if ((phase == 1U)
            && (g_patch_assign_family_filter == PATCH_ASSIGN_FAMILY_ALL)
            && (g_patch_assign_type_filter == PATCH_ASSIGN_TYPE_ALL))
    {
        return (state == PATCH_SD_SLOT_INVALID) ? 1U : 0U;
    }

    return 0U;
}

static uint8_t ui_page_patch_assign_slot_visible(uint16_t slot)
{
    for (uint8_t phase = 0U; phase < 3U; ++phase)
    {
        if (ui_page_patch_assign_slot_visible_in_phase(slot, phase) != 0U)
        {
            return 1U;
        }
    }
    return 0U;
}

static uint16_t ui_page_patch_assign_visible_count(void)
{
    uint16_t count = 0U;
    for (uint8_t phase = 0U; phase < 3U; ++phase)
    {
        for (uint16_t slot = 0U; slot < PATCH_V1_SLOT_COUNT; ++slot)
        {
            if (ui_page_patch_assign_slot_visible_in_phase(slot, phase) != 0U)
            {
                ++count;
            }
        }
    }
    return count;
}

static uint16_t ui_page_patch_assign_slot_for_view_index(uint16_t index)
{
    uint16_t seen = 0U;
    for (uint8_t phase = 0U; phase < 3U; ++phase)
    {
        for (uint16_t slot = 0U; slot < PATCH_V1_SLOT_COUNT; ++slot)
        {
            if (ui_page_patch_assign_slot_visible_in_phase(slot, phase) == 0U)
            {
                continue;
            }
            if (seen == index)
            {
                return slot;
            }
            ++seen;
        }
    }
    return PATCH_V1_INVALID_SLOT;
}

static uint16_t ui_page_patch_assign_view_index_for_slot(uint16_t selected_slot)
{
    uint16_t seen = 0U;
    for (uint8_t phase = 0U; phase < 3U; ++phase)
    {
        for (uint16_t slot = 0U; slot < PATCH_V1_SLOT_COUNT; ++slot)
        {
            if (ui_page_patch_assign_slot_visible_in_phase(slot, phase) == 0U)
            {
                continue;
            }
            if (slot == selected_slot)
            {
                return seen;
            }
            ++seen;
        }
    }
    return PATCH_V1_INVALID_SLOT;
}

static void ui_page_patch_assign_ensure_visible_selection(void)
{
    if (ui_page_patch_assign_slot_visible(g_patch_assign.selected_slot) != 0U)
    {
        patch_v1_set_current_slot(g_patch_assign.selected_slot);
        return;
    }

    const uint16_t first_slot = ui_page_patch_assign_slot_for_view_index(0U);
    if (first_slot < PATCH_V1_SLOT_COUNT)
    {
        g_patch_assign.selected_slot = first_slot;
        patch_v1_set_current_slot(g_patch_assign.selected_slot);
    }
}

static void ui_page_patch_assign_step_selection(int16_t delta)
{
    const uint16_t count = ui_page_patch_assign_visible_count();
    if (count == 0U)
    {
        ui_page_patch_assign_set_status("NO PATCH");
        return;
    }

    uint16_t index = ui_page_patch_assign_view_index_for_slot(g_patch_assign.selected_slot);
    if (index >= count)
    {
        index = 0U;
    }

    int32_t next = (int32_t)index + (int32_t)delta;
    if (next < 0)
    {
        next = 0;
    }
    if (next >= (int32_t)count)
    {
        next = (int32_t)count - 1;
    }

    const uint16_t slot = ui_page_patch_assign_slot_for_view_index((uint16_t)next);
    if (slot < PATCH_V1_SLOT_COUNT)
    {
        g_patch_assign.selected_slot = slot;
        patch_v1_set_current_slot(g_patch_assign.selected_slot);
        ui_page_patch_assign_set_status(0);
    }
}

static void ui_page_patch_assign_step_family_filter(int16_t delta)
{
    int32_t next = (int32_t)g_patch_assign_family_filter + (int32_t)delta;
    if (next < 0)
    {
        next = 0;
    }
    if (next >= (int32_t)PATCH_ASSIGN_FAMILY_COUNT)
    {
        next = (int32_t)PATCH_ASSIGN_FAMILY_COUNT - 1;
    }
    g_patch_assign_family_filter = (patch_assign_family_filter_t)next;
    if ((g_patch_assign_family_filter == PATCH_ASSIGN_FAMILY_ALL)
            || (ui_page_patch_assign_type_filter_allowed(g_patch_assign_family_filter,
                                                         g_patch_assign_type_filter) == 0U))
    {
        g_patch_assign_type_filter = PATCH_ASSIGN_TYPE_ALL;
    }
    ui_page_patch_assign_cancel_actions();
    ui_page_patch_assign_ensure_visible_selection();
    ui_page_patch_assign_set_status(ui_page_patch_assign_visible_count() == 0U
                                    ? "NO PATCH"
                                    : 0);
}

static void ui_page_patch_assign_step_type_filter(int16_t delta)
{
    g_patch_assign_type_filter = (g_patch_assign_family_filter == PATCH_ASSIGN_FAMILY_ALL)
        ? PATCH_ASSIGN_TYPE_ALL
        : ui_page_patch_assign_type_filter_next(delta);
    ui_page_patch_assign_cancel_actions();
    ui_page_patch_assign_ensure_visible_selection();
    ui_page_patch_assign_set_status(ui_page_patch_assign_visible_count() == 0U
                                    ? "NO PATCH"
                                    : 0);
}

static uint8_t ui_page_patch_assign_slot_valid(uint16_t slot)
{
    return (patch_sd_bank_get_slot_state(slot) == PATCH_SD_SLOT_VALID) ? 1U : 0U;
}

static uint8_t ui_page_patch_assign_selection_is_visible(void)
{
    return ((ui_page_patch_assign_visible_count() != 0U)
            && (ui_page_patch_assign_slot_visible(g_patch_assign.selected_slot) != 0U))
        ? 1U
        : 0U;
}

static void ui_page_patch_assign_cancel_actions(void)
{
    g_patch_assign.delete_confirm = 0U;
}

static uint8_t ui_page_patch_assign_target_count(void)
{
    uint8_t count = 0U;
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        if ((g_patch_assign.target_mask & (uint16_t)(1UL << track)) != 0U)
        {
            ++count;
        }
    }
    return count;
}

static void ui_page_patch_assign_toggle_target(uint8_t track)
{
    if ((track >= UI_ACTIVE_TRACK_COUNT) || (track_topology_is_play(track) == 0U))
    {
        return;
    }

    g_patch_assign.target_mask ^= (uint16_t)(1UL << track);
    ui_page_patch_assign_cancel_actions();
    ui_page_patch_assign_set_status(0);
}

static void ui_page_patch_assign_format_filter(char *out, uint32_t out_size)
{
    const char *family = ui_page_patch_assign_family_filter_label(g_patch_assign_family_filter);
    const char *type = ui_page_patch_assign_type_filter_label(g_patch_assign_type_filter);

    if ((out == 0) || (out_size == 0U))
    {
        return;
    }

    if ((g_patch_assign_family_filter == PATCH_ASSIGN_FAMILY_ALL)
            && (g_patch_assign_type_filter == PATCH_ASSIGN_TYPE_ALL))
    {
        (void)snprintf(out, out_size, "ALL");
        return;
    }

    (void)snprintf(out, out_size, "%s/%s", family, type);
}

static void ui_page_patch_assign_apply_selected(void)
{
    const uint8_t target_count = ui_page_patch_assign_target_count();
    patch_v1_metadata_t meta;

    if (target_count == 0U)
    {
        ui_page_patch_assign_set_status("NO TARGET");
        return;
    }
    if (ui_page_patch_assign_selection_is_visible() == 0U)
    {
        ui_page_patch_assign_set_status("NO PATCH");
        return;
    }
    if (patch_sd_bank_get_slot_metadata(g_patch_assign.selected_slot, &meta) == 0U)
    {
        ui_page_patch_assign_set_status("BAD PATCH");
        return;
    }
    patch_v1_set_current_slot(g_patch_assign.selected_slot);
    uint8_t applied = 0U;
    uint8_t requested = 0U;
    patch_v1_result_t first_error = PATCH_V1_RESULT_OK;
    uint8_t voice_limited = 0U;

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        if ((g_patch_assign.target_mask & (uint16_t)(1UL << track)) == 0U)
        {
            continue;
        }

        ++requested;
        const patch_v1_result_t result =
            patch_v1_apply_slot_to_track(g_patch_assign.selected_slot, track);
        if ((result == PATCH_V1_RESULT_OK) || (result == PATCH_V1_RESULT_VOICE_LIMITED))
        {
            ++applied;
            if (result == PATCH_V1_RESULT_VOICE_LIMITED) voice_limited = 1U;
        }
        else if (first_error == PATCH_V1_RESULT_OK)
        {
            first_error = result;
        }
    }

    if ((requested != 0U) && (applied == requested))
    {
        ui_page_patch_assign_set_status((voice_limited != 0U)
            ? "VOICE LIMITED" : ((applied > 1U) ? "PATCHES APPLIED" : "PATCH APPLIED"));
    }
    else if (applied != 0U)
    {
        char status[24];
        (void)snprintf(status,
                       sizeof(status),
                       "AP %u/%u %s",
                       (unsigned)applied,
                       (unsigned)requested,
                       patch_v1_result_label(first_error));
        ui_page_patch_assign_set_status(status);
    }
    else
    {
        ui_page_patch_assign_set_status(patch_v1_result_label(first_error));
    }
}

static void ui_page_patch_assign_begin_rename(void)
{
    patch_v1_metadata_t meta;
    char name[PATCH_V1_NAME_MAX];
    if (ui_page_patch_assign_selection_is_visible() == 0U)
    {
        ui_page_patch_assign_set_status("NO PATCH");
        return;
    }
    if (ui_page_patch_assign_slot_valid(g_patch_assign.selected_slot) == 0U)
    {
        ui_page_patch_assign_set_status(
            (patch_sd_bank_get_slot_state(g_patch_assign.selected_slot) == PATCH_SD_SLOT_EMPTY)
            ? "EMPTY"
            : "BAD PATCH");
        return;
    }
    if (patch_sd_bank_get_slot_metadata(g_patch_assign.selected_slot, &meta) == 0U)
    {
        ui_page_patch_assign_set_status("BAD PATCH");
        return;
    }

    memset(name, 0, sizeof(name));
    memcpy(name, meta.name, sizeof(name));
    if (name[0] == '\0')
    {
        (void)snprintf(name,
                       sizeof(name),
                       "PATCH %03u",
                       (unsigned)g_patch_assign.selected_slot);
    }
    g_patch_assign.delete_confirm = 0U;

    if (ui_page_name_edit_open(UI_PAGE_PATCH_ASSIGN,
                               "PATCH",
                               "RENAME",
                               name,
                               PATCH_V1_NAME_MAX,
                               ui_page_patch_assign_name_done,
                               0) == 0U)
    {
        ui_page_patch_assign_set_status("ERROR");
    }
}

static void ui_page_patch_assign_name_done(ui_page_name_edit_result_t result,
                                           const char *name,
                                           void *user)
{
    (void)user;
    if (result != UI_PAGE_NAME_EDIT_RESULT_OK)
    {
        ui_page_patch_assign_set_status(0);
        return;
    }

    const patch_v1_result_t rename_result =
        patch_v1_rename_slot(g_patch_assign.selected_slot, name);
    ui_page_patch_assign_ensure_visible_selection();
    ui_page_patch_assign_set_status((rename_result == PATCH_V1_RESULT_OK)
                                    ? "PATCH RENAMED"
                                    : patch_v1_result_label(rename_result));
}

static void ui_page_patch_assign_delete_action(void)
{
    if (ui_page_patch_assign_selection_is_visible() == 0U)
    {
        ui_page_patch_assign_set_status("NO PATCH");
        g_patch_assign.delete_confirm = 0U;
        return;
    }

    if (ui_page_patch_assign_slot_valid(g_patch_assign.selected_slot) == 0U)
    {
        ui_page_patch_assign_set_status(
            (patch_sd_bank_get_slot_state(g_patch_assign.selected_slot) == PATCH_SD_SLOT_EMPTY)
            ? "EMPTY"
            : "BAD PATCH");
        g_patch_assign.delete_confirm = 0U;
        return;
    }

    if (g_patch_assign.delete_confirm == 0U)
    {
        g_patch_assign.delete_confirm = 1U;
        ui_page_patch_assign_set_status("DELETE?");
        return;
    }

    uint16_t next_slot = g_patch_assign.selected_slot;
    const patch_v1_result_t result =
        patch_v1_delete_slot(g_patch_assign.selected_slot, &next_slot);
    if (result == PATCH_V1_RESULT_OK)
    {
        g_patch_assign.selected_slot = next_slot;
        ui_page_patch_assign_ensure_visible_selection();
        ui_page_patch_assign_set_status("PATCH DELETED");
    }
    else
    {
        ui_page_patch_assign_set_status(patch_v1_result_label(result));
    }
    g_patch_assign.delete_confirm = 0U;
}

static void ui_page_patch_assign_enter(void)
{
    patch_v1_set_current_slot(g_patch_assign.selected_slot);
}

static void ui_page_patch_assign_leave(void)
{
    if (ui_get_hall_mode() == UI_HALL_MODE_PATCH)
    {
        ui_set_hall_mode(g_patch_assign.previous_hall_mode);
    }
}

void ui_page_patch_assign_open(uint8_t target_track, ui_hall_mode_t previous_hall_mode)
{
    if (track_topology_is_play(target_track) == 0U)
    {
        target_track = ui_get_active_track();
    }
    if (track_topology_is_play(target_track) == 0U)
    {
        return;
    }

    const uint8_t current_page = ui_page_get_id();
    if (current_page != UI_PAGE_PATCH_ASSIGN)
    {
        g_patch_assign.previous_page = current_page;
    }
    g_patch_assign.previous_hall_mode = previous_hall_mode;
    g_patch_assign.target_track = target_track;
    g_patch_assign.target_mask = (uint16_t)(1UL << target_track);
    ui_page_patch_assign_cancel_actions();
    g_patch_assign_family_filter =
        ui_page_patch_assign_family_filter_from_track(ui_get_track_family(target_track));
    g_patch_assign_type_filter =
        ui_page_patch_assign_type_filter_from_track(ui_get_track_family(target_track),
                                                    ui_get_track_type(target_track));

    const uint16_t current_slot = patch_v1_get_current_slot();
    if (current_slot < PATCH_V1_SLOT_COUNT)
    {
        g_patch_assign.selected_slot = current_slot;
    }
    else
    {
        const uint16_t first_empty = patch_sd_bank_find_first_empty_slot();
        g_patch_assign.selected_slot =
            (first_empty < PATCH_V1_SLOT_COUNT) ? first_empty : 0U;
    }
    ui_page_patch_assign_ensure_visible_selection();

    ui_page_patch_assign_set_status(ui_page_patch_assign_visible_count() == 0U
                                    ? "NO PATCH"
                                    : 0);
    ui_page_set(UI_PAGE_PATCH_ASSIGN);
}

void ui_page_patch_assign_close(void)
{
    if (ui_page_patch_assign_is_open() != 0U)
    {
        ui_page_set(g_patch_assign.previous_page);
    }
}

static void ui_page_patch_assign_handle_event(const ui_event_t *ev)
{
    if (ev == 0)
    {
        return;
    }

    if (ev->type == UI_EVENT_HALL_PRESS)
    {
        ui_page_patch_assign_toggle_target(ev->id);
        return;
    }

    if (ev->type != UI_EVENT_BUTTON_PRESS)
    {
        return;
    }

    switch ((button_id_t)ev->id)
    {
        case BTN_PAGE_1:
            ui_page_patch_assign_close();
            break;

        case BTN_PAGE_2:
            g_patch_assign.delete_confirm = 0U;
            ui_page_patch_assign_apply_selected();
            break;

        case BTN_PAGE_3:
            ui_page_patch_assign_begin_rename();
            break;

        case BTN_PAGE_4:
            ui_page_patch_assign_delete_action();
            break;

        default:
            g_patch_assign.delete_confirm = 0U;
            break;
    }
}

uint8_t ui_page_patch_assign_handle_encoder(uint8_t encoder, int16_t delta)
{
    if (delta == 0)
    {
        return 1U;
    }

    g_patch_assign.delete_confirm = 0U;

    if (encoder == 0U)
    {
        ui_page_patch_assign_step_selection(delta);
        return 1U;
    }

    if (encoder == 1U)
    {
        ui_page_patch_assign_step_family_filter(delta);
        return 1U;
    }

    if (encoder == 2U)
    {
        ui_page_patch_assign_step_type_filter(delta);
        return 1U;
    }

    return 1U;
}

uint8_t ui_page_patch_assign_is_open(void)
{
    return (ui_page_get_id() == UI_PAGE_PATCH_ASSIGN) ? 1U : 0U;
}

uint8_t ui_page_patch_assign_get_target_hall_led(uint8_t hall, uint8_t *out_on)
{
    if ((out_on == 0) || (ui_page_patch_assign_is_open() == 0U) || (hall >= HALL_UI_LANE_COUNT))
    {
        return 0U;
    }

    *out_on = 0U;
    if (hall < UI_ACTIVE_TRACK_COUNT)
    {
        *out_on = ((g_patch_assign.target_mask & (uint16_t)(1UL << hall)) != 0U) ? 1U : 0U;
    }
    return 1U;
}

static void ui_page_patch_assign_draw_row(uint8_t row,
                                          uint16_t slot,
                                          uint8_t selected)
{
    char line[32];
    char fit[32];
    const uint8_t y = (uint8_t)(PATCH_ASSIGN_LIST_Y0 + (row * PATCH_ASSIGN_LIST_PITCH));
    patch_v1_metadata_t meta;
    const patch_sd_slot_state_t state = patch_sd_bank_get_slot_state(slot);
    if ((state == PATCH_SD_SLOT_VALID)
            && (patch_sd_bank_get_slot_metadata(slot, &meta) != 0U))
    {
        const char *family =
            ui_get_track_family_short_name((ui_track_family_t)meta.family);
        const char *type =
            ui_get_track_type_short_name((ui_track_family_t)meta.family,
                                         (ui_track_type_t)meta.type);
        char short_name[13];
        memset(short_name, 0, sizeof(short_name));
        for (uint8_t i = 0U; i < (uint8_t)(sizeof(short_name) - 1U); ++i)
        {
            short_name[i] = meta.name[i];
            if (meta.name[i] == '\0')
            {
                break;
            }
        }
        (void)snprintf(line,
                       sizeof(line),
                       "%-14s %s/%s",
                       short_name,
                       family,
                       type);
    }
    else if (state == PATCH_SD_SLOT_INVALID)
    {
        (void)snprintf(line, sizeof(line), "BAD PATCH");
    }
    else
    {
        (void)snprintf(line, sizeof(line), "EMPTY");
    }

    ui_page_patch_assign_fit_label(fit, sizeof(fit), line, 122U);
    if (selected != 0U)
    {
        drv_display_fill_rect(0U, (uint8_t)(y - 1U), 124U, PATCH_ASSIGN_SELECT_H);
        drv_display_draw_text_inverted(1U, y, fit);
    }
    else
    {
        drv_display_draw_text(1U, y, fit);
    }
}

static void ui_page_patch_assign_draw_header(void)
{
    char filter[32];
    char fit[32];

    drv_display_set_font(&FONT_4X6);
    drv_display_draw_text(0U, 0U, "PATCH");

    drv_display_draw_line(43, 0, 43, 5);
    ui_page_patch_assign_format_filter(filter, sizeof(filter));
    ui_page_patch_assign_fit_label(fit, sizeof(fit), filter, 77U);
    drv_display_draw_text(48U, 0U, fit);
    drv_display_draw_line(0, PATCH_ASSIGN_HEADER_LINE_Y, 127, PATCH_ASSIGN_HEADER_LINE_Y);
}

static void ui_page_patch_assign_draw_family_band(void)
{
    typedef struct
    {
        patch_assign_family_filter_t family;
        const char *label;
        uint8_t x;
        uint8_t w;
    } family_band_item_t;

    static const family_band_item_t k_items[] = {
        { PATCH_ASSIGN_FAMILY_SYNTH, "SYN", 0U, 24U },
        { PATCH_ASSIGN_FAMILY_SAMPLER, "SMP", 25U, 29U },
        { PATCH_ASSIGN_FAMILY_DRUM, "DRM", 55U, 25U },
        { PATCH_ASSIGN_FAMILY_INPUT, "IN", 81U, 20U },
    };

    drv_display_set_font(&FONT_4X6);
    for (uint8_t i = 0U; i < (uint8_t)(sizeof(k_items) / sizeof(k_items[0])); ++i)
    {
        const family_band_item_t *const item = &k_items[i];
        const uint8_t active =
            ((g_patch_assign_family_filter == PATCH_ASSIGN_FAMILY_ALL)
             || (g_patch_assign_family_filter == item->family)) ? 1U : 0U;
        const uint8_t text_w = drv_display_text_width(item->label);
        const uint8_t text_x = (text_w >= item->w)
            ? item->x
            : (uint8_t)(item->x + ((item->w - text_w) / 2U));

        if (active != 0U)
        {
            drv_display_fill_rect(item->x, PATCH_ASSIGN_FAMILY_BAND_Y - 1U, item->w, 7U);
            drv_display_draw_text_inverted(text_x, PATCH_ASSIGN_FAMILY_BAND_Y, item->label);
        }
        else
        {
            drv_display_draw_text(text_x, PATCH_ASSIGN_FAMILY_BAND_Y, item->label);
        }
    }
    drv_display_draw_line(0, PATCH_ASSIGN_FAMILY_BAND_LINE_Y, 127, PATCH_ASSIGN_FAMILY_BAND_LINE_Y);
}

static void ui_page_patch_assign_draw_position(uint16_t selected_view, uint16_t visible_count)
{
    enum
    {
        POS_Y0 = PATCH_ASSIGN_LIST_Y0 - 1U,
        POS_Y1 = 51,
        POS_H = POS_Y1 - POS_Y0,
        POS_CURSOR_MAX_H = 9,
        POS_CURSOR_MIN_H = 3,
        POS_CURSOR_REF_ITEMS = 64
    };

    drv_display_draw_line(PATCH_ASSIGN_SCROLL_X, POS_Y0, PATCH_ASSIGN_SCROLL_X, POS_Y1);
    if (visible_count <= 1U)
    {
        drv_display_draw_pixel(PATCH_ASSIGN_SCROLL_X, POS_Y0, true);
        return;
    }

    uint8_t cursor_h = (uint8_t)((uint32_t)POS_H * PATCH_ASSIGN_LIST_VISIBLE_LINES / visible_count);
    if (cursor_h > POS_CURSOR_MAX_H)
    {
        cursor_h = POS_CURSOR_MAX_H;
    }
    if (cursor_h < POS_CURSOR_MIN_H)
    {
        cursor_h = POS_CURSOR_MIN_H;
    }
    if ((visible_count > POS_CURSOR_REF_ITEMS) && (cursor_h > POS_CURSOR_MIN_H))
    {
        cursor_h = POS_CURSOR_MIN_H;
    }

    const uint8_t travel = (POS_H > cursor_h) ? (uint8_t)(POS_H - cursor_h) : 0U;
    const uint8_t cursor_y = (uint8_t)(POS_Y0
                                       + (((uint32_t)selected_view * travel)
                                          / (visible_count - 1U)));
    drv_display_fill_rect(PATCH_ASSIGN_SCROLL_X, cursor_y, 2U, cursor_h);
}

static void ui_page_patch_assign_draw_footer(void)
{
    ui_page_patch_assign_draw_centered_label(0U, 32U, PATCH_ASSIGN_FOOTER_LABEL_Y, "RETURN");
    ui_page_patch_assign_draw_centered_label(32U, 32U, PATCH_ASSIGN_FOOTER_LABEL_Y, "APPLY");
    ui_page_patch_assign_draw_centered_label(64U, 32U, PATCH_ASSIGN_FOOTER_LABEL_Y, "REN");
    ui_page_patch_assign_draw_centered_label(96U, 32U, PATCH_ASSIGN_FOOTER_LABEL_Y, "DEL");
}

static void ui_page_patch_assign_render(void)
{
    const uint16_t visible_count = ui_page_patch_assign_visible_count();
    uint16_t selected_view = ui_page_patch_assign_view_index_for_slot(g_patch_assign.selected_slot);
    if ((visible_count == 0U) || (selected_view >= visible_count))
    {
        selected_view = 0U;
    }

    ui_page_patch_assign_draw_header();
    ui_page_patch_assign_draw_family_band();

    if (visible_count == 0U)
    {
        drv_display_set_font(&FONT_5X7);
        ui_page_patch_assign_draw_centered_label(0U, 128U, 38U, "NO PATCH");
    }
    else
    {
        uint16_t first_view = 0U;
        if (selected_view > 0U)
        {
            first_view = (uint16_t)(selected_view - 1U);
        }
        if ((visible_count > PATCH_ASSIGN_LIST_VISIBLE_LINES)
                && (first_view > (uint16_t)(visible_count - PATCH_ASSIGN_LIST_VISIBLE_LINES)))
        {
            first_view = (uint16_t)(visible_count - PATCH_ASSIGN_LIST_VISIBLE_LINES);
        }

        drv_display_set_font(&FONT_4X6);
        for (uint8_t row = 0U; row < PATCH_ASSIGN_LIST_VISIBLE_LINES; ++row)
        {
            const uint16_t view_index = (uint16_t)(first_view + row);
            if (view_index >= visible_count)
            {
                break;
            }

            const uint16_t slot = ui_page_patch_assign_slot_for_view_index(view_index);
            if (slot >= PATCH_V1_SLOT_COUNT)
            {
                break;
            }
            ui_page_patch_assign_draw_row(row,
                                          slot,
                                          (slot == g_patch_assign.selected_slot) ? 1U : 0U);
        }
        ui_page_patch_assign_draw_position(selected_view, visible_count);
    }

    if (g_patch_assign.status[0] != '\0')
    {
        drv_display_set_font(&FONT_4X6);
        drv_display_draw_text(0U, 54U, g_patch_assign.status);
    }
    else
    {
        ui_page_patch_assign_draw_footer();
    }
}

const ui_page_t g_ui_page_patch_assign = {
    .enter = ui_page_patch_assign_enter,
    .leave = ui_page_patch_assign_leave,
    .handle_event = ui_page_patch_assign_handle_event,
    .tick = 0,
    .sync_active_context = 0,
    .render = ui_page_patch_assign_render,
    .context = 0,
};
