#ifndef POLYPHONY_CONTROL_H
#define POLYPHONY_CONTROL_H

#include <stdint.h>
#include "App/live_parameter_audio_publication.h"

typedef struct { uint8_t voice_count; float spread; } polyphony_control_state_t;

void polyphony_control_init(void);
uint8_t polyphony_control_reset(uint8_t track);
uint8_t polyphony_control_get_voice_count(uint8_t track);
uint8_t polyphony_control_set_voice_count(uint8_t track, uint8_t voices);
uint8_t polyphony_control_get_spread(uint8_t track, float *out_spread);
uint8_t polyphony_control_set_spread(uint8_t track, float spread);
uint8_t polyphony_control_capture(uint8_t track, polyphony_control_state_t *out_state);
uint8_t polyphony_control_prepare(const polyphony_control_state_t *state,
                                  polyphony_control_state_t *out_prepared);
uint8_t polyphony_control_bulk_add(uint8_t track,
    const polyphony_control_state_t *prepared,
    live_parameter_audio_bulk_t *bulk);
uint8_t polyphony_control_install_prepared(uint8_t track,
    const polyphony_control_state_t *prepared);
uint8_t polyphony_control_restore(uint8_t track, const polyphony_control_state_t *state);

#endif
