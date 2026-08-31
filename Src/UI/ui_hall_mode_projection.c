#include "ui_hall_mode_projection.h"

#include <stdio.h>

#include "Board/board_product.h"
#include "Track/entity_topology.h"
#include "pages/ui_page_patch_assign.h"
#include "stm32h7xx_hal.h"
#include "ui_core_mute.h"
#include "ui_core_pattern.h"
#include "ui_core_runtime_bridge.h"
#include "ui_hall_mode_contract.h"
#include "ui_page_manager.h"

#define UI_HALL_PATCH_FEEDBACK_MS 1000U

static uint32_t g_ui_hall_patch_feedback_until_ms = 0U;

static uint8_t ui_hall_patch_feedback_active(uint32_t now_ms)
{
    return ((int32_t)(g_ui_hall_patch_feedback_until_ms - now_ms) > 0) ? 1U : 0U;
}

void ui_hall_patch_feedback_begin(uint32_t now_ms)
{
    g_ui_hall_patch_feedback_until_ms = now_ms + UI_HALL_PATCH_FEEDBACK_MS;
}

void ui_hall_patch_feedback_end(uint32_t now_ms)
{
    g_ui_hall_patch_feedback_until_ms = now_ms + UI_HALL_PATCH_FEEDBACK_MS;
}

ui_hall_rout_context_t ui_hall_mode_resolve_rout_context(uint8_t track, ui_hall_mode_t raw_mode)
{
    (void)raw_mode;
    if (track >= TRACK_COUNT)
    {
        return UI_HALL_ROUT_CONTEXT_NONE;
    }

    const track_family_t family = ui_get_track_family(track);
    const track_type_t type = ui_get_track_type(track);

    if ((family == TRACK_FAMILY_SAMPLER) && (type == TRACK_TYPE_LOOPER))
    {
        return UI_HALL_ROUT_CONTEXT_SAMPLER_LOOPER;
    }

    return UI_HALL_ROUT_CONTEXT_NONE;
}

ui_hall_mode_effective_view_t ui_hall_mode_resolve_effective_view(uint8_t track, ui_hall_mode_t raw_mode)
{
    switch (raw_mode)
    {
        case UI_HALL_MODE_SEQ:
            return UI_HALL_MODE_VIEW_SEQ;

        case UI_HALL_MODE_KEYBOARD:
            return (entity_topology_has_capability(track, TRACK_CAPABILITY_KEYBOARD) != 0U)
                    ? UI_HALL_MODE_VIEW_KEYBOARD
                    : UI_HALL_MODE_VIEW_SEQ;

        case UI_HALL_MODE_MACRO:
            return UI_HALL_MODE_VIEW_MACRO;

        case UI_HALL_MODE_AUDIO_REC:
            return UI_HALL_MODE_VIEW_AUDIO_REC;

        case UI_HALL_MODE_PATCH:
            return UI_HALL_MODE_VIEW_PATCH;

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
    if (entity_topology_has_capability(track, TRACK_CAPABILITY_KEYBOARD) == 0U)
    {
        return 0U;
    }
    const ui_hall_mode_effective_view_t view = ui_hall_mode_resolve_effective_view(track, raw_mode);
    return (uint8_t)(view == UI_HALL_MODE_VIEW_KEYBOARD);
}

uint8_t ui_hall_is_seq_context(ui_hall_mode_t raw_mode)
{
    if (raw_mode == UI_HALL_MODE_SEQ)
    {
        return 1U;
    }

    const board_product_capabilities_t *const caps = board_product_capabilities();
    if ((caps == 0)
            || (caps->has_step_binary_lanes == 0U)
            || (caps->has_separate_hall_keyboard == 0U))
    {
        return 0U;
    }

    return (uint8_t)(raw_mode == UI_HALL_MODE_KEYBOARD);
}

const char *ui_get_hall_mode_short_label(void)
{
    const uint8_t active_track = ui_get_active_lane();
    const ui_hall_mode_t raw_mode = ui_get_hall_mode();
    ui_macro_overlay_submode_t macro_overlay_submode = UI_MACRO_OVERLAY_SUBMODE_CTRL;

    if (ui_macro_overlay_get_submode(&macro_overlay_submode) != 0U)
    {
        return (macro_overlay_submode == UI_MACRO_OVERLAY_SUBMODE_ASSIGN) ? "M-Assign" : "M-Ctrl";
    }

    if (ui_is_track_modifier_held() != 0U)
    {
        return "TRACK";
    }

    if ((ui_page_patch_assign_is_open() != 0U)
            || (ui_hall_patch_feedback_active(HAL_GetTick()) != 0U))
    {
        return "PATCH";
    }

    if ((ui_page_get_id() == UI_PAGE_MIDI_FX)
            || (ui_page_get_id() == UI_PAGE_AUDIO_FX))
    {
        if (ui_hall_mode_resolve_rout_context(active_track, raw_mode) != UI_HALL_ROUT_CONTEXT_NONE)
        {
            return "ROUT";
        }
        return (ui_page_get_id() == UI_PAGE_AUDIO_FX) ? "FX 2/2" : "FX 1/2";
    }

    const ui_hall_mode_effective_view_t view =
        ui_hall_mode_resolve_effective_view(active_track, raw_mode);
    if (view == UI_HALL_MODE_VIEW_ROUT)
    {
        return "ROUT";
    }

    if (view == UI_HALL_MODE_VIEW_KEYBOARD)
    {
        static char chord_label[8];
        ui_core_runtime_bridge_get_keyboard_chord_label(chord_label, sizeof(chord_label));
        return chord_label;
    }

    return ui_hall_mode_get_base_label(raw_mode);
}

const char *ui_get_hall_mode_suffix_label(void)
{
    static char label[6];
    const uint8_t active_track = ui_get_active_lane();
    const ui_hall_mode_t raw_mode = ui_get_hall_mode();

    if (ui_macro_overlay_is_active() != 0U)
    {
        return "";
    }

    if (ui_is_track_modifier_held() != 0U)
    {
        return "";
    }

    if ((ui_page_patch_assign_is_open() != 0U)
            || (ui_hall_patch_feedback_active(HAL_GetTick()) != 0U))
    {
        return "";
    }

    if (raw_mode == UI_HALL_MODE_SEQ)
    {
        const uint8_t page = ui_core_runtime_bridge_get_seq_edit_page(active_track);
        (void)snprintf(label, sizeof(label), "P%u", (unsigned int)(page + 1U));
        return label;
    }

    if (raw_mode == UI_HALL_MODE_PATTERN)
    {
        return (ui_core_pattern_get_mode() == UI_PATTERN_MODE_STORE) ? "STR" : "RCL";
    }

    if (raw_mode == UI_HALL_MODE_MACRO)
    {
        return "";
    }

    if (raw_mode == UI_HALL_MODE_MUTE)
    {
        const ui_mute_submode_t submode = ui_core_mute_get_submode();
        if (submode == UI_MUTE_SUBMODE_PREPARE)
        {
            return "PRE";
        }

        if (submode == UI_MUTE_SUBMODE_HOLD_QUICK)
        {
            return "HLD";
        }

        return "";
    }

    if (ui_hall_mode_resolve_effective_view(active_track, raw_mode) == UI_HALL_MODE_VIEW_ROUT)
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
