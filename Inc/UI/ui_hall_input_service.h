#ifndef UI_HALL_INPUT_SERVICE_H
#define UI_HALL_INPUT_SERVICE_H

#include <stdint.h>

#include "App/Hall/hall_engine.h"
#include "ui_core.h"

typedef void (*ui_hall_input_service_set_active_track_fn)(uint8_t track);

void ui_hall_input_service_handle_hall(uint8_t hall,
                                       uint8_t pressed,
                                       uint8_t was_pressed,
                                       uint8_t shift_down,
                                       uint8_t track_select_armed,
                                       uint8_t mute_active,
                                       uint32_t mode_tap_ms[UI_HALL_MODE_COUNT],
                                       uint32_t cfg_tap_ms[UI_TRACK_COUNT],
                                       uint8_t hall_note_suppressed[HALL_KEY_COUNT],
                                       ui_hall_input_service_set_active_track_fn set_active_track);

void ui_hall_input_service_handle_transpose(uint8_t shift_down,
                                           uint8_t track_select_armed,
                                           uint8_t active_track);

#endif /* UI_HALL_INPUT_SERVICE_H */
