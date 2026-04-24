#include "ui_macro_interaction.h"

#include <stddef.h>

#include "App/Hall/hall_engine.h"
#include "Core/track_runtime.h"
#include "Param/param_macro.h"
#include "Param/param_registry.h"
#include "Storage/project_v1.h"
#include "ui_core.h"
#include "ui_page_manager.h"
#include "ui_param.h"

typedef struct
{
    uint8_t armed;
    uint8_t hall;
    uint8_t encoder;
    uint8_t capture_track;
    uint8_t has_param;
    uint8_t has_scene_value;
    param_id_t param;
    float scene_value;
} ui_macro_interaction_state_t;

static ui_macro_interaction_state_t g_ui_macro_interaction;

static uint8_t ui_macro_interaction_is_capture_mode(void)
{
    return (uint8_t)((ui_get_hall_mode() == UI_HALL_MODE_MACRO)
            && (ui_page_get_id() != UI_PAGE_TEMPLATE_MACRO)
            && (project_v1_macro_get_hall_switch_mode() == PROJECT_V1_MACRO_HALL_SWITCH_SLOT));
}

static uint8_t ui_macro_interaction_is_assignable_param(param_id_t param)
{
    const uint8_t active_track = g_ui_macro_interaction.capture_track;
    if (param_macro_slot_target_is_supported(active_track, param) == 0U)
    {
        return 0U;
    }

    return 1U;
}

static uint8_t ui_macro_interaction_resolve_slot_param(param_id_t *out_param)
{
    project_v1_macro_slot_t slot;
    const uint8_t hall = g_ui_macro_interaction.hall;
    const uint8_t bank = project_v1_macro_get_active_bank();
    const uint8_t macro = (uint8_t)(hall / PROJECT_V1_MACRO_SLOT_COUNT);
    const uint8_t slot_index = (uint8_t)(hall % PROJECT_V1_MACRO_SLOT_COUNT);

    if (out_param == NULL)
    {
        return 0U;
    }

    *out_param = PARAM_COUNT;
    if ((g_ui_macro_interaction.armed == 0U) || (ui_macro_interaction_is_capture_mode() == 0U))
    {
        return 0U;
    }

    if (g_ui_macro_interaction.has_param != 0U)
    {
        if (ui_macro_interaction_is_assignable_param(g_ui_macro_interaction.param) == 0U)
        {
            return 0U;
        }

        *out_param = g_ui_macro_interaction.param;
        return 1U;
    }

    if (project_v1_macro_get_slot(bank, macro, slot_index, &slot) == 0U)
    {
        return 0U;
    }

    if ((slot.track == PROJECT_V1_MACRO_SLOT_TRACK_NONE)
            || (slot.param == PROJECT_V1_MACRO_SLOT_PARAM_NONE))
    {
        return 0U;
    }

    if (track_runtime_get_effective_param_status(slot.track, slot.param) != TRACK_RUNTIME_PARAM_ALLOWED)
    {
        return 0U;
    }

    *out_param = slot.param;
    return 1U;
}

