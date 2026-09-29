#include "ui_macro_interaction.h"

#include <stddef.h>
#include "App/Hall/hall_keymap.h"
#include "App/Hall/hall_engine.h"
#include "buttons.h"
#include "Storage/project_control.h"
#include "Param/param_macro.h"
#include "Track/track_runtime.h"
#include "ui_core.h"

#define UI_MACRO_DOUBLE_TAP_MS 400U

static ui_macro_mode_t g_mode = UI_MACRO_LIVE_PRESSURE;
static ui_macro_mode_t g_last_live = UI_MACRO_LIVE_PRESSURE;
static uint8_t g_held_macro = 0xFFU;
static uint8_t g_held_track = 0xFFU;
static uint8_t g_pending_tap;
static uint32_t g_pending_tap_ms;

static uint8_t ui_macro_white_index(uint8_t hall, uint8_t *out)
{
    hall_key_metadata_t key;
    if (out == NULL || hall_keymap_metadata(hall, &key) == 0U
        || key.kind != HALL_KEY_KIND_WHITE || key.white_index == 0U
        || key.white_index > PERSIST_CONTROL_MACRO_COUNT) return 0U;
    *out = (uint8_t)(key.white_index - 1U);
    return 1U;
}

void ui_macro_interaction_init(void)
{
    g_last_live = UI_MACRO_LIVE_PRESSURE;
    g_mode = UI_MACRO_LIVE_PRESSURE;
    ui_macro_interaction_reset();
}

void ui_macro_interaction_reset(void)
{
    param_macro_reset();
    g_held_macro = 0xFFU;
    g_held_track = 0xFFU;
    g_pending_tap = 0U;
}

void ui_macro_interaction_enter(void)
{
    ui_macro_interaction_reset();
    g_mode = g_last_live;
}

void ui_macro_interaction_leave(void)
{
    if (g_pending_tap != 0U && g_mode != UI_MACRO_ASSIGN)
        g_mode = g_mode == UI_MACRO_LIVE_PRESSURE
            ? UI_MACRO_LIVE_TOGGLE : UI_MACRO_LIVE_PRESSURE;
    if (g_mode != UI_MACRO_ASSIGN) g_last_live = g_mode;
    ui_macro_interaction_reset();
}

ui_macro_mode_t ui_macro_interaction_get_mode(void)
{
    return g_mode;
}

uint8_t ui_macro_interaction_get_held_macro(uint8_t *out_macro)
{
    if (out_macro == NULL || g_mode != UI_MACRO_ASSIGN
        || g_held_macro >= PERSIST_CONTROL_MACRO_COUNT) return 0U;
    *out_macro = g_held_macro;
    return 1U;
}

void ui_macro_interaction_shift_tap(uint32_t now_ms)
{
    (void)ui_macro_interaction_service_navigation(now_ms);
    if (g_mode == UI_MACRO_ASSIGN)
    {
        g_mode = g_last_live;
        ui_macro_interaction_reset();
        return;
    }
    if (g_pending_tap != 0U
        && (uint32_t)(now_ms - g_pending_tap_ms) <= UI_MACRO_DOUBLE_TAP_MS)
    {
        g_pending_tap = 0U;
        g_mode = UI_MACRO_ASSIGN;
        ui_macro_interaction_reset();
        return;
    }
    g_pending_tap = 1U;
    g_pending_tap_ms = now_ms;
}

uint8_t ui_macro_interaction_service_navigation(uint32_t now_ms)
{
    if (g_pending_tap == 0U
        || (uint32_t)(now_ms - g_pending_tap_ms) <= UI_MACRO_DOUBLE_TAP_MS) return 0U;
    g_pending_tap = 0U;
    g_mode = g_mode == UI_MACRO_LIVE_PRESSURE
        ? UI_MACRO_LIVE_TOGGLE : UI_MACRO_LIVE_PRESSURE;
    g_last_live = g_mode;
    ui_macro_interaction_reset();
    return 1U;
}

