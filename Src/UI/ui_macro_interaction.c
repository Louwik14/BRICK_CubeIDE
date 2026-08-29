#include "ui_macro_interaction.h"

#include <stddef.h>

#include "App/Hall/hall_engine.h"
#include "App/Hall/hall_surface.h"
#include "Board/board_product.h"
#include "buttons.h"
#include "Track/track_runtime.h"
#include "Param/param_macro.h"
#include "Param/param_registry.h"
#include "Storage/project_control.h"
#include "ui_core.h"
#include "ui_param.h"

#define HALL_PRESSURE_RAW_NOISE_FLOOR 400U
#define HALL_PRESSURE_RAW_NOISE_MARGIN 200U
#define HALL_PRESSURE_HYST 150U
#define HALL_PRESSURE_AMOUNT_DEADZONE 25U

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
static uint8_t g_hall_pressure_active[HALL_UI_LANE_COUNT];

static float ui_macro_interaction_clampf(float value, float min_value, float max_value)
{
    if (value < min_value)
    {
        return min_value;
    }

    if (value > max_value)
    {
        return max_value;
    }

    return value;
}
static uint16_t hall_pressure_delta(uint8_t hall)
{
    const uint16_t min_value = hall_engine_get_min(hall);
    const uint16_t raw_value = hall_engine_get_raw(hall);

    return (raw_value > min_value) ? (uint16_t)(raw_value - min_value) : 0U;
}

static uint8_t hall_pressure_uses_binary_surface(void)
{
    const board_product_capabilities_t *caps = board_product_capabilities();
    return ((caps != 0)
            && (caps->has_step_binary_lanes != 0U)
            && (caps->has_analog_hall_lanes == 0U)) ? 1U : 0U;
}

static uint8_t hall_pressure_update(uint8_t hall)
{
    uint16_t min_value = 0U;
    uint16_t max_value = 0U;
    uint16_t delta = 0U;
    const uint16_t on_delta = (uint16_t)(HALL_PRESSURE_RAW_NOISE_FLOOR + HALL_PRESSURE_RAW_NOISE_MARGIN);
    const uint16_t off_delta = (on_delta > HALL_PRESSURE_HYST) ? (uint16_t)(on_delta - HALL_PRESSURE_HYST) : 0U;

    if (hall >= HALL_UI_LANE_COUNT)
    {
        return 0U;
    }

    if (hall_pressure_uses_binary_surface() != 0U)
    {
        g_hall_pressure_active[hall] = hall_surface_is_pressed(hall);
        return g_hall_pressure_active[hall];
    }

    min_value = hall_engine_get_min(hall);
    max_value = hall_engine_get_max(hall);
    delta = hall_pressure_delta(hall);

    if ((max_value <= min_value) || ((uint16_t)(max_value - min_value) <= on_delta))
    {
        g_hall_pressure_active[hall] = 0U;
        return 0U;
    }

    if (g_hall_pressure_active[hall] == 0U)
    {
        if (delta >= on_delta)
        {
            g_hall_pressure_active[hall] = 1U;
        }
    }
    else if (delta <= off_delta)
    {
        g_hall_pressure_active[hall] = 0U;
    }

    return g_hall_pressure_active[hall];
}

static float hall_pressure_amount(uint8_t hall)
{
    uint16_t min_value = 0U;
    uint16_t max_value = 0U;
    uint16_t range = 0U;
    uint16_t amount_start = 0U;
    uint16_t delta = 0U;
    float start = 0.0f;
    float amount = 0.0f;

    if (hall >= HALL_UI_LANE_COUNT)
    {
        return 0.0f;
    }

    if (hall_pressure_uses_binary_surface() != 0U)
    {
        return (hall_surface_is_pressed(hall) != 0U) ? 1.0f : 0.0f;
    }

    min_value = hall_engine_get_min(hall);
    max_value = hall_engine_get_max(hall);
    if (max_value <= min_value)
    {
        return 0.0f;
    }

    range = (uint16_t)(max_value - min_value);
    amount_start = (uint16_t)(HALL_PRESSURE_RAW_NOISE_FLOOR
                              + HALL_PRESSURE_RAW_NOISE_MARGIN
                              + HALL_PRESSURE_AMOUNT_DEADZONE);
    if (range <= amount_start)
    {
        return 0.0f;
    }

    delta = hall_pressure_delta(hall);
    start = (float)amount_start;
    amount = ((float)delta - start) / ((float)range - start);
    return ui_macro_interaction_clampf(amount, 0.0f, 1.0f);
}