static uint8_t ui_macro_interaction_resolve_slot_value(uint8_t *out_track,
                                                       param_id_t *out_param,
                                                       float *out_scene_value)
{
    project_v1_macro_slot_t slot;
    const uint8_t hall = g_ui_macro_interaction.hall;
    const uint8_t bank = project_v1_macro_get_active_bank();
    const uint8_t macro = (uint8_t)(hall / PROJECT_V1_MACRO_SLOT_COUNT);
    const uint8_t slot_index = (uint8_t)(hall % PROJECT_V1_MACRO_SLOT_COUNT);

    if ((out_track == NULL) || (out_param == NULL) || (out_scene_value == NULL))
    {
        return 0U;
    }

    *out_track = PROJECT_V1_MACRO_SLOT_TRACK_NONE;
    *out_param = PARAM_COUNT;
    *out_scene_value = 0.0f;

    if ((g_ui_macro_interaction.armed == 0U) || (ui_macro_interaction_is_capture_mode() == 0U))
    {
        return 0U;
    }

    if (g_ui_macro_interaction.has_param != 0U)
    {
        if (ui_macro_interaction_is_assignable_param(g_ui_macro_interaction.param) == 0U)
        {
            return 0U;
        }

        *out_track = g_ui_macro_interaction.capture_track;
        *out_param = g_ui_macro_interaction.param;
        *out_scene_value = g_ui_macro_interaction.scene_value;
        return 1U;
    }

    if (project_v1_macro_get_slot(bank, macro, slot_index, &slot) == 0U)
    {
        return 0U;
    }

    if ((slot.track == PROJECT_V1_MACRO_SLOT_TRACK_NONE)
            || (slot.param == PROJECT_V1_MACRO_SLOT_PARAM_NONE)
            || (param_macro_slot_target_is_supported(slot.track, slot.param) == 0U))
    {
        return 0U;
    }

    *out_track = slot.track;
    *out_param = slot.param;
    *out_scene_value = slot.scene_value;
    return 1U;
}

static uint8_t ui_macro_interaction_resolve_slot_target(uint8_t *out_bank,
                                                        uint8_t *out_macro,
                                                        uint8_t *out_slot)
{
    const uint8_t hall = g_ui_macro_interaction.hall;

    if ((out_bank == NULL) || (out_macro == NULL) || (out_slot == NULL))
    {
        return 0U;
    }

    *out_bank = 0U;
    *out_macro = 0U;
    *out_slot = 0U;

    if ((g_ui_macro_interaction.armed == 0U) || (ui_macro_interaction_is_capture_mode() == 0U))
    {
        return 0U;
    }

    if (hall >= HALL_KEY_COUNT)
    {
        return 0U;
    }

    *out_bank = project_v1_macro_get_active_bank();
    *out_macro = (uint8_t)(hall / PROJECT_V1_MACRO_SLOT_COUNT);
    *out_slot = (uint8_t)(hall % PROJECT_V1_MACRO_SLOT_COUNT);
    return 1U;
}

void ui_macro_interaction_init(void)
{
    ui_macro_interaction_reset();
}

void ui_macro_interaction_reset(void)
{
    g_ui_macro_interaction.armed = 0U;
    g_ui_macro_interaction.hall = 0U;
    g_ui_macro_interaction.encoder = 0U;
    g_ui_macro_interaction.capture_track = PROJECT_V1_MACRO_SLOT_TRACK_NONE;
    g_ui_macro_interaction.has_param = 0U;
    g_ui_macro_interaction.has_scene_value = 0U;
    g_ui_macro_interaction.param = PARAM_COUNT;
    g_ui_macro_interaction.scene_value = 0.0f;
}

void ui_macro_interaction_note_hall_press(uint8_t hall)
{
    if ((hall >= HALL_KEY_COUNT) || (ui_macro_interaction_is_capture_mode() == 0U))
    {
        return;
    }

    g_ui_macro_interaction.armed = 1U;
    g_ui_macro_interaction.hall = hall;
    g_ui_macro_interaction.encoder = 0U;
    g_ui_macro_interaction.capture_track = ui_get_active_track();
    g_ui_macro_interaction.has_param = 0U;
    g_ui_macro_interaction.has_scene_value = 0U;
    g_ui_macro_interaction.param = PARAM_COUNT;
    g_ui_macro_interaction.scene_value = 0.0f;
}