void ui_macro_interaction_note_hall_press(uint8_t hall)
{
    uint8_t macro;
    if (ui_macro_white_index(hall, &macro) == 0U) return;
    if (g_mode == UI_MACRO_ASSIGN)
    {
        g_held_macro = macro;
        g_held_track = ui_get_active_track();
    }
    else if (g_mode == UI_MACRO_LIVE_TOGGLE)
    {
        (void)param_macro_set_amount(macro,
            param_macro_get_amount(macro) > 0.0f ? 0.0f : 1.0f);
    }
}

void ui_macro_interaction_note_hall_release(uint8_t hall)
{
    uint8_t macro;
    if (ui_macro_white_index(hall, &macro) == 0U) return;
    if (g_mode == UI_MACRO_ASSIGN && g_held_macro == macro)
    {
        g_held_macro = 0xFFU;
        g_held_track = 0xFFU;
    }
    else if (g_mode == UI_MACRO_LIVE_PRESSURE)
        (void)param_macro_set_amount(macro, 0.0f);
}

void ui_macro_interaction_service_hall(uint8_t hall, uint8_t pressed)
{
    uint8_t macro;
    if (g_mode != UI_MACRO_LIVE_PRESSURE
        || ui_macro_white_index(hall, &macro) == 0U) return;
    const float amount = (pressed != 0U)
        ? (float)hall_engine_get_value(hall) / 100.0f : 0.0f;
    (void)param_macro_set_amount(macro, amount);
}

static uint8_t ui_macro_get_held_lock(param_id_t param,
                                      project_control_macro_lock_t *out)
{
    if (g_mode != UI_MACRO_ASSIGN || g_held_macro >= PERSIST_CONTROL_MACRO_COUNT
        || g_held_track >= TRACK_COUNT || param >= PARAM_COUNT) return 0U;
    return project_control_get_macro_lock_for_param(
        g_held_macro, g_held_track, param, out);
}

uint8_t ui_macro_interaction_note_encoder_delta_with_context(
    const ui_param_encoder_context_t *ctx, uint8_t encoder, int16_t delta)
{
    if (ctx == NULL || ctx->valid == 0U || encoder >= 4U || delta == 0
        || g_mode != UI_MACRO_ASSIGN || g_held_macro >= PERSIST_CONTROL_MACRO_COUNT
        || g_held_track >= TRACK_COUNT) return 0U;
    const param_id_t param = ctx->bank.params[encoder];
    if (param >= PARAM_COUNT
        || param_macro_lock_target_is_supported(g_held_track, param) == 0U)
        return 0U;

    if (button_down(BTN_SHIFT) != 0U)
    {
        (void)project_control_clear_macro_lock(g_held_macro, g_held_track, param);
        return 1U;
    }

    project_control_macro_lock_t prior;
    float value = 0.0f;
    if (ui_macro_get_held_lock(param, &prior) != 0U)
        value = prior.target_value;
    else if (param_registry_get_track_value(param, g_held_track, &value) == 0U)
        return 0U;

    const param_desc_t *desc = &param_registry[param];
    value += (float)delta * desc->step;
    if (value < desc->min) value = desc->min;
    if (value > desc->max) value = desc->max;
    if (project_control_assign_macro_lock(g_held_macro, g_held_track, param, value) == 0U)
        return 1U;
    ui_param_note_user_value_flash(encoder, param, g_held_track, value,
                                   UI_PARAM_VALUE_FLASH_MACRO_ASSIGN);
    return 1U;
}

uint8_t ui_macro_interaction_param_is_locked(param_id_t param)
{
    project_control_macro_lock_t lock;
    return ui_macro_get_held_lock(param, &lock);
}

uint8_t ui_macro_interaction_get_param_lock_value(param_id_t param,
                                                  uint8_t *out_track,
                                                  float *out_target_value)
{
    project_control_macro_lock_t lock;
    if (out_track == NULL || out_target_value == NULL
        || ui_macro_get_held_lock(param, &lock) == 0U) return 0U;
    *out_track = lock.track;
    *out_target_value = lock.target_value;
    return 1U;
}
