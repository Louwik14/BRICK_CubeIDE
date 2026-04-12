#ifndef BRICK_UI_KBD_INPUT_MAPPER_H
#define BRICK_UI_KBD_INPUT_MAPPER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void kbd_input_mapper_init(bool omnichord_state);
void kbd_input_mapper_set_omnichord_state(bool enabled);
void kbd_input_mapper_process(uint8_t seq_index, bool pressed);

#ifdef __cplusplus
}
#endif

#endif /* BRICK_UI_KBD_INPUT_MAPPER_H */
