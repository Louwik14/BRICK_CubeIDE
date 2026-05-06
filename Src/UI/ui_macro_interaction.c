#include "ui_macro_interaction.h"

#include <stddef.h>

#include "App/Hall/hall_engine.h"
#include "buttons.h"
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

static uint8_t ui_macro_interaction_is_scene_mode(void)
{
    return (uint8_t)((ui_get_hall_mode() == UI_HALL_MODE_MACRO)
            && (ui_page_get_id() != UI_PAGE_TEMPLATE_MACRO)
            && (project_v1_macro_get_hall_switch_mode() == PROJECT_V1_MACRO_HALL_SWITCH_SCENE));
}

static uint8_t ui_macro_interaction_is_switch_mode(void)
{
    return (uint8_t)((ui_get_hall_mode() == UI_HALL_MODE_MACRO)
            && (ui_page_get_id() != UI_PAGE_TEMPLATE_MACRO)
            && (project_v1_macro_get_hall_switch_mode() == PROJECT_V1_MACRO_HALL_SWITCH_SWITCH));
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

static uint8_t ui_macro_interaction_get_held_scene_and_track(uint8_t *out_scene, uint8_t *out_track)
{
    if ((out_scene == NULL)
            || (out_track == NULL)
            || (g_ui_macro_interaction.armed == 0U)
            || (ui_macro_interaction_is_scene_mode() == 0U)
            || (g_ui_macro_interaction.hall >= PROJECT_V1_MACRO_SCENE_COUNT)
            || (g_ui_macro_interaction.capture_track >= UI_TRACK_COUNT))
    {
        return 0U;
    }

    *out_scene = g_ui_macro_interaction.hall;
    *out_track = g_ui_macro_interaction.capture_track;
    return 1U;
}

static uint8_t ui_macro_interaction_get_scene_lock_for_param(param_id_t param,
                                                            project_v1_macro_lock_t *out_lock)
{
    uint8_t scene = 0U;
    uint8_t track = 0U;

    if ((out_lock == NULL)
            || (param >= PARAM_COUNT)
            || (ui_macro_interaction_get_held_scene_and_track(&scene, &track) == 0U))
    {
        return 0U;
    }

    if ((g_ui_macro_interaction.has_param != 0U)
            && (g_ui_macro_interaction.param == param)
            && (ui_macro_interaction_is_assignable_param(param) != 0U))
    {
        out_lock->track = track;
        out_lock->param = param;
        out_lock->scene_value = g_ui_macro_interaction.scene_value;
        return 1U;
    }

    return project_v1_macro_get_scene_lock_for_param(scene, track, param, out_lock);
}

static uint8_t ui_macro_interaction_commit_pending_lock(void)
{
    uint8_t scene = 0U;
    uint8_t track = 0U;

    if ((g_ui_macro_interaction.has_param == 0U)
            || (g_ui_macro_interaction.has_scene_value == 0U)
            || (ui_macro_interaction_get_held_scene_and_track(&scene, &track) == 0U)
            || (ui_macro_interaction_is_assignable_param(g_ui_macro_interaction.param) == 0U))
    {
        return 0U;
    }

    return project_v1_macro_assign_scene_lock(scene,
                                              track,
                                              g_ui_macro_interaction.param,
                                              g_ui_macro_interaction.scene_value);
}

static uint8_t ui_macro_interaction_resolve_lock_param(param_id_t *out_param)
{
    project_v1_macro_lock_t lock_entry;
    const uint8_t hall = g_ui_macro_interaction.hall;
    const uint8_t scene = hall;

    if (out_param == NULL)
    {
        return 0U;
    }

    *out_param = PARAM_COUNT;
    if ((g_ui_macro_interaction.armed == 0U) || (ui_macro_interaction_is_scene_mode() == 0U))
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

    for (uint8_t lock = 0U; lock < PROJECT_V1_MACRO_SCENE_LOCK_COUNT; ++lock)
    {
        if (project_v1_macro_get_scene_lock(scene, lock, &lock_entry) == 0U)
        {
            continue;
        }

        if ((lock_entry.track == PROJECT_V1_MACRO_LOCK_TRACK_NONE)
                || (lock_entry.param == PROJECT_V1_MACRO_LOCK_PARAM_NONE))
        {
            continue;
        }

        if (track_runtime_get_effective_param_status(lock_entry.track, lock_entry.param) != TRACK_RUNTIME_PARAM_ALLOWED)
        {
            continue;
        }

        *out_param = lock_entry.param;
        return 1U;
    }

    return 0U;
}

static uint8_t ui_macro_interaction_resolve_lock_value(uint8_t *out_track,
                                                       param_id_t *out_param,
                                                       float *out_scene_value)
{
    project_v1_macro_lock_t lock_entry;
    const uint8_t hall = g_ui_macro_interaction.hall;
    const uint8_t scene = hall;

    if ((out_track == NULL) || (out_param == NULL) || (out_scene_value == NULL))
    {
        return 0U;
    }

    *out_track = PROJECT_V1_MACRO_LOCK_TRACK_NONE;
    *out_param = PARAM_COUNT;
    *out_scene_value = 0.0f;

    if ((g_ui_macro_interaction.armed == 0U) || (ui_macro_interaction_is_scene_mode() == 0U))
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

    for (uint8_t lock = 0U; lock < PROJECT_V1_MACRO_SCENE_LOCK_COUNT; ++lock)
    {
        if (project_v1_macro_get_scene_lock(scene, lock, &lock_entry) == 0U)
        {
            continue;
        }

        if ((lock_entry.track == PROJECT_V1_MACRO_LOCK_TRACK_NONE)
                || (lock_entry.param == PROJECT_V1_MACRO_LOCK_PARAM_NONE)
                || (param_macro_slot_target_is_supported(lock_entry.track, lock_entry.param) == 0U))
        {
            continue;
        }

        *out_track = lock_entry.track;
        *out_param = lock_entry.param;
        *out_scene_value = lock_entry.scene_value;
        return 1U;
    }

    return 0U;
}

static uint8_t ui_macro_interaction_resolve_slot_target(uint8_t *out_bank,
                                                        uint8_t *out_macro,
                                                        uint8_t *out_slot)
{
    if ((out_bank == NULL) || (out_macro == NULL) || (out_slot == NULL))
    {
        return 0U;
    }

    *out_bank = 0U;
    *out_macro = 0U;
    *out_slot = 0U;

    return 0U;
}

void ui_macro_interaction_init(void)
{
    ui_macro_interaction_reset();
}

void ui_macro_interaction_reset(void)
{
    for (uint8_t scene = 0U; scene < PROJECT_V1_MACRO_SCENE_COUNT; ++scene)
    {
        param_macro_release_scene_source(scene);
    }

    g_ui_macro_interaction.armed = 0U;
    g_ui_macro_interaction.hall = 0U;
    g_ui_macro_interaction.encoder = 0U;
    g_ui_macro_interaction.capture_track = PROJECT_V1_MACRO_LOCK_TRACK_NONE;
    g_ui_macro_interaction.has_param = 0U;
    g_ui_macro_interaction.has_scene_value = 0U;
    g_ui_macro_interaction.param = PARAM_COUNT;
    g_ui_macro_interaction.scene_value = 0.0f;
}

void ui_macro_interaction_note_hall_press(uint8_t hall)
{
    if (hall >= HALL_KEY_COUNT)
    {
        return;
    }

    if (ui_macro_interaction_is_switch_mode() != 0U)
    {
        (void)param_macro_set_scene_source_amount(hall, ((float)hall_engine_get_value(hall)) * 0.01f);
        return;
    }

    if (ui_macro_interaction_is_scene_mode() == 0U)
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

uint8_t ui_macro_interaction_note_encoder_delta_with_context(const ui_param_encoder_context_t *ctx,
                                                            uint8_t encoder,
                                                            int16_t delta)
{
    param_id_t param = PARAM_COUNT;
    const param_desc_t *desc = NULL;
    float current_value = 0.0f;

    if ((ctx == 0) || (ctx->valid == 0U) || (delta == 0) || (encoder >= 4U))
    {
        return 0U;
    }

    if (g_ui_macro_interaction.armed == 0U)
    {
        return 0U;
    }

    if (ui_macro_interaction_is_scene_mode() == 0U)
    {
        return 0U;
    }

    param = ctx->bank.params[encoder];
    if (param >= PARAM_COUNT)
    {
        return 0U;
    }

    if (ui_macro_interaction_is_assignable_param(param) == 0U)
    {
        return 0U;
    }

    if (button_down(BTN_SHIFT) != 0U)
    {
        uint8_t scene = 0U;
        uint8_t track = 0U;
        if ((g_ui_macro_interaction.has_param != 0U) && (g_ui_macro_interaction.param != param))
        {
            (void)ui_macro_interaction_commit_pending_lock();
        }

        if (ui_macro_interaction_get_held_scene_and_track(&scene, &track) != 0U)
        {
            (void)project_v1_macro_clear_scene_lock(scene, track, param);
        }

        g_ui_macro_interaction.encoder = encoder;
        g_ui_macro_interaction.has_param = 0U;
        g_ui_macro_interaction.has_scene_value = 0U;
        g_ui_macro_interaction.param = PARAM_COUNT;
        g_ui_macro_interaction.scene_value = 0.0f;
        return 1U;
    }

    desc = &param_registry[param];
    if ((g_ui_macro_interaction.has_param != 0U) && (g_ui_macro_interaction.param != param))
    {
        (void)ui_macro_interaction_commit_pending_lock();
    }

    if (g_ui_macro_interaction.has_param == 0U)
    {
        project_v1_macro_lock_t existing_lock;
        if (ui_macro_interaction_get_scene_lock_for_param(param, &existing_lock) != 0U)
        {
            current_value = existing_lock.scene_value;
        }
        else if (param_registry_get_track_value(param, g_ui_macro_interaction.capture_track, &current_value) == 0U)
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
        project_v1_macro_lock_t existing_lock;
        if (ui_macro_interaction_get_scene_lock_for_param(param, &existing_lock) != 0U)
        {
            current_value = existing_lock.scene_value;
        }
        else if (param_registry_get_track_value(param, g_ui_macro_interaction.capture_track, &current_value) == 0U)
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

uint8_t ui_macro_interaction_note_encoder_delta(uint8_t encoder, int16_t delta)
{
    ui_param_encoder_context_t ctx;
    ui_param_capture_encoder_context(&ctx);
    return ui_macro_interaction_note_encoder_delta_with_context(&ctx, encoder, delta);
}

void ui_macro_interaction_note_hall_release(uint8_t hall)
{
    uint8_t active_track = 0U;

    if (ui_macro_interaction_is_switch_mode() != 0U)
    {
        param_macro_release_scene_source(hall);
        return;
    }

    if ((g_ui_macro_interaction.armed == 0U) || (g_ui_macro_interaction.hall != hall))
    {
        return;
    }

    if (ui_macro_interaction_is_scene_mode() == 0U)
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

    if (g_ui_macro_interaction.has_scene_value == 0U)
    {
        ui_macro_interaction_reset();
        return;
    }

    (void)ui_macro_interaction_commit_pending_lock();
    ui_macro_interaction_reset();
}

void ui_macro_interaction_service_hall(uint8_t hall, uint8_t pressed)
{
    if ((hall >= HALL_KEY_COUNT) || (ui_macro_interaction_is_switch_mode() == 0U))
    {
        return;
    }

    if (pressed != 0U)
    {
        (void)param_macro_set_scene_source_amount(hall, ((float)hall_engine_get_value(hall)) * 0.01f);
    }
}

uint8_t param_macro_get_ui_held_scene(uint8_t macro, uint8_t *out_scene)
{
    (void)macro;
    return ui_macro_interaction_get_held_scene(out_scene);
}

uint8_t ui_macro_interaction_get_held_scene(uint8_t *out_scene)
{
    if ((out_scene == NULL)
            || (g_ui_macro_interaction.armed == 0U)
            || (ui_macro_interaction_is_scene_mode() == 0U)
            || (g_ui_macro_interaction.hall >= PROJECT_V1_MACRO_SCENE_COUNT))
    {
        return 0U;
    }

    *out_scene = g_ui_macro_interaction.hall;
    return 1U;
}

uint8_t ui_macro_interaction_param_is_locked(param_id_t param)
{
    project_v1_macro_lock_t lock_entry;
    return ui_macro_interaction_get_scene_lock_for_param(param, &lock_entry);
}

uint8_t ui_macro_interaction_get_param_lock_value(param_id_t param,
                                                  uint8_t *out_track,
                                                  float *out_scene_value)
{
    project_v1_macro_lock_t lock_entry;

    if ((out_track == NULL) || (out_scene_value == NULL))
    {
        return 0U;
    }

    *out_track = PROJECT_V1_MACRO_LOCK_TRACK_NONE;
    *out_scene_value = 0.0f;

    if (ui_macro_interaction_get_scene_lock_for_param(param, &lock_entry) == 0U)
    {
        return 0U;
    }

    *out_track = lock_entry.track;
    *out_scene_value = lock_entry.scene_value;
    return 1U;
}

uint8_t ui_macro_interaction_get_active_slot_lock(param_id_t *out_param)
{
    return ui_macro_interaction_resolve_lock_param(out_param);
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
    return ui_macro_interaction_resolve_lock_value(out_track, out_param, out_scene_value);
}
