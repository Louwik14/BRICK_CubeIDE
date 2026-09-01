#ifndef BRICK_KEYBOARD_ENGINE_H
#define BRICK_KEYBOARD_ENGINE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "Seq/seq_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

void keyboard_engine_note_on(uint8_t track, uint8_t note, uint8_t velocity);
void keyboard_engine_note_off(uint8_t track, uint8_t note);
void keyboard_engine_note_on_for_track(uint8_t track, uint8_t note, uint8_t velocity);
void keyboard_engine_note_off_for_track(uint8_t track, uint8_t note);
void keyboard_engine_note_on_for_track_timed(uint8_t track, uint8_t note,
                                             uint8_t velocity,
                                             uint32_t capture_tick,
                                             uint32_t ingress_serial);
void keyboard_engine_note_off_for_track_timed(uint8_t track, uint8_t note,
                                              uint32_t capture_tick,
                                              uint32_t ingress_serial);
void keyboard_engine_note_on_from_source(seq_live_rec_source_t source, uint8_t track, uint8_t channel_zero_based, uint8_t note, uint8_t velocity);
void keyboard_engine_note_off_from_source(seq_live_rec_source_t source, uint8_t track, uint8_t channel_zero_based, uint8_t note);
void keyboard_engine_clear_source_occurrences_silent(void);
void keyboard_engine_clear_state_silent(void);
void keyboard_engine_midi_receive(const uint8_t *msg, size_t len);
void keyboard_engine_midi_receive_timed(const uint8_t *msg, size_t len,
                                        uint32_t capture_tick,
                                        uint32_t ingress_serial);

#ifdef __cplusplus
}
#endif

#endif