uint8_t ui_macro_interaction_note_encoder_delta(uint8_t encoder, int16_t delta)
{
    param_id_t param = PARAM_COUNT;
    const param_desc_t *desc = NULL;
    float current_value = 0.0f;

    if ((delta == 0) || (encoder >= 4U))
    {
        return 0U;
    }

    if (g_ui_macro_interaction.armed == 0U)
    {
        return 0U;
    }

    if (ui_macro_interaction_is_capture_mode() == 0U)
    {
        return 0U;
    }

    if (ui_param_get_active_bank_param(encoder, &param) == 0U)
    {
        return 0U;
    }

    if (ui_macro_interaction_is_assignable_param(param) == 0U)
    {
        return 0U;
    }

    desc = &param_registry[param];
    if (g_ui_macro_interaction.has_param == 0U)
    {
        if (param_registry_get_track_value(param, g_ui_macro_interaction.capture_track, &current_value) == 0U)
        {
            current_value = param_get(param);
        }

        g_ui_macro_interaction.param = param;
        g_ui_macro_interaction.scene_value = current_value;
        g_ui_macro_interaction.has_scene_value = 1U;
        g_ui_macro_interaction.has_param = 1U;
    }
    else if (g_ui_macro_interaction.param != param)
    {
        if (param_registry_get_track_value(param, g_ui_macro_interaction.capture_track, &current_value) == 0U)
        {
            current_value = param_get(param);
        }

        g_ui_macro_interaction.param = param;
        g_ui_macro_interaction.scene_value = current_value;
        g_ui_macro_interaction.has_scene_value = 1U;
    }

    if (g_ui_macro_interaction.has_scene_value == 0U)
    {
        return 0U;
    }

    g_ui_macro_interaction.encoder = encoder;
    current_value = g_ui_macro_interaction.scene_value + ((float)delta * desc->step);
    if (current_value < desc->min)
    {
        current_value = desc->min;
    }
    else if (current_value > desc->max)
    {
        current_value = desc->max;
    }

    g_ui_macro_interaction.scene_value = current_value;
    return 1U;
}

void ui_macro_interaction_note_hall_release(uint8_t hall)
{
    project_v1_macro_slot_t slot;
    float scene_value = 0.0f;
    uint8_t active_track = 0U;
    uint8_t bank = 0U;
    uint8_t macro = 0U;
    uint8_t slot_index = 0U;

    if ((g_ui_macro_interaction.armed == 0U) || (g_ui_macro_interaction.hall != hall))
    {
        return;
    }

    if (ui_macro_interaction_is_capture_mode() == 0U)
    {
        ui_macro_interaction_reset();
        return;
    }

    if (g_ui_macro_interaction.has_param == 0U)
    {
        ui_macro_interaction_reset();
        return;
    }

    active_track = g_ui_macro_interaction.capture_track;
    if (active_track >= UI_TRACK_COUNT)
    {
        ui_macro_interaction_reset();
        return;
    }

    if (g_ui_macro_interaction.has_scene_value != 0U)
    {
        scene_value = g_ui_macro_interaction.scene_value;
    }
    else if (param_registry_get_track_value(g_ui_macro_interaction.param, active_track, &scene_value) == 0U)
    {
        ui_macro_interaction_reset();
        return;
    }

    bank = project_v1_macro_get_active_bank();
    macro = (uint8_t)(hall / PROJECT_V1_MACRO_SLOT_COUNT);
    slot_index = (uint8_t)(hall % PROJECT_V1_MACRO_SLOT_COUNT);

    slot.track = active_track;
    slot.param = g_ui_macro_interaction.param;
    slot.scene_value = scene_value;
    if (param_macro_slot_target_is_supported(slot.track, slot.param) != 0U)
    {
        (void)project_v1_macro_set_slot(bank, macro, slot_index, &slot);
    }
    ui_macro_interaction_reset();
}

uint8_t ui_macro_interaction_get_active_slot_lock(param_id_t *out_param)
{
    return ui_macro_interaction_resolve_slot_param(out_param);
}

uint8_t ui_macro_interaction_get_active_slot_target(uint8_t *out_bank,
                                                    uint8_t *out_macro,
                                                    uint8_t *out_slot)
{
    return ui_macro_interaction_resolve_slot_target(out_bank, out_macro, out_slot);
}

uint8_t ui_macro_interaction_get_active_slot_value(uint8_t *out_track,
                                                   param_id_t *out_param,
                                                   float *out_scene_value)
{
    return ui_macro_interaction_resolve_slot_value(out_track, out_param, out_scene_value);
}
