#ifndef BRICK_KEYBOARD_INPUT_H
#define BRICK_KEYBOARD_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void keyboard_input_init(void);
void keyboard_input_process_hall(uint8_t hall_index, bool pressed, uint8_t velocity);
void keyboard_input_process_hall_timed(uint8_t hall_index, bool pressed,
                                       uint8_t velocity, uint32_t capture_tick,
                                       uint32_t ingress_serial,
                                       uint8_t hall_mode, uint8_t shift_down,
                                       uint8_t context_track);

#ifdef __cplusplus
}
#endif

#endif