static uint8_t ui_macro_interaction_is_scene_mode(void)
{
    ui_macro_overlay_submode_t overlay_submode = UI_MACRO_OVERLAY_SUBMODE_CTRL;
    if (ui_macro_overlay_get_submode(&overlay_submode) != 0U)
    {
        return (overlay_submode == UI_MACRO_OVERLAY_SUBMODE_ASSIGN) ? 1U : 0U;
    }

    return 0U;
}

static uint8_t ui_macro_interaction_is_switch_mode(void)
{
    ui_macro_overlay_submode_t overlay_submode = UI_MACRO_OVERLAY_SUBMODE_CTRL;
    if (ui_macro_overlay_get_submode(&overlay_submode) != 0U)
    {
        return (overlay_submode == UI_MACRO_OVERLAY_SUBMODE_CTRL) ? 1U : 0U;
    }

    return 0U;
}

static uint8_t ui_macro_interaction_is_assignable_param(param_id_t param)
{
    const uint8_t active_track = g_ui_macro_interaction.capture_track;
    if (param_macro_lock_target_is_supported(active_track, param) == 0U)
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
            || (g_ui_macro_interaction.hall >= PERSIST_CONTROL_MACRO_SCENE_COUNT)
            || (g_ui_macro_interaction.capture_track >= UI_TRACK_COUNT))
    {
        return 0U;
    }

    *out_scene = g_ui_macro_interaction.hall;
    *out_track = g_ui_macro_interaction.capture_track;
    return 1U;
}

static uint8_t ui_macro_interaction_get_scene_lock_for_param(param_id_t param,
                                                            project_control_macro_lock_t *out_lock)
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

    return project_control_get_scene_lock_for_param(scene, track, param, out_lock);
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

    return project_control_assign_scene_lock(scene,
                                              track,
                                              g_ui_macro_interaction.param,
                                              g_ui_macro_interaction.scene_value);
}

