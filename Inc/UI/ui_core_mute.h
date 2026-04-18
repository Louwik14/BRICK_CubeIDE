#ifndef UI_CORE_MUTE_H
#define UI_CORE_MUTE_H

#include <stdint.h>

#include "ui_core.h"
#include "ui_event.h"

typedef ui_hall_mode_t (*ui_core_mute_get_hall_mode_fn)(void);
typedef void (*ui_core_mute_set_hall_mode_fn)(ui_hall_mode_t mode);
typedef void (*ui_core_mute_suppress_hall_note_fn)(uint8_t hall);

void ui_core_mute_init(void);
void ui_core_mute_reset(void);
uint8_t ui_core_mute_is_active(void);
ui_mute_submode_t ui_core_mute_get_submode(void);
ui_mute_state_t ui_core_mute_get_state(void);
uint8_t ui_core_mute_get_hall_led(uint8_t hall, ui_mute_hall_led_t *out_led);
uint8_t ui_core_mute_handle_event(const ui_event_t *ev,
                                  uint8_t *io_shift_down,
                                  uint8_t track_select_armed,
                                  ui_core_mute_get_hall_mode_fn get_hall_mode,
                                  ui_core_mute_set_hall_mode_fn set_hall_mode,
                                  ui_core_mute_suppress_hall_note_fn suppress_hall_note);

#endif /* UI_CORE_MUTE_H */
