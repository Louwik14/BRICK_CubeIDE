#include "Keyboard/keyboard_note_lifecycle.h"

#include <string.h>

void keyboard_note_time_order_reset(keyboard_note_time_order_t *state)
{
    if (state != 0)
        memset(state, 0, sizeof(*state));
}

uint64_t keyboard_note_time_order_apply(keyboard_note_time_order_t *state,
                                        uint32_t capture_tick,
                                        uint32_t ingress_serial,
                                        uint64_t capture_sample)
{
    if (state == 0)
        return capture_sample;

    /* The hall/MIDI capture clock is coarser than the audio clock.  Preserve
     * the ISR ingress order inside one capture tick so that ON/OFF/ON cannot
     * be reclassified as OFF/OFF/ON by the terminal same-sample ordering. */
    if ((state->valid != 0U) && (state->capture_tick == capture_tick)
            && ((int32_t)(ingress_serial - state->ingress_serial) > 0)
            && (capture_sample <= state->ordered_sample))
    {
        capture_sample = state->ordered_sample + 1U;
        state->tie_adjustments++;
    }

    state->capture_tick = capture_tick;
    state->ingress_serial = ingress_serial;
    state->ordered_sample = capture_sample;
    state->valid = 1U;
    return capture_sample;
}