static uint8_t ui_macro_interaction_resolve_lock_param(param_id_t *out_param)
{
    project_control_macro_lock_t lock_entry;
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

    for (uint8_t lock = 0U; lock < PERSIST_CONTROL_MACRO_LOCK_COUNT; ++lock)
    {
        if (project_control_get_scene_lock(scene, lock, &lock_entry) == 0U)
        {
            continue;
        }

        if ((lock_entry.track == 0xFFU)
                || (lock_entry.param == PARAM_COUNT))
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
    project_control_macro_lock_t lock_entry;
    const uint8_t hall = g_ui_macro_interaction.hall;
    const uint8_t scene = hall;

    if ((out_track == NULL) || (out_param == NULL) || (out_scene_value == NULL))
    {
        return 0U;
    }

    *out_track = 0xFFU;
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

    for (uint8_t lock = 0U; lock < PERSIST_CONTROL_MACRO_LOCK_COUNT; ++lock)
    {
        if (project_control_get_scene_lock(scene, lock, &lock_entry) == 0U)
        {
            continue;
        }

        if ((lock_entry.track == 0xFFU)
                || (lock_entry.param == PARAM_COUNT)
                || (param_macro_lock_target_is_supported(lock_entry.track, lock_entry.param) == 0U))
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

static uint8_t ui_macro_interaction_resolve_lock_target(uint8_t *out_scene,
                                                        uint8_t *out_lock)
{
    if ((out_scene == NULL) || (out_lock == NULL))
    {
        return 0U;
    }

    *out_scene = 0U;
    *out_lock = 0U;

    return 0U;
}

void ui_macro_interaction_init(void)
{
    ui_macro_interaction_reset();
}

void ui_macro_interaction_reset(void)
{
    for (uint8_t scene = 0U; scene < PERSIST_CONTROL_MACRO_SCENE_COUNT; ++scene)
    {
        param_macro_release_scene_source(scene);
    }

    for (uint8_t hall = 0U; hall < HALL_UI_LANE_COUNT; ++hall)
    {
        g_hall_pressure_active[hall] = 0U;
    }

    g_ui_macro_interaction.armed = 0U;
    g_ui_macro_interaction.hall = 0U;
    g_ui_macro_interaction.encoder = 0U;
    g_ui_macro_interaction.capture_track = 0xFFU;
    g_ui_macro_interaction.has_param = 0U;
    g_ui_macro_interaction.has_scene_value = 0U;
    g_ui_macro_interaction.param = PARAM_COUNT;
    g_ui_macro_interaction.scene_value = 0.0f;
}

void ui_macro_interaction_note_hall_press(uint8_t hall)
{
    if (hall >= HALL_UI_LANE_COUNT)
    {
        return;
    }

    if (ui_macro_interaction_is_switch_mode() != 0U)
    {
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
            (void)project_control_clear_scene_lock(scene, track, param);
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
        project_control_macro_lock_t existing_lock;
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
        project_control_macro_lock_t existing_lock;
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
    ui_param_note_user_value_flash(encoder,
                                   param,
                                   g_ui_macro_interaction.capture_track,
                                   current_value,
                                   UI_PARAM_VALUE_FLASH_MACRO_SCENE_ASSIGN);
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
    (void)pressed;

    if ((hall >= HALL_UI_LANE_COUNT) || (ui_macro_interaction_is_switch_mode() == 0U))
    {
        return;
    }

    if (hall_pressure_update(hall) != 0U)
    {
        (void)param_macro_set_scene_source_amount(hall, hall_pressure_amount(hall));
    }
    else
    {
        param_macro_release_scene_source(hall);
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
            || (g_ui_macro_interaction.hall >= PERSIST_CONTROL_MACRO_SCENE_COUNT))
    {
        return 0U;
    }

    *out_scene = g_ui_macro_interaction.hall;
    return 1U;
}

uint8_t ui_macro_interaction_param_is_locked(param_id_t param)
{
    project_control_macro_lock_t lock_entry;
    return ui_macro_interaction_get_scene_lock_for_param(param, &lock_entry);
}

uint8_t ui_macro_interaction_get_param_lock_value(param_id_t param,
                                                  uint8_t *out_track,
                                                  float *out_scene_value)
{
    project_control_macro_lock_t lock_entry;

    if ((out_track == NULL) || (out_scene_value == NULL))
    {
        return 0U;
    }

    *out_track = 0xFFU;
    *out_scene_value = 0.0f;

    if (ui_macro_interaction_get_scene_lock_for_param(param, &lock_entry) == 0U)
    {
        return 0U;
    }

    *out_track = lock_entry.track;
    *out_scene_value = lock_entry.scene_value;
    return 1U;
}

uint8_t ui_macro_interaction_get_active_lock_param(param_id_t *out_param)
{
    return ui_macro_interaction_resolve_lock_param(out_param);
}

uint8_t ui_macro_interaction_get_active_lock_target(uint8_t *out_scene,
                                                    uint8_t *out_lock)
{
    return ui_macro_interaction_resolve_lock_target(out_scene, out_lock);
}

uint8_t ui_macro_interaction_get_active_lock_value(uint8_t *out_track,
                                                   param_id_t *out_param,
                                                   float *out_scene_value)
{
    return ui_macro_interaction_resolve_lock_value(out_track, out_param, out_scene_value);
}
