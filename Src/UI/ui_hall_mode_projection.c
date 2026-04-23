#include "ui_hall_mode_projection.h"

#include <stdio.h>

#include "ui_core_mute.h"
#include "ui_core_pattern.h"
#include "ui_core_runtime_bridge.h"

static uint8_t ui_hall_mode_projection_is_master_buffer(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return 0U;
    }

    return (uint8_t)((ui_get_track_family(track) == UI_TRACK_FAMILY_MASTER)
            && (ui_get_track_type(track) == UI_TRACK_TYPE_BUFFER));
}

ui_hall_mode_effective_view_t ui_hall_mode_resolve_effective_view(uint8_t track, ui_hall_mode_t raw_mode)
{
    switch (raw_mode)
    {
        case UI_HALL_MODE_SEQ:
            return UI_HALL_MODE_VIEW_SEQ;

        case UI_HALL_MODE_KEYBOARD:
            return UI_HALL_MODE_VIEW_KEYBOARD;

        case UI_HALL_MODE_ARP:
            return (ui_hall_mode_projection_is_master_buffer(track) != 0U)
                    ? UI_HALL_MODE_VIEW_ROUT
                    : UI_HALL_MODE_VIEW_ARP;

        case UI_HALL_MODE_PATTERN:
            return UI_HALL_MODE_VIEW_PATTERN;

        case UI_HALL_MODE_MUTE:
            return UI_HALL_MODE_VIEW_MUTE;

        default:
            return UI_HALL_MODE_VIEW_SEQ;
    }
}

uint8_t ui_hall_allows_injection(uint8_t track, ui_hall_mode_t raw_mode)
{
    const ui_hall_mode_effective_view_t view = ui_hall_mode_resolve_effective_view(track, raw_mode);
    return (uint8_t)((view == UI_HALL_MODE_VIEW_KEYBOARD) || (view == UI_HALL_MODE_VIEW_ARP));
}

uint8_t ui_hall_uses_arp_engine(uint8_t track, ui_hall_mode_t raw_mode)
{
    return (uint8_t)(ui_hall_mode_resolve_effective_view(track, raw_mode) == UI_HALL_MODE_VIEW_ARP);
}

uint8_t ui_hall_is_seq_context(ui_hall_mode_t raw_mode)
{
    return (uint8_t)(raw_mode == UI_HALL_MODE_SEQ);
}

const char *ui_get_hall_mode_short_label(void)
{
    const uint8_t active_track = ui_get_active_track();

    if (ui_is_track_modifier_held() != 0U)
    {
        return "TRACK";
    }

    const ui_hall_mode_effective_view_t view =
        ui_hall_mode_resolve_effective_view(active_track, ui_get_hall_mode());
    if (view == UI_HALL_MODE_VIEW_ROUT)
    {
        return "ROUT";
    }

    return ui_hall_mode_get_base_label(ui_get_hall_mode());
}

const char *ui_get_hall_mode_suffix_label(void)
{
    static char label[6];
    const uint8_t active_track = ui_get_active_track();

    if (ui_is_track_modifier_held() != 0U)
    {
        return "";
    }

    if (ui_get_hall_mode() == UI_HALL_MODE_SEQ)
    {
        const uint8_t page = ui_core_runtime_bridge_get_seq_edit_page(active_track);
        (void)snprintf(label, sizeof(label), "P%u", (unsigned int)(page + 1U));
        return label;
    }

    if (ui_get_hall_mode() == UI_HALL_MODE_PATTERN)
    {
        return (ui_core_pattern_get_mode() == UI_PATTERN_MODE_STORE) ? "STR" : "RCL";
    }

    if (ui_get_hall_mode() == UI_HALL_MODE_MUTE)
    {
        if (ui_core_mute_get_submode() == UI_MUTE_SUBMODE_PREPARE)
        {
            return "PRE";
        }

        if (ui_core_mute_get_submode() == UI_MUTE_SUBMODE_HOLD_QUICK)
        {
            return "HLD";
        }

        return "";
    }

    if (ui_hall_mode_resolve_effective_view(active_track, ui_get_hall_mode()) == UI_HALL_MODE_VIEW_ROUT)
    {
        return "";
    }

    const int8_t octave_shift = ui_core_runtime_bridge_get_keyboard_octave_shift();
    if (octave_shift == 0)
    {
        return "";
    }

    (void)snprintf(label, sizeof(label), "%+d", (int)octave_shift);
    return label;
}
