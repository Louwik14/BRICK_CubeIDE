#ifndef UI_HALL_MODE_FLOW_H
#define UI_HALL_MODE_FLOW_H

#include <stdint.h>

#include "App/Hall/hall_engine.h"
#include "ui_core.h"
#include "ui_event.h"

typedef enum
{
    UI_HALL_DIRECT_ACTION_NONE = 0,
    UI_HALL_DIRECT_ACTION_SHIFT_MODE,
    UI_HALL_DIRECT_ACTION_TRACK_SELECT
} ui_hall_direct_action_t;

typedef void (*ui_hall_mode_flow_set_active_track_fn)(uint8_t track);
typedef void (*ui_hall_mode_flow_feedback_fn)(const char *message);

ui_hall_direct_action_t ui_hall_mode_flow_resolve_direct_action(uint8_t shift_down,
                                                                uint8_t track_select_armed,
                                                                uint8_t was_pressed,
                                                                uint8_t pressed);

void ui_hall_mode_flow_handle_shift_hall_action(
    uint8_t hall,
    uint32_t now_ms,
    ui_hall_mode_t hall_mode,
    uint8_t context_track,
    uint32_t mode_tap_ms[UI_HALL_MODE_COUNT]);

uint8_t ui_hall_mode_flow_handle_shift_step(uint8_t step,
                                             uint32_t now_ms,
                                             uint32_t mode_tap_ms[UI_HALL_MODE_COUNT]);

void ui_hall_mode_flow_handle_track_hall_action(uint8_t hall,
                                                uint32_t now_ms,
                                                uint8_t held_master_candidate,
                                                uint8_t has_held_master_candidate,
                                                uint32_t cfg_tap_ms[TRACK_COUNT],
                                                ui_hall_mode_flow_set_active_track_fn set_active_track,
                                                ui_hall_mode_flow_feedback_fn feedback);

void ui_hall_mode_flow_service_pending(uint32_t now_ms);

#endif /* UI_HALL_MODE_FLOW_H */
