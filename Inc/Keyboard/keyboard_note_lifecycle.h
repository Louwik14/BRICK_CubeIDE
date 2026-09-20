#ifndef BRICK_KEYBOARD_NOTE_LIFECYCLE_H
#define BRICK_KEYBOARD_NOTE_LIFECYCLE_H

#include <stdint.h>

typedef struct
{
    uint32_t capture_tick;
    uint32_t ingress_serial;
    uint64_t ordered_sample;
    uint32_t tie_adjustments;
    uint8_t valid;
} keyboard_note_time_order_t;

void keyboard_note_time_order_reset(keyboard_note_time_order_t *state);
uint64_t keyboard_note_time_order_apply(keyboard_note_time_order_t *state,
                                        uint32_t capture_tick,
                                        uint32_t ingress_serial,
                                        uint64_t capture_sample);

#endif
